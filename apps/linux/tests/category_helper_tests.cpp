#include "CueletWindowHelpers.h"

#include <cassert>
#include <iostream>
#include <set>

namespace {

void colorSelectionsAreStable()
{
    const auto& colors = cuelet_linux::colorPalette();
    assert(!colors.empty());
    assert(cuelet_linux::categoryColorIndex("#009688") < colors.size());
    assert(colors[cuelet_linux::categoryColorIndex("#009688")].first == "Teal");
    assert(cuelet_linux::categoryColorIndex("not-a-color") == 0);

    std::set<std::string> values;
    for (const auto& [name, value] : colors) {
        assert(!name.empty());
        assert(!value.empty());
        assert(values.insert(value).second);
    }
}

void iconSelectionsCanonicalizeAliases()
{
    const auto& icons = cuelet_linux::iconChoices();
    assert(!icons.empty());
    assert(cuelet_linux::canonicalCategoryIconId("music.note") == "music-note");
    assert(cuelet_linux::canonicalCategoryIconId("audio-x-generic-symbolic") == "music-note");
    assert(cuelet_linux::categoryIconIndex("music.note") == cuelet_linux::categoryIconIndex("music-note"));
    assert(cuelet_linux::categoryIconIndex("unknown-icon") == 0);

    std::set<std::string> ids;
    for (const auto& icon : icons) {
        assert(!icon.label.empty());
        assert(!icon.id.empty());
        assert(!icon.linuxIconName.empty());
        assert(ids.insert(icon.id).second);
        assert(cuelet_linux::linuxCategoryIconName(icon.id) == icon.linuxIconName);
    }
}

} // namespace

int main()
{
    colorSelectionsAreStable();
    iconSelectionsCanonicalizeAliases();
    std::cout << "cuelet category helper tests passed\n";
    return 0;
}
