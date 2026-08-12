#include "ui/interface_fonts.hpp"

#include <algorithm>
#include <unordered_map>

namespace wowee::ui {

namespace {

/// The lower-case stem of a path, with any directory and extension removed.
/// "Fonts\\MORPHEUS.ttf" and "morpheus.ttf" both come out as "morpheus", which
/// is what lets a font object written one way find a file named the other.
std::string faceKey(const std::string& pathOrName) {
    const size_t slash = pathOrName.find_last_of("\\/");
    std::string stem = (slash == std::string::npos) ? pathOrName
                                                    : pathOrName.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return stem;
}

std::unordered_map<std::string, ImFont*>& faces() {
    static std::unordered_map<std::string, ImFont*> map;
    return map;
}

} // namespace

void registerInterfaceFace(const std::string& pathOrName, ImFont* font) {
    if (!font) return;
    faces()[faceKey(pathOrName)] = font;
}

ImFont* interfaceFace(const std::string& pathOrName) {
    if (pathOrName.empty()) return nullptr;
    const auto it = faces().find(faceKey(pathOrName));
    return (it == faces().end()) ? nullptr : it->second;
}

} // namespace wowee::ui
