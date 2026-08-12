#include "game/world.hpp"

namespace wowee {
namespace game {

void World::update([[maybe_unused]] float deltaTime) {
    // World state updates are handled by Application (terrain streaming, entity sync,
    // camera, etc.) and GameHandler (server packet processing). World is a thin
    // ownership token; per-frame logic lives in those subsystems.
}

void World::loadMap([[maybe_unused]] uint32_t mapId) {
    // Terrain loading is driven by Application::loadOnlineWorld() via TerrainManager.
    // This method exists as an extension point; no action needed here.
}

} // namespace game
} // namespace wowee
