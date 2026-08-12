# Multi-Expansion Architecture Guide

WoWee supports four World of Warcraft expansion profiles in a unified codebase using an expansion profile system. This guide explains how the multi-expansion support works.

## Supported Expansions

- **Vanilla (Classic) 1.12** - Original World of Warcraft
- **The Burning Crusade (TBC) 2.4.3** - First expansion
- **Wrath of the Lich King (WotLK) 3.3.5a** - Second expansion
- **Turtle WoW 1.18** - Custom Vanilla-based server with extended content

## Architecture Overview

The multi-expansion support is built on the **Expansion Profile** system:

1. **ExpansionProfile** (`include/game/expansion_profile.hpp`) - Metadata about each expansion
   - Defines protocol version, data paths, asset locations
   - Specifies which packet parsers to use

2. **Packet Parsers** - Expansion-specific message handling
   - `packet_parsers_classic.cpp` - Vanilla 1.12 / Turtle WoW message parsing
   - `packet_parsers_tbc.cpp` - TBC 2.4.3 message parsing
   - Default (WotLK 3.3.5a) parsers in `game_handler.cpp` and domain handlers

3. **Update Fields** - Expansion-specific entity data layout
   - Loaded from `update_fields.json` in expansion data directory
   - Defines UNIT_END, OBJECT_END, field indices for stats/health/mana

## How to Use Different Expansions

### At Startup

WoWee auto-detects the expansion based on:
1. Realm list response (protocol version)
2. Server build number
3. Update field count

### Manual Selection

Choose the expansion in the auth/realm screen at launch. The selection
calls `ExpansionRegistry::setActive(id)` (see `src/ui/auth_screen.cpp`)
which loads the matching opcode table, update-field layout, and DBC
layout for that expansion.

## Key Differences Between Expansions

### Packet Format Differences

#### SMSG_SPELL_COOLDOWN
- **Classic**: 12 bytes per entry (spellId + itemId + cooldown, no flags)
- **TBC/WotLK**: 8 bytes per entry (spellId + cooldown) + flags byte

#### SMSG_ACTION_BUTTONS
- **Classic**: 120 slots, no mode byte
- **TBC**: 132 slots, no mode byte
- **WotLK**: 144 slots + uint8 mode byte

#### SMSG_PARTY_MEMBER_STATS
- **Classic/TBC**: Full uint64 for guid, uint16 health
- **WotLK**: PackedGuid format, uint32 health

### Data Differences

- **Talent trees**: Different spell IDs and tree structure per expansion
- **Items**: Different ItemDisplayInfo entries
- **Spells**: Different base stats, cooldowns
- **Character textures**: Expansion-specific variants for races

## Adding Support for Another Expansion

1. Create new expansion profile entry in `expansion_profile.cpp`
2. Add packet parser file (`packet_parsers_*.cpp`) for message variants
3. Create update_fields.json with correct field layout
4. Test realm connection and character loading

## Code Patterns

### Checking Current Expansion

```cpp
#include "game/game_utils.hpp"

// Shared helpers (defined in game_utils.hpp)
if (isActiveExpansion("tbc")) {
    // TBC-specific code
}

if (isClassicLikeExpansion()) {
    // Classic or Turtle WoW
}

if (isPreWotlk()) {
    // Classic, Turtle, or TBC (not WotLK)
}
```

### Expansion-Specific Packet Parsing

```cpp
// In packet_parsers_*.cpp, implement expansion-specific logic
bool TbcPacketParsers::parseXxx(network::Packet& packet, XxxData& data) {
    // Custom logic for this expansion's packet format
}
```

## Common Issues

### "Update fields mismatch" Error
- Ensure `update_fields.json` matches server's field layout
- Check OBJECT_END and UNIT_END values
- Verify field indices for your target expansion

### "Unknown packet" Warnings
- Expansion-specific opcodes may not be registered
- Check packet parser registration in `game_handler.cpp`
- Verify expansion profile is active

### Packet Parsing Failures
- Each expansion has different struct layouts
- Always read data size first, then upfront validate
- Use size capping (e.g., max 100 items in list)

## References

- `include/game/expansion_profile.hpp` - Expansion metadata
- `include/game/game_utils.hpp` - `isActiveExpansion()`, `isClassicLikeExpansion()`, `isPreWotlk()`
- `src/game/packet_parsers_classic.cpp` / `packet_parsers_tbc.cpp` - Expansion-specific parsing
- `docs/status.md` - Current feature support
- `docs/` directory - Additional protocol documentation
