#include "CueletWindow.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <unordered_set>

namespace {

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool hasSupportedAudioExtension(const std::filesystem::path& path)
{
    static const std::unordered_set<std::string> extensions = {
        ".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aif", ".aiff",
    };
    return extensions.count(asciiLower(path.extension().string())) > 0;
}

} // namespace

int CueletWindow::executeCliCommand(const cuelet_linux::CliCommand& command,
                                    std::string& standardOutput,
                                    std::string& standardError)
{
    using cuelet_linux::CliAction;

    auto fail = [&](const std::string& message) {
        standardError = "cuelet: " + message + "\n";
        return 1;
    };
    auto findById = [&](const std::string& id) {
        return cuelet_linux::soundByStableId(clips_, id);
    };
    auto playClip = [&](cuelet::SoundClip& clip) {
        if (clip.missing || clip.absolutePath.empty()) {
            return fail("sound file is missing: " + clip.searchableName());
        }
        if (!audio_.play(clip)) {
            return fail("could not play sound: " + clip.searchableName());
        }
        clip.lastPlayedAt = std::time(nullptr);
        notifyPlaybackStarted();
        saveMetadata();
        refreshContent();
        refreshNowPlaying();
        return 0;
    };

    switch (command.action) {
    case CliAction::Help:
        standardOutput = cuelet_linux::cliHelpText();
        return 0;
    case CliAction::Version:
        standardOutput = cuelet_linux::cliVersionText();
        return 0;
    case CliAction::ListSounds:
        standardOutput = cuelet_linux::formatSoundList(
            clips_,
            command.json,
            cuelet_linux::cueletExecutablePath());
        return 0;
    case CliAction::ListCategories:
        standardOutput = cuelet_linux::formatCategoryList(categories_, command.json);
        return 0;
    case CliAction::PlayId: {
        const auto clip = findById(command.value);
        if (!clip) {
            return fail("no sound has ID '" + command.value + "'.");
        }
        return playClip(*clip);
    }
    case CliAction::PlayName: {
        const std::string requested = asciiLower(command.value);
        const auto clip = std::find_if(clips_.begin(), clips_.end(), [&](const cuelet::SoundClip& item) {
            return asciiLower(item.searchableName()) == requested;
        });
        if (clip == clips_.end()) {
            return fail("no sound is named '" + command.value + "'.");
        }
        return playClip(*clip);
    }
    case CliAction::PlayFile: {
        const auto existing = std::find_if(clips_.begin(), clips_.end(), [&](const cuelet::SoundClip& clip) {
            return clip.relativePath == command.value
                || clip.absolutePath == command.value
                || clip.filename == command.value;
        });
        if (existing != clips_.end()) {
            return playClip(*existing);
        }

        std::error_code error;
        const auto path = std::filesystem::path(cuelet_linux::resolveCliPath(
            command.value,
            command.workingDirectory));
        if (error || !std::filesystem::is_regular_file(path, error) || error) {
            return fail("audio file does not exist: " + command.value);
        }
        if (!hasSupportedAudioExtension(path)) {
            return fail("unsupported audio file type: " + command.value);
        }
        cuelet::SoundClip external;
        external.id = path.string();
        external.absolutePath = path.string();
        external.relativePath = path.string();
        external.filename = path.filename().string();
        external.displayName = path.stem().string();
        if (!audio_.play(external)) {
            return fail("could not play file: " + command.value);
        }
        refreshNowPlaying();
        return 0;
    }
    case CliAction::Stop: {
        const auto candidates = cuelet_linux::cliStopCandidates(
            command.value, command.workingDirectory, clips_);
        const auto path = std::find_if(
            candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return audio_.isPlaying(candidate);
            });
        if (path == candidates.end()) {
            return fail("sound is not playing: " + command.value);
        }
        stopSound(*path);
        return 0;
    }
    case CliAction::StopAll:
        stopAll();
        return 0;
    case CliAction::Show:
        present();
        return 0;
    case CliAction::Hide:
        gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
        return 0;
    case CliAction::Exit:
        stopAll();
        return 0;
    case CliAction::Rescan:
        if (libraryPath_.empty()) {
            return fail("no library is selected.");
        }
        return rescanLibrary() ? 0 : fail("could not rescan the active library.");
    case CliAction::Library: {
        std::error_code error;
        const std::filesystem::path path(cuelet_linux::resolveCliPath(
            command.value,
            command.workingDirectory));
        if (!std::filesystem::is_directory(path, error) || error) {
            return fail("library folder does not exist: " + command.value);
        }
        return loadLibrary(path) ? 0 : fail("could not scan library folder: " + path.string());
    }
    }

    return fail("unsupported command.");
}
