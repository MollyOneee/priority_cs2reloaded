#include "pch.h"

#include "esp.hpp"
#include "fonts.hpp"
#include "menu.hpp"
#include "pattern.hpp"
#include "settings.hpp"
#include "trace.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>

namespace
{
    struct vector3 { float x{}, y{}, z{}; };
    struct quaternion { float x{}, y{}, z{}, w{}; };
    struct bone_data { vector3 position{}; float scale{}; quaternion rotation{}; };
    struct matrix4x4 { float value[4][4]{}; };

    struct offsets_t
    {
        std::uint32_t pawn_alive{}, pawn_handle{}, player_name{}, health{}, team{};
        std::uint32_t scene_node{}, dormant{}, absolute_origin{}, model_state{};
        std::uint32_t spotted_state{}, spotted_mask{}, crosshair_entity{};
        std::uint32_t weapon_services{}, active_weapon{}, my_weapons{}, attribute_manager{}, item{}, item_definition_index{};
        std::uint32_t item_id_high{}, item_id_low{}, account_id{}, initialized{};
        std::uint32_t fallback_paint{}, fallback_seed{}, fallback_wear{}, fallback_stattrak{}, steam_id{};
        std::uint32_t hud_model_arms{}, child{}, owner{}, next_sibling{};

        bool valid() const
        {
            return pawn_alive && pawn_handle && player_name && health && team && scene_node && dormant && absolute_origin;
        }
    };

    struct state_t
    {
        std::uintptr_t entity_list_storage{}, local_controller_storage{}, view_matrix{};
        void* schema_system{};
        offsets_t offsets{};
        bool scan_attempted{}, initialized{};
        ULONGLONG next_retry_ms{};
    } state;

    struct player_snapshot
    {
        std::uintptr_t controller{}, pawn{};
        vector3 origin{};
        std::array<bone_data, 24> bones{};
        char name[64]{};
        int health{}, team{};
        bool bones_valid{}, visible{ true };
    };

    struct trigger_runtime_t
    {
        std::uintptr_t candidate{};
        ULONGLONG candidate_since{}, cooldown_until{}, revolver_release_at{};
        bool holding{};
    } trigger_runtime;

    struct original_skin_t
    {
        std::uint32_t item_id_high{}, item_id_low{}, account_id{};
        int paint_kit{}, seed{}, stattrak{};
        float wear{};
        bool initialized{}, captured{}, overridden{};
        int applied_paint{}, applied_seed{}, applied_stattrak{};
        float applied_wear{};
    };

    std::unordered_map<std::uintptr_t, original_skin_t> original_skins;
    using weapon_update_skin_fn = void(__fastcall*)(std::uintptr_t, int*);
    using weapon_set_mesh_group_fn = void(__fastcall*)(std::uintptr_t, bool);
    using weapon_update_composite_fn = void(__fastcall*)(std::uintptr_t, char);
    weapon_update_skin_fn weapon_update_skin{};
    weapon_set_mesh_group_fn weapon_set_mesh_group{};
    weapon_update_composite_fn weapon_update_composite{};
    std::uint32_t processed_skin_revision{};
    std::uint32_t last_skin_active_handle{};
    bool local_player_available{};

    // Plain SEH boundary: bad game pointers are rejected without a VirtualQuery per field.
    bool safe_copy(void* destination, const void* source, std::size_t size)
    {
        if (!destination || !source || size == 0)
            return false;
        __try
        {
            std::memcpy(destination, source, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::memset(destination, 0, size);
            return false;
        }
    }

    template <typename T>
    bool read(std::uintptr_t address, T& output)
    {
        return safe_copy(&output, reinterpret_cast<const void*>(address), sizeof(T));
    }

    template <typename T>
    T read(std::uintptr_t address)
    {
        T value{};
        read(address, value);
        return value;
    }

    template <typename T>
    bool write(std::uintptr_t address, const T& value)
    {
        if (!address)
            return false;
        __try
        {
            *reinterpret_cast<T*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <typename Result, typename... Args>
    Result call_virtual(void* object, std::size_t index, Args... args)
    {
        auto table = object ? *reinterpret_cast<void***>(object) : nullptr;
        if (!table)
        {
            if constexpr (!std::is_void_v<Result>) return Result{};
            else return;
        }
        using function_t = Result(__fastcall*)(void*, Args...);
        return reinterpret_cast<function_t>(table[index])(object, args...);
    }

    std::uint32_t schema_offset(const char* class_name, const char* field_name)
    {
        auto scope = call_virtual<void*>(state.schema_system, 13, "client.dll", static_cast<void*>(nullptr));
        if (!scope)
            return 0;
        std::uintptr_t class_info{};
        call_virtual<void>(scope, 2, &class_info, class_name);
        const auto fields = read<std::uintptr_t>(class_info + 0x30);
        const auto count = read<std::uint16_t>(class_info + 0x24);
        for (std::uint16_t index = 0; fields && index < count; ++index)
        {
            const auto field = fields + static_cast<std::uintptr_t>(index) * 0x20;
            const auto name_pointer = read<std::uintptr_t>(field);
            char name[96]{};
            if (name_pointer && safe_copy(name, reinterpret_cast<const void*>(name_pointer), sizeof(name) - 1) && std::strcmp(name, field_name) == 0)
                return read<std::uint32_t>(field + 0x10);
        }
        return 0;
    }

    bool initialize()
    {
        HMODULE client = GetModuleHandleW(L"client.dll");
        HMODULE schemas = GetModuleHandleW(L"schemasystem.dll");
        if (!client || !schemas)
            return false;
        if (!state.scan_attempted)
        {
            state.scan_attempted = true;
            // These are RIP-relative MOV/LEA instructions: displacement starts at +3,
            // and the complete instruction is 7 bytes long. The call-relative helper
            // defaults (1, 5) must not be used here.
            state.entity_list_storage = pattern::resolve_relative(
                pattern::find(client, "48 8B 0D ? ? ? ? 8B FB C1 EB 0E"), 3, 7);
            state.local_controller_storage = pattern::resolve_relative(
                pattern::find(client, "48 39 1D ? ? ? ? 75 04 B0 01"), 3, 7);
            state.view_matrix = pattern::resolve_relative(
                pattern::find(client, "48 8D 0D ? ? ? ? 48 C1 E0 06"), 3, 7);
        }
        if (!state.entity_list_storage || !state.local_controller_storage || !state.view_matrix)
            return false;

        using create_interface_t = void* (__cdecl*)(const char*, int*);
        const auto create_interface = reinterpret_cast<create_interface_t>(GetProcAddress(schemas, "CreateInterface"));
        state.schema_system = create_interface ? create_interface("SchemaSystem_001", nullptr) : nullptr;
        if (!state.schema_system)
            return false;

        state.offsets.pawn_alive = schema_offset("CCSPlayerController", "m_bPawnIsAlive");
        state.offsets.pawn_handle = schema_offset("CCSPlayerController", "m_hPlayerPawn");
        state.offsets.player_name = schema_offset("CCSPlayerController", "m_sSanitizedPlayerName");
        state.offsets.health = schema_offset("C_BaseEntity", "m_iHealth");
        state.offsets.team = schema_offset("C_BaseEntity", "m_iTeamNum");
        state.offsets.scene_node = schema_offset("C_BaseEntity", "m_pGameSceneNode");
        state.offsets.dormant = schema_offset("CGameSceneNode", "m_bDormant");
        state.offsets.absolute_origin = schema_offset("CGameSceneNode", "m_vecAbsOrigin");
        state.offsets.model_state = schema_offset("CSkeletonInstance", "m_modelState");
        state.offsets.spotted_state = schema_offset("C_CSPlayerPawn", "m_entitySpottedState");
        state.offsets.spotted_mask = schema_offset("EntitySpottedState_t", "m_bSpottedByMask");
        state.offsets.crosshair_entity = schema_offset("C_CSPlayerPawnBase", "m_iIDEntIndex");
        state.offsets.weapon_services = schema_offset("C_CSPlayerPawnBase", "m_pWeaponServices");
        state.offsets.active_weapon = schema_offset("CPlayer_WeaponServices", "m_hActiveWeapon");
        if (!state.offsets.active_weapon)
            state.offsets.active_weapon = schema_offset("CCSPlayer_WeaponServices", "m_hActiveWeapon");
        state.offsets.my_weapons = schema_offset("CPlayer_WeaponServices", "m_hMyWeapons");
        if (!state.offsets.my_weapons)
            state.offsets.my_weapons = schema_offset("CCSPlayer_WeaponServices", "m_hMyWeapons");
        state.offsets.attribute_manager = schema_offset("C_EconEntity", "m_AttributeManager");
        state.offsets.item = schema_offset("C_AttributeContainer", "m_Item");
        state.offsets.item_definition_index = schema_offset("C_EconItemView", "m_iItemDefinitionIndex");
        state.offsets.item_id_high = schema_offset("C_EconItemView", "m_iItemIDHigh");
        state.offsets.item_id_low = schema_offset("C_EconItemView", "m_iItemIDLow");
        state.offsets.account_id = schema_offset("C_EconItemView", "m_iAccountID");
        state.offsets.initialized = schema_offset("C_EconItemView", "m_bInitialized");
        state.offsets.fallback_paint = schema_offset("C_EconEntity", "m_nFallbackPaintKit");
        state.offsets.fallback_seed = schema_offset("C_EconEntity", "m_nFallbackSeed");
        state.offsets.fallback_wear = schema_offset("C_EconEntity", "m_flFallbackWear");
        state.offsets.fallback_stattrak = schema_offset("C_EconEntity", "m_nFallbackStatTrak");
        state.offsets.steam_id = schema_offset("CBasePlayerController", "m_steamID");
        state.offsets.hud_model_arms = schema_offset("C_CSPlayerPawn", "m_hHudModelArms");
        state.offsets.child = schema_offset("CGameSceneNode", "m_pChild");
        state.offsets.owner = schema_offset("CGameSceneNode", "m_pOwner");
        state.offsets.next_sibling = schema_offset("CGameSceneNode", "m_pNextSibling");
        if (!weapon_update_skin)
        {
            weapon_update_skin = reinterpret_cast<weapon_update_skin_fn>(pattern::find(client,
                "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 40 48 8B EA 48 8B F1 48 8D 54 24 60 E8 ? ? ? ? 48 8B 46 10"));
        }
        if (!weapon_set_mesh_group)
        {
            const auto call = pattern::find(client,
                "E8 ? ? ? ? E9 ? ? ? ? 83 B8 A8 0A 00 00 00 0F 8F ? ? ? ? 83 B8 C0 0A 00 00 00");
            weapon_set_mesh_group = reinterpret_cast<weapon_set_mesh_group_fn>(pattern::resolve_relative(call));
        }
        if (!weapon_update_composite)
        {
            const auto anchor = pattern::find(client, "00 40 0F B6 D7 48 8B CB E8 ? ? ? ? 48 8D 4C 24 20");
            weapon_update_composite = reinterpret_cast<weapon_update_composite_fn>(pattern::resolve_relative(anchor ? anchor + 8 : 0));
        }
        state.initialized = state.offsets.valid();
        return state.initialized;
    }

    std::uintptr_t entity_by_index(std::uintptr_t entity_list, int index)
    {
        if (!entity_list || index < 0 || index >= 16384)
            return 0;
        const auto chunk = read<std::uintptr_t>(entity_list + static_cast<std::uintptr_t>(index >> 9) * 8 + 0x10);
        return chunk ? read<std::uintptr_t>(chunk + static_cast<std::uintptr_t>(index & 0x1ff) * 112) : 0;
    }

    std::uintptr_t entity_by_handle(std::uintptr_t entity_list, std::uint32_t handle)
    {
        return !handle || handle == 0xffffffff ? 0 : entity_by_index(entity_list, static_cast<int>(handle & 0x7fff));
    }

    bool schema_class_name(std::uintptr_t entity, char* output, std::size_t output_size)
    {
        if (!entity || !output || output_size < 2)
            return false;
        const auto identity = read<std::uintptr_t>(entity + 0x10);
        const auto class_info = read<std::uintptr_t>(identity + 0x8);
        const auto name_container = read<std::uintptr_t>(class_info + 0x8);
        const auto name_pointer = read<std::uintptr_t>(name_container + 0x8);
        return name_pointer && safe_copy(output, reinterpret_cast<const void*>(name_pointer), output_size - 1);
    }

    bool is_player_controller(std::uintptr_t entity)
    {
        char name[32]{};
        return schema_class_name(entity, name, sizeof(name)) && std::strcmp(name, "CCSPlayerController") == 0;
    }

    std::uintptr_t hud_model_weapon(std::uintptr_t entity_list, std::uintptr_t local_pawn)
    {
        const auto& offsets = state.offsets;
        if (!entity_list || !local_pawn || !offsets.hud_model_arms || !offsets.child || !offsets.owner || !offsets.next_sibling)
            return 0;
        const auto arms = entity_by_handle(entity_list, read<std::uint32_t>(local_pawn + offsets.hud_model_arms));
        const auto scene = arms ? read<std::uintptr_t>(arms + offsets.scene_node) : 0;
        auto node = scene ? read<std::uintptr_t>(scene + offsets.child) : 0;
        for (int depth = 0; node && depth < 32; ++depth)
        {
            const auto owner = read<std::uintptr_t>(node + offsets.owner);
            char name[40]{};
            if (owner && schema_class_name(owner, name, sizeof(name)) && std::strcmp(name, "C_CS2HudModelWeapon") == 0)
                return owner;
            node = read<std::uintptr_t>(node + offsets.next_sibling);
        }
        return 0;
    }

    bool project(const vector3& world, const matrix4x4& matrix, const ImVec2& display, ImVec2& screen, float* clip_w = nullptr)
    {
        const float w = matrix.value[3][0] * world.x + matrix.value[3][1] * world.y + matrix.value[3][2] * world.z + matrix.value[3][3];
        const float x = matrix.value[0][0] * world.x + matrix.value[0][1] * world.y + matrix.value[0][2] * world.z + matrix.value[0][3];
        const float y = matrix.value[1][0] * world.x + matrix.value[1][1] * world.y + matrix.value[1][2] * world.z + matrix.value[1][3];
        if (clip_w) *clip_w = w;
        if (std::abs(w) < 0.001f) return false;
        screen = ImVec2(display.x * 0.5f * (1.0f + x / w), display.y * 0.5f * (1.0f - y / w));
        return w > 0.01f && std::isfinite(screen.x) && std::isfinite(screen.y);
    }

    bool collect_player(std::uintptr_t entity_list, std::uintptr_t controller, player_snapshot& result)
    {
        result.controller = controller;
        if (!read<bool>(controller + state.offsets.pawn_alive))
            return false;
        const auto pawn = entity_by_handle(entity_list, read<std::uint32_t>(controller + state.offsets.pawn_handle));
        result.pawn = pawn;
        result.health = pawn ? read<int>(pawn + state.offsets.health) : 0;
        const auto scene = pawn ? read<std::uintptr_t>(pawn + state.offsets.scene_node) : 0;
        if (!pawn || !scene || result.health <= 0 || result.health > 100 || read<bool>(scene + state.offsets.dormant))
            return false;
        result.origin = read<vector3>(scene + state.offsets.absolute_origin);
        result.team = read<int>(pawn + state.offsets.team);
        const auto name_pointer = read<std::uintptr_t>(controller + state.offsets.player_name);
        if (name_pointer)
            safe_copy(result.name, reinterpret_cast<const void*>(name_pointer), sizeof(result.name) - 1);

        if ((settings::esp_skeleton || settings::aim_enabled || settings::trigger_enabled) && state.offsets.model_state)
        {
            const auto cache = read<std::uintptr_t>(scene + state.offsets.model_state + 0x80);
            const int count = read<int>(scene + state.offsets.model_state + 0x8c);
            result.bones_valid = cache && count >= static_cast<int>(result.bones.size()) &&
                safe_copy(result.bones.data(), reinterpret_cast<const void*>(cache), sizeof(result.bones));
        }
        return true;
    }

    ImU32 color_u32(const ImVec4& color) { return ImGui::ColorConvertFloat4ToU32(color); }

    void outlined_text(ImDrawList* draw, ImFont* font, float size, const ImVec2& position, ImU32 color, const char* text)
    {
        constexpr ImU32 outline = IM_COL32(0, 0, 0, 220);
        draw->AddText(font, size, ImVec2(position.x - 1, position.y), outline, text);
        draw->AddText(font, size, ImVec2(position.x + 1, position.y), outline, text);
        draw->AddText(font, size, ImVec2(position.x, position.y - 1), outline, text);
        draw->AddText(font, size, ImVec2(position.x, position.y + 1), outline, text);
        draw->AddText(font, size, position, color, text);
    }

    void draw_skeleton(ImDrawList* draw, const player_snapshot& player, const matrix4x4& matrix, const ImVec2& display, ImU32 color)
    {
        if (!player.bones_valid)
            return;
        constexpr unsigned none = ~0u;
        constexpr std::array chains{
            std::array<unsigned, 7>{ 7, 6, 23, 4, 3, 2, 1 },
            std::array<unsigned, 7>{ 11, 10, 9, 8, 23, none, none },
            std::array<unsigned, 7>{ 15, 14, 13, 12, 23, none, none },
            std::array<unsigned, 7>{ 19, 18, 17, 1, none, none, none },
            std::array<unsigned, 7>{ 22, 21, 20, 1, none, none, none }
        };
        for (const auto& chain : chains)
        {
            ImVec2 previous{};
            bool has_previous = false;
            for (unsigned bone : chain)
            {
                if (bone == none) break;
                ImVec2 screen{};
                if (!project(player.bones[bone].position, matrix, display, screen)) { has_previous = false; continue; }
                if (has_previous)
                {
                    draw->AddLine(previous, screen, IM_COL32(0, 0, 0, 210), 3.0f);
                    draw->AddLine(previous, screen, color, 1.0f);
                }
                previous = screen;
                has_previous = true;
            }
        }
    }

    void draw_offscreen_arrow(ImDrawList* draw, const vector3& origin, const matrix4x4& matrix, const ImVec2& display, ImU32 color)
    {
        const float w = matrix.value[3][0] * origin.x + matrix.value[3][1] * origin.y + matrix.value[3][2] * origin.z + matrix.value[3][3];
        float dx = matrix.value[0][0] * origin.x + matrix.value[0][1] * origin.y + matrix.value[0][2] * origin.z + matrix.value[0][3];
        float dy = -(matrix.value[1][0] * origin.x + matrix.value[1][1] * origin.y + matrix.value[1][2] * origin.z + matrix.value[1][3]);
        if (w < 0.0f) { dx = -dx; dy = -dy; }
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.001f) return;
        dx /= length; dy /= length;
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        const float radius = std::min(display.x, display.y) * 0.39f;
        const ImVec2 base(center.x + dx * radius, center.y + dy * radius);
        const ImVec2 perpendicular(-dy, dx);
        const ImVec2 tip(base.x + dx * 11.0f, base.y + dy * 11.0f);
        const ImVec2 left(base.x - dx * 6.0f + perpendicular.x * 7.0f, base.y - dy * 6.0f + perpendicular.y * 7.0f);
        const ImVec2 right(base.x - dx * 6.0f - perpendicular.x * 7.0f, base.y - dy * 6.0f - perpendicular.y * 7.0f);
        draw->AddTriangleFilled(tip, left, right, IM_COL32(0, 0, 0, 220));
        draw->AddTriangleFilled(ImVec2(base.x + dx * 9.0f, base.y + dy * 9.0f), left, right, color);
    }

    float aim_radius(const ImVec2& display)
    {
        constexpr float degrees_to_radians = 0.017453292519943295f;
        return std::tan(settings::aim_fov * 0.5f * degrees_to_radians) * display.x * 0.5f;
    }

    bool aim_key_down()
    {
        if (settings::aim_key == 3)
            return true;
        constexpr int keys[] = { VK_LBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        const int index = std::clamp(settings::aim_key, 0, 2);
        return (GetAsyncKeyState(keys[index]) & 0x8000) != 0;
    }

    bool point_visible(const player_snapshot& player, const vector3& eye, const vector3& point, std::uintptr_t local_pawn)
    {
        if (!settings::aim_visible_only)
            return true;
        if (!game_trace::available())
            return player.visible;
        return game_trace::visible({ eye.x, eye.y, eye.z }, { point.x, point.y, point.z }, player.pawn, local_pawn);
    }

    void run_aim(const std::array<player_snapshot, 64>& players, std::size_t player_count,
        const matrix4x4& matrix, const ImVec2& display, int local_team, const vector3& eye, std::uintptr_t local_pawn)
    {
        if (!settings::aim_enabled || menu::is_open() || !aim_key_down())
            return;

        constexpr unsigned aim_bones[] = { 7, 6, 4, 1 };
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        const float radius = aim_radius(display);
        float best_distance_squared = radius * radius;
        ImVec2 best_point{};
        bool found{};

        for (std::size_t index = 0; index < player_count; ++index)
        {
            const player_snapshot& player = players[index];
            if (!player.bones_valid || (!settings::aim_teammates && local_team && player.team == local_team))
                continue;
            for (std::size_t hitbox = 0; hitbox < settings::aim_hitboxes.size(); ++hitbox)
            {
                if (!settings::aim_hitboxes[hitbox])
                    continue;
                const vector3& world_point = player.bones[aim_bones[hitbox]].position;
                ImVec2 point{};
                if (!project(world_point, matrix, display, point))
                    continue;
                const float dx = point.x - center.x;
                const float dy = point.y - center.y;
                const float distance_squared = dx * dx + dy * dy;
                if (distance_squared >= best_distance_squared || !point_visible(player, eye, world_point, local_pawn))
                    continue;
                best_distance_squared = distance_squared;
                best_point = point;
                found = true;
            }
        }
        if (!found)
            return;

        static float residual_x{};
        static float residual_y{};
        const float smoothing = std::max(1.0f, settings::aim_smooth);
        const float response = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 24.0f / smoothing);
        const float movement_x = (best_point.x - center.x) * response + residual_x;
        const float movement_y = (best_point.y - center.y) * response + residual_y;
        const LONG delta_x = static_cast<LONG>(std::round(movement_x));
        const LONG delta_y = static_cast<LONG>(std::round(movement_y));
        residual_x = movement_x - static_cast<float>(delta_x);
        residual_y = movement_y - static_cast<float>(delta_y);
        if (!delta_x && !delta_y)
            return;

        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = delta_x;
        input.mi.dy = delta_y;
        SendInput(1, &input, sizeof(input));
    }

    void mouse_button(bool down)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(input));
    }

    std::uint16_t active_weapon_id(std::uintptr_t entity_list, std::uintptr_t local_pawn)
    {
        const auto& offsets = state.offsets;
        if (!local_pawn || !offsets.weapon_services || !offsets.active_weapon || !offsets.attribute_manager ||
            !offsets.item || !offsets.item_definition_index)
            return 0;
        const auto services = read<std::uintptr_t>(local_pawn + offsets.weapon_services);
        const auto weapon = services ? entity_by_handle(entity_list, read<std::uint32_t>(services + offsets.active_weapon)) : 0;
        if (!weapon)
            return 0;
        return read<std::uint16_t>(weapon + offsets.attribute_manager + offsets.item + offsets.item_definition_index);
    }

    float estimated_direct_damage(std::uint16_t definition, float distance)
    {
        float base = 100.0f;
        switch (definition)
        {
        case 1: base = 53.0f; break; case 2: base = 38.0f; break; case 3: base = 32.0f; break;
        case 4: base = 30.0f; break; case 7: base = 36.0f; break; case 8: base = 28.0f; break;
        case 9: base = 115.0f; break; case 10: base = 30.0f; break; case 11: base = 40.0f; break;
        case 13: base = 30.0f; break; case 14: base = 32.0f; break; case 16: base = 33.0f; break;
        case 17: base = 29.0f; break; case 19: base = 26.0f; break; case 23: base = 27.0f; break;
        case 24: base = 35.0f; break; case 25: base = 80.0f; break; case 26: base = 27.0f; break;
        case 27: base = 90.0f; break; case 28: base = 35.0f; break; case 29: base = 96.0f; break;
        case 30: base = 33.0f; break; case 32: base = 35.0f; break; case 33: base = 29.0f; break;
        case 34: base = 26.0f; break; case 35: base = 78.0f; break; case 36: base = 38.0f; break;
        case 38: base = 80.0f; break; case 39: base = 30.0f; break; case 40: base = 88.0f; break;
        case 60: base = 38.0f; break; case 61: base = 35.0f; break; case 63: base = 31.0f; break;
        case 64: base = 86.0f; break;
        default: break;
        }
        return base * std::pow(0.98f, distance / 500.0f);
    }

    void update_skin_material(std::uintptr_t weapon)
    {
        if (!weapon_update_skin || !weapon)
            return;
        __try
        {
            int empty_mask[3]{};
            weapon_update_skin(weapon, empty_mask);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void rebuild_weapon_skin(std::uintptr_t entity_list, std::uintptr_t local_pawn, std::uintptr_t weapon, bool update_view_model)
    {
        if (!weapon)
            return;
        update_skin_material(weapon);
        __try
        {
            // These are the same refresh steps used by Velocity after changing the
            // fallback econ fields. Merely writing the paint kit does not invalidate
            // CS2's composite material cache.
            *reinterpret_cast<bool*>(weapon + 0x18b8) = false;
            if (weapon_set_mesh_group) weapon_set_mesh_group(weapon + 0x608, true);
            call_virtual<void>(reinterpret_cast<void*>(weapon), 11, 1);
            call_virtual<void>(reinterpret_cast<void*>(weapon), 110, 1);
            if (weapon_update_composite) weapon_update_composite(weapon, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (!update_view_model)
            return;
        const auto view_model = hud_model_weapon(entity_list, local_pawn);
        if (!view_model)
            return;
        update_skin_material(view_model);
        __try
        {
            if (weapon_set_mesh_group) weapon_set_mesh_group(view_model + 0x608, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void run_skin_changer(std::uintptr_t entity_list, std::uintptr_t local_controller, std::uintptr_t local_pawn)
    {
        const auto& offsets = state.offsets;
        const bool offsets_ready = offsets.weapon_services && offsets.active_weapon && offsets.attribute_manager && offsets.item &&
            offsets.item_id_high && offsets.item_id_low && offsets.account_id && offsets.initialized && offsets.fallback_paint &&
            offsets.fallback_seed && offsets.fallback_wear && offsets.fallback_stattrak;
        if (!offsets_ready || !entity_list || !local_pawn)
            return;

        if (!settings::skin_changer_enabled || settings::skin_paint_kit < 1.0f)
        {
            const auto restore_services = read<std::uintptr_t>(local_pawn + offsets.weapon_services);
            const auto restore_active = restore_services ? entity_by_handle(entity_list,
                read<std::uint32_t>(restore_services + offsets.active_weapon)) : 0;
            for (auto& [weapon, original] : original_skins)
            {
                if (!original.captured || !original.overridden)
                    continue;
                const auto item_view = weapon + offsets.attribute_manager + offsets.item;
                write(item_view + offsets.item_id_high, original.item_id_high);
                write(item_view + offsets.item_id_low, original.item_id_low);
                write(item_view + offsets.account_id, original.account_id);
                write(item_view + offsets.initialized, original.initialized);
                write(weapon + offsets.fallback_paint, original.paint_kit);
                write(weapon + offsets.fallback_seed, original.seed);
                write(weapon + offsets.fallback_wear, original.wear);
                write(weapon + offsets.fallback_stattrak, original.stattrak);
                rebuild_weapon_skin(entity_list, local_pawn, weapon, weapon == restore_active);
            }
            original_skins.clear();
            processed_skin_revision = settings::skin_apply_revision;
            return;
        }

        const auto services = read<std::uintptr_t>(local_pawn + offsets.weapon_services);
        if (!services)
            return;
        const std::uint32_t active_handle = read<std::uint32_t>(services + offsets.active_weapon);
        const auto active_entity = entity_by_handle(entity_list, active_handle);
        std::uintptr_t weapon{};
        if (offsets.my_weapons)
        {
            const auto weapons_base = services + offsets.my_weapons;
            const int count = read<int>(weapons_base);
            const auto handles = read<std::uintptr_t>(weapons_base + 0x8);
            for (int index = 0; handles && index < std::clamp(count, 0, 64); ++index)
            {
                const auto candidate = entity_by_handle(entity_list, read<std::uint32_t>(handles + static_cast<std::uintptr_t>(index) * 4));
                if (!candidate) continue;
                const auto definition = read<std::uint16_t>(candidate + offsets.attribute_manager + offsets.item + offsets.item_definition_index);
                if (definition == settings::skin_weapon_definition) { weapon = candidate; break; }
            }
        }
        if (!weapon)
        {
            const auto definition = active_entity ? read<std::uint16_t>(active_entity + offsets.attribute_manager + offsets.item + offsets.item_definition_index) : 0;
            if (definition == settings::skin_weapon_definition) weapon = active_entity;
        }
        if (!weapon)
            return;

        const auto item_view = weapon + offsets.attribute_manager + offsets.item;
        auto& original = original_skins[weapon];
        if (!original.captured)
        {
            original.item_id_high = read<std::uint32_t>(item_view + offsets.item_id_high);
            original.item_id_low = read<std::uint32_t>(item_view + offsets.item_id_low);
            original.account_id = read<std::uint32_t>(item_view + offsets.account_id);
            original.initialized = read<bool>(item_view + offsets.initialized);
            original.paint_kit = read<int>(weapon + offsets.fallback_paint);
            original.seed = read<int>(weapon + offsets.fallback_seed);
            original.wear = read<float>(weapon + offsets.fallback_wear);
            original.stattrak = read<int>(weapon + offsets.fallback_stattrak);
            original.captured = true;
        }

        const int paint_kit = std::clamp(static_cast<int>(std::round(settings::skin_paint_kit)), 1, 20000);
        const int seed = std::clamp(static_cast<int>(std::round(settings::skin_seed)), 0, 1000);
        const float wear = std::clamp(settings::skin_wear, 0.0001f, 1.0f);
        const int stattrak = settings::skin_stattrak ? 0 : -1;
        const bool explicit_apply = processed_skin_revision != settings::skin_apply_revision;
        const bool active_changed = last_skin_active_handle != active_handle;
        last_skin_active_handle = active_handle;
        if (!explicit_apply && !active_changed && original.overridden && original.applied_paint == paint_kit &&
            original.applied_seed == seed && original.applied_stattrak == stattrak && std::abs(original.applied_wear - wear) < 0.00001f)
            return;

        const std::uint64_t steam_id = offsets.steam_id && local_controller ? read<std::uint64_t>(local_controller + offsets.steam_id) : 0;
        write(item_view + offsets.item_id_high, std::uint32_t{ 0xf0000000 });
        write(item_view + offsets.item_id_low, std::uint32_t{ 0x10 });
        write(item_view + offsets.account_id, static_cast<std::uint32_t>(steam_id));
        write(item_view + offsets.initialized, true);
        write(weapon + offsets.fallback_paint, paint_kit);
        write(weapon + offsets.fallback_seed, seed);
        write(weapon + offsets.fallback_wear, wear);
        write(weapon + offsets.fallback_stattrak, stattrak);
        original.overridden = true;
        original.applied_paint = paint_kit;
        original.applied_seed = seed;
        original.applied_wear = wear;
        original.applied_stattrak = stattrak;
        processed_skin_revision = settings::skin_apply_revision;
        rebuild_weapon_skin(entity_list, local_pawn, weapon, weapon == active_entity);
    }

    void run_trigger(const std::array<player_snapshot, 64>& players, std::size_t player_count,
        std::uintptr_t entity_list, std::uintptr_t local_pawn, int local_team, const vector3& local_origin,
        const matrix4x4& matrix, const ImVec2& display)
    {
        const ULONGLONG now = GetTickCount64();
        if (trigger_runtime.holding && (now >= trigger_runtime.revolver_release_at || !settings::trigger_enabled || menu::is_open()))
        {
            mouse_button(false);
            trigger_runtime.holding = false;
            trigger_runtime.cooldown_until = now + 90;
        }
        if (!settings::trigger_enabled || menu::is_open() || !local_pawn || trigger_runtime.holding)
        {
            trigger_runtime.candidate = 0;
            return;
        }

        // Let the game's own crosshair trace decide whether an entity is actually
        // intersected. Projected screen circles are only approximations and could
        // fire beside a model at unusual FOV/aspect ratios.
        const player_snapshot* target{};
        if (state.offsets.crosshair_entity)
        {
            const int crosshair_index = read<int>(local_pawn + state.offsets.crosshair_entity);
            const auto crossed_entity = crosshair_index > 0 ? entity_by_index(entity_list, crosshair_index & 0x7fff) : 0;
            for (std::size_t index = 0; crossed_entity && index < player_count; ++index)
            {
                if (players[index].pawn == crossed_entity)
                    target = &players[index];
            }
        }
        if (!target || (!settings::trigger_teammates && local_team && target->team == local_team))
        {
            trigger_runtime.candidate = 0;
            return;
        }
        if (trigger_runtime.candidate != target->pawn)
        {
            trigger_runtime.candidate = target->pawn;
            trigger_runtime.candidate_since = now;
        }
        if (now < trigger_runtime.cooldown_until || now - trigger_runtime.candidate_since < static_cast<ULONGLONG>(settings::trigger_delay))
            return;

        const float dx = target->origin.x - local_origin.x;
        const float dy = target->origin.y - local_origin.y;
        const float dz = target->origin.z - local_origin.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const std::uint16_t weapon = active_weapon_id(entity_list, local_pawn);
        if (estimated_direct_damage(weapon, distance) < settings::trigger_min_damage)
            return;

        if (weapon == 64 && settings::trigger_revolver_hold)
        {
            mouse_button(true);
            trigger_runtime.holding = true;
            trigger_runtime.revolver_release_at = now + 450;
        }
        else
        {
            mouse_button(true);
            // Keep the button down across frames. A same-call DOWN+UP pair can be
            // missed by CS2 raw input and was the reason the trigger appeared dead.
            trigger_runtime.holding = true;
            trigger_runtime.revolver_release_at = now + 24;
        }
        trigger_runtime.candidate_since = now;
    }
}

namespace esp
{
    void on_frame_stage(int stage)
    {
        if (stage != 6 || (!settings::skin_changer_enabled && original_skins.empty()))
            return;
        if (!state.initialized && !initialize())
            return;
        const auto entity_list = read<std::uintptr_t>(state.entity_list_storage);
        const auto local_controller = read<std::uintptr_t>(state.local_controller_storage);
        const auto local_pawn = entity_list && local_controller ? entity_by_handle(entity_list,
            read<std::uint32_t>(local_controller + state.offsets.pawn_handle)) : 0;
        if (entity_list && local_pawn)
            run_skin_changer(entity_list, local_controller, local_pawn);
    }

    void draw(ImDrawList* draw_list, const ImVec2& display_size)
    {
        if (trigger_runtime.holding && (!settings::trigger_enabled || menu::is_open()))
        {
            mouse_button(false);
            trigger_runtime = {};
        }
        if ((!settings::esp_enabled && !settings::aim_enabled && !settings::trigger_enabled &&
            !settings::third_person_enabled && !settings::skin_changer_enabled) ||
            !draw_list || display_size.x <= 0.0f || display_size.y <= 0.0f)
        {
            local_player_available = false;
            return;
        }

        if (settings::aim_enabled && settings::aim_show_fov)
            draw_list->AddCircle(ImVec2(display_size.x * 0.5f, display_size.y * 0.5f), aim_radius(display_size),
                color_u32(settings::aim_fov_color), 96, 1.0f);
        if (!state.initialized)
        {
            const ULONGLONG now = GetTickCount64();
            if (now < state.next_retry_ms) return;
            state.next_retry_ms = now + 5000;
            if (!initialize()) return;
        }

        matrix4x4 matrix{};
        const auto entity_list = read<std::uintptr_t>(state.entity_list_storage);
        if (!entity_list || !read(state.view_matrix, matrix))
            return;

        const auto local_controller = read<std::uintptr_t>(state.local_controller_storage);
        const auto local_pawn = local_controller ? entity_by_handle(entity_list, read<std::uint32_t>(local_controller + state.offsets.pawn_handle)) : 0;
        const auto local_scene = local_pawn ? read<std::uintptr_t>(local_pawn + state.offsets.scene_node) : 0;
        const vector3 local_origin = local_scene ? read<vector3>(local_scene + state.offsets.absolute_origin) : vector3{};
        vector3 local_eye = local_origin;
        local_eye.z += 64.0f;
        const int local_team = local_pawn ? read<int>(local_pawn + state.offsets.team) : 0;
        local_player_available = local_pawn && read<int>(local_pawn + state.offsets.health) > 0;
        int local_index{};
        for (int entity_index = 1; entity_index <= 64 && !local_index; ++entity_index)
            if (entity_by_index(entity_list, entity_index) == local_controller) local_index = entity_index;

        std::array<player_snapshot, 64> players{};
        std::size_t player_count = 0;
        for (int entity_index = 1; entity_index <= 64 && player_count < players.size(); ++entity_index)
        {
            const auto controller = entity_by_index(entity_list, entity_index);
            if (!controller || controller == local_controller || !is_player_controller(controller)) continue;
            player_snapshot snapshot{};
            if (!collect_player(entity_list, controller, snapshot)) continue;
            if (state.offsets.spotted_state && state.offsets.spotted_mask && local_index > 0)
            {
                const std::uint64_t spotted = read<std::uint64_t>(snapshot.pawn + state.offsets.spotted_state + state.offsets.spotted_mask);
                snapshot.visible = (spotted & (std::uint64_t{ 1 } << (local_index - 1))) != 0;
            }
            const float dx = snapshot.origin.x - local_origin.x;
            const float dy = snapshot.origin.y - local_origin.y;
            const float dz = snapshot.origin.z - local_origin.z;
            if (local_scene && dx * dx + dy * dy + dz * dz > settings::esp_distance * settings::esp_distance) continue;
            players[player_count++] = snapshot;
        }

        run_aim(players, player_count, matrix, display_size, local_team, local_eye, local_pawn);
        run_trigger(players, player_count, entity_list, local_pawn, local_team, local_origin, matrix, display_size);

        for (std::size_t player_index = 0; player_index < player_count; ++player_index)
        {
            const player_snapshot& player = players[player_index];
            if (!settings::esp_enabled || (!settings::esp_teammates && local_team && player.team == local_team))
                continue;
            const bool teammate = local_team && player.team == local_team;
            const ImU32 color = color_u32(teammate ? settings::team_color : settings::enemy_color);
            const ImU32 skeleton_color = color_u32(teammate ? settings::esp_skeleton_team_color : settings::esp_skeleton_enemy_color);
            const ImU32 arrow_color = color_u32(teammate ? settings::esp_arrow_team_color : settings::esp_arrow_enemy_color);
            vector3 head = player.origin; head.z += 72.0f;
            ImVec2 feet_screen{}, head_screen{};
            const bool feet_visible = project(player.origin, matrix, display_size, feet_screen);
            const bool head_visible = project(head, matrix, display_size, head_screen);
            const bool on_screen = feet_visible && head_visible && feet_screen.x >= 0.0f && feet_screen.x <= display_size.x &&
                feet_screen.y >= 0.0f && feet_screen.y <= display_size.y && head_screen.x >= 0.0f && head_screen.x <= display_size.x &&
                head_screen.y >= 0.0f && head_screen.y <= display_size.y;
            if (!on_screen)
            {
                if (settings::esp_offscreen) draw_offscreen_arrow(draw_list, player.origin, matrix, display_size, arrow_color);
                continue;
            }

            const float box_height = std::abs(feet_screen.y - head_screen.y);
            if (box_height < 8.0f || box_height > display_size.y * 1.5f) continue;
            const float box_width = box_height * 0.44f;
            const ImVec2 minimum(head_screen.x - box_width * 0.5f, head_screen.y);
            const ImVec2 maximum(head_screen.x + box_width * 0.5f, feet_screen.y);

            if (settings::esp_box)
            {
                draw_list->AddRect(ImVec2(minimum.x - 1, minimum.y - 1), ImVec2(maximum.x + 1, maximum.y + 1), IM_COL32(0, 0, 0, 210), 0.0f, 0, 3.0f);
                draw_list->AddRect(minimum, maximum, color, 0.0f, 0, 1.0f);
                draw_list->AddRect(ImVec2(minimum.x + 1, minimum.y + 1), ImVec2(maximum.x - 1, maximum.y - 1), IM_COL32(0, 0, 0, 170));
            }
            if (settings::esp_skeleton) draw_skeleton(draw_list, player, matrix, display_size, skeleton_color);
            if (settings::esp_health)
            {
                const float fraction = std::clamp(player.health / 100.0f, 0.0f, 1.0f);
                const float bar_x = minimum.x - 6.0f;
                draw_list->AddRectFilled(ImVec2(bar_x - 1, minimum.y - 1), ImVec2(bar_x + 3, maximum.y + 1), IM_COL32(0, 0, 0, 190));
                const ImVec4& low = settings::esp_health_low_color;
                const ImVec4& high = settings::esp_health_high_color;
                const ImVec4 health_color(low.x + (high.x - low.x) * fraction, low.y + (high.y - low.y) * fraction,
                    low.z + (high.z - low.z) * fraction, low.w + (high.w - low.w) * fraction);
                draw_list->AddRectFilled(ImVec2(bar_x, maximum.y - box_height * fraction), ImVec2(bar_x + 2, maximum.y), color_u32(health_color));
            }
            if (settings::esp_name && player.name[0])
            {
                const ImVec2 size = fonts::regular->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, player.name);
                outlined_text(draw_list, fonts::regular, 13.0f, ImVec2(head_screen.x - size.x * 0.5f, minimum.y - 16.0f),
                    color_u32(settings::esp_name_color), player.name);
            }
        }
    }

    bool has_local_player() { return local_player_available; }

    void reset()
    {
        if (trigger_runtime.holding)
            mouse_button(false);
        trigger_runtime = {};
        original_skins.clear();
        weapon_update_skin = nullptr;
        weapon_set_mesh_group = nullptr;
        weapon_update_composite = nullptr;
        processed_skin_revision = 0;
        last_skin_active_handle = 0;
        local_player_available = false;
        state = {};
        game_trace::reset();
    }
}
