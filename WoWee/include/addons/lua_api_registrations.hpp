// lua_api_registrations.hpp — Forward declarations for per-domain Lua API
// registration functions.  Called from LuaEngine::registerCoreAPI().
// Extracted from lua_engine.cpp as part of §5.1 (Tame LuaEngine).
#pragma once

struct lua_State;

namespace wowee::addons {

void registerUnitLuaAPI(lua_State* L);
void registerSpellLuaAPI(lua_State* L);
void registerInventoryLuaAPI(lua_State* L);
void registerQuestLuaAPI(lua_State* L);
void registerSocialLuaAPI(lua_State* L);
void registerSystemLuaAPI(lua_State* L);
void registerActionLuaAPI(lua_State* L);

} // namespace wowee::addons
