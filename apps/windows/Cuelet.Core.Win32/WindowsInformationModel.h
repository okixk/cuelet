#pragma once

#include <filesystem>
#include <string>

namespace cuelet::windows {

struct AboutInformation {
    std::wstring applicationName;
    std::wstring contributors;
    std::wstring version;
    std::wstring description;
    std::wstring licenseStatement;
    std::wstring spdxIdentifier;
    std::wstring projectUri;
    std::wstring issueTrackerUri;
};

AboutInformation aboutInformation(std::wstring version);

// Reads the three-component product version from Cuelet's PE version resource.
// The release metadata validator keeps that resource synchronized with VERSION.
std::wstring applicationVersionFromFile(const std::filesystem::path& executable);
std::wstring applicationVersionFromCurrentModule();

} // namespace cuelet::windows
