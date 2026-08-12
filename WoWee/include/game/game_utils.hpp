#pragma once

#include "game/expansion_profile.hpp"
#include "core/application.hpp"

namespace wowee {
namespace game {

inline bool isActiveExpansion(const char* expansionId) {
    auto& app = core::Application::getInstance();
    auto* registry = app.getExpansionRegistry();
    if (!registry) return false;
    auto* profile = registry->getActive();
    if (!profile) return false;
    return profile->id == expansionId;
}

inline bool isClassicLikeExpansion() {
    return isActiveExpansion("classic") || isActiveExpansion("turtle");
}

inline bool isPreWotlk() {
    return isClassicLikeExpansion() || isActiveExpansion("tbc");
}

// Shared item link formatter used by inventory, quest, spell, and social handlers.
// Centralised here so quality color table changes propagate everywhere.
inline std::string buildItemLink(uint32_t itemId, uint32_t quality, const std::string& name) {
    static const char* kQualHex[] = {
        "9d9d9d",  // 0 Poor
        "ffffff",  // 1 Common
        "1eff00",  // 2 Uncommon
        "0070dd",  // 3 Rare
        "a335ee",  // 4 Epic
        "ff8000",  // 5 Legendary
        "e6cc80",  // 6 Artifact
        "e6cc80",  // 7 Heirloom
    };
    uint32_t qi = quality < 8 ? quality : 1u;
    char buf[512];
    snprintf(buf, sizeof(buf), "|cff%s|Hitem:%u:0:0:0:0:0:0:0:0|h[%s]|h|r",
             kQualHex[qi], itemId, name.c_str());
    return buf;
}

} // namespace game
} // namespace wowee
