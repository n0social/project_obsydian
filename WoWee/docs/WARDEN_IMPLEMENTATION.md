# Warden Implementation

**Status**: Partial — infrastructure complete, execution path still has stubs (`src/game/warden_module.cpp:1023,1155,1167`). The module is loaded, decrypted, and parsed, but import binding and `WardenFuncList` extraction return placeholder values; the server falls back to fake responses via `GameHandler` (`warden_module.cpp:234`).
**WoW Version**: 3.3.5a (build 12340)

---

## Overview

Warden is WoW's client integrity checking system. The server sends encrypted modules
containing native x86 code; the client is expected to load and execute them, then
return check results.

Wowee handles this via Unicorn Engine CPU emulation — the x86 module is executed
directly in an emulated environment with Windows API hooks, without Wine or a Windows OS.

---

## Loading Pipeline (8 steps)

```
1. MD5       - Verify module checksum matches server challenge
2. RC4       - Decrypt module payload
3. RSA-2048  - Verify module signature (currently uses a hardcoded placeholder modulus — see Crypto Layer below)
4. zlib      - Decompress module
5. Parse     - Read PE header (sections, relocations, imports)
6. Relocate  - Apply base relocations to load address
7. Bind      - Resolve imports (Windows API stubs + Warden callbacks)
8. Init      - Call module entry point via Unicorn Engine
```

---

## Unicorn Engine Execution

The module entry point is called inside an Unicorn x86 emulator with:

- Executable memory mapped at the module's load address
- A simulated stack
- Windows API interception for calls the module makes

Intercepted APIs include `VirtualAlloc`, `GetTickCount`, `Sleep`, `ReadProcessMemory`,
and other common Warden targets. Each hook returns a plausible value without
accessing real process memory.

---

## Module Cache

After the first load, modules are written to disk:

```
~/.local/share/wowee/warden_cache/<MD5>.wdn
```

The key for lookup is the MD5 of the encrypted module. On subsequent connections
the cached decompressed module is loaded directly, skipping steps 1-4.

---

## Crypto Layer

| Algorithm | Purpose |
|-----------|---------|
| RC4 | Encrypt/decrypt Warden traffic (separate in/out ciphers) |
| MD5 | Module identity hash |
| SHA1 | HMAC and check hashes |
| RSA-2048 | Module signature verification |

The RSA public modulus used here is a hardcoded placeholder
(`include/game/warden_module.hpp`). The real 3.3.5a modulus lives in
the retail `WoW.exe` `.rdata` section and must be extracted from a
client install at runtime; until that path is implemented, signature
verification will not match a stock Blizzard module.

---

## Opcodes

- `SMSG_WARDEN_DATA` = 0x2E6 — server sends module + checks
- `CMSG_WARDEN_DATA` = 0x2E7 — client sends results

---

## Check Responses

| Check type | Opcode | Notes |
|------------|--------|-------|
| Module info | 0x00 | Returns module status |
| Hash check | 0x01 | File/memory hash validation |
| Lua check | 0x02 | Anti-addon detection |
| Timing check | 0x04 | Speedhack detection |
| Memory scan | 0x05 | Memory scan results |

---

## Key Files

```
include/game/warden_handler.hpp      - Packet handler interface
src/game/warden_handler.cpp          - handleWardenData + module manager init
include/game/warden_module.hpp       - Module loader interface
src/game/warden_module.cpp           - 8-step pipeline
include/game/warden_emulator.hpp     - Emulator interface
src/game/warden_emulator.cpp         - Unicorn Engine executor + API hooks
include/game/warden_crypto.hpp       - Crypto interface
src/game/warden_crypto.cpp           - RC4 / key derivation
include/game/warden_memory.hpp       - PE image + memory patch interface
src/game/warden_memory.cpp           - PE loader, runtime globals patching
```

---

## Performance

- First check (cold, no cache): ~120ms
- Subsequent checks (cache hit): ~1-5ms

---

## Dependencies

Requires `libunicorn-dev` (Unicorn Engine). The client compiles without it but
falls back to crypto-only mode (check responses are fabricated, not executed).

---

## References

- [WoWDev Wiki - Warden](https://wowdev.wiki/Warden)
- [WoWDev Wiki - SMSG_WARDEN_DATA](https://wowdev.wiki/SMSG_WARDEN_DATA)
- [TrinityCore Warden](https://github.com/TrinityCore/TrinityCore/tree/3.3.5/src/server/game/Warden)

---

**Last Updated**: 2026-02-17
