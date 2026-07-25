#include "services/LinuxPipeWireRoutingService.h"

#include <gio/gio.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <sys/wait.h>
#include <utility>

namespace {

class GioChildProcess final : public LinuxPipeWireRoutingService::ChildProcess {
public:
    explicit GioChildProcess(GSubprocess* process)
        : process_(process)
    {
    }

    ~GioChildProcess() override
    {
        if (!stopped_ && process_) {
            // This is the exact GSubprocess created by Cuelet. Never fall back
            // to matching process names or touching unrelated PipeWire nodes.
            g_subprocess_force_exit(process_);
        }
        if (process_) {
            g_object_unref(process_);
        }
    }

    LinuxPipeWireRoutingService::StopResult stop(
        const LinuxPipeWireRoutingPlan::ProcessStop& request) override
    {
        if (stopped_) {
            return {true, false, {}};
        }

        struct WaitState {
            GMainLoop* loop = nullptr;
            bool completed = false;
            bool waitSucceeded = false;
            GError* error = nullptr;
        };

        const auto waitCallback = [](
                                      GObject* source,
                                      GAsyncResult* result,
                                      gpointer userData) {
            auto* state = static_cast<WaitState*>(userData);
            state->waitSucceeded = g_subprocess_wait_finish(
                G_SUBPROCESS(source), result, &state->error);
            state->completed = true;
            g_main_loop_quit(state->loop);
        };
        const auto timeoutCallback = [](gpointer userData) -> gboolean {
            auto* state = static_cast<WaitState*>(userData);
            g_main_loop_quit(state->loop);
            return G_SOURCE_REMOVE;
        };

        WaitState state;
        GMainContext* context = g_main_context_new();
        state.loop = g_main_loop_new(context, FALSE);
        g_main_context_push_thread_default(context);
        g_subprocess_wait_async(process_, nullptr, waitCallback, &state);
        g_subprocess_send_signal(process_, SIGTERM);
        GSource* timeoutSource =
            g_timeout_source_new(request.gracePeriodMilliseconds);
        g_source_set_callback(timeoutSource, timeoutCallback, &state, nullptr);
        g_source_attach(timeoutSource, context);
        g_main_loop_run(state.loop);

        bool forced = false;
        if (!state.completed) {
            forced = true;
            g_subprocess_force_exit(process_);
            g_main_loop_run(state.loop);
        }
        if (!g_source_is_destroyed(timeoutSource)) {
            g_source_destroy(timeoutSource);
        }

        std::string error;
        if (state.error) {
            error = state.error->message ? state.error->message : "Could not wait for pw-loopback.";
            g_error_free(state.error);
        }
        g_source_unref(timeoutSource);
        g_main_context_pop_thread_default(context);
        g_main_loop_unref(state.loop);
        g_main_context_unref(context);

        stopped_ = state.completed && state.waitSucceeded;
        return {stopped_, forced, std::move(error)};
    }

    bool isRunning() const override
    {
        if (stopped_ || !process_) {
            return false;
        }

        const gchar* identifier = g_subprocess_get_identifier(process_);
        if (!identifier || *identifier == '\0') {
            return false;
        }
        gchar* end = nullptr;
        errno = 0;
        const gint64 parsedIdentifier = g_ascii_strtoll(identifier, &end, 10);
        if (errno != 0 || end == identifier || (end && *end != '\0') ||
            parsedIdentifier <= 0 ||
            static_cast<guint64>(parsedIdentifier) >
                static_cast<guint64>(std::numeric_limits<id_t>::max())) {
            return false;
        }

        siginfo_t childInformation{};
        if (waitid(
                P_PID,
                static_cast<id_t>(parsedIdentifier),
                &childInformation,
                WEXITED | WNOHANG | WNOWAIT) == 0) {
            return childInformation.si_pid == 0;
        }
        return errno != ECHILD;
    }

private:
    GSubprocess* process_ = nullptr;
    bool stopped_ = false;
};

class GioProcessRunner final : public LinuxPipeWireRoutingService::ProcessRunner {
public:
    LinuxPipeWireRoutingPlan::ToolState probeCapabilities() override
    {
        gchar* executable = g_find_program_in_path("pw-loopback");
        const bool toolAvailable = executable != nullptr;
        executablePath_ = executable ? executable : "";
        g_free(executable);

        const gchar* runtimeDirectory = g_getenv("PIPEWIRE_RUNTIME_DIR");
        if (!runtimeDirectory || *runtimeDirectory == '\0') {
            runtimeDirectory = g_get_user_runtime_dir();
        }
        const gchar* remoteName = g_getenv("PIPEWIRE_REMOTE");
        if (!remoteName || *remoteName == '\0') {
            remoteName = "pipewire-0";
        }

        bool sessionReachable = false;
        if (g_path_is_absolute(remoteName)) {
            sessionReachable = g_file_test(remoteName, G_FILE_TEST_EXISTS);
        } else if (runtimeDirectory && *runtimeDirectory != '\0') {
            gchar* socketPath = g_build_filename(runtimeDirectory, remoteName, nullptr);
            sessionReachable = g_file_test(socketPath, G_FILE_TEST_EXISTS);
            g_free(socketPath);
        }

        return {sessionReachable, toolAvailable};
    }

    LinuxPipeWireRoutingService::SpawnResult spawn(
        const std::vector<std::string>& argv) override
    {
        if (executablePath_.empty() || argv.empty()) {
            return {nullptr, "pw-loopback was not resolved during capability probing."};
        }

        std::vector<const gchar*> directArgv;
        directArgv.reserve(argv.size() + 1);
        directArgv.push_back(executablePath_.c_str());
        for (std::size_t index = 1; index < argv.size(); ++index) {
            directArgv.push_back(argv[index].c_str());
        }
        directArgv.push_back(nullptr);

        GError* error = nullptr;
        GSubprocess* process = g_subprocess_newv(
            directArgv.data(),
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
            &error);
        if (!process) {
            std::string message = error && error->message
                ? error->message
                : "Could not start pw-loopback.";
            if (error) {
                g_error_free(error);
            }
            return {nullptr, std::move(message)};
        }

        return {std::make_unique<GioChildProcess>(process), {}};
    }

private:
    std::string executablePath_;
};

LinuxPipeWireRoutingPlan::Diagnostic invalidDiagnostic(std::string detail)
{
    return {
        LinuxPipeWireRoutingPlan::DiagnosticStatus::InvalidRuntimeState,
        "Cuelet rejected an unsafe or inconsistent PipeWire routing plan.",
        {std::move(detail)},
    };
}

bool isLowerHexIdentifier(std::string_view value)
{
    if (value.size() != 16) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const auto character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
    });
}

bool consumeJsonString(std::string_view value, std::size_t& position)
{
    if (position >= value.size() || value[position] != '"') {
        return false;
    }
    ++position;
    while (position < value.size()) {
        const auto character = static_cast<unsigned char>(value[position++]);
        if (character == '"') {
            return true;
        }
        if (character < 0x20) {
            return false;
        }
        if (character != '\\') {
            continue;
        }
        if (position >= value.size()) {
            return false;
        }
        const char escape = value[position++];
        if (escape == 'u') {
            if (position + 4 > value.size()) {
                return false;
            }
            for (std::size_t digit = 0; digit < 4; ++digit) {
                const char hex = value[position + digit];
                if (!((hex >= '0' && hex <= '9') ||
                      (hex >= 'a' && hex <= 'f') ||
                      (hex >= 'A' && hex <= 'F'))) {
                    return false;
                }
            }
            position += 4;
        } else if (
            escape != '"' && escape != '\\' && escape != '/' &&
            escape != 'b' && escape != 'f' && escape != 'n' &&
            escape != 'r' && escape != 't') {
            return false;
        }
    }
    return false;
}

bool isGeneratedPropertyArgument(
    std::string_view argument,
    std::string_view option,
    std::string_view mediaClass,
    std::string_view nodeName)
{
    const std::string prefix =
        std::string(option) +
        "{\"media.class\":\"" + std::string(mediaClass) +
        "\",\"node.name\":\"" + std::string(nodeName) +
        "\",\"node.description\":";
    static constexpr std::string_view suffix =
        ",\"node.virtual\":true"
        ",\"node.autoconnect\":false"
        ",\"object.linger\":false"
        ",\"priority.driver\":0"
        ",\"priority.session\":0"
        ",\"audio.channels\":2"
        ",\"audio.position\":[\"FL\",\"FR\"]}";
    if (argument.rfind(prefix, 0) != 0) {
        return false;
    }
    std::size_t position = prefix.size();
    return consumeJsonString(argument, position) &&
        argument.substr(position) == suffix;
}

std::string validatePlan(const LinuxPipeWireRoutingPlan::Plan& plan)
{
    if (!plan.enabled) {
        return {};
    }
    if (plan.scope != LinuxPipeWireRoutingPlan::Scope::UserSession) {
        return "Only user-session routing is supported.";
    }
    if (plan.changesDefaultInput || plan.changesDefaultOutput ||
        plan.routesLocalPlayback || plan.writesSystemConfiguration ||
        !plan.configurationWrites.empty() ||
        !plan.bridgesVirtualSinkMonitorToSource) {
        return "The plan requested a default-device, local-playback, or configuration change.";
    }
    if (plan.requiredExecutables != std::vector<std::string>{"pw-loopback"}) {
        return "The runtime plan must require only pw-loopback.";
    }
    if (plan.startProcesses.empty() ||
        plan.startProcesses.size() != plan.stopProcesses.size()) {
        return "Every process start must have one matching scoped stop action.";
    }

    std::map<std::string, const LinuxPipeWireRoutingPlan::ProcessStop*> stops;
    for (const auto& stop : plan.stopProcesses) {
        if (stop.ownershipToken.empty() ||
            stop.mode != LinuxPipeWireRoutingPlan::StopMode::TerminateOwnedProcess ||
            !stop.requiresTrackedChildHandle ||
            stop.allowNameBasedFallback ||
            !stop.forceAfterGracePeriod ||
            stop.gracePeriodMilliseconds > 5000 ||
            !stops.emplace(stop.ownershipToken, &stop).second) {
            return "A stop action was unscoped, duplicated, or permitted name-based cleanup.";
        }
    }

    std::set<std::string> starts;
    for (std::size_t processIndex = 0;
         processIndex < plan.startProcesses.size();
         ++processIndex) {
        const auto& start = plan.startProcesses[processIndex];
        if (start.ownershipToken.empty() || !starts.insert(start.ownershipToken).second ||
            start.usesShell || !start.longRunning ||
            start.argv.empty() || start.argv.front() != "pw-loopback") {
            return "A start action was unowned, duplicated, shell-based, or not pw-loopback.";
        }
        static constexpr std::string_view ownershipPrefix = "cuelet-pipewire-session-";
        if (start.ownershipToken.rfind(ownershipPrefix, 0) != 0) {
            return "A process start did not have a Cuelet session ownership token.";
        }
        const std::string_view identifier(start.ownershipToken.data() + ownershipPrefix.size(),
                                          start.ownershipToken.size() - ownershipPrefix.size());
        if (!isLowerHexIdentifier(identifier)) {
            return "A process start had an invalid Cuelet session identifier.";
        }
        if (start.argv.size() != 7) {
            return "A pw-loopback start did not match Cuelet's generated argument shape.";
        }
        std::size_t totalArgumentBytes = 0;
        if (stops.count(start.ownershipToken) == 0) {
            return "A start action had no matching exact-handle cleanup action.";
        }
        for (const auto& argument : start.argv) {
            if (argument.size() > 32768 ||
                totalArgumentBytes > 131072 - std::min<std::size_t>(argument.size(), 131072)) {
                return "Process arguments exceeded Cuelet's bounded routing-plan size.";
            }
            totalArgumentBytes += argument.size();
            if (argument.find('\0') != std::string::npos) {
                return "Process arguments may not contain embedded NUL bytes.";
            }
        }

        const auto virtualSinkNode = "cuelet_virtual_sink_" + std::string(identifier);
        const auto virtualSourceNode = "cuelet_virtual_source_" + std::string(identifier);
        if (start.argv[1] != "--name=" + start.ownershipToken ||
            start.argv[2] != "--group=" + start.ownershipToken ||
            start.argv[3] != "--channels=2" ||
            start.argv[4] != "--channel-map=[ FL, FR ]" ||
            !isGeneratedPropertyArgument(
                start.argv[5], "--capture-props=", "Audio/Sink", virtualSinkNode) ||
            !isGeneratedPropertyArgument(
                start.argv[6], "--playback-props=", "Audio/Source", virtualSourceNode)) {
            return "The runtime accepts only Cuelet-generated virtual sink/source arguments.";
        }
        if (processIndex == 0 &&
            (plan.virtualSinkNode != virtualSinkNode ||
             plan.virtualSourceNode != virtualSourceNode)) {
            return "The exposed endpoint names did not match the first owned process.";
        }
    }

    return {};
}

bool equivalentPlans(
    const LinuxPipeWireRoutingPlan::Plan& left,
    const LinuxPipeWireRoutingPlan::Plan& right)
{
    if (left.enabled != right.enabled ||
        left.virtualSinkNode != right.virtualSinkNode ||
        left.virtualSourceNode != right.virtualSourceNode ||
        left.startProcesses.size() != right.startProcesses.size() ||
        left.stopProcesses.size() != right.stopProcesses.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.startProcesses.size(); ++index) {
        const auto& leftStart = left.startProcesses[index];
        const auto& rightStart = right.startProcesses[index];
        if (leftStart.ownershipToken != rightStart.ownershipToken ||
            leftStart.argv != rightStart.argv) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.stopProcesses.size(); ++index) {
        const auto& leftStop = left.stopProcesses[index];
        const auto& rightStop = right.stopProcesses[index];
        if (leftStop.ownershipToken != rightStop.ownershipToken ||
            leftStop.mode != rightStop.mode ||
            leftStop.gracePeriodMilliseconds != rightStop.gracePeriodMilliseconds ||
            leftStop.requiresTrackedChildHandle != rightStop.requiresTrackedChildHandle ||
            leftStop.allowNameBasedFallback != rightStop.allowNameBasedFallback ||
            leftStop.forceAfterGracePeriod != rightStop.forceAfterGracePeriod) {
            return false;
        }
    }
    return true;
}

const LinuxPipeWireRoutingPlan::ProcessStop* findStop(
    const LinuxPipeWireRoutingPlan::Plan& plan,
    const std::string& ownershipToken)
{
    const auto found = std::find_if(
        plan.stopProcesses.begin(),
        plan.stopProcesses.end(),
        [&](const auto& stop) {
            return stop.ownershipToken == ownershipToken;
        });
    return found == plan.stopProcesses.end() ? nullptr : &*found;
}

void setObservation(
    std::vector<LinuxPipeWireRoutingPlan::ProcessObservation>& observations,
    LinuxPipeWireRoutingPlan::ProcessObservation replacement)
{
    const auto found = std::find_if(
        observations.begin(),
        observations.end(),
        [&](const auto& observation) {
            return observation.ownershipToken == replacement.ownershipToken;
        });
    if (found == observations.end()) {
        observations.push_back(std::move(replacement));
    } else {
        *found = std::move(replacement);
    }
}

} // namespace

struct LinuxPipeWireRoutingService::Impl {
    struct OwnedProcess {
        std::string ownershipToken;
        std::unique_ptr<ChildProcess> process;
    };

    explicit Impl(ProcessRunner& injectedRunner)
        : runner(&injectedRunner)
    {
    }

    Impl()
        : ownedRunner(std::make_unique<GioProcessRunner>())
        , runner(ownedRunner.get())
    {
    }

    std::unique_ptr<ProcessRunner> ownedRunner;
    ProcessRunner* runner = nullptr;
    LinuxPipeWireRoutingPlan::Plan plan;
    LinuxPipeWireRoutingPlan::ToolState tools;
    std::vector<LinuxPipeWireRoutingPlan::ProcessObservation> observations;
    std::vector<OwnedProcess> processes;
    std::optional<LinuxPipeWireRoutingPlan::Diagnostic> operationDiagnostic;
    LinuxPipeWireRoutingPlan::Diagnostic lastDiagnostic{
        LinuxPipeWireRoutingPlan::DiagnosticStatus::Disabled,
        "Virtual microphone routing is disabled.",
        {},
    };
};

LinuxPipeWireRoutingService::LinuxPipeWireRoutingService()
    : impl_(std::make_unique<Impl>())
{
}

LinuxPipeWireRoutingService::LinuxPipeWireRoutingService(ProcessRunner& runner)
    : impl_(std::make_unique<Impl>(runner))
{
}

LinuxPipeWireRoutingService::~LinuxPipeWireRoutingService()
{
    stop();
}

LinuxPipeWireRoutingPlan::ToolState LinuxPipeWireRoutingService::probeCapabilities()
{
    impl_->tools = impl_->runner->probeCapabilities();
    return impl_->tools;
}

bool LinuxPipeWireRoutingService::start(const LinuxPipeWireRoutingPlan::Plan& plan)
{
    const auto validationError = validatePlan(plan);
    if (!validationError.empty()) {
        impl_->operationDiagnostic = invalidDiagnostic(validationError);
        return false;
    }
    impl_->operationDiagnostic.reset();

    if (!plan.enabled) {
        if (!impl_->processes.empty() && !stop()) {
            return false;
        }
        impl_->plan = plan;
        impl_->observations.clear();
        impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
            impl_->plan, impl_->tools, impl_->observations);
        return true;
    }

    if (!impl_->processes.empty()) {
        refreshDiagnostic();
        if (equivalentPlans(impl_->plan, plan) &&
            impl_->processes.size() == plan.startProcesses.size() &&
            impl_->lastDiagnostic.status ==
                LinuxPipeWireRoutingPlan::DiagnosticStatus::Active) {
            return true;
        }
        if (equivalentPlans(impl_->plan, plan) &&
            impl_->lastDiagnostic.status !=
                LinuxPipeWireRoutingPlan::DiagnosticStatus::Active) {
            if (!stop()) {
                return false;
            }
        } else {
            impl_->operationDiagnostic = invalidDiagnostic(
                "Another plan is still owned; use restart so it is cleaned up first.");
            return false;
        }
    }

    impl_->plan = plan;
    impl_->observations.clear();
    impl_->tools = impl_->runner->probeCapabilities();
    impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
        impl_->plan, impl_->tools, impl_->observations);
    if (impl_->lastDiagnostic.status != LinuxPipeWireRoutingPlan::DiagnosticStatus::Ready) {
        return false;
    }

    std::vector<Impl::OwnedProcess> started;
    for (const auto& processStart : plan.startProcesses) {
        auto spawn = impl_->runner->spawn(processStart.argv);
        if (!spawn.process) {
            setObservation(
                impl_->observations,
                {
                    processStart.ownershipToken,
                    LinuxPipeWireRoutingPlan::ProcessState::FailedToStart,
                    {},
                    spawn.error.empty() ? "Could not start pw-loopback." : std::move(spawn.error),
                });

            std::vector<Impl::OwnedProcess> cleanupFailures;
            for (auto startedProcess = started.rbegin();
                 startedProcess != started.rend();
                 ++startedProcess) {
                const auto* stopRequest = findStop(plan, startedProcess->ownershipToken);
                const auto stopped = startedProcess->process->stop(*stopRequest);
                setObservation(
                    impl_->observations,
                    {
                        startedProcess->ownershipToken,
                        stopped.stopped
                            ? LinuxPipeWireRoutingPlan::ProcessState::Stopped
                            : LinuxPipeWireRoutingPlan::ProcessState::FailedToStop,
                        {},
                        stopped.error,
                    });
                if (!stopped.stopped) {
                    cleanupFailures.push_back({
                        startedProcess->ownershipToken,
                        std::move(startedProcess->process),
                    });
                }
            }
            std::reverse(cleanupFailures.begin(), cleanupFailures.end());
            impl_->processes = std::move(cleanupFailures);
            impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
                impl_->plan, impl_->tools, impl_->observations);
            return false;
        }

        setObservation(
            impl_->observations,
            {
                processStart.ownershipToken,
                LinuxPipeWireRoutingPlan::ProcessState::Running,
                {},
                {},
            });
        started.push_back({
            processStart.ownershipToken,
            std::move(spawn.process),
        });
    }

    impl_->processes = std::move(started);
    impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
        impl_->plan, impl_->tools, impl_->observations);
    return impl_->lastDiagnostic.status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Active;
}

bool LinuxPipeWireRoutingService::restart(const LinuxPipeWireRoutingPlan::Plan& plan)
{
    if (!stop()) {
        return false;
    }
    return start(plan);
}

bool LinuxPipeWireRoutingService::stop()
{
    impl_->operationDiagnostic.reset();
    if (impl_->processes.empty()) {
        return true;
    }

    std::vector<Impl::OwnedProcess> cleanupFailures;
    for (auto process = impl_->processes.rbegin();
         process != impl_->processes.rend();
         ++process) {
        const auto* stopRequest = findStop(impl_->plan, process->ownershipToken);
        if (!stopRequest) {
            setObservation(
                impl_->observations,
                {
                    process->ownershipToken,
                    LinuxPipeWireRoutingPlan::ProcessState::FailedToStop,
                    {},
                    "The owned process had no matching stop action.",
                });
            cleanupFailures.push_back({
                process->ownershipToken,
                std::move(process->process),
            });
            continue;
        }

        const auto stopped = process->process->stop(*stopRequest);
        setObservation(
            impl_->observations,
            {
                process->ownershipToken,
                stopped.stopped
                    ? LinuxPipeWireRoutingPlan::ProcessState::Stopped
                    : LinuxPipeWireRoutingPlan::ProcessState::FailedToStop,
                {},
                stopped.error,
            });
        if (!stopped.stopped) {
            cleanupFailures.push_back({
                process->ownershipToken,
                std::move(process->process),
            });
        }
    }

    std::reverse(cleanupFailures.begin(), cleanupFailures.end());
    impl_->processes = std::move(cleanupFailures);
    impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
        impl_->plan, impl_->tools, impl_->observations);
    return impl_->processes.empty();
}

void LinuxPipeWireRoutingService::refreshDiagnostic()
{
    for (const auto& process : impl_->processes) {
        if (!process.process->isRunning()) {
            setObservation(
                impl_->observations,
                {
                    process.ownershipToken,
                    LinuxPipeWireRoutingPlan::ProcessState::ExitedUnexpectedly,
                    {},
                    "The owned pw-loopback process is no longer running.",
                });
        }
    }
    impl_->lastDiagnostic = LinuxPipeWireRoutingPlan::diagnose(
        impl_->plan, impl_->tools, impl_->observations);
}

bool LinuxPipeWireRoutingService::isActive()
{
    if (!impl_->processes.empty()) {
        refreshDiagnostic();
    }
    return impl_->lastDiagnostic.status == LinuxPipeWireRoutingPlan::DiagnosticStatus::Active;
}

std::size_t LinuxPipeWireRoutingService::ownedProcessCount() const
{
    return impl_->processes.size();
}

std::string LinuxPipeWireRoutingService::virtualSinkNode() const
{
    return impl_->plan.virtualSinkNode;
}

std::string LinuxPipeWireRoutingService::virtualSourceNode() const
{
    return impl_->plan.virtualSourceNode;
}

const LinuxPipeWireRoutingPlan::Diagnostic& LinuxPipeWireRoutingService::diagnostic()
{
    if (impl_->operationDiagnostic.has_value()) {
        return *impl_->operationDiagnostic;
    }
    if (!impl_->processes.empty()) {
        refreshDiagnostic();
    }
    return impl_->lastDiagnostic;
}
