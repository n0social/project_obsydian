#pragma once

#include "ui/auth_screen.hpp"
#include "ui/realm_screen.hpp"
#include "ui/character_create_screen.hpp"
#include "ui/character_screen.hpp"
#include "ui/game_screen.hpp"
#include "ui/ui_services.hpp"
#include <memory>
#include <string>

// Forward declare SDL_Event
union SDL_Event;

namespace wowee {

// Forward declarations
namespace core { class Window; class AppearanceComposer; enum class AppState; }
namespace auth { class AuthHandler; }
namespace game { class GameHandler; }

namespace ui {

/**
 * UIManager - Manages all UI screens and ImGui rendering
 *
 * Coordinates screen transitions and rendering based on application state
 */
class UIManager {
public:
    UIManager();
    ~UIManager();

    /**
     * Initialize ImGui and UI screens
     * @param window Window instance for ImGui initialization
     */
    bool initialize(core::Window* window);

    /// Loads the game's own interface font, if it is in the data.
    ///
    /// Separate from initialize because the asset path is not settled until
    /// after the expansion profile is chosen, and separate from drawing because
    /// the glyph atlas is built once, before the first frame — adding a face
    /// afterwards means tearing the font texture down and rebuilding it, which
    /// cannot happen while a frame is in flight.
    void loadInterfaceFont(const std::string& dataRoot);

    /**
     * Shutdown ImGui and cleanup
     */
    void shutdown();

    /**
     * Update UI state
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * Render UI based on current application state
     * @param appState Current application state
     * @param authHandler Authentication handler reference
     * @param gameHandler Game handler reference
     */
    void render(core::AppState appState, auth::AuthHandler* authHandler, game::GameHandler* gameHandler);

    /**
     * Process SDL event for ImGui
     * @param event SDL event to process
     */
    void processEvent(const SDL_Event& event);

    /**
     * Get screen instances for callback setup
     */
    AuthScreen& getAuthScreen() { return *authScreen; }
    RealmScreen& getRealmScreen() { return *realmScreen; }
    CharacterCreateScreen& getCharacterCreateScreen() { return *characterCreateScreen; }
    CharacterScreen& getCharacterScreen() { return *characterScreen; }
    GameScreen& getGameScreen() { return *gameScreen; }

    // Dependency injection forwarding (Phase A singleton breaking)
    void setAppearanceComposer(core::AppearanceComposer* ac) {
        if (gameScreen) gameScreen->setAppearanceComposer(ac);
    }

    // UIServices injection (Phase B singleton breaking)
    void setServices(const UIServices& services) {
        services_ = services;
        if (gameScreen) gameScreen->setServices(services);
        if (authScreen) authScreen->setServices(services);
        if (characterScreen) characterScreen->setServices(services);
    }
    const UIServices& getServices() const { return services_; }

private:
    core::Window* window = nullptr;
    UIServices services_;  // Injected services

    // UI Screens
    std::unique_ptr<AuthScreen> authScreen;
    std::unique_ptr<RealmScreen> realmScreen;
    std::unique_ptr<CharacterCreateScreen> characterCreateScreen;
    std::unique_ptr<CharacterScreen> characterScreen;
    std::unique_ptr<GameScreen> gameScreen;

    // ImGui state
    bool imguiInitialized = false;
};

} // namespace ui
} // namespace wowee
