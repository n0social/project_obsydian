# Animation System

Unified, FSM-based animation system for all characters (players, NPCs, companions).
Every character uses the same `CharacterAnimator` — there is no separate NPC/Mob animator.

## Architecture

```
AnimationController          (thin adapter — bridges Renderer ↔ CharacterAnimator)
  └─ CharacterAnimator       (FSM composer — implements ICharacterAnimator)
       ├─ CombatFSM          (stun, hit reaction, spell cast, melee, ranged, charge)
       ├─ ActivityFSM         (emote, loot, sit/stand/kneel/sleep)
       ├─ LocomotionFSM       (idle, walk, run, sprint, jump, swim, strafe)
       └─ MountFSM            (mount idle, mount run, flight)

AnimationManager             (registry of CharacterAnimator instances by ID)
AnimCapabilitySet            (probed once per model — cached resolved anim IDs)
AnimCapabilityProbe          (queries which animations a model supports)
```

### Priority Resolution

`CharacterAnimator::resolveAnimation()` runs every frame. The first FSM to
return a valid `AnimOutput` wins:

1. **Mount** — if mounted, return `MOUNT` (overrides everything)
2. **Combat** — stun > hit reaction > spell > charge > melee/ranged > combat idle
3. **Activity** — emote > loot > sit/stand transitions
4. **Locomotion** — run/walk/sprint/jump/swim/strafe/idle

If no FSM produces a valid output, the last animation continues (STAY policy).

### Overlay Layer

After resolution, `applyOverlays()` substitutes stealth animation variants
(stealth idle, stealth walk, stealth run) without changing sub-FSM state.

## File Map

### Headers (`include/rendering/animation/`)

| File | Purpose |
|---|---|
| `i_animator.hpp` | Base interface: `onEvent()`, `update()` |
| `i_character_animator.hpp` | 20 virtual methods (combat, spells, emotes, mounts, etc.) |
| `character_animator.hpp` | FSM composer — the single animator class |
| `locomotion_fsm.hpp` | Movement states: idle, walk, run, sprint, jump, swim |
| `combat_fsm.hpp` | Combat states: melee, ranged, spell cast, stun, hit reaction |
| `activity_fsm.hpp` | Activity states: emote, loot, sit/stand/kneel |
| `mount_fsm.hpp` | Mount states: idle, run, flight, taxi |
| `anim_capability_set.hpp` | Probed capability flags + resolved animation IDs |
| `anim_capability_probe.hpp` | Probes a model for available animations |
| `anim_event.hpp` | `AnimEvent` enum (MOVE_START, MOVE_STOP, JUMP, etc.) |
| `animation_manager.hpp` | Central registry of CharacterAnimator instances |
| `weapon_type.hpp` | WeaponLoadout, RangedWeaponType enums |
| `emote_registry.hpp` | Emote name → animation ID lookup |
| `footstep_driver.hpp` | Footstep sound event driver |
| `sfx_state_driver.hpp` | State-transition SFX (jump, land, swim enter/exit) |
| `i_anim_renderer.hpp` | Interface for renderer animation queries |

### Sources (`src/rendering/animation/`)

| File | Purpose |
|---|---|
| `character_animator.cpp` | ICharacterAnimator implementation + priority resolver |
| `locomotion_fsm.cpp` | Locomotion state transitions + resolve logic |
| `combat_fsm.cpp` | Combat state transitions + resolve logic |
| `activity_fsm.cpp` | Activity state transitions + resolve logic |
| `mount_fsm.cpp` | Mount state transitions + resolve logic |
| `anim_capability_probe.cpp` | Model animation probing |
| `animation_manager.cpp` | Registry CRUD + bulk update |
| `emote_registry.cpp` | Emote database |
| `footstep_driver.cpp` | Footstep timing logic |
| `sfx_state_driver.cpp` | SFX transition detection |

### Controller (`include/rendering/animation_controller.hpp` + `src/rendering/animation_controller.cpp`)

Thin adapter that:
- Collects per-frame input from camera/renderer → `CharacterAnimator::FrameInput`
- Forwards state changes (combat, emote, spell, mount, etc.) → `CharacterAnimator`
- Reads `AnimOutput` → applies via `CharacterRenderer`
- Owns footstep and SFX drivers

## Key Types

- **`AnimEvent`** — discrete events: `MOVE_START`, `MOVE_STOP`, `JUMP`, `LAND`, `MOUNT`, `DISMOUNT`, etc.
- **`AnimOutput`** — result of FSM resolution: `{animId, loop, valid}`. `valid=false` means STAY.
- **`AnimCapabilitySet`** — probed once per model load. Caches resolved IDs and capability flags.
- **`CharacterAnimator::FrameInput`** — per-frame input struct (movement flags, timers, animation state queries).

## Adding a New Animation State

1. Decide which FSM owns the state (combat, activity, locomotion, or mount).
2. Add the state enum to the FSM's `State` enum.
3. Add transitions in the FSM's `resolve()` method.
4. Add resolved ID fields to `AnimCapabilitySet` if the animation needs model probing.
5. If the state needs external triggering, add a method to `ICharacterAnimator` and implement in `CharacterAnimator`.

## Tests

Each FSM has its own test file in `tests/`:
- `test_locomotion_fsm.cpp`
- `test_combat_fsm.cpp`
- `test_activity_fsm.cpp`
- `test_anim_capability.cpp`

Run all tests:
```bash
cd build && ctest --output-on-failure
```
