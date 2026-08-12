#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace game {

// Conservative tile fallback for Duskwood's interior. Northern Duskwood and
// southern Elwynn share ADTs, so their bank areas must be distinguished through
// AreaTable parentage rather than broad tile rectangles.
constexpr bool isDuskwoodAdtTile(int tileX, int tileY) {
    const bool runtime = tileX >= 33 && tileX <= 35 && tileY >= 52 && tileY <= 53;
    const bool transposed = tileX >= 52 && tileX <= 53 && tileY >= 33 && tileY <= 35;
    return runtime || transposed;
}

struct ZoneInfo {
    uint32_t id;
    std::string name;
    std::vector<std::string> musicPaths;  // MPQ paths to music files
};

class ZoneManager {
public:
    void initialize();

    // Supplement zone music paths using AreaTable → ZoneMusic → SoundEntries DBC chain.
    // Safe to call after initialize(); idempotent and additive (does not remove existing paths).
    void enrichFromDBC(pipeline::AssetManager* assets);

    uint32_t getZoneId(int tileX, int tileY) const;
    uint32_t resolveAreaZoneId(uint32_t areaId) const;
    const ZoneInfo* getZoneInfo(uint32_t zoneId) const;
    std::string getRandomMusic(uint32_t zoneId);
    std::vector<std::string> getAllMusicPaths() const;

    // When false, file: (original soundtrack) tracks are excluded from the pool
    void setUseOriginalSoundtrack(bool use) { useOriginalSoundtrack_ = use; }
    bool getUseOriginalSoundtrack() const { return useOriginalSoundtrack_; }

private:
    // tile key = tileX * 100 + tileY
    std::unordered_map<int, uint32_t> tileToZone;
    std::unordered_map<uint32_t, ZoneInfo> zones;
    std::unordered_map<uint32_t, uint32_t> areaParents_;
    std::string lastPlayedMusic_;
    bool useOriginalSoundtrack_ = true;
};

} // namespace game
} // namespace wowee
