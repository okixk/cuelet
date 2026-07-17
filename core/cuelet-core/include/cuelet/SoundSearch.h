#pragma once

#include "cuelet/SoundTypes.h"

#include <vector>

namespace cuelet {

std::vector<SoundClip> filterAndSortSounds(const std::vector<SoundClip>& clips,
                                           const std::vector<Category>& categories,
                                           const FilterOptions& options);

const Category* categoryForId(const std::vector<Category>& categories, const std::string& id);

} // namespace cuelet
