#pragma once

#include <string>

namespace wowee::core {

// Absolute path to the directory holding the running executable.
// Empty if it cannot be determined.
std::string getExecutableDir();

// Root directory for user config (login.cfg, settings.cfg, last_character.cfg,
// characters/). Two modes:
//   - Portable: if a "portable.txt" marker file, or an existing "config" folder,
//     sits next to the executable, config lives in <exe_dir>/config. This keeps
//     the whole client self-contained in one folder (USB sticks, clean uninstall,
//     easy backup of server profiles).
//   - Per-user (default): %APPDATA%\wowee on Windows, ~/.wowee elsewhere.
std::string getConfigRoot();

// One-time seeding of portable config. On the first launch after the user drops
// a "portable.txt" marker next to the executable (before any config folder
// exists), copies the existing per-user config tree into <exe_dir>/config so
// saved server profiles, settings, and characters carry over. No-op afterwards,
// and a no-op when not in portable mode. Call once at startup before config is
// read.
void migratePortableConfigIfNeeded();

}  // namespace wowee::core
