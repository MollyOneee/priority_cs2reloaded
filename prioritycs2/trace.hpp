#pragma once

#include <cstdint>

namespace game_trace
{
    struct vector3 { float x{}, y{}, z{}; };
    bool available();
    bool visible(const vector3& start, const vector3& end, std::uintptr_t target, std::uintptr_t skip);
    void reset();
}
