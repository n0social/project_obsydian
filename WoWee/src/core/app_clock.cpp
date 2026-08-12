#include "core/app_clock.hpp"

#include <chrono>

namespace wowee::core {

double appTimeSeconds() {
    using clock = std::chrono::steady_clock;
    // Fixed on the first call, so every later caller measures from the same
    // point however late it starts asking.
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

} // namespace wowee::core
