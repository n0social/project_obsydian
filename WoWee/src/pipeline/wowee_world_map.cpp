#include "pipeline/wowee_world_map.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'P', 'X'};
constexpr uint32_t kVersion = 1;

template <typename T>
void writePOD(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool readPOD(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

std::string normalizePath(std::string base) {
    if (base.size() < 5 || base.substr(base.size() - 5) != ".womx") {
        base += ".womx";
    }
    return base;
}

size_t bitmapBytesFor(uint32_t gridSize) {
    return (static_cast<size_t>(gridSize) * gridSize + 7) / 8;
}

} // namespace

bool WoweeWorldMap::hasTile(uint32_t x, uint32_t y) const {
    if (x >= gridSize || y >= gridSize) return false;
    size_t bit = static_cast<size_t>(y) * gridSize + x;
    size_t byte = bit / 8;
    if (byte >= tileBitmap.size()) return false;
    return (tileBitmap[byte] >> (bit & 7)) & 1;
}

void WoweeWorldMap::setTile(uint32_t x, uint32_t y, bool present) {
    if (x >= gridSize || y >= gridSize) return;
    size_t bit = static_cast<size_t>(y) * gridSize + x;
    size_t byte = bit / 8;
    if (tileBitmap.size() <= byte) tileBitmap.resize(byte + 1, 0);
    uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
    if (present) tileBitmap[byte] |= mask;
    else         tileBitmap[byte] &= static_cast<uint8_t>(~mask);
}

uint32_t WoweeWorldMap::countTiles() const {
    uint32_t n = 0;
    size_t totalBits = static_cast<size_t>(gridSize) * gridSize;
    for (size_t bit = 0; bit < totalBits; ++bit) {
        size_t byte = bit / 8;
        if (byte >= tileBitmap.size()) break;
        if ((tileBitmap[byte] >> (bit & 7)) & 1) n++;
    }
    return n;
}

const char* WoweeWorldMap::worldTypeName(uint8_t t) {
    switch (t) {
        case Continent:    return "continent";
        case Instance:     return "instance";
        case Battleground: return "battleground";
        case Arena:        return "arena";
        default:           return "unknown";
    }
}

bool WoweeWorldMapLoader::save(const WoweeWorldMap& m,
                               const std::string& basePath) {
    if (m.gridSize == 0 || m.gridSize > 128) return false;
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    uint32_t nameLen = static_cast<uint32_t>(m.name.size());
    writePOD(os, nameLen);
    if (nameLen > 0) os.write(m.name.data(), nameLen);
    writePOD(os, m.worldType);
    writePOD(os, m.gridSize);
    uint16_t pad = 0;
    writePOD(os, pad);
    writePOD(os, m.defaultLightId);
    writePOD(os, m.defaultWeatherId);
    uint32_t reserved = 0;
    writePOD(os, reserved);
    writePOD(os, reserved);
    writePOD(os, reserved);
    size_t expectedBytes = bitmapBytesFor(m.gridSize);
    uint32_t bitmapBytes = static_cast<uint32_t>(expectedBytes);
    writePOD(os, bitmapBytes);
    // Pad bitmap up to expectedBytes if caller under-sized it
    // (setTile may not have grown it to the full grid coverage).
    if (m.tileBitmap.size() >= expectedBytes) {
        os.write(reinterpret_cast<const char*>(m.tileBitmap.data()),
                 expectedBytes);
    } else {
        if (!m.tileBitmap.empty()) {
            os.write(reinterpret_cast<const char*>(m.tileBitmap.data()),
                     m.tileBitmap.size());
        }
        std::vector<uint8_t> tail(expectedBytes - m.tileBitmap.size(), 0);
        os.write(reinterpret_cast<const char*>(tail.data()), tail.size());
    }
    return os.good();
}

WoweeWorldMap WoweeWorldMapLoader::load(const std::string& basePath) {
    WoweeWorldMap out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    uint32_t nameLen = 0;
    if (!readPOD(is, nameLen)) return out;
    if (nameLen > 0) {
        out.name.resize(nameLen);
        is.read(out.name.data(), nameLen);
        if (is.gcount() != static_cast<std::streamsize>(nameLen)) {
            out.name.clear();
            return out;
        }
    }
    if (!readPOD(is, out.worldType)) return out;
    if (!readPOD(is, out.gridSize)) return out;
    uint16_t pad = 0;
    if (!readPOD(is, pad)) return out;
    if (!readPOD(is, out.defaultLightId)) return out;
    if (!readPOD(is, out.defaultWeatherId)) return out;
    uint32_t reserved = 0;
    if (!readPOD(is, reserved)) return out;
    if (!readPOD(is, reserved)) return out;
    if (!readPOD(is, reserved)) return out;
    uint32_t bitmapBytes = 0;
    if (!readPOD(is, bitmapBytes)) return out;
    // Cap to a sane upper bound in case the file is corrupted —
    // a 128×128 grid is 2048 bytes, so anything > 4 KiB is a sign
    // of trouble.
    if (bitmapBytes > 4096) {
        out.gridSize = 0;
        return out;
    }
    out.tileBitmap.resize(bitmapBytes);
    if (bitmapBytes > 0) {
        is.read(reinterpret_cast<char*>(out.tileBitmap.data()), bitmapBytes);
        if (is.gcount() != static_cast<std::streamsize>(bitmapBytes)) {
            out.tileBitmap.clear();
            out.gridSize = 0;
            return out;
        }
    }
    return out;
}

bool WoweeWorldMapLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeWorldMap WoweeWorldMapLoader::makeContinent(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Continent;
    m.gridSize = 64;
    m.tileBitmap.assign(bitmapBytesFor(64), 0xFF);
    // Last byte may have spare bits past 64*64 — but 64*64 is
    // a multiple of 8 (4096), so this is exact and no masking
    // is needed.
    return m;
}

WoweeWorldMap WoweeWorldMapLoader::makeInstance(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Instance;
    m.gridSize = 4;
    m.tileBitmap.assign(bitmapBytesFor(4), 0);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
            m.setTile(x, y, true);
    return m;
}

WoweeWorldMap WoweeWorldMapLoader::makeArena(const std::string& mapName) {
    WoweeWorldMap m;
    m.name = mapName;
    m.worldType = WoweeWorldMap::Arena;
    m.gridSize = 1;
    m.tileBitmap.assign(bitmapBytesFor(1), 0);
    m.setTile(0, 0, true);
    return m;
}

} // namespace pipeline
} // namespace wowee
