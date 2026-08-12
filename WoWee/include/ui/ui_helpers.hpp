#pragma once

#include <imgui.h>
#include <cstdint>
#include <cstdio>
#include "ui/ui_colors.hpp"
#include "game/entity.hpp"
#include "game/update_field_table.hpp"
#include "game/character.hpp"

namespace wowee::ui::helpers {

// ---- Duration / aura formatting ----

// Format a duration in seconds as compact text: "2h", "3:05", "42"
inline void fmtDurationCompact(char* buf, size_t sz, int secs) {
    if (secs >= 3600) snprintf(buf, sz, "%dh", secs / 3600);
    else if (secs >= 60) snprintf(buf, sz, "%d:%02d", secs / 60, secs % 60);
    else snprintf(buf, sz, "%d", secs);
}

// Render "Remaining: Xs" or "Remaining: Xm Ys" in a tooltip (light gray)
inline void renderAuraRemaining(int remainMs) {
    if (remainMs <= 0) return;
    int s = remainMs / 1000;
    char buf[32];
    if (s < 60) snprintf(buf, sizeof(buf), "Remaining: %ds", s);
    else snprintf(buf, sizeof(buf), "Remaining: %dm %ds", s / 60, s % 60);
    ImGui::TextColored(colors::kLightGray, "%s", buf);
}

// ---- Class color / name helpers ----

inline ImVec4 classColorVec4(uint8_t classId) { return getClassColor(classId); }
inline ImU32 classColorU32(uint8_t classId, int alpha = 255) { return getClassColorU32(classId, alpha); }

inline const char* classNameStr(uint8_t classId) {
    return game::getClassName(static_cast<game::Class>(classId));
}

// Extract class id from a unit's UNIT_FIELD_BYTES_0 update field.
// Returns 0 if the entity pointer is null or field is unset.
inline uint8_t entityClassId(const game::Entity* entity) {
    if (!entity) return 0;
    using UF = game::UF;
    uint32_t bytes0 = entity->getField(game::fieldIndex(UF::UNIT_FIELD_BYTES_0));
    return static_cast<uint8_t>((bytes0 >> 8) & 0xFF);
}

// ---- Shared UI data tables ----

// Aura dispel-type names (indexed by dispelType 0-4)
inline constexpr const char* kDispelNames[] = { "", "Magic", "Curse", "Disease", "Poison" };

// Raid mark names with symbol prefixes (indexed 0-7: Star..Skull)
inline constexpr const char* kRaidMarkNames[] = {
    "{*} Star", "{O} Circle", "{<>} Diamond", "{^} Triangle",
    "{)} Moon", "{ } Square", "{x} Cross", "{8} Skull"
};

} // namespace wowee::ui::helpers
