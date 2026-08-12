#pragma once

#include <cstdint>

namespace wowee {
namespace game {

class World {
public:
    World() = default;
    ~World() = default;

    void update(float deltaTime);
    void loadMap(uint32_t mapId);
};

} // namespace game
} // namespace wowee
