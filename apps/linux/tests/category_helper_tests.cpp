#include "CueletWindowHelpers.h"
#include "TestSupport.h"

#include <set>

namespace {

void colorSelectionsAreStable()
{
    const auto& colors = cuelet_linux::colorPalette();
    CUELET_REQUIRE(!colors.empty());
    CUELET_REQUIRE(cuelet_linux::categoryColorIndex("#009688") < colors.size());
    CUELET_REQUIRE(colors[cuelet_linux::categoryColorIndex("#009688")].first == "Teal");
    CUELET_REQUIRE(cuelet_linux::categoryColorIndex("not-a-color") == 0);

    std::set<std::string> values;
    for (const auto& [name, value] : colors) {
        CUELET_REQUIRE(!name.empty());
        CUELET_REQUIRE(!value.empty());
        CUELET_REQUIRE(values.insert(value).second);
    }
}

void iconSelectionsCanonicalizeAliases()
{
    const auto& icons = cuelet_linux::iconChoices();
    CUELET_REQUIRE(!icons.empty());
    CUELET_REQUIRE(cuelet_linux::canonicalCategoryIconId("music.note") == "music-note");
    CUELET_REQUIRE(cuelet_linux::canonicalCategoryIconId("audio-x-generic-symbolic") == "music-note");
    CUELET_REQUIRE(cuelet_linux::categoryIconIndex("music.note") == cuelet_linux::categoryIconIndex("music-note"));
    CUELET_REQUIRE(cuelet_linux::categoryIconIndex("unknown-icon") == 0);

    std::set<std::string> ids;
    for (const auto& icon : icons) {
        CUELET_REQUIRE(!icon.label.empty());
        CUELET_REQUIRE(!icon.id.empty());
        CUELET_REQUIRE(!icon.linuxIconName.empty());
        CUELET_REQUIRE(ids.insert(icon.id).second);
        CUELET_REQUIRE(cuelet_linux::linuxCategoryIconName(icon.id) == icon.linuxIconName);
    }
}

void soundMenuPolicyMatchesStorageSafety()
{
    cuelet::SoundClip managed;
    managed.storageMode = cuelet::SoundStorageMode::Managed;
    managed.absolutePath = "/library/managed.wav";
    const auto managedPolicy = cuelet_linux::soundMenuPolicy(&managed);
    CUELET_REQUIRE(managedPolicy.canPlay);
    CUELET_REQUIRE(managedPolicy.canReveal);
    CUELET_REQUIRE(managedPolicy.canRename);
    CUELET_REQUIRE(managedPolicy.canDeleteManagedFile);

    cuelet::SoundClip linked = managed;
    linked.storageMode = cuelet::SoundStorageMode::Linked;
    linked.absolutePath = "/external/linked.wav";
    const auto linkedPolicy = cuelet_linux::soundMenuPolicy(&linked);
    CUELET_REQUIRE(linkedPolicy.canPlay);
    CUELET_REQUIRE(linkedPolicy.canReveal);
    CUELET_REQUIRE(linkedPolicy.canRename);
    CUELET_REQUIRE(!linkedPolicy.canDeleteManagedFile);

    cuelet::SoundClip missing = managed;
    missing.missing = true;
    const auto missingPolicy = cuelet_linux::soundMenuPolicy(&missing);
    CUELET_REQUIRE(!missingPolicy.canPlay);
    CUELET_REQUIRE(!missingPolicy.canReveal);
    CUELET_REQUIRE(!missingPolicy.canRename);
    CUELET_REQUIRE(!missingPolicy.canDeleteManagedFile);

    const auto stalePolicy = cuelet_linux::soundMenuPolicy(nullptr);
    CUELET_REQUIRE(!stalePolicy.canPlay);
    CUELET_REQUIRE(!stalePolicy.canReveal);
    CUELET_REQUIRE(!stalePolicy.canRename);
    CUELET_REQUIRE(!stalePolicy.canDeleteManagedFile);
}

void soundActivationKeysMatchGtkExpectations()
{
    CUELET_REQUIRE(cuelet_linux::isSoundActivationKey(GDK_KEY_Return));
    CUELET_REQUIRE(cuelet_linux::isSoundActivationKey(GDK_KEY_KP_Enter));
    CUELET_REQUIRE(cuelet_linux::isSoundActivationKey(GDK_KEY_space));
    CUELET_REQUIRE(!cuelet_linux::isSoundActivationKey(GDK_KEY_Escape));
}

void actionRowMarkupEscapesPortalAndCommandText()
{
    CUELET_REQUIRE(
        cuelet_linux::escapeMarkup("Press <Control><Alt>9 & play")
        == "Press &lt;Control&gt;&lt;Alt&gt;9 &amp; play");
    CUELET_REQUIRE(cuelet_linux::escapeMarkup("").empty());
}

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet category helper tests", [] {
        colorSelectionsAreStable();
        iconSelectionsCanonicalizeAliases();
        soundMenuPolicyMatchesStorageSafety();
        soundActivationKeysMatchGtkExpectations();
        actionRowMarkupEscapesPortalAndCommandText();
    });
}
