#include "pipeline/asset_manifest.hpp"
#include "core/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace wowee {
namespace pipeline {

bool AssetManifest::load(const std::string& manifestPath) {
    auto startTime = std::chrono::steady_clock::now();

    loaded_ = false;
    basePath_.clear();
    manifestDir_.clear();
    entries_.clear();

    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open manifest: ", manifestPath);
        return false;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("Failed to parse manifest JSON: ", e.what());
        return false;
    }

    // Read header
    int version = doc.value("version", 0);
    if (version != 1) {
        LOG_ERROR("Unsupported manifest version: ", version);
        return false;
    }

    basePath_ = doc.value("basePath", "assets");
    manifestDir_ = std::filesystem::path(manifestPath).parent_path().string();

    // If basePath is relative, resolve against manifest directory
    if (!basePath_.empty() && basePath_[0] != '/') {
        basePath_ = manifestDir_ + "/" + basePath_;
    }

    // Parse entries
    auto& entriesObj = doc["entries"];
    if (!entriesObj.is_object()) {
        LOG_ERROR("Manifest missing 'entries' object");
        return false;
    }

    entries_.reserve(entriesObj.size());
    for (auto& [key, val] : entriesObj.items()) {
        Entry entry;
        entry.filesystemPath = val.value("p", "");
        entry.size = val.value("s", uint64_t(0));

        // Parse CRC32 from hex string
        std::string hexHash = val.value("h", "");
        if (!hexHash.empty()) {
            entry.crc32 = static_cast<uint32_t>(std::strtoul(hexHash.c_str(), nullptr, 16));
        } else {
            entry.crc32 = 0;
        }

        entries_[key] = std::move(entry);
    }

    loaded_ = true;

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_INFO("Loaded asset manifest: ", entries_.size(), " entries in ", ms, "ms (base: ", basePath_, ")");

    return true;
}

const AssetManifest::Entry* AssetManifest::lookup(const std::string& normalizedWowPath) const {
    auto it = entries_.find(normalizedWowPath);
    if (it != entries_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string AssetManifest::resolveFilesystemPath(const std::string& normalizedWowPath) const {
    auto it = entries_.find(normalizedWowPath);
    if (it == entries_.end()) {
        return {};
    }
    return basePath_ + "/" + it->second.filesystemPath;
}

bool AssetManifest::hasEntry(const std::string& normalizedWowPath) const {
    return entries_.find(normalizedWowPath) != entries_.end();
}

} // namespace pipeline
} // namespace wowee
