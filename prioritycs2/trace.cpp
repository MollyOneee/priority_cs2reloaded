#include "pch.h"

#include "trace.hpp"
#include "pattern.hpp"

#include <array>
#include <cstddef>

namespace
{
    struct filter
    {
        std::uintptr_t vtable{}, mask{};
        std::array<std::int64_t, 2> v1{};
        std::array<int, 4> skip_handles{};
        std::array<std::int16_t, 2> collisions{};
        std::int16_t v2{};
        std::uint8_t layer{}, flags{}, v5{}, v6{};
        std::byte pad0[0x6]{};
        char v7{};
    };

    struct ray
    {
        game_trace::vector3 mins{}, maxs{};
        std::byte pad0[0x10]{};
        std::uint8_t type{};
        std::byte pad1[0x7]{};
    };

    struct result
    {
        void* surface{};
        std::uintptr_t hit_entity{};
        void* hitbox_data{};
        std::byte pad0[0x38]{};
        std::uint32_t contents{};
        std::byte pad1[0x24]{};
        game_trace::vector3 start_pos{}, end_pos{}, normal{}, position{};
        std::byte pad2[0x4]{};
        float fraction{};
        std::byte pad3[0x6]{};
        bool all_solid{};
        std::byte pad4[0x4d]{};
    };

    using filter_init_fn = void(__fastcall*)(filter*, std::uintptr_t, std::uintptr_t, std::uint8_t, int);
    using trace_ray_fn = bool(__fastcall*)(std::uintptr_t, ray*, const game_trace::vector3*, const game_trace::vector3*, const filter*, result*);
    std::uintptr_t manager{};
    filter_init_fn filter_init{};
    trace_ray_fn trace_ray{};
    bool attempted{};

    bool initialize()
    {
        if (attempted)
            return manager && filter_init && trace_ray;
        attempted = true;
        const auto manager_instruction = pattern::find(L"client.dll", "48 8B 0D ? ? ? ? 48 8D 34 52");
        const auto manager_storage = pattern::resolve_relative(manager_instruction, 3, 7);
        manager = manager_storage ? *reinterpret_cast<std::uintptr_t*>(manager_storage) : 0;
        const auto filter_call = pattern::find(L"client.dll", "7D 78 F3 44 0F 58 45 7C E8 ? ? ? ? F3 0F 10 05");
        const auto trace_call = pattern::find(L"client.dll", "20 48 8D 95 E0 00 00 00 E8 ? ? ? ? 48 8B 3D");
        filter_init = reinterpret_cast<filter_init_fn>(filter_call ? pattern::resolve_relative(filter_call + 8) : 0);
        trace_ray = reinterpret_cast<trace_ray_fn>(trace_call ? pattern::resolve_relative(trace_call + 8) : 0);
        return manager && filter_init && trace_ray;
    }
}

namespace game_trace
{
    bool available()
    {
        return initialize();
    }

    bool visible(const vector3& start, const vector3& end, std::uintptr_t target, std::uintptr_t skip)
    {
        if (!initialize())
            return false;
        filter trace_filter{};
        filter_init(&trace_filter, skip, 0x1c3003, 4, 7);
        ray trace_ray_data{};
        result trace_result{};
        trace_ray(manager, &trace_ray_data, &start, &end, &trace_filter, &trace_result);
        return trace_result.hit_entity == target || trace_result.fraction > 0.97f;
    }

    void reset()
    {
        manager = 0;
        filter_init = nullptr;
        trace_ray = nullptr;
        attempted = false;
    }
}
