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

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet category helper tests", [] {
        colorSelectionsAreStable();
        iconSelectionsCanonicalizeAliases();
    });
}
