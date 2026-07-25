#pragma once

#include "cuelet/SoundTypes.h"

#include <string>
#include <vector>

namespace cuelet_linux {

enum class CliAction {
    Show,
    Help,
    ListSounds,
    ListCategories,
    PlayId,
    PlayName,
    PlayFile,
    Stop,
    StopAll,
    Hide,
    Exit,
    Rescan,
    Library,
};

struct CliCommand {
    CliAction action = CliAction::Show;
    std::string value;
    std::string workingDirectory;
    bool json = false;
    bool demo = false;
};

struct CliParseResult {
    CliCommand command;
    std::string error;

    bool ok() const { return error.empty(); }
};

CliParseResult parseCliArguments(const std::vector<std::string>& arguments);
std::string cliHelpText();
std::string cueletExecutablePath();
std::string resolveCliPath(const std::string& value, const std::string& workingDirectory);
std::string shortcutCommandForSound(const cuelet::SoundClip& clip,
                                    const std::string& executablePath);
std::string formatSoundList(const std::vector<cuelet::SoundClip>& clips,
                            bool json,
                            const std::string& executablePath = "cuelet");
std::string formatCategoryList(const std::vector<cuelet::Category>& categories, bool json);

} // namespace cuelet_linux
