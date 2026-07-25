#include "CueletCli.h"
#include "TestSupport.h"

using cuelet_linux::CliAction;

namespace {

void parsesCommandsAndModifiers()
{
    const auto list = cuelet_linux::parseCliArguments({"list-sounds", "--json"});
    CUELET_REQUIRE(list.ok());
    CUELET_REQUIRE(list.command.action == CliAction::ListSounds);
    CUELET_REQUIRE(list.command.json);

    const auto play = cuelet_linux::parseCliArguments({"--play-name", "Door Knock"});
    CUELET_REQUIRE(play.ok());
    CUELET_REQUIRE(play.command.action == CliAction::PlayName);
    CUELET_REQUIRE(play.command.value == "Door Knock");

    const auto library = cuelet_linux::parseCliArguments({"library", "/tmp/sounds"});
    CUELET_REQUIRE(library.ok());
    CUELET_REQUIRE(library.command.action == CliAction::Library);
    CUELET_REQUIRE(library.command.value == "/tmp/sounds");

    const auto stop = cuelet_linux::parseCliArguments({"stop-id", "sound-1"});
    CUELET_REQUIRE(stop.ok());
    CUELET_REQUIRE(stop.command.action == CliAction::Stop);
    CUELET_REQUIRE(stop.command.value == "sound-1");

    const auto exit = cuelet_linux::parseCliArguments({"--exit"});
    CUELET_REQUIRE(exit.ok());
    CUELET_REQUIRE(exit.command.action == CliAction::Exit);

    const auto defaultCommand = cuelet_linux::parseCliArguments({});
    CUELET_REQUIRE(defaultCommand.ok());
    CUELET_REQUIRE(defaultCommand.command.action == CliAction::Show);
}

void rejectsInvalidCommands()
{
    CUELET_REQUIRE(!cuelet_linux::parseCliArguments({"play-id"}).ok());
    CUELET_REQUIRE(!cuelet_linux::parseCliArguments({"show", "hide"}).ok());
    CUELET_REQUIRE(!cuelet_linux::parseCliArguments({"play-id", "one", "--json"}).ok());
    CUELET_REQUIRE(!cuelet_linux::parseCliArguments({"unknown"}).ok());
}

void resolvesForwardedPathsAgainstCallingDirectory()
{
    CUELET_REQUIRE(cuelet_linux::resolveCliPath("sounds/hit.wav", "/home/user")
        == "/home/user/sounds/hit.wav");
    CUELET_REQUIRE(cuelet_linux::resolveCliPath("/srv/sounds/hit.wav", "/home/user")
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
    CUELET_REQUIRE(text.find("id: sound-1\nname: Impact \"Hit\"\nfile: hit.wav\n") != std::string::npos);
    CUELET_REQUIRE(text.find("relative-path: fx/hit.wav\nabsolute-path: /sounds/fx/hit.wav\n") != std::string::npos);
    CUELET_REQUIRE(text.find("favorite: yes\nduration: 0:01\n") != std::string::npos);
    CUELET_REQUIRE(text.find("shortcut: Ctrl+1\ncommand: '/opt/Cuelet App/cuelet' --play-id 'sound-1'\n\n") != std::string::npos);

    const auto json = cuelet_linux::formatSoundList({clip}, true, "/opt/cuelet");
    CUELET_REQUIRE(json.find("\"name\":\"Impact \\\"Hit\\\"\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"filename\":\"hit.wav\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"relativePath\":\"fx/hit.wav\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"absolutePath\":\"/sounds/fx/hit.wav\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"durationFormatted\":\"0:01\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"shortcut\":\"Ctrl+1\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"command\":\"'/opt/cuelet' --play-id 'sound-1'\"") != std::string::npos);
    CUELET_REQUIRE(json.find("\"favorite\":true") != std::string::npos);

    clip.id = "quote'check";
    CUELET_REQUIRE(cuelet_linux::shortcutCommandForSound(clip, "/opt/cuelet")
        == "'/opt/cuelet' --play-id 'quote'\\''check'");

    clip.shortcut.reset();
    const auto noShortcut = cuelet_linux::formatSoundList({clip}, false, "/opt/cuelet");
    CUELET_REQUIRE(noShortcut.find("shortcut: none\n") != std::string::npos);

    const cuelet::Category category{"fx", "Effects", "#123456", "bolt", true};
    const auto categories = cuelet_linux::formatCategoryList({category}, true);
    CUELET_REQUIRE(categories == "[{\"id\":\"fx\",\"name\":\"Effects\",\"color\":\"#123456\",\"icon\":\"bolt\",\"editable\":true}]\n");
}

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet CLI tests", [] {
        parsesCommandsAndModifiers();
        rejectsInvalidCommands();
        resolvesForwardedPathsAgainstCallingDirectory();
        formatsTextAndJsonSafely();
    });
}
