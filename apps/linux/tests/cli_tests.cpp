#include "CueletCli.h"

#include <cassert>
#include <iostream>

using cuelet_linux::CliAction;

namespace {

void parsesCommandsAndModifiers()
{
    const auto list = cuelet_linux::parseCliArguments({"list-sounds", "--json"});
    assert(list.ok());
    assert(list.command.action == CliAction::ListSounds);
    assert(list.command.json);

    const auto play = cuelet_linux::parseCliArguments({"--play-name", "Door Knock"});
    assert(play.ok());
    assert(play.command.action == CliAction::PlayName);
    assert(play.command.value == "Door Knock");

    const auto library = cuelet_linux::parseCliArguments({"library", "/tmp/sounds"});
    assert(library.ok());
    assert(library.command.action == CliAction::Library);
    assert(library.command.value == "/tmp/sounds");

    const auto stop = cuelet_linux::parseCliArguments({"stop-id", "sound-1"});
    assert(stop.ok());
    assert(stop.command.action == CliAction::Stop);
    assert(stop.command.value == "sound-1");

    const auto defaultCommand = cuelet_linux::parseCliArguments({});
    assert(defaultCommand.ok());
    assert(defaultCommand.command.action == CliAction::Show);
}

void rejectsInvalidCommands()
{
    assert(!cuelet_linux::parseCliArguments({"play-id"}).ok());
    assert(!cuelet_linux::parseCliArguments({"show", "hide"}).ok());
    assert(!cuelet_linux::parseCliArguments({"play-id", "one", "--json"}).ok());
    assert(!cuelet_linux::parseCliArguments({"unknown"}).ok());
}

void resolvesForwardedPathsAgainstCallingDirectory()
{
    assert(cuelet_linux::resolveCliPath("sounds/hit.wav", "/home/user")
        == "/home/user/sounds/hit.wav");
    assert(cuelet_linux::resolveCliPath("/srv/sounds/hit.wav", "/home/user")
        == "/srv/sounds/hit.wav");
}

void formatsTextAndJsonSafely()
{
    cuelet::SoundClip clip;
    clip.id = "sound-1";
    clip.relativePath = "fx/hit.wav";
    clip.filename = "hit.wav";
    clip.displayName = "Impact \"Hit\"";
    clip.categoryId = "fx";
    clip.durationSeconds = 1.25;
    clip.favorite = true;

    clip.absolutePath = "/sounds/fx/hit.wav";
    clip.shortcut = cuelet::Shortcut{1, 2, "Ctrl+1"};
    const auto text = cuelet_linux::formatSoundList({clip}, false, "/opt/Cuelet App/cuelet");
    assert(text.find("id: sound-1\nname: Impact \"Hit\"\nfile: hit.wav\n") != std::string::npos);
    assert(text.find("relative-path: fx/hit.wav\nabsolute-path: /sounds/fx/hit.wav\n") != std::string::npos);
    assert(text.find("favorite: yes\nduration: 0:01\n") != std::string::npos);
    assert(text.find("shortcut: Ctrl+1\ncommand: '/opt/Cuelet App/cuelet' --play-id 'sound-1'\n\n") != std::string::npos);

    const auto json = cuelet_linux::formatSoundList({clip}, true, "/opt/cuelet");
    assert(json.find("\"name\":\"Impact \\\"Hit\\\"\"") != std::string::npos);
    assert(json.find("\"filename\":\"hit.wav\"") != std::string::npos);
    assert(json.find("\"relativePath\":\"fx/hit.wav\"") != std::string::npos);
    assert(json.find("\"absolutePath\":\"/sounds/fx/hit.wav\"") != std::string::npos);
    assert(json.find("\"durationFormatted\":\"0:01\"") != std::string::npos);
    assert(json.find("\"shortcut\":\"Ctrl+1\"") != std::string::npos);
    assert(json.find("\"command\":\"'/opt/cuelet' --play-id 'sound-1'\"") != std::string::npos);
    assert(json.find("\"favorite\":true") != std::string::npos);

    clip.id = "quote'check";
    assert(cuelet_linux::shortcutCommandForSound(clip, "/opt/cuelet")
        == "'/opt/cuelet' --play-id 'quote'\\''check'");

    clip.shortcut.reset();
    const auto noShortcut = cuelet_linux::formatSoundList({clip}, false, "/opt/cuelet");
    assert(noShortcut.find("shortcut: none\n") != std::string::npos);

    const cuelet::Category category{"fx", "Effects", "#123456", "bolt", true};
    const auto categories = cuelet_linux::formatCategoryList({category}, true);
    assert(categories == "[{\"id\":\"fx\",\"name\":\"Effects\",\"color\":\"#123456\",\"icon\":\"bolt\",\"editable\":true}]\n");
}

} // namespace

int main()
{
    parsesCommandsAndModifiers();
    rejectsInvalidCommands();
    resolvesForwardedPathsAgainstCallingDirectory();
    formatsTextAndJsonSafely();
    std::cout << "cuelet CLI tests passed\n";
    return 0;
}
