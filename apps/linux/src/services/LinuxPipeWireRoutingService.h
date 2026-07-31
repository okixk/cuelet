#pragma once

#include "services/LinuxPipeWireRoutingPlan.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class LinuxPipeWireRoutingService {
public:
    struct StopResult {
        bool stopped = false;
        bool forced = false;
        std::string error;
    };

    class ChildProcess {
    public:
        virtual ~ChildProcess() = default;
        virtual StopResult stop(
            const LinuxPipeWireRoutingPlan::ProcessStop& request) = 0;
        virtual bool isRunning() const = 0;
        virtual std::string errorDetail() const { return {}; }
    };

    struct SpawnResult {
        std::unique_ptr<ChildProcess> process;
        std::string error;
    };

    class ProcessRunner {
    public:
        virtual ~ProcessRunner() = default;
        virtual LinuxPipeWireRoutingPlan::ToolState probeCapabilities() = 0;
        virtual SpawnResult spawn(const std::vector<std::string>& argv) = 0;
    };

    // Uses the production GSubprocess runner.
    LinuxPipeWireRoutingService();

    // The injected runner must outlive this service.
    explicit LinuxPipeWireRoutingService(ProcessRunner& runner);

    ~LinuxPipeWireRoutingService();

    LinuxPipeWireRoutingService(const LinuxPipeWireRoutingService&) = delete;
    LinuxPipeWireRoutingService& operator=(const LinuxPipeWireRoutingService&) = delete;
    LinuxPipeWireRoutingService(LinuxPipeWireRoutingService&&) = delete;
    LinuxPipeWireRoutingService& operator=(LinuxPipeWireRoutingService&&) = delete;

    LinuxPipeWireRoutingPlan::ToolState probeCapabilities();
    bool start(const LinuxPipeWireRoutingPlan::Plan& plan);
    bool restart(const LinuxPipeWireRoutingPlan::Plan& plan);
    bool stop();
    void refreshDiagnostic();

    bool isActive();
    std::size_t ownedProcessCount() const;
    std::string virtualSinkNode() const;
    std::string virtualSourceNode() const;
    const LinuxPipeWireRoutingPlan::Diagnostic& diagnostic();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
