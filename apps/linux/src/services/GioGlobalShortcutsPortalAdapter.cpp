#include "services/LinuxGlobalShortcutsPortal.h"

#include <gio/gio.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cuelet_linux {
namespace {

constexpr const char* portalBusName = "org.freedesktop.portal.Desktop";
constexpr const char* portalObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char* globalShortcutsInterface =
    "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* hostRegistryInterface =
    "org.freedesktop.host.portal.Registry";
constexpr const char* requestInterface = "org.freedesktop.portal.Request";
constexpr const char* sessionInterface = "org.freedesktop.portal.Session";
constexpr const char* propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr const char* cueletApplicationId = "io.cuelet.Cuelet";

std::string operationError(GError* error, const std::string& fallback)
{
    return error && error->message ? error->message : fallback;
}

PortalOperationResult operationForResponse(
    guint response,
    const std::string& error = {})
{
    if (response == 0) {
        return {PortalResponseOutcome::Success, {}};
    }
    if (response == 1) {
        return {
            PortalResponseOutcome::Cancelled,
            error.empty() ? "The portal request was cancelled." : error,
        };
    }
    return {
        PortalResponseOutcome::Failed,
        error.empty() ? "The portal request failed." : error,
    };
}

bool hostRegistryIsUnavailable(GError* error)
{
    if (!error) {
        return false;
    }
    if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD)) {
        return true;
    }

    char* remoteName = g_dbus_error_get_remote_error(error);
    const bool unavailable = g_strcmp0(
        remoteName,
        "org.freedesktop.DBus.Error.UnknownMethod") == 0;
    g_free(remoteName);
    return unavailable;
}

std::vector<PortalShortcutBinding> parseBindingsArray(GVariant* array)
{
    std::vector<PortalShortcutBinding> bindings;
    if (!array || !g_variant_is_of_type(array, G_VARIANT_TYPE("a(sa{sv})"))) {
        return bindings;
    }

    GVariantIter iterator;
    g_variant_iter_init(&iterator, array);
    const char* portalId = nullptr;
    GVariant* properties = nullptr;
    while (g_variant_iter_next(&iterator, "(&s@a{sv})", &portalId, &properties)) {
        const char* description = nullptr;
        const char* triggerDescription = nullptr;
        g_variant_lookup(properties, "description", "&s", &description);
        g_variant_lookup(
            properties,
            "trigger_description",
            "&s",
            &triggerDescription);
        bindings.push_back(PortalShortcutBinding{
            portalId ? portalId : "",
            description ? description : "",
            triggerDescription ? triggerDescription : "",
        });
        g_variant_unref(properties);
    }
    return bindings;
}

std::vector<PortalShortcutBinding> parseBindingsResult(GVariant* results)
{
    if (!results || !g_variant_is_of_type(results, G_VARIANT_TYPE_VARDICT)) {
        return {};
    }
    GVariant* shortcuts = g_variant_lookup_value(
        results,
        "shortcuts",
        G_VARIANT_TYPE("a(sa{sv})"));
    if (!shortcuts) {
        return {};
    }
    auto bindings = parseBindingsArray(shortcuts);
    g_variant_unref(shortcuts);
    return bindings;
}

std::string uniqueToken(const char* prefix)
{
    char* random = g_uuid_string_random();
    std::string token = std::string(prefix) + (random ? random : "request");
    g_free(random);
    std::replace(token.begin(), token.end(), '-', '_');
    return token;
}

std::string requestPathForToken(GDBusConnection* connection, const std::string& token)
{
    const char* uniqueName = connection
        ? g_dbus_connection_get_unique_name(connection)
        : nullptr;
    if (!uniqueName || !*uniqueName) {
        return {};
    }
    std::string sender = uniqueName[0] == ':' ? uniqueName + 1 : uniqueName;
    std::replace(sender.begin(), sender.end(), '.', '_');
    return "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
}

class GioGlobalShortcutsPortalAdapter final
    : public GlobalShortcutsPortalAdapter,
      public std::enable_shared_from_this<GioGlobalShortcutsPortalAdapter> {
public:
    ~GioGlobalShortcutsPortalAdapter() override
    {
        stop();
    }

    void start(DetectionCallback callback, PortalEventSink sink) override
    {
        if (started_ || stopped_) {
            return;
        }
        started_ = true;
        detectionCallback_ = std::move(callback);
        eventSink_ = std::move(sink);
        resetCancellable();

        const char* address = g_getenv("DBUS_SESSION_BUS_ADDRESS");
        if (!address || !*address) {
            detectionCallback_(PortalDetectionResult{
                false,
                0,
                "DBUS_SESSION_BUS_ADDRESS is not available.",
            });
            return;
        }

        auto* context = new ConnectionContext{shared_from_this()};
        g_dbus_connection_new_for_address(
            address,
            static_cast<GDBusConnectionFlags>(
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr,
            cancellable_,
            +[](GObject*, GAsyncResult* asyncResult, gpointer userData) {
                std::unique_ptr<ConnectionContext> context(
                    static_cast<ConnectionContext*>(userData));
                GError* error = nullptr;
                GDBusConnection* connection =
                    g_dbus_connection_new_for_address_finish(asyncResult, &error);
                auto& self = *context->self;
                if (self.stopped_) {
                    g_clear_object(&connection);
                    g_clear_error(&error);
                    return;
                }
                if (!connection) {
                    if (self.detectionCallback_) {
                        self.detectionCallback_(PortalDetectionResult{
                            false,
                            0,
                            operationError(
                                error,
                                "Could not connect to the session D-Bus."),
                        });
                    }
                    g_clear_error(&error);
                    return;
                }

                self.connection_ = connection;
                g_dbus_connection_set_exit_on_close(self.connection_, false);
                self.watchId_ = g_bus_watch_name_on_connection(
                    self.connection_,
                    portalBusName,
                    G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                    +[](GDBusConnection*,
                        const gchar*,
                        const gchar* owner,
                        gpointer userData) {
                        static_cast<GioGlobalShortcutsPortalAdapter*>(userData)
                            ->portalAppeared(owner ? owner : "");
                    },
                    +[](GDBusConnection*, const gchar*, gpointer userData) {
                        static_cast<GioGlobalShortcutsPortalAdapter*>(userData)
                            ->portalVanished();
                    },
                    &self,
                    nullptr);
            },
            context);
    }

    void createSession(SessionCallback callback) override
    {
        if (!connection_) {
            callback(PortalSessionResult{
                {PortalResponseOutcome::Failed, "The portal is not connected."},
                {},
            });
            return;
        }

        const std::string sessionToken = uniqueToken("cuelet_session_");
        request(
            "CreateSession",
            [sessionToken](const std::string& handleToken) {
                GVariantBuilder options;
                g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
                g_variant_builder_add(
                    &options,
                    "{sv}",
                    "handle_token",
                    g_variant_new_string(handleToken.c_str()));
                g_variant_builder_add(
                    &options,
                    "{sv}",
                    "session_handle_token",
                    g_variant_new_string(sessionToken.c_str()));
                return g_variant_new("(a{sv})", &options);
            },
            [callback = std::move(callback)](
                const PortalOperationResult& operation,
                GVariant* results) {
                const char* sessionHandle = nullptr;
                if (operation.outcome == PortalResponseOutcome::Success && results) {
                    g_variant_lookup(
                        results,
                        "session_handle",
                        "&s",
                        &sessionHandle);
                }
                PortalSessionResult result{operation, sessionHandle ? sessionHandle : ""};
                if (result.operation.outcome == PortalResponseOutcome::Success
                    && result.sessionHandle.empty()) {
                    result.operation = {
                        PortalResponseOutcome::Failed,
                        "CreateSession returned no session handle.",
                    };
                }
                callback(result);
            });
    }

    void listShortcuts(
        const std::string& sessionHandle,
        BindingsCallback callback) override
    {
        request(
            "ListShortcuts",
            [sessionHandle](const std::string& handleToken) {
                GVariantBuilder options;
                g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
                g_variant_builder_add(
                    &options,
                    "{sv}",
                    "handle_token",
                    g_variant_new_string(handleToken.c_str()));
                return g_variant_new(
                    "(o@a{sv})",
                    sessionHandle.c_str(),
                    g_variant_builder_end(&options));
            },
            [callback = std::move(callback)](
                const PortalOperationResult& operation,
                GVariant* results) {
                callback(PortalBindingsResult{
                    operation,
                    operation.outcome == PortalResponseOutcome::Success
                        ? parseBindingsResult(results)
                        : std::vector<PortalShortcutBinding>{},
                });
            });
    }

    void bindShortcuts(
        const std::string& sessionHandle,
        const std::vector<PortalShortcutSpec>& shortcuts,
        const std::string& parentWindow,
        BindingsCallback callback) override
    {
        request(
            "BindShortcuts",
            [sessionHandle, shortcuts, parentWindow](const std::string& handleToken) {
                GVariantBuilder shortcutArray;
                g_variant_builder_init(
                    &shortcutArray,
                    G_VARIANT_TYPE("a(sa{sv})"));
                for (const auto& shortcut : shortcuts) {
                    GVariantBuilder properties;
                    g_variant_builder_init(&properties, G_VARIANT_TYPE_VARDICT);
                    g_variant_builder_add(
                        &properties,
                        "{sv}",
                        "description",
                        g_variant_new_string(shortcut.description.c_str()));
                    if (!shortcut.preferredTrigger.empty()) {
                        g_variant_builder_add(
                            &properties,
                            "{sv}",
                            "preferred_trigger",
                            g_variant_new_string(shortcut.preferredTrigger.c_str()));
                    }
                    g_variant_builder_add(
                        &shortcutArray,
                        "(s@a{sv})",
                        shortcut.portalId.c_str(),
                        g_variant_builder_end(&properties));
                }

                GVariantBuilder options;
                g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
                g_variant_builder_add(
                    &options,
                    "{sv}",
                    "handle_token",
                    g_variant_new_string(handleToken.c_str()));
                return g_variant_new(
                    "(o@a(sa{sv})s@a{sv})",
                    sessionHandle.c_str(),
                    g_variant_builder_end(&shortcutArray),
                    parentWindow.c_str(),
                    g_variant_builder_end(&options));
            },
            [callback = std::move(callback)](
                const PortalOperationResult& operation,
                GVariant* results) {
                callback(PortalBindingsResult{
                    operation,
                    operation.outcome == PortalResponseOutcome::Success
                        ? parseBindingsResult(results)
                        : std::vector<PortalShortcutBinding>{},
                });
            });
    }

    void configureShortcuts(
        const std::string& sessionHandle,
        const std::string& parentWindow,
        OperationCallback callback) override
    {
        if (!connection_) {
            callback({PortalResponseOutcome::Failed, "The portal is not connected."});
            return;
        }
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        auto* context = new OperationCallContext{
            shared_from_this(),
            std::move(callback),
        };
        g_dbus_connection_call(
            connection_,
            portalBusName,
            portalObjectPath,
            globalShortcutsInterface,
            "ConfigureShortcuts",
            g_variant_new(
                "(os@a{sv})",
                sessionHandle.c_str(),
                parentWindow.c_str(),
                g_variant_builder_end(&options)),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            cancellable_,
            +[](GObject* source, GAsyncResult* asyncResult, gpointer userData) {
                std::unique_ptr<OperationCallContext> context(
                    static_cast<OperationCallContext*>(userData));
                GError* error = nullptr;
                GVariant* result = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source),
                    asyncResult,
                    &error);
                if (context->self->stopped_) {
                    g_clear_pointer(&result, g_variant_unref);
                    g_clear_error(&error);
                    return;
                }
                if (!result) {
                    context->callback({
                        PortalResponseOutcome::Failed,
                        operationError(error, "ConfigureShortcuts failed."),
                    });
                    g_clear_error(&error);
                    return;
                }
                g_variant_unref(result);
                context->callback({PortalResponseOutcome::Success, {}});
            },
            context);
    }

    void closeSession(const std::string& sessionHandle) override
    {
        if (!connection_ || sessionHandle.empty()) {
            return;
        }
        g_dbus_connection_call(
            connection_,
            portalBusName,
            sessionHandle.c_str(),
            sessionInterface,
            "Close",
            nullptr,
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            nullptr,
            nullptr);
    }

    void cancelOutstandingRequests() override
    {
        if (connection_) {
            std::vector<std::string> paths;
            paths.reserve(pendingRequests_.size());
            for (const auto& [path, pending] : pendingRequests_) {
                (void)pending;
                paths.push_back(path);
            }
            for (const auto& path : paths) {
                g_dbus_connection_call(
                    connection_,
                    portalBusName,
                    path.c_str(),
                    requestInterface,
                    "Close",
                    nullptr,
                    nullptr,
                    G_DBUS_CALL_FLAGS_NONE,
                    -1,
                    nullptr,
                    nullptr,
                    nullptr);
                removePendingRequest(path);
            }
        } else {
            pendingRequests_.clear();
        }
        resetCancellable();
    }

    void stop() override
    {
        if (stopped_) {
            return;
        }
        stopped_ = true;
        cancelOutstandingRequests();
        if (watchId_ != 0) {
            g_bus_unwatch_name(watchId_);
            watchId_ = 0;
        }
        clearConnection();
        g_clear_object(&cancellable_);
        detectionCallback_ = {};
        eventSink_ = {};
    }

private:
    using ResponseHandler = std::function<void(
        const PortalOperationResult&,
        GVariant*)>;
    using ParametersBuilder = std::function<GVariant*(const std::string&)>;

    struct PendingRequest {
        guint subscriptionId = 0;
        ResponseHandler handler;
    };

    struct ConnectionContext {
        std::shared_ptr<GioGlobalShortcutsPortalAdapter> self;
    };

    struct RequestCallContext {
        std::shared_ptr<GioGlobalShortcutsPortalAdapter> self;
        std::string expectedPath;
    };

    struct PropertyCallContext {
        std::shared_ptr<GioGlobalShortcutsPortalAdapter> self;
        std::string expectedOwner;
    };

    struct RegistrationContext {
        std::shared_ptr<GioGlobalShortcutsPortalAdapter> self;
        std::string expectedOwner;
    };

    struct OperationCallContext {
        std::shared_ptr<GioGlobalShortcutsPortalAdapter> self;
        OperationCallback callback;
    };

    DetectionCallback detectionCallback_;
    PortalEventSink eventSink_;
    GDBusConnection* connection_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    guint watchId_ = 0;
    std::vector<guint> signalSubscriptionIds_;
    std::unordered_map<std::string, PendingRequest> pendingRequests_;
    std::string portalOwner_;
    bool started_ = false;
    bool stopped_ = false;
    bool previouslyAvailable_ = false;
    bool registrationInFlight_ = false;

    void resetCancellable()
    {
        if (cancellable_) {
            g_cancellable_cancel(cancellable_);
            g_object_unref(cancellable_);
        }
        cancellable_ = stopped_ ? nullptr : g_cancellable_new();
    }

    static bool isSandboxed()
    {
        return g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS)
            || (g_getenv("SNAP") && *g_getenv("SNAP"));
    }

    void portalAppeared(const std::string& owner)
    {
        if (stopped_ || !connection_ || owner.empty()) {
            return;
        }
        if (owner == portalOwner_
            && (registrationInFlight_ || previouslyAvailable_)) {
            return;
        }
        clearPortalRuntime();
        portalOwner_ = owner;
        resetCancellable();

        if (isSandboxed()) {
            inspectPortalVersion(owner);
            return;
        }

        registrationInFlight_ = true;
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        auto* context = new RegistrationContext{shared_from_this(), owner};
        g_dbus_connection_call(
            connection_,
            portalBusName,
            portalObjectPath,
            hostRegistryInterface,
            "Register",
            g_variant_new(
                "(s@a{sv})",
                cueletApplicationId,
                g_variant_builder_end(&options)),
            G_VARIANT_TYPE_UNIT,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            cancellable_,
            +[](GObject* source, GAsyncResult* asyncResult, gpointer userData) {
                std::unique_ptr<RegistrationContext> context(
                    static_cast<RegistrationContext*>(userData));
                auto& self = *context->self;
                GError* error = nullptr;
                GVariant* result = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source),
                    asyncResult,
                    &error);
                if (self.stopped_
                    || context->expectedOwner != self.portalOwner_) {
                    g_clear_pointer(&result, g_variant_unref);
                    g_clear_error(&error);
                    return;
                }
                self.registrationInFlight_ = false;
                if (!result) {
                    // The host Registry is explicitly allowed to disappear in
                    // future portal releases. In that case, continue with the
                    // portal's own host-app identification instead of
                    // incorrectly treating GlobalShortcuts as unavailable.
                    if (hostRegistryIsUnavailable(error)) {
                        g_clear_error(&error);
                        self.inspectPortalVersion(context->expectedOwner);
                        return;
                    }
                    self.previouslyAvailable_ = false;
                    if (self.detectionCallback_) {
                        self.detectionCallback_(PortalDetectionResult{
                            false,
                            0,
                            operationError(
                                error,
                                "Could not register Cuelet with the desktop portal."),
                        });
                    }
                    g_clear_error(&error);
                    return;
                }
                g_variant_unref(result);
                self.inspectPortalVersion(context->expectedOwner);
            },
            context);
    }

    void inspectPortalVersion(const std::string& owner)
    {
        if (stopped_ || !connection_ || owner != portalOwner_) {
            return;
        }
        subscribePortalSignals();

        auto* context = new PropertyCallContext{shared_from_this(), owner};
        g_dbus_connection_call(
            connection_,
            portalBusName,
            portalObjectPath,
            propertiesInterface,
            "Get",
            g_variant_new("(ss)", globalShortcutsInterface, "version"),
            G_VARIANT_TYPE("(v)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            cancellable_,
            +[](GObject* source, GAsyncResult* asyncResult, gpointer userData) {
                std::unique_ptr<PropertyCallContext> context(
                    static_cast<PropertyCallContext*>(userData));
                auto& self = *context->self;
                GError* error = nullptr;
                GVariant* result = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source),
                    asyncResult,
                    &error);
                if (self.stopped_
                    || context->expectedOwner != self.portalOwner_) {
                    g_clear_pointer(&result, g_variant_unref);
                    g_clear_error(&error);
                    return;
                }
                if (!result) {
                    self.previouslyAvailable_ = false;
                    if (self.detectionCallback_) {
                        self.detectionCallback_(PortalDetectionResult{
                            false,
                            0,
                            operationError(error, "Could not read the portal version."),
                        });
                    }
                    g_clear_error(&error);
                    return;
                }

                GVariant* boxed = nullptr;
                g_variant_get(result, "(@v)", &boxed);
                GVariant* value = boxed ? g_variant_get_variant(boxed) : nullptr;
                const guint version = value
                    && g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)
                    ? g_variant_get_uint32(value)
                    : 0;
                g_clear_pointer(&value, g_variant_unref);
                g_clear_pointer(&boxed, g_variant_unref);
                g_variant_unref(result);
                self.previouslyAvailable_ = version > 0;
                if (self.detectionCallback_) {
                    self.detectionCallback_(PortalDetectionResult{
                        version > 0,
                        version,
                        version > 0
                            ? std::string{}
                            : "The GlobalShortcuts portal reported no supported version.",
                    });
                }
            },
            context);
    }

    void portalVanished()
    {
        if (stopped_) {
            return;
        }
        const bool wasAvailable = previouslyAvailable_;
        previouslyAvailable_ = false;
        registrationInFlight_ = false;
        clearPortalRuntime();
        resetCancellable();
        if (wasAvailable && eventSink_.disconnected) {
            eventSink_.disconnected();
        } else if (detectionCallback_) {
            detectionCallback_(PortalDetectionResult{
                false,
                0,
                "The GlobalShortcuts portal service is unavailable.",
            });
        }
    }

    void clearPortalRuntime()
    {
        if (connection_) {
            for (const guint id : signalSubscriptionIds_) {
                g_dbus_connection_signal_unsubscribe(connection_, id);
            }
            signalSubscriptionIds_.clear();
            std::vector<std::string> pendingPaths;
            pendingPaths.reserve(pendingRequests_.size());
            for (const auto& [path, pending] : pendingRequests_) {
                (void)pending;
                pendingPaths.push_back(path);
            }
            for (const auto& path : pendingPaths) {
                removePendingRequest(path);
            }
        } else {
            signalSubscriptionIds_.clear();
            pendingRequests_.clear();
        }
        portalOwner_.clear();
    }

    void clearConnection()
    {
        clearPortalRuntime();
        if (connection_) {
            g_dbus_connection_close(connection_, nullptr, nullptr, nullptr);
            g_clear_object(&connection_);
        }
    }

    void subscribePortalSignals()
    {
        const auto subscribe = [&](const char* interfaceName,
                                   const char* signalName,
                                   const char* objectPath) {
            signalSubscriptionIds_.push_back(
                g_dbus_connection_signal_subscribe(
                    connection_,
                    portalBusName,
                    interfaceName,
                    signalName,
                    objectPath,
                    nullptr,
                    G_DBUS_SIGNAL_FLAGS_NONE,
                    +[](GDBusConnection*,
                        const gchar*,
                        const gchar* objectPath,
                        const gchar* interfaceName,
                        const gchar* signalName,
                        GVariant* parameters,
                        gpointer userData) {
                        static_cast<GioGlobalShortcutsPortalAdapter*>(userData)
                            ->portalSignal(
                                objectPath ? objectPath : "",
                                interfaceName ? interfaceName : "",
                                signalName ? signalName : "",
                                parameters);
                    },
                    this,
                    nullptr));
        };
        subscribe(globalShortcutsInterface, "Activated", portalObjectPath);
        subscribe(globalShortcutsInterface, "Deactivated", portalObjectPath);
        subscribe(globalShortcutsInterface, "ShortcutsChanged", portalObjectPath);
        subscribe(sessionInterface, "Closed", nullptr);
    }

    void portalSignal(
        const std::string& objectPath,
        const std::string& interfaceName,
        const std::string& signalName,
        GVariant* parameters)
    {
        if (interfaceName == sessionInterface && signalName == "Closed") {
            if (eventSink_.sessionClosed) {
                eventSink_.sessionClosed(objectPath);
            }
            return;
        }
        if (interfaceName != globalShortcutsInterface) {
            return;
        }
        if (signalName == "Activated" || signalName == "Deactivated") {
            const char* sessionHandle = nullptr;
            const char* shortcutId = nullptr;
            guint64 timestamp = 0;
            GVariant* options = nullptr;
            g_variant_get(
                parameters,
                "(&o&st@a{sv})",
                &sessionHandle,
                &shortcutId,
                &timestamp,
                &options);
            if (signalName == "Activated" && eventSink_.activated) {
                eventSink_.activated(
                    sessionHandle ? sessionHandle : "",
                    shortcutId ? shortcutId : "",
                    timestamp);
            } else if (signalName == "Deactivated" && eventSink_.deactivated) {
                eventSink_.deactivated(
                    sessionHandle ? sessionHandle : "",
                    shortcutId ? shortcutId : "",
                    timestamp);
            }
            g_clear_pointer(&options, g_variant_unref);
            return;
        }
        if (signalName == "ShortcutsChanged") {
            const char* sessionHandle = nullptr;
            GVariant* shortcuts = nullptr;
            g_variant_get(parameters, "(&o@a(sa{sv}))", &sessionHandle, &shortcuts);
            const auto bindings = parseBindingsArray(shortcuts);
            if (eventSink_.shortcutsChanged) {
                eventSink_.shortcutsChanged(
                    sessionHandle ? sessionHandle : "",
                    bindings);
            }
            g_clear_pointer(&shortcuts, g_variant_unref);
        }
    }

    void request(
        const char* method,
        ParametersBuilder parametersBuilder,
        ResponseHandler handler)
    {
        if (!connection_) {
            handler(
                {PortalResponseOutcome::Failed, "The portal is not connected."},
                nullptr);
            return;
        }
        const std::string handleToken = uniqueToken("cuelet_request_");
        const std::string expectedPath = requestPathForToken(connection_, handleToken);
        if (expectedPath.empty()) {
            handler(
                {PortalResponseOutcome::Failed, "Could not derive the portal request path."},
                nullptr);
            return;
        }
        addPendingRequest(expectedPath, std::move(handler));
        auto* context = new RequestCallContext{shared_from_this(), expectedPath};
        g_dbus_connection_call(
            connection_,
            portalBusName,
            portalObjectPath,
            globalShortcutsInterface,
            method,
            parametersBuilder(handleToken),
            G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            cancellable_,
            +[](GObject* source, GAsyncResult* asyncResult, gpointer userData) {
                std::unique_ptr<RequestCallContext> context(
                    static_cast<RequestCallContext*>(userData));
                GError* error = nullptr;
                GVariant* result = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source),
                    asyncResult,
                    &error);
                if (!result) {
                    context->self->failPendingRequest(
                        context->expectedPath,
                        operationError(error, "The portal method call failed."));
                    g_clear_error(&error);
                    return;
                }
                const char* actualPath = nullptr;
                g_variant_get(result, "(&o)", &actualPath);
                if (actualPath && context->expectedPath != actualPath) {
                    context->self->movePendingRequest(
                        context->expectedPath,
                        actualPath);
                }
                g_variant_unref(result);
            },
            context);
    }

    void addPendingRequest(
        const std::string& path,
        ResponseHandler handler)
    {
        const guint subscriptionId = g_dbus_connection_signal_subscribe(
            connection_,
            portalBusName,
            requestInterface,
            "Response",
            path.c_str(),
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,
                const gchar*,
                const gchar* objectPath,
                const gchar*,
                const gchar*,
                GVariant* parameters,
                gpointer userData) {
                static_cast<GioGlobalShortcutsPortalAdapter*>(userData)
                    ->requestResponse(objectPath ? objectPath : "", parameters);
            },
            this,
            nullptr);
        pendingRequests_[path] = PendingRequest{
            subscriptionId,
            std::move(handler),
        };
    }

    void removePendingRequest(const std::string& path)
    {
        const auto found = pendingRequests_.find(path);
        if (found == pendingRequests_.end()) {
            return;
        }
        const guint subscriptionId = found->second.subscriptionId;
        pendingRequests_.erase(found);
        if (connection_ && subscriptionId != 0) {
            g_dbus_connection_signal_unsubscribe(connection_, subscriptionId);
        }
    }

    void movePendingRequest(
        const std::string& oldPath,
        const std::string& newPath)
    {
        const auto found = pendingRequests_.find(oldPath);
        if (found == pendingRequests_.end() || newPath.empty()) {
            return;
        }
        ResponseHandler handler = std::move(found->second.handler);
        removePendingRequest(oldPath);
        addPendingRequest(newPath, std::move(handler));
    }

    void failPendingRequest(
        const std::string& path,
        const std::string& error)
    {
        const auto found = pendingRequests_.find(path);
        if (found == pendingRequests_.end()) {
            return;
        }
        ResponseHandler handler = std::move(found->second.handler);
        removePendingRequest(path);
        handler({PortalResponseOutcome::Failed, error}, nullptr);
    }

    void requestResponse(const std::string& path, GVariant* parameters)
    {
        const auto found = pendingRequests_.find(path);
        if (found == pendingRequests_.end()) {
            return;
        }
        ResponseHandler handler = std::move(found->second.handler);
        removePendingRequest(path);

        guint response = 2;
        GVariant* results = nullptr;
        g_variant_get(parameters, "(u@a{sv})", &response, &results);
        handler(operationForResponse(response), results);
        g_clear_pointer(&results, g_variant_unref);
    }
};

} // namespace

std::shared_ptr<GlobalShortcutsPortalAdapter> makeGioGlobalShortcutsPortalAdapter()
{
    return std::make_shared<GioGlobalShortcutsPortalAdapter>();
}

} // namespace cuelet_linux
