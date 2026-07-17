#include "cuelet/SoundSearch.h"

#include <algorithm>
#include <sstream>

namespace cuelet {

namespace {

std::vector<std::string> splitTokens(const std::string& query)
{
    std::istringstream stream(normalizeForSearch(query));
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string categoryName(const std::vector<Category>& categories, const SoundClip& clip)
{
    if (const auto* category = categoryForId(categories, clip.categoryId)) {
        return category->name;
    }
    return clip.categoryId == "uncategorized" ? "Uncategorized" : clip.categoryId;
}

std::string searchableText(const SoundClip& clip, const std::vector<Category>& categories)
{
    std::string text = clip.searchableName() + " " + clip.filename + " " + clip.relativePath + " "
        + clip.notes + " " + categoryName(categories, clip);
    for (const auto& alias : clip.aliases) {
        text += " " + alias;
    }
    return normalizeForSearch(text);
}

bool matchesScope(const SoundClip& clip, const FilterOptions& options)
{
    switch (options.scope) {
    case LibraryScope::Favorites:
        return clip.favorite;
    case LibraryScope::Recent:
        return clip.lastPlayedAt.has_value();
    case LibraryScope::Category:
        return clip.categoryId == options.categoryId;
    case LibraryScope::All:
    case LibraryScope::AllCategories:
        return true;
    }
    return true;
}

int rankingScore(const SoundClip& clip,
                 const std::vector<Category>& categories,
                 const std::string& normalizedQuery)
{
    if (normalizedQuery.empty()) {
        return 0;
    }

    const auto name = normalizeForSearch(clip.searchableName());
    const auto filename = normalizeForSearch(clip.filename);
    const auto category = normalizeForSearch(categoryName(categories, clip));

    if (name == normalizedQuery) {
        return 1000;
    }
    if (name.rfind(normalizedQuery, 0) == 0) {
        return 900;
    }
    if (filename == normalizedQuery) {
        return 800;
    }
    if (filename.rfind(normalizedQuery, 0) == 0) {
        return 700;
    }
    if (name.find(normalizedQuery) != std::string::npos) {
        return 600;
    }
    if (filename.find(normalizedQuery) != std::string::npos) {
        return 500;
    }
    if (category.find(normalizedQuery) != std::string::npos) {
        return 400;
    }
    return 0;
}

bool compareName(const SoundClip& left, const SoundClip& right, bool ascending)
{
    const auto lhs = normalizeForSearch(left.searchableName());
    const auto rhs = normalizeForSearch(right.searchableName());
    if (lhs != rhs) {
        return ascending ? lhs < rhs : lhs > rhs;
    }
    return normalizeForSearch(left.filename) < normalizeForSearch(right.filename);
}

} // namespace

const Category* categoryForId(const std::vector<Category>& categories, const std::string& id)
{
    const auto it = std::find_if(categories.begin(), categories.end(), [&](const Category& category) {
        return category.id == id;
    });
    return it == categories.end() ? nullptr : &*it;
}

std::vector<SoundClip> filterAndSortSounds(const std::vector<SoundClip>& clips,
                                           const std::vector<Category>& categories,
                                           const FilterOptions& options)
{
    const auto tokens = splitTokens(options.searchText);
    std::vector<SoundClip> filtered;
    filtered.reserve(clips.size());

    for (const auto& clip : clips) {
        if (!matchesScope(clip, options)) {
            continue;
        }

        const auto haystack = searchableText(clip, categories);
        const auto allTokensMatch = std::all_of(tokens.begin(), tokens.end(), [&](const std::string& token) {
            return haystack.find(token) != std::string::npos;
        });
        if (allTokensMatch) {
            filtered.push_back(clip);
        }
    }

    const auto normalizedQuery = normalizeForSearch(options.searchText);
    std::sort(filtered.begin(), filtered.end(), [&](const SoundClip& left, const SoundClip& right) {
        if (!normalizedQuery.empty()) {
            const auto lhsScore = rankingScore(left, categories, normalizedQuery);
            const auto rhsScore = rankingScore(right, categories, normalizedQuery);
            if (lhsScore != rhsScore) {
                return lhsScore > rhsScore;
            }
        }

        if (options.scope == LibraryScope::Recent) {
            const auto lhs = left.lastPlayedAt.value_or(0);
            const auto rhs = right.lastPlayedAt.value_or(0);
            if (lhs != rhs) {
                return lhs > rhs;
            }
        }

        switch (options.sort) {
        case SortOption::NameAscending:
            return compareName(left, right, true);
        case SortOption::NameDescending:
            return compareName(left, right, false);
        case SortOption::LatestAdded:
            if (left.addedAt != right.addedAt) {
                return left.addedAt > right.addedAt;
            }
            return compareName(left, right, true);
        case SortOption::OldestAdded:
            if (left.addedAt != right.addedAt) {
                return left.addedAt < right.addedAt;
            }
            return compareName(left, right, true);
        case SortOption::DurationShortest:
            if (left.durationSeconds != right.durationSeconds) {
                return left.durationSeconds < right.durationSeconds;
            }
            return compareName(left, right, true);
        case SortOption::DurationLongest:
            if (left.durationSeconds != right.durationSeconds) {
                return left.durationSeconds > right.durationSeconds;
            }
            return compareName(left, right, true);
        case SortOption::Category: {
            const auto lhs = normalizeForSearch(categoryName(categories, left));
            const auto rhs = normalizeForSearch(categoryName(categories, right));
            if (lhs != rhs) {
                return lhs < rhs;
            }
            return compareName(left, right, true);
        }
        }

        return compareName(left, right, true);
    });

    return filtered;
}

} // namespace cuelet
