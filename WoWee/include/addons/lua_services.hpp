// lua_services.hpp — Dependency-injected services for Lua bindings.
// Replaces Application::getInstance() calls in domain API files (§5.2).
#pragma once

namespace wowee::core  { class Window; }
namespace wowee::audio { class AudioCoordinator; }
namespace wowee::game  { class ExpansionRegistry; }

namespace wowee::addons {

struct LuaServices {
    core::Window*            window            = nullptr;
    audio::AudioCoordinator* audioCoordinator  = nullptr;
    game::ExpansionRegistry* expansionRegistry = nullptr;
};

} // namespace wowee::addons
