#pragma once

#include <imgui.h>

#include <array>

namespace settings
{
    inline bool aim_enabled = false;
    inline bool aim_show_fov = true;
    inline bool aim_teammates = false;
    inline float aim_fov = 4.0f;
    inline float aim_smooth = 8.0f;
    inline std::array<bool, 4> aim_hitboxes{ true, false, false, false };
    inline bool aim_visible_only = true;
    inline int aim_key = 0;
    inline ImVec4 aim_fov_color{ 0.64f, 0.66f, 0.78f, 0.55f };

    inline bool trigger_enabled = false;
    inline bool trigger_teammates = false;
    inline bool trigger_revolver_hold = true;
    inline float trigger_delay = 35.0f;
    inline float trigger_min_damage = 1.0f;

    inline bool viewmodel_enabled = false;
    inline float viewmodel_x = 0.0f;
    inline float viewmodel_y = 0.0f;
    inline float viewmodel_z = 0.0f;
    inline float viewmodel_fov = 68.0f;

    inline bool esp_enabled = true;
    inline bool esp_box = true;
    inline bool esp_name = true;
    inline bool esp_health = true;
    inline bool esp_skeleton = false;
    inline bool esp_offscreen = true;
    inline bool esp_teammates = false;
    inline float esp_distance = 1200.0f;
    inline ImVec4 enemy_color{ 0.92f, 0.36f, 0.40f, 1.0f };
    inline ImVec4 team_color{ 0.35f, 0.58f, 0.95f, 1.0f };
    inline ImVec4 esp_name_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    inline ImVec4 esp_health_high_color{ 0.30f, 0.86f, 0.40f, 1.0f };
    inline ImVec4 esp_health_low_color{ 0.95f, 0.25f, 0.25f, 1.0f };
    inline ImVec4 esp_skeleton_enemy_color{ 0.92f, 0.36f, 0.40f, 1.0f };
    inline ImVec4 esp_skeleton_team_color{ 0.35f, 0.58f, 0.95f, 1.0f };
    inline ImVec4 esp_arrow_enemy_color{ 0.92f, 0.36f, 0.40f, 1.0f };
    inline ImVec4 esp_arrow_team_color{ 0.35f, 0.58f, 0.95f, 1.0f };

    // 75%, 100%, 125%, 150%.
    inline int menu_scale_index = 1;

    inline float menu_scale()
    {
        constexpr float values[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        return values[menu_scale_index < 0 ? 0 : menu_scale_index > 3 ? 3 : menu_scale_index];
    }
}
