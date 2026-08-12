#include "pipeline/wowee_weather.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'O', 'W', 'A'};
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
    if (base.size() < 4 || base.substr(base.size() - 4) != ".wow") {
        base += ".wow";
    }
    return base;
}

} // namespace

float WoweeWeather::totalWeight() const {
    float t = 0.0f;
    for (const auto& e : entries) t += e.weight;
    return t;
}

const char* WoweeWeather::typeName(uint32_t typeId) {
    switch (typeId) {
        case Clear:     return "clear";
        case Rain:      return "rain";
        case Snow:      return "snow";
        case Storm:     return "storm";
        case Sandstorm: return "sandstorm";
        case Fog:       return "fog";
        case Blizzard:  return "blizzard";
        default:        return "unknown";
    }
}

bool WoweeWeatherLoader::save(const WoweeWeather& w,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    uint32_t nameLen = static_cast<uint32_t>(w.name.size());
    writePOD(os, nameLen);
    if (nameLen > 0) os.write(w.name.data(), nameLen);
    uint32_t entryCount = static_cast<uint32_t>(w.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : w.entries) {
        writePOD(os, e.weatherTypeId);
        writePOD(os, e.minIntensity);
        writePOD(os, e.maxIntensity);
        writePOD(os, e.weight);
        writePOD(os, e.minDurationSec);
        writePOD(os, e.maxDurationSec);
    }
    return os.good();
}

WoweeWeather WoweeWeatherLoader::load(const std::string& basePath) {
    WoweeWeather out;
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
    uint32_t entryCount = 0;
    if (!readPOD(is, entryCount)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.weatherTypeId) ||
            !readPOD(is, e.minIntensity) ||
            !readPOD(is, e.maxIntensity) ||
            !readPOD(is, e.weight) ||
            !readPOD(is, e.minDurationSec) ||
            !readPOD(is, e.maxDurationSec)) {
            out.entries.clear();
            return out;
        }
    }
    return out;
}

bool WoweeWeatherLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeWeather WoweeWeatherLoader::makeTemperate(const std::string& zoneName) {
    WoweeWeather w;
    w.name = zoneName;
    w.entries.push_back({WoweeWeather::Clear, 0.0f, 0.0f, 6.0f, 300, 1800});
    w.entries.push_back({WoweeWeather::Rain,  0.3f, 0.7f, 2.0f, 120, 900});
    w.entries.push_back({WoweeWeather::Fog,   0.4f, 0.8f, 1.0f, 180, 600});
    return w;
}

WoweeWeather WoweeWeatherLoader::makeArctic(const std::string& zoneName) {
    WoweeWeather w;
    w.name = zoneName;
    w.entries.push_back({WoweeWeather::Snow,     0.3f, 0.7f, 5.0f, 300, 1800});
    w.entries.push_back({WoweeWeather::Blizzard, 0.7f, 1.0f, 2.0f, 120, 600});
    w.entries.push_back({WoweeWeather::Fog,      0.5f, 0.9f, 2.0f, 180, 900});
    w.entries.push_back({WoweeWeather::Clear,    0.0f, 0.0f, 1.0f, 180, 600});
    return w;
}

WoweeWeather WoweeWeatherLoader::makeDesert(const std::string& zoneName) {
    WoweeWeather w;
    w.name = zoneName;
    w.entries.push_back({WoweeWeather::Clear,     0.0f, 0.0f, 8.0f, 600, 2400});
    w.entries.push_back({WoweeWeather::Sandstorm, 0.5f, 0.9f, 2.0f, 120, 600});
    return w;
}

WoweeWeather WoweeWeatherLoader::makeStormy(const std::string& zoneName) {
    WoweeWeather w;
    w.name = zoneName;
    w.entries.push_back({WoweeWeather::Rain,  0.5f, 0.9f, 5.0f, 300, 1200});
    w.entries.push_back({WoweeWeather::Storm, 0.6f, 1.0f, 3.0f, 180, 600});
    w.entries.push_back({WoweeWeather::Fog,   0.4f, 0.7f, 1.0f, 120, 300});
    w.entries.push_back({WoweeWeather::Clear, 0.0f, 0.0f, 1.0f, 60, 240});
    return w;
}

} // namespace pipeline
} // namespace wowee
