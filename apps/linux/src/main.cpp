#include "CueletWindow.h"

#include <adwaita.h>
#include <gst/gst.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char* applicationId = "io.cuelet.Cuelet";
constexpr const char* windowDataKey = "cuelet-window";

CueletWindow* ensureWindow(GtkApplication* app)
{
    auto* window = static_cast<CueletWindow*>(g_object_get_data(G_OBJECT(app), windowDataKey));
    if (window && !window->isClosedForCliExit()) {
        return window;
    }

    window = new CueletWindow(ADW_APPLICATION(app));
    g_object_set_data_full(G_OBJECT(app), windowDataKey, window, [](gpointer data) {
        delete static_cast<CueletWindow*>(data);
    });
    return window;
}

void onActivate(GtkApplication* app, gpointer)
{
    ensureWindow(app)->present();
}

void onAbout(GSimpleAction*, GVariant*, gpointer userData)
{
    auto* app = GTK_APPLICATION(userData);
    CueletWindow* window = ensureWindow(app);
    window->present();
    window->showAbout();
}

void addApplicationActions(AdwApplication* app)
{
    GSimpleAction* about = g_simple_action_new("about", nullptr);
    g_signal_connect(about, "activate", G_CALLBACK(onAbout), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(about));
    g_object_unref(about);
}

void appendBooleanOption(GVariantDict* options,
                         const char* name,
                         std::vector<std::string>& arguments)
{
    if (g_variant_dict_contains(options, name)) {
        arguments.emplace_back(std::string("--") + name);
    }
}

void appendStringOption(GVariantDict* options,
                        const char* name,
                        std::vector<std::string>& arguments)
{
    const char* value = nullptr;
    if (g_variant_dict_lookup(options, name, "&s", &value)) {
        arguments.emplace_back(std::string("--") + name);
        arguments.emplace_back(value ? value : "");
    }
}

std::vector<std::string> commandArguments(GApplicationCommandLine* commandLine)
{
    int argumentCount = 0;
    char** rawArguments = g_application_command_line_get_arguments(commandLine, &argumentCount);
    std::vector<std::string> arguments;
    for (int index = 1; index < argumentCount; ++index) {
        arguments.emplace_back(rawArguments[index]);
    }
    g_strfreev(rawArguments);

    GVariantDict* options = g_application_command_line_get_options_dict(commandLine);
    appendBooleanOption(options, "json", arguments);
    appendBooleanOption(options, "version", arguments);
    appendBooleanOption(options, "list-sounds", arguments);
    appendBooleanOption(options, "list-categories", arguments);
    appendBooleanOption(options, "stop-all", arguments);
    appendBooleanOption(options, "show", arguments);
    appendBooleanOption(options, "hide", arguments);
    appendBooleanOption(options, "exit", arguments);
    appendBooleanOption(options, "quit", arguments);
    appendBooleanOption(options, "rescan", arguments);
    appendStringOption(options, "play-id", arguments);
    appendStringOption(options, "play-name", arguments);
    appendStringOption(options, "play-file", arguments);
    appendStringOption(options, "stop", arguments);
    appendStringOption(options, "stop-id", arguments);
    appendStringOption(options, "library", arguments);
    return arguments;
}

int onCommandLine(GApplication* application,
                  GApplicationCommandLine* commandLine,
                  gpointer)
{
    auto parsed = cuelet_linux::parseCliArguments(commandArguments(commandLine));
    if (!parsed.ok()) {
        g_application_command_line_printerr(
            commandLine,
            "cuelet: %s\nTry 'cuelet help' for usage.\n",
            parsed.error.c_str());
        return 2;
    }
    if (parsed.command.action == cuelet_linux::CliAction::Help) {
        g_application_command_line_print(commandLine, "%s", cuelet_linux::cliHelpText().c_str());
        return 0;
    }
    if (parsed.command.action == cuelet_linux::CliAction::Version) {
        g_application_command_line_print(commandLine, "%s", cuelet_linux::cliVersionText().c_str());
        return 0;
    }

    const char* workingDirectory = g_application_command_line_get_cwd(commandLine);
    parsed.command.workingDirectory = workingDirectory ? workingDirectory : "";

    const bool hadWindow = g_object_get_data(G_OBJECT(application), windowDataKey) != nullptr;
    auto* window = ensureWindow(GTK_APPLICATION(application));
    std::string standardOutput;
    std::string standardError;
    const int status = window->executeCliCommand(parsed.command, standardOutput, standardError);
    if (!standardOutput.empty()) {
        g_application_command_line_print(commandLine, "%s", standardOutput.c_str());
    }
    if (!standardError.empty()) {
        g_application_command_line_printerr(commandLine, "%s", standardError.c_str());
    }
    if (parsed.command.action == cuelet_linux::CliAction::Exit) {
        window->closeForCliExit();
        g_application_quit(application);
        return status;
    }
    const bool needsResidentWindow = status == 0
        && (parsed.command.action == cuelet_linux::CliAction::Show
            || parsed.command.action == cuelet_linux::CliAction::PlayId
            || parsed.command.action == cuelet_linux::CliAction::PlayName
            || parsed.command.action == cuelet_linux::CliAction::PlayFile);
    if (!hadWindow && !needsResidentWindow) {
        g_object_steal_data(G_OBJECT(application), windowDataKey);
        window->closeForCliExit();
        delete window;
    }
    return status;
}

void addCommandLineOptions(GApplication* application)
{
    const GOptionEntry entries[] = {
        {"json", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Use JSON output for list commands", nullptr},
        {"version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Show Cuelet version", nullptr},
        {"list-sounds", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "List sounds", nullptr},
        {"list-categories", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "List categories", nullptr},
        {"play-id", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Play a sound by ID", "ID"},
        {"play-name", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Play a sound by name", "NAME"},
        {"play-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Play an audio file", "FILE"},
        {"stop", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Stop a sound by ID or path", "ID"},
        {"stop-id", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Stop a sound by ID", "ID"},
        {"stop-all", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Stop all sounds", nullptr},
        {"show", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Show the window", nullptr},
        {"hide", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Hide the window", nullptr},
        {"exit", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Stop playback and exit Cuelet", nullptr},
        {"quit", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, nullptr, "Stop playback and exit Cuelet", nullptr},
        {"rescan", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, "Rescan the active library", nullptr},
        {"library", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, nullptr, "Select a library folder", "FOLDER"},
        {nullptr, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };
    g_application_add_main_option_entries(application, entries);
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> startupArguments;
    startupArguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        startupArguments.emplace_back(argv[index]);
    }
    const auto startupCommand = cuelet_linux::parseCliArguments(startupArguments);
    if (startupCommand.ok()
        && startupCommand.command.action == cuelet_linux::CliAction::Version) {
        std::cout << cuelet_linux::cliVersionText();
        return 0;
    }

    gst_init(&argc, &argv);
    adw_init();

    g_autoptr(AdwApplication) app = adw_application_new(
        applicationId,
        G_APPLICATION_HANDLES_COMMAND_LINE);
    addApplicationActions(app);
    addCommandLineOptions(G_APPLICATION(app));
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), nullptr);
    g_signal_connect(app, "command-line", G_CALLBACK(onCommandLine), nullptr);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
