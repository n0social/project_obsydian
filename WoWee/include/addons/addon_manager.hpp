#pragma once

#include "addons/lua_engine.hpp"
#include "addons/toc_parser.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {

class AddonManager {
public:
    AddonManager();
    ~AddonManager();

    bool initialize(game::GameHandler* gameHandler, const LuaServices& services = {});
    void scanAddons(const std::string& addonsPath);
    void loadAllAddons();
    /// Parse an XML file, build what it declares, and follow its includes and
    /// scripts. depth guards against a file that includes itself.
    bool loadXmlFile(const std::string& path, int depth);
    /// Load the original interface from its own manifest, in the order it
    /// states. Opt-in through WOWEE_LOAD_FRAMEXML; see loadAllAddons.
    bool loadFrameXml(const std::string& frameXmlDir);
    /// Where FrameXML lives, remembered at scan time so the loader can find it.
    void setFrameXmlDir(const std::string& dir) { frameXmlDir_ = dir; }
    bool runScript(const std::string& code);
    void fireEvent(const std::string& event, const std::vector<std::string>& args = {});
    void update(float deltaTime);
    void shutdown();

    const std::vector<TocFile>& getAddons() const { return addons_; }
    LuaEngine* getLuaEngine() { return &luaEngine_; }
    bool isInitialized() const { return luaEngine_.isInitialized(); }

    // Per-addon enable/disable (persisted). Disabled addons are skipped by
    // loadAllAddons; changes take effect on the next load (world enter or /reload).
    bool isAddonEnabled(const std::string& addonName) const;
    void setAddonEnabled(const std::string& addonName, bool enabled);
    // True once any addon has been loaded this session (so the UI can note that a
    // toggle only applies after the next reload).
    bool addonsLoaded() const { return addonsLoaded_; }

    void saveAllSavedVariables();
    void setCharacterName(const std::string& name) { characterName_ = name; }

    /// Re-initialize the Lua VM and reload all addons (used by /reload).
    bool reload();

private:
    LuaEngine luaEngine_;
    std::vector<TocFile> addons_;
    game::GameHandler* gameHandler_ = nullptr;
    LuaServices luaServices_;
    std::string addonsPath_;

    bool loadAddon(const TocFile& addon);
    std::string getSavedVariablesPath(const TocFile& addon) const;
    std::string getSavedVariablesPerCharacterPath(const TocFile& addon) const;
    std::string characterName_;

    // addonName -> enabled. Absent means enabled (default on).
    std::unordered_map<std::string, bool> addonEnabled_;
    std::string frameXmlDir_;
    /// Why the last loadXmlFile returned false, so a caller loading many files
    /// can report the reasons together instead of leaving them scattered.
    std::string lastXmlError_;
    bool addonsLoaded_ = false;
    static std::string enabledStatePath();
    void loadEnabledState();
    void saveEnabledState() const;
};

} // namespace wowee::addons
