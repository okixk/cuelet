#include "CueletCli.h"
#include "CueletVersion.h"

#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace cuelet_linux {
namespace {

struct CommandSpec {
    CliAction action;
    bool needsValue;
};

const std::unordered_map<std::string, CommandSpec>& commandSpecs()
{
    static const std::unordered_map<std::string, CommandSpec> specs = {
        {"help", {CliAction::Help, false}},
        {"version", {CliAction::Version, false}},
        {"list-sounds", {CliAction::ListSounds, false}},
        {"list-categories", {CliAction::ListCategories, false}},
        {"play-id", {CliAction::PlayId, true}},
        {"play-name", {CliAction::PlayName, true}},
        {"play-file", {CliAction::PlayFile, true}},
        {"stop", {CliAction::Stop, true}},
        {"stop-id", {CliAction::Stop, true}},
        {"stop-all", {CliAction::StopAll, false}},
        {"show", {CliAction::Show, false}},
        {"hide", {CliAction::Hide, false}},
        {"exit", {CliAction::Exit, false}},
        {"quit", {CliAction::Exit, false}},
        {"rescan", {CliAction::Rescan, false}},
        {"library", {CliAction::Library, true}},
    };
    return specs;
}

std::string optionName(const std::string& argument)
{
    return argument.rfind("--", 0) == 0 ? argument.substr(2) : argument;
}

std::string jsonString(const std::string& value)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            } else {
                output << character;
            }
        }
    }
    output << '"';
    return output.str();
}

std::string shellQuote(const std::string& value)
{
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

std::string formattedDuration(double durationSeconds)
{
    const auto totalSeconds = static_cast<long long>(
        std::llround(std::isfinite(durationSeconds) ? std::max(0.0, durationSeconds) : 0.0));
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    std::ostringstream output;
    output << minutes << ':' << std::setw(2) << std::setfill('0') << seconds;
    return output.str();
}

} // namespace

CliParseResult parseCliArguments(const std::vector<std::string>& arguments)
{
    CliParseResult result;
    bool hasAction = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string name = optionName(arguments[index]);
        if (name == "json") {
            result.command.json = true;
            continue;
        }
        const auto found = commandSpecs().find(name);
        if (found == commandSpecs().end()) {
            result.error = "Unknown command: " + arguments[index];
            return result;
        }
        if (hasAction) {
            result.error = "Only one command can be used at a time.";
            return result;
        }

        hasAction = true;
        result.command.action = found->second.action;
        if (found->second.needsValue) {
            if (++index >= arguments.size()) {
                result.error = "Command requires a value: " + name;
                return result;
            }
            result.command.value = arguments[index];
            if (result.command.value.empty()) {
                result.error = "Command requires a non-empty value: " + name;
                return result;
            }
        }
    }

    if (result.command.json
        && result.command.action != CliAction::ListSounds
        && result.command.action != CliAction::ListCategories) {
        result.error = "--json can only be used with list-sounds or list-categories.";
    }
    return result;
}

std::string cliHelpText()
{
    return
        "Usage: cuelet --COMMAND [VALUE] [--json]\n"
        "\n"
        "Commands:\n"
        "  --help                    Show this help\n"
        "  --version                 Show Cuelet version\n"
        "  --list-sounds [--json]    List sounds\n"
        "  --list-categories [--json] List categories\n"
        "  --play-id ID              Play a sound by stable ID\n"
        "  --play-name NAME          Play a sound by display name\n"
        "  --play-file FILE          Play a library sound or audio file\n"
        "  --stop ID                 Stop a sound by ID or path\n"
        "  --stop-all                Stop all sounds\n"
        "  --show                    Show the Cuelet window\n"
        "  --hide                    Hide the Cuelet window\n"
        "  --exit                    Stop playback and exit Cuelet\n"
        "  --rescan                  Rescan the active library\n"
        "  --library FOLDER          Select and scan a library folder\n"
        "\n"
        "Subcommand forms such as 'cuelet play-id ID' are also accepted.\n";
}

std::string cliVersionText()
{
    return std::string("Cuelet ") + CUELET_VERSION + '\n';
}

std::string cueletExecutablePath()
{
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error || path.empty() ? "cuelet" : path.string();
}

std::string resolveCliPath(const std::string& value, const std::string& workingDirectory)
{
    const std::filesystem::path path(value);
    if (path.is_absolute() || workingDirectory.empty()) {
        return path.lexically_normal().string();
    }
    return (std::filesystem::path(workingDirectory) / path).lexically_normal().string();
}

std::vector<std::string> cliStopCandidates(
    const std::string& value,
    const std::string& workingDirectory,
    const std::vector<cuelet::SoundClip>& clips)
{
    std::vector<std::string> candidates;
    const auto libraryClip = std::find_if(
        clips.begin(), clips.end(), [&](const auto& clip) {
            return clip.id == value
                || clip.relativePath == value
                || clip.absolutePath == value
                || clip.filename == value;
        });
    if (libraryClip != clips.end()) {
        candidates.push_back(libraryClip->relativePath);
    }
    if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
        candidates.push_back(value);
    }
    const auto resolved = resolveCliPath(value, workingDirectory);
    if (std::find(candidates.begin(), candidates.end(), resolved) == candidates.end()) {
        candidates.push_back(resolved);
    }
    return candidates;
}

std::string shortcutCommandForSound(const cuelet::SoundClip& clip,
                                    const std::string& executablePath)
{
    return shellQuote(executablePath) + " --play-id " + shellQuote(clip.id);
}

std::string formatSoundList(const std::vector<cuelet::SoundClip>& clips,
                            bool json,
                            const std::string& executablePath)
{
    std::ostringstream output;
    if (json) {
        output << '[';
        for (std::size_t index = 0; index < clips.size(); ++index) {
            const auto& clip = clips[index];
            const std::string shortcut = clip.shortcut ? clip.shortcut->label : "";
            const std::string command = shortcutCommandForSound(clip, executablePath);
            const double numericDuration = std::isfinite(clip.durationSeconds)
                ? std::max(0.0, clip.durationSeconds)
                : 0.0;
            if (index > 0) {
                output << ',';
            }
            output << "{\"id\":" << jsonString(clip.id)
                   << ",\"name\":" << jsonString(clip.searchableName())
                   << ",\"filename\":" << jsonString(clip.filename)
                   << ",\"relativePath\":" << jsonString(clip.relativePath)
                   << ",\"absolutePath\":" << jsonString(clip.absolutePath)
                   << ",\"category\":" << jsonString(clip.categoryId)
                   << ",\"duration\":" << numericDuration
                   << ",\"durationFormatted\":" << jsonString(formattedDuration(clip.durationSeconds))
                   << ",\"favorite\":" << (clip.favorite ? "true" : "false")
                   << ",\"shortcut\":" << jsonString(shortcut)
                   << ",\"command\":" << jsonString(command)
                   << '}';
        }
        output << "]\n";
        return output.str();
    }

    for (const auto& clip : clips) {
        output << "id: " << clip.id << '\n'
               << "name: " << clip.searchableName() << '\n'
               << "file: " << clip.filename << '\n'
               << "relative-path: " << clip.relativePath << '\n'
               << "absolute-path: " << clip.absolutePath << '\n'
               << "category: " << clip.categoryId << '\n'
               << "favorite: " << (clip.favorite ? "yes" : "no") << '\n'
               << "duration: " << formattedDuration(clip.durationSeconds) << '\n'
               << "shortcut: " << (clip.shortcut ? clip.shortcut->label : "none") << '\n'
               << "command: " << shortcutCommandForSound(clip, executablePath) << "\n\n";
    }
    return output.str();
}

std::string formatCategoryList(const std::vector<cuelet::Category>& categories, bool json)
{
    std::ostringstream output;
    if (json) {
        output << '[';
        for (std::size_t index = 0; index < categories.size(); ++index) {
            const auto& category = categories[index];
            if (index > 0) {
                output << ',';
            }
            output << "{\"id\":" << jsonString(category.id)
                   << ",\"name\":" << jsonString(category.name)
                   << ",\"color\":" << jsonString(category.colorHex)
                   << ",\"icon\":" << jsonString(category.iconName)
                   << ",\"editable\":" << (category.editable ? "true" : "false")
                   << '}';
        }
        output << "]\n";
        return output.str();
    }

    for (const auto& category : categories) {
        output << category.id << '\t' << category.name << '\n';
    }
    return output.str();
}

} // namespace cuelet_linux
