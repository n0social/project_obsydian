#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {

struct TocFile {
    std::string addonName;
    std::string basePath;

    std::unordered_map<std::string, std::string> directives;
    std::vector<std::string> files;

    std::string getTitle() const;
    std::string getInterface() const;
    bool isLoadOnDemand() const;
    std::vector<std::string> getSavedVariables() const;
    std::vector<std::string> getSavedVariablesPerCharacter() const;
};

std::optional<TocFile> parseTocFile(const std::string& tocPath);

} // namespace wowee::addons
