#include "pch.h"

#include "menu.hpp"
#include "config.hpp"
#include "fonts.hpp"
#include "skin_catalog.hpp"
#include "settings.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>

namespace
{
    constexpr const char* icon_logo = "\xEE\x82\x9A";       // radar, U+E09A
    constexpr const char* icon_combat = "\xEE\x83\xBC";     // shield, U+E0FC
    constexpr const char* icon_movement = "\xEE\x81\x86";   // mouse, U+E046
    constexpr const char* icon_visuals = "\xEE\x80\xBD";    // monitor, U+E03D
    constexpr const char* icon_player = "\xEE\x85\xAB";     // user, U+E16B
    constexpr const char* icon_skins = "\xEE\x80\xBD";      // monitor, U+E03D
    constexpr const char* icon_misc = "\xEE\x83\xB1";       // setting, U+E0F1
    constexpr const char* icon_configs = "\xE2\x86\x91";    // folder, U+2191
    constexpr const char* icon_scripts = "\xC3\x9C";         // code, U+00DC
    constexpr const char* icon_search = "\xEE\x83\x9C";     // search-normal, U+E0DC

    enum class category : int
    {
        combat,
        movement,
        visuals,
        player,
        skins,
        misc,
        configs,
        scripts,
        count
    };

    struct category_entry
    {
        category value;
        const char* name;
        const char* icon;
        bool secondary;
    };

    constexpr std::array categories = {
        category_entry{ category::combat, "Combat", icon_combat, false },
        category_entry{ category::movement, "Movement", icon_movement, false },
        category_entry{ category::visuals, "Visuals", icon_visuals, false },
        category_entry{ category::player, "Player", icon_player, false },
        category_entry{ category::skins, "Skins", icon_skins, false },
        category_entry{ category::misc, "Misc", icon_misc, false },
        category_entry{ category::configs, "Configs", icon_configs, true },
        category_entry{ category::scripts, "Scripts", icon_scripts, true }
    };

    std::atomic_bool open = false;
    category selected_category = category::combat;
    std::array<float, static_cast<std::size_t>(category::count)> category_animation{};
    char search_text[64]{};

    bool recoil_enabled = true;
    bool backtrack_enabled = false;
    float recoil_amount = 72.0f;
    float backtrack_window = 120.0f;

    bool bunny_hop = true;
    bool auto_strafe = true;
    bool edge_jump = false;
    bool jump_bug = false;
    bool fast_stop = false;
    float strafe_strength = 80.0f;
    float edge_window = 35.0f;

    bool glow_enabled = false;
    bool dropped_weapons = true;
    bool grenade_warning = true;
    float glow_alpha = 45.0f;

    bool fov_changer = false;
    bool remove_flash = false;
    bool remove_smoke = false;
    bool remove_scope = false;
    float camera_fov = 90.0f;

    bool watermark = true;
    bool spectator_list = true;
    bool hitmarker = true;
    bool hit_sound = false;
    bool auto_accept = false;
    bool reveal_ranks = false;
    int hit_sound_type = 0;

    std::unordered_map<ImGuiID, float> toggle_animations;
    std::unordered_map<ImGuiID, float> row_animations;
    float content_animation = 1.0f;
    float draw_alpha = 1.0f;
    float interface_scale = 1.0f;

    float px(float value)
    {
        return value * interface_scale;
    }

    ImVec2 px(float x, float y)
    {
        return ImVec2(x * interface_scale, y * interface_scale);
    }

    void push_font(ImFont* font, float size)
    {
        ImGui::PushFont(font, px(size));
    }

    ImU32 rgba(int red, int green, int blue, int alpha)
    {
        return IM_COL32(red, green, blue, static_cast<int>(alpha * draw_alpha));
    }

    ImU32 accent_color(float alpha_value = 1.0f)
    {
        const ImVec4& accent = settings::menu_accent;
        return ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, accent.w * alpha_value * draw_alpha));
    }

    void add_text(ImDrawList* draw, ImFont* font, float size, const ImVec2& position, ImU32 color, const char* text)
    {
        draw->AddText(font, px(size), position, color, text);
    }

    ImVec2 text_size(ImFont* font, float size, const char* text)
    {
        return font->CalcTextSizeA(px(size), FLT_MAX, 0.0f, text);
    }

    std::string lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool matches_search(const char* title, const char* keywords)
    {
        if (search_text[0] == '\0')
            return true;

        const std::string query = lowercase(search_text);
        return lowercase(std::string(title) + " " + keywords).find(query) != std::string::npos;
    }

    void toggle_row(const char* label, bool* value, const std::function<void()>& settings_popup = {})
    {
        ImGui::PushID(label);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = px(25.0f);
        const bool configurable = static_cast<bool>(settings_popup);
        const float gear_slot = configurable ? px(25.0f) : 0.0f;
        ImGui::InvisibleButton("##toggle", ImVec2(width - gear_slot, height));
        if (ImGui::IsItemClicked())
            *value = !*value;

        const ImGuiID id = ImGui::GetItemID();
        float& animation = toggle_animations[id];
        const float target = *value ? 1.0f : 0.0f;
        animation += (target - animation) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        add_text(draw, fonts::regular, 14.0f, ImVec2(position.x, position.y + px(4.0f)), rgba(255, 255, 255, 165), label);

        // Java original: a small square with a minus while disabled and a dot while enabled.
        const float side = px(16.0f);
        const ImVec2 toggle_position(position.x + width - side - gear_slot, position.y + px(4.0f));
        draw->AddRectFilled(toggle_position, ImVec2(toggle_position.x + side, toggle_position.y + side),
            rgba(255, 255, 255, static_cast<int>(5.0f + animation * 7.0f)), px(3.0f));
        draw->AddRect(toggle_position, ImVec2(toggle_position.x + side, toggle_position.y + side),
            rgba(255, 255, 255, static_cast<int>(12.0f + animation * 8.0f)), px(3.0f));
        draw->AddRectFilled(toggle_position, ImVec2(toggle_position.x + side, toggle_position.y + side),
            accent_color(animation * 0.12f), px(3.0f));

        const ImU32 glyph = ImGui::ColorConvertFloat4ToU32(ImVec4(
            1.0f + (settings::menu_accent.x - 1.0f) * animation,
            1.0f + (settings::menu_accent.y - 1.0f) * animation,
            1.0f + (settings::menu_accent.z - 1.0f) * animation,
            (0.40f + animation * 0.60f) * draw_alpha));
        const float minus_alpha = 1.0f - animation;
        if (minus_alpha > 0.01f)
            draw->AddRectFilled(ImVec2(toggle_position.x + px(5.0f), toggle_position.y + px(7.5f)),
                ImVec2(toggle_position.x + px(11.0f), toggle_position.y + px(8.5f)),
                rgba(255, 255, 255, static_cast<int>(100.0f * minus_alpha)), px(1.0f));
        if (animation > 0.01f)
            draw->AddRectFilled(ImVec2(toggle_position.x + px(6.0f), toggle_position.y + px(6.0f)),
                ImVec2(toggle_position.x + px(10.0f), toggle_position.y + px(10.0f)), glyph, px(1.0f));

        if (configurable)
        {
            const ImVec2 gear_position(position.x + width - px(17.0f), position.y + px(4.0f));
            ImGui::SetCursorScreenPos(ImVec2(gear_position.x - px(4.0f), position.y));
            ImGui::InvisibleButton("##gear", px(21.0f, 25.0f));
            const bool gear_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                ImGui::OpenPopup("##toggle_settings");
            if (gear_hovered)
                draw->AddCircleFilled(ImVec2(gear_position.x + px(7.0f), gear_position.y + px(8.0f)), px(10.0f), rgba(255, 255, 255, 7));
            add_text(draw, fonts::icons, 14.0f, gear_position, accent_color(gear_hovered ? 0.90f : 0.42f), icon_misc);

            ImGui::SetNextWindowPos(ImVec2(position.x + width - px(206.0f), position.y + height + px(4.0f)));
            ImGui::SetNextWindowSizeConstraints(px(206.0f, 0.0f), px(206.0f, FLT_MAX));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(10.0f, 9.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, px(1.0f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, rgba(13, 13, 16, 252));
            ImGui::PushStyleColor(ImGuiCol_Border, rgba(255, 255, 255, 15));
            if (ImGui::BeginPopup("##toggle_settings", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs))
            {
                ImGui::PushItemWidth(px(186.0f));
                settings_popup();
                ImGui::PopItemWidth();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            ImGui::SetCursorScreenPos(position);
            ImGui::Dummy(ImVec2(width, height));
        }
        ImGui::PopID();
    }

    void slider_row(const char* label, float* value, float minimum, float maximum, const char* format)
    {
        ImGui::PushID(label);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = px(39.0f);
        ImGui::InvisibleButton("##slider", ImVec2(width, height));

        const float bar_height = px(5.0f);
        const float bar_y = position.y + px(28.0f);
        if (ImGui::IsItemActive())
        {
            const float fraction = std::clamp((ImGui::GetIO().MousePos.x - position.x) / width, 0.0f, 1.0f);
            *value = minimum + (maximum - minimum) * fraction;
        }

        char value_text[32]{};
        std::snprintf(value_text, sizeof(value_text), format, *value);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        add_text(draw, fonts::regular, 14.0f, ImVec2(position.x, position.y + px(2.0f)), rgba(255, 255, 255, 165), label);
        const ImVec2 value_size = text_size(fonts::regular, 13.0f, value_text);
        add_text(draw, fonts::regular, 13.0f, ImVec2(position.x + width - value_size.x, position.y + px(3.0f)), rgba(255, 255, 255, 92), value_text);

        const float fraction = std::clamp((*value - minimum) / (maximum - minimum), 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(position.x, bar_y), ImVec2(position.x + width, bar_y + bar_height), rgba(255, 255, 255, 15), px(2.0f));
        draw->AddRectFilled(ImVec2(position.x, bar_y), ImVec2(position.x + width * fraction, bar_y + bar_height), accent_color(), px(2.0f));
        draw->AddCircleFilled(ImVec2(position.x + width * fraction, bar_y + bar_height * 0.5f), px(4.5f), accent_color());
        ImGui::PopID();
    }

    void combo_row(const char* label, int* value, const char* const* items, int item_count)
    {
        ImGui::PushID(label);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float box_width = px(112.0f);
        const float box_height = px(24.0f);
        const ImVec2 box(start.x + width - box_width, start.y + px(1.0f));
        add_text(ImGui::GetWindowDrawList(), fonts::regular, 14.0f, ImVec2(start.x, start.y + px(5.0f)), rgba(255, 255, 255, 165), label);

        ImGui::SetCursorScreenPos(box);
        ImGui::InvisibleButton("##mode", ImVec2(box_width, box_height));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            ImGui::OpenPopup("##mode_popup");

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(box, ImVec2(box.x + box_width, box.y + box_height), rgba(255, 255, 255, hovered ? 10 : 6), px(3.0f));
        draw->AddRect(box, ImVec2(box.x + box_width, box.y + box_height), rgba(255, 255, 255, hovered ? 20 : 12), px(3.0f));
        add_text(draw, fonts::regular, 13.0f, ImVec2(box.x + px(8.0f), box.y + px(5.0f)), rgba(255, 255, 255, 150), items[*value]);
        draw->AddTriangleFilled(ImVec2(box.x + box_width - px(14.0f), box.y + px(10.0f)),
            ImVec2(box.x + box_width - px(8.0f), box.y + px(10.0f)), ImVec2(box.x + box_width - px(11.0f), box.y + px(14.0f)),
            rgba(255, 255, 255, 70));

        ImGui::SetNextWindowPos(ImVec2(box.x, box.y + box_height + px(4.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(5.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(0.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, px(1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, rgba(13, 13, 16, 250));
        ImGui::PushStyleColor(ImGuiCol_Border, rgba(255, 255, 255, 14));
        if (ImGui::BeginPopup("##mode_popup", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs))
        {
            for (int index = 0; index < item_count; ++index)
            {
                ImGui::PushID(index);
                const ImVec2 row = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##item", ImVec2(box_width - px(10.0f), px(23.0f)));
                const bool item_hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                {
                    *value = index;
                    ImGui::CloseCurrentPopup();
                }
                float& animation = row_animations[ImGui::GetItemID()];
                const float target = *value == index ? 1.0f : item_hovered ? 0.45f : 0.0f;
                animation += (target - animation) * std::min(1.0f, ImGui::GetIO().DeltaTime * 15.0f);
                ImDrawList* popup_draw = ImGui::GetWindowDrawList();
                if (animation > 0.01f)
                    popup_draw->AddRectFilled(row, ImVec2(row.x + box_width - px(10.0f), row.y + px(23.0f)), rgba(255, 255, 255, static_cast<int>(8 * animation)), px(3.0f));
                if (*value == index)
                    popup_draw->AddCircleFilled(ImVec2(row.x + px(8.0f), row.y + px(11.5f)), px(2.0f), accent_color());
                add_text(popup_draw, fonts::regular, 13.0f, ImVec2(row.x + px(*value == index ? 16.0f : 8.0f), row.y + px(4.0f)),
                    rgba(255, 255, 255, *value == index ? 205 : 115), items[index]);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(width, px(28.0f)));
        ImGui::PopID();
    }

    void multi_bool_row(const char* label, bool* values, const char* const* items, int item_count)
    {
        ImGui::PushID(label);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float box_width = px(112.0f);
        const float box_height = px(24.0f);
        const ImVec2 box(start.x + width - box_width, start.y + px(1.0f));

        int selected_count = 0;
        int first_selected = -1;
        for (int index = 0; index < item_count; ++index)
        {
            if (!values[index]) continue;
            if (first_selected < 0) first_selected = index;
            ++selected_count;
        }
        std::string summary;
        if (selected_count == 0)
            summary = "None";
        else if (selected_count == item_count)
            summary = "All";
        else if (selected_count == 1)
            summary = items[first_selected];
        else
            summary = std::string(items[first_selected]) + " +" + std::to_string(selected_count - 1);

        add_text(ImGui::GetWindowDrawList(), fonts::regular, 14.0f,
            ImVec2(start.x, start.y + px(5.0f)), rgba(255, 255, 255, 165), label);
        ImGui::SetCursorScreenPos(box);
        ImGui::InvisibleButton("##multi", ImVec2(box_width, box_height));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            ImGui::OpenPopup("##multi_popup");

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(box, ImVec2(box.x + box_width, box.y + box_height),
            rgba(255, 255, 255, hovered ? 10 : 6), px(3.0f));
        draw->AddRect(box, ImVec2(box.x + box_width, box.y + box_height),
            rgba(255, 255, 255, hovered ? 20 : 12), px(3.0f));
        add_text(draw, fonts::regular, 13.0f, ImVec2(box.x + px(8.0f), box.y + px(5.0f)),
            rgba(255, 255, 255, 150), summary.c_str());
        draw->AddTriangleFilled(ImVec2(box.x + box_width - px(14.0f), box.y + px(10.0f)),
            ImVec2(box.x + box_width - px(8.0f), box.y + px(10.0f)),
            ImVec2(box.x + box_width - px(11.0f), box.y + px(14.0f)), rgba(255, 255, 255, 70));

        ImGui::SetNextWindowPos(ImVec2(box.x, box.y + box_height + px(4.0f)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(box_width, 0.0f), ImVec2(box_width, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(5.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(0.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, px(1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, rgba(13, 13, 16, 250));
        ImGui::PushStyleColor(ImGuiCol_Border, rgba(255, 255, 255, 14));
        if (ImGui::BeginPopup("##multi_popup", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs))
        {
            for (int index = 0; index < item_count; ++index)
            {
                ImGui::PushID(index);
                const ImVec2 row = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##item", ImVec2(box_width - px(10.0f), px(23.0f)));
                const bool item_hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                    values[index] = !values[index];

                float& animation = row_animations[ImGui::GetItemID()];
                const float target = values[index] ? 1.0f : item_hovered ? 0.45f : 0.0f;
                animation += (target - animation) * std::min(1.0f, ImGui::GetIO().DeltaTime * 15.0f);
                ImDrawList* popup_draw = ImGui::GetWindowDrawList();
                if (item_hovered)
                    popup_draw->AddRectFilled(row, ImVec2(row.x + box_width - px(10.0f), row.y + px(23.0f)),
                        rgba(255, 255, 255, 6), px(3.0f));

                const ImVec2 check(row.x + px(5.0f), row.y + px(5.5f));
                popup_draw->AddRectFilled(check, ImVec2(check.x + px(12.0f), check.y + px(12.0f)),
                    values[index] ? accent_color(0.22f + animation * 0.12f) : rgba(255, 255, 255, 6), px(3.0f));
                popup_draw->AddRect(check, ImVec2(check.x + px(12.0f), check.y + px(12.0f)),
                    values[index] ? accent_color(0.75f) : rgba(255, 255, 255, 18), px(3.0f));
                if (values[index])
                {
                    popup_draw->AddLine(ImVec2(check.x + px(3.0f), check.y + px(6.0f)),
                        ImVec2(check.x + px(5.2f), check.y + px(8.2f)), accent_color(), px(1.5f));
                    popup_draw->AddLine(ImVec2(check.x + px(5.2f), check.y + px(8.2f)),
                        ImVec2(check.x + px(9.5f), check.y + px(3.5f)), accent_color(), px(1.5f));
                }
                add_text(popup_draw, fonts::regular, 13.0f, ImVec2(row.x + px(23.0f), row.y + px(4.0f)),
                    rgba(255, 255, 255, values[index] ? 205 : 115), items[index]);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(width, px(28.0f)));
        ImGui::PopID();
    }

    void compact_color_picker(ImVec4* color)
    {
        float hue{}, saturation{}, value{};
        ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, hue, saturation, value);

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 sv_size = px(132.0f, 108.0f);
        const float hue_width = px(10.0f);
        const float gap = px(8.0f);
        const float alpha_height = px(8.0f);

        ImGui::SetCursorScreenPos(start);
        ImGui::InvisibleButton("##sv", sv_size);
        if (ImGui::IsItemActive())
        {
            saturation = std::clamp((ImGui::GetIO().MousePos.x - start.x) / sv_size.x, 0.0f, 1.0f);
            value = 1.0f - std::clamp((ImGui::GetIO().MousePos.y - start.y) / sv_size.y, 0.0f, 1.0f);
        }

        const ImVec2 hue_start(start.x + sv_size.x + gap, start.y);
        ImGui::SetCursorScreenPos(hue_start);
        ImGui::InvisibleButton("##hue", ImVec2(hue_width, sv_size.y));
        if (ImGui::IsItemActive())
            hue = std::clamp((ImGui::GetIO().MousePos.y - hue_start.y) / sv_size.y, 0.0f, 1.0f);

        const ImVec2 alpha_start(start.x, start.y + sv_size.y + gap);
        ImGui::SetCursorScreenPos(alpha_start);
        ImGui::InvisibleButton("##alpha", ImVec2(sv_size.x + gap + hue_width, alpha_height));
        if (ImGui::IsItemActive())
            color->w = std::clamp((ImGui::GetIO().MousePos.x - alpha_start.x) / (sv_size.x + gap + hue_width), 0.0f, 1.0f);

        ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color->x, color->y, color->z);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float hue_red{}, hue_green{}, hue_blue{};
        ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, hue_red, hue_green, hue_blue);
        const ImU32 hue_color = ImGui::ColorConvertFloat4ToU32(ImVec4(hue_red, hue_green, hue_blue, 1.0f));
        draw->AddRectFilledMultiColor(start, ImVec2(start.x + sv_size.x, start.y + sv_size.y),
            IM_COL32_WHITE, hue_color, hue_color, IM_COL32_WHITE);
        draw->AddRectFilledMultiColor(start, ImVec2(start.x + sv_size.x, start.y + sv_size.y),
            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);
        draw->AddRect(start, ImVec2(start.x + sv_size.x, start.y + sv_size.y), rgba(255, 255, 255, 18), px(3.0f));

        constexpr std::array<ImU32, 7> hue_colors = {
            IM_COL32(255, 70, 70, 255), IM_COL32(255, 225, 70, 255), IM_COL32(70, 255, 100, 255),
            IM_COL32(70, 235, 255, 255), IM_COL32(75, 100, 255, 255), IM_COL32(210, 70, 255, 255), IM_COL32(255, 70, 70, 255)
        };
        for (std::size_t index = 0; index + 1 < hue_colors.size(); ++index)
        {
            const float top = hue_start.y + sv_size.y * static_cast<float>(index) / 6.0f;
            const float bottom = hue_start.y + sv_size.y * static_cast<float>(index + 1) / 6.0f;
            draw->AddRectFilledMultiColor(ImVec2(hue_start.x, top), ImVec2(hue_start.x + hue_width, bottom),
                hue_colors[index], hue_colors[index], hue_colors[index + 1], hue_colors[index + 1]);
        }
        draw->AddRect(hue_start, ImVec2(hue_start.x + hue_width, hue_start.y + sv_size.y), rgba(255, 255, 255, 20), px(2.0f));

        const ImU32 transparent = ImGui::ColorConvertFloat4ToU32(ImVec4(color->x, color->y, color->z, 0.0f));
        const ImU32 opaque = ImGui::ColorConvertFloat4ToU32(ImVec4(color->x, color->y, color->z, 1.0f));
        draw->AddRectFilledMultiColor(alpha_start, ImVec2(alpha_start.x + sv_size.x + gap + hue_width, alpha_start.y + alpha_height),
            transparent, opaque, opaque, transparent);
        draw->AddRect(alpha_start, ImVec2(alpha_start.x + sv_size.x + gap + hue_width, alpha_start.y + alpha_height), rgba(255, 255, 255, 20), px(2.0f));

        const ImVec2 sv_marker(start.x + saturation * sv_size.x, start.y + (1.0f - value) * sv_size.y);
        draw->AddCircle(sv_marker, px(4.0f), IM_COL32(0, 0, 0, 210), 12, px(3.0f));
        draw->AddCircle(sv_marker, px(4.0f), IM_COL32_WHITE, 12, px(1.0f));
        const float hue_y = hue_start.y + hue * sv_size.y;
        draw->AddRectFilled(ImVec2(hue_start.x - px(2.0f), hue_y - px(1.0f)),
            ImVec2(hue_start.x + hue_width + px(2.0f), hue_y + px(1.0f)), IM_COL32_WHITE, px(1.0f));
        const float alpha_x = alpha_start.x + color->w * (sv_size.x + gap + hue_width);
        draw->AddRectFilled(ImVec2(alpha_x - px(1.0f), alpha_start.y - px(2.0f)),
            ImVec2(alpha_x + px(1.0f), alpha_start.y + alpha_height + px(2.0f)), IM_COL32_WHITE, px(1.0f));

        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(sv_size.x + gap + hue_width, sv_size.y + gap + alpha_height));
    }

    void color_row(const char* label, ImVec4* color)
    {
        ImGui::PushID(label);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float preview_size = px(16.0f);
        const ImVec2 preview(start.x + width - preview_size, start.y + px(4.0f));
        add_text(ImGui::GetWindowDrawList(), fonts::regular, 14.0f, ImVec2(start.x, start.y + px(4.0f)), rgba(255, 255, 255, 165), label);
        ImGui::SetCursorScreenPos(preview);
        ImGui::InvisibleButton("##color_preview", ImVec2(preview_size, preview_size));
        if (ImGui::IsItemClicked())
            ImGui::OpenPopup("##color_popup");

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(preview, ImVec2(preview.x + preview_size, preview.y + preview_size), ImGui::ColorConvertFloat4ToU32(*color), px(3.0f));
        draw->AddRect(preview, ImVec2(preview.x + preview_size, preview.y + preview_size), rgba(255, 255, 255, ImGui::IsItemHovered() ? 45 : 20), px(3.0f));

        ImGui::SetNextWindowPos(ImVec2(start.x + width - px(176.0f), preview.y + preview_size + px(5.0f)));
        ImGui::SetNextWindowSize(px(176.0f, 151.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, px(1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, rgba(13, 13, 16, 252));
        ImGui::PushStyleColor(ImGuiCol_Border, rgba(255, 255, 255, 15));
        if (ImGui::BeginPopup("##color_popup", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs))
        {
            compact_color_picker(color);
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(width, px(24.0f)));
        ImGui::PopID();
    }

    template <typename Callback>
    float card(const char* id, const char* title, const char* description, const ImVec2& position, float width, Callback callback)
    {
        ImGui::SetCursorPos(position);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, px(1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(13.0f, 11.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, rgba(255, 255, 255, 4));
        ImGui::PushStyleColor(ImGuiCol_Border, rgba(255, 255, 255, 10));

        constexpr ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize;
        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::BeginChild(id, ImVec2(width, 0.0f), child_flags, window_flags))
        {
            push_font(fonts::medium, 15.0f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.90f), "%s", title);
            ImGui::PopFont();
            if (description && description[0])
            {
                push_font(fonts::regular, 12.0f);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.25f), "%s", description);
                ImGui::PopFont();
            }
            ImGui::Spacing();
            callback();
            ImGui::Dummy(px(1.0f, 1.0f));
        }
        ImGui::EndChild();
        const float height = ImGui::GetItemRectSize().y;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        return height;
    }

    struct card_layout
    {
        float width;
        float gap = px(13.0f);
        float y[2]{};

        template <typename Callback>
        void add(int column, const char* id, const char* title, const char* description, float /*legacy_height*/, const char* keywords, Callback callback)
        {
            if (!matches_search(title, keywords))
                return;

            const ImVec2 position(column * (width + gap), y[column]);
            const float actual_height = card(id, title, description, position, width, callback);
            y[column] += actual_height + gap;
        }

        float bottom() const
        {
            return std::max(y[0], y[1]);
        }
    };

    ImU32 rarity_color(int rarity, int alpha = 255)
    {
        constexpr std::array<ImVec4, 8> colors{
            ImVec4{ 0.55f, 0.58f, 0.63f, 1.0f }, ImVec4{ 0.69f, 0.76f, 0.85f, 1.0f },
            ImVec4{ 0.37f, 0.60f, 0.85f, 1.0f }, ImVec4{ 0.29f, 0.41f, 1.0f, 1.0f },
            ImVec4{ 0.53f, 0.28f, 1.0f, 1.0f }, ImVec4{ 0.83f, 0.17f, 0.90f, 1.0f },
            ImVec4{ 0.92f, 0.29f, 0.29f, 1.0f }, ImVec4{ 0.89f, 0.68f, 0.22f, 1.0f }
        };
        ImVec4 color = colors[std::clamp(rarity, 0, 7)];
        color.w = (alpha / 255.0f) * draw_alpha;
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    const char* rarity_name(int rarity)
    {
        constexpr const char* names[] = { "Base", "Consumer", "Industrial", "Mil-Spec", "Restricted", "Classified", "Covert", "Contraband" };
        return names[std::clamp(rarity, 0, 7)];
    }

    bool catalog_row(const char* id, const char* name, bool selected, int rarity = -1)
    {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = px(31.0f);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##catalog_row", ImVec2(width, height));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (selected || hovered)
            draw->AddRectFilled(position, ImVec2(position.x + width, position.y + height),
                selected ? accent_color(0.13f) : rgba(255, 255, 255, 7), px(5.0f));
        if (selected)
            draw->AddRect(position, ImVec2(position.x + width, position.y + height), accent_color(0.28f), px(5.0f));
        if (rarity >= 0)
            draw->AddRectFilled(position, ImVec2(position.x + px(3.0f), position.y + height), rarity_color(rarity), px(2.0f));
        add_text(draw, fonts::regular, 13.0f, ImVec2(position.x + px(rarity >= 0 ? 11.0f : 9.0f), position.y + px(8.0f)),
            rgba(255, 255, 255, selected ? 235 : hovered ? 205 : 150), name);
        if (rarity >= 0)
        {
            const char* label = rarity_name(rarity);
            const ImVec2 size = fonts::regular->CalcTextSizeA(px(10.0f), FLT_MAX, 0.0f, label);
            add_text(draw, fonts::regular, 10.0f, ImVec2(position.x + width - size.x - px(8.0f), position.y + px(10.0f)),
                rarity_color(rarity, selected ? 235 : 165), label);
        }
        ImGui::PopID();
        return clicked;
    }

    bool action_button(const char* label)
    {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton(label, ImVec2(width, px(32.0f)));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, ImVec2(position.x + width, position.y + px(32.0f)), accent_color(hovered ? 0.28f : 0.20f), px(5.0f));
        draw->AddRect(position, ImVec2(position.x + width, position.y + px(32.0f)), accent_color(hovered ? 0.52f : 0.35f), px(5.0f));
        const ImVec2 size = fonts::medium->CalcTextSizeA(px(13.0f), FLT_MAX, 0.0f, label);
        add_text(draw, fonts::medium, 13.0f, ImVec2(position.x + (width - size.x) * 0.5f, position.y + px(9.0f)), rgba(255, 255, 255, 235), label);
        return clicked;
    }

    void draw_combat(card_layout& layout)
    {
        static const char* aim_keys[] = { "Mouse 1", "Mouse 4", "Mouse 5", "Always" };
        static const char* aim_hitboxes[] = { "Head", "Neck", "Chest", "Pelvis" };

        layout.add(0, "aim_assist", "Aim Assist", "Target selection and smoothing", 208.0f, "enabled visible fov smooth target", [] {
            toggle_row("Enabled", &settings::aim_enabled, [] {
                combo_row("Activation", &settings::aim_key, aim_keys, IM_ARRAYSIZE(aim_keys));
                toggle_row("Visible only", &settings::aim_visible_only);
                toggle_row("Target teammates", &settings::aim_teammates);
                multi_bool_row("Hitboxes", settings::aim_hitboxes.data(), aim_hitboxes, IM_ARRAYSIZE(aim_hitboxes));
                toggle_row("Draw FOV", &settings::aim_show_fov);
                color_row("FOV color", &settings::aim_fov_color);
            });
            slider_row("Field of view", &settings::aim_fov, 0.5f, 20.0f, "%.1f°");
            slider_row("Smoothness", &settings::aim_smooth, 1.0f, 20.0f, "%.1f");
        });
        layout.add(1, "trigger", "Trigger", "Automatic shot conditions", 175.0f, "enabled delay hitbox", [] {
            toggle_row("Enabled", &settings::trigger_enabled, [] {
                toggle_row("Target teammates", &settings::trigger_teammates);
                toggle_row("Revolver hold", &settings::trigger_revolver_hold);
            });
            slider_row("Shot delay", &settings::trigger_delay, 0.0f, 150.0f, "%.0f ms");
            slider_row("Minimum damage", &settings::trigger_min_damage, 1.0f, 100.0f, "%.0f");
        });
        layout.add(1, "backtrack", "Backtrack", "Historical target window", 125.0f, "enabled window history", [] {
            toggle_row("Enabled", &backtrack_enabled);
            slider_row("Window", &backtrack_window, 20.0f, 200.0f, "%.0f ms");
        });
        layout.add(0, "recoil", "Recoil Control", "Compensate weapon recoil", 125.0f, "enabled amount compensation", [] {
            toggle_row("Enabled", &recoil_enabled);
            slider_row("Compensation", &recoil_amount, 0.0f, 100.0f, "%.0f%%");
        });
    }

    void draw_movement(card_layout& layout)
    {
        layout.add(0, "air", "Air Movement", "Jump and strafe helpers", 175.0f, "bunny hop auto strafe strength", [] {
            toggle_row("Bunny hop", &bunny_hop);
            toggle_row("Auto strafe", &auto_strafe);
            slider_row("Strafe strength", &strafe_strength, 0.0f, 100.0f, "%.0f%%");
        });
        layout.add(1, "edge", "Edge Assistance", "Edge-sensitive movement", 175.0f, "edge jump jump bug window", [] {
            toggle_row("Edge jump", &edge_jump);
            toggle_row("Jump bug", &jump_bug);
            slider_row("Detection window", &edge_window, 5.0f, 80.0f, "%.0f ms");
        });
        layout.add(0, "ground", "Ground Movement", "Ground movement utilities", 92.0f, "fast stop", [] {
            toggle_row("Fast stop", &fast_stop);
        });
    }

    void draw_visuals(card_layout& layout)
    {
        layout.add(0, "player_esp", "Player ESP", "Player overlay components", 258.0f, "enabled box name health weapon skeleton distance", [] {
            toggle_row("Enabled", &settings::esp_enabled, [] {
                toggle_row("Show teammates", &settings::esp_teammates);
                color_row("Enemy color", &settings::enemy_color);
                color_row("Team color", &settings::team_color);
            });
            toggle_row("Bounding box", &settings::esp_box, [] {
                color_row("Enemy color", &settings::enemy_color);
                color_row("Team color", &settings::team_color);
            });
            toggle_row("Player name", &settings::esp_name, [] {
                color_row("Text color", &settings::esp_name_color);
            });
            toggle_row("Health bar", &settings::esp_health, [] {
                color_row("High HP", &settings::esp_health_high_color);
                color_row("Low HP", &settings::esp_health_low_color);
            });
            toggle_row("Skeleton", &settings::esp_skeleton, [] {
                color_row("Enemy color", &settings::esp_skeleton_enemy_color);
                color_row("Team color", &settings::esp_skeleton_team_color);
            });
            toggle_row("Off-screen arrows", &settings::esp_offscreen, [] {
                color_row("Enemy color", &settings::esp_arrow_enemy_color);
                color_row("Team color", &settings::esp_arrow_team_color);
            });
            slider_row("Max distance", &settings::esp_distance, 200.0f, 3000.0f, "%.0f");
        });
        layout.add(1, "glow", "Glow", "Model outline settings", 125.0f, "enabled alpha", [] {
            toggle_row("Enabled", &glow_enabled);
            slider_row("Opacity", &glow_alpha, 0.0f, 100.0f, "%.0f%%");
        });
        layout.add(1, "world_esp", "World ESP", "World entity information", 118.0f, "dropped weapons grenades warning", [] {
            toggle_row("Dropped weapons", &dropped_weapons);
            toggle_row("Grenade warning", &grenade_warning);
        });
        layout.add(1, "world_modulation", "World Modulation", "Tint scene geometry", 95.0f, "world color modulation", [] {
            toggle_row("Enabled", &settings::world_modulation_enabled, [] {
                color_row("World color", &settings::world_modulation_color);
            });
        });
    }

    void draw_player(card_layout& layout)
    {
        layout.add(0, "camera", "Camera", "Camera and viewmodel", 177.0f, "fov third person viewmodel distance", [] {
            toggle_row("FOV changer", &fov_changer);
            slider_row("Camera FOV", &camera_fov, 60.0f, 140.0f, "%.0f°");
        });
        layout.add(1, "third_person", "Third Person", "Third-person camera", 125.0f, "enabled distance", [] {
            toggle_row("Enabled", &settings::third_person_enabled);
            slider_row("Distance", &settings::third_person_distance, 40.0f, 220.0f, "%.0f");
        });
        layout.add(1, "viewmodel", "Viewmodel", "Weapon position and FOV", 170.0f, "viewmodel x y z fov", [] {
            toggle_row("Enabled", &settings::viewmodel_enabled, [] {
                slider_row("Offset X", &settings::viewmodel_x, -2.0f, 2.5f, "%.1f");
                slider_row("Offset Y", &settings::viewmodel_y, -2.0f, 2.0f, "%.1f");
                slider_row("Offset Z", &settings::viewmodel_z, -2.0f, 2.0f, "%.1f");
                slider_row("Viewmodel FOV", &settings::viewmodel_fov, 60.0f, 68.0f, "%.0f°");
            });
        });
        layout.add(0, "removals", "Removals", "Hide distracting effects", 150.0f, "flash smoke scope", [] {
            toggle_row("Flash effect", &remove_flash);
            toggle_row("Smoke overlay", &remove_smoke);
            toggle_row("Scope overlay", &remove_scope);
        });
    }

    void draw_skins(card_layout& layout)
    {
        static int pending_weapon = settings::skin_weapon_definition;
        static int pending_paint = static_cast<int>(settings::skin_paint_kit);

        layout.add(0, "skin_weapons", "Weapons", "Choose an inventory weapon", 0.0f, "weapon ak awp m4 deagle", [&] {
            for (const auto& weapon : skin_catalog::weapons)
            {
                if (catalog_row(weapon.name, weapon.name, pending_weapon == weapon.definition))
                {
                    pending_weapon = weapon.definition;
                    const auto first = std::find_if(skin_catalog::paint_kits.begin(), skin_catalog::paint_kits.end(),
                        [&](const auto& kit) { return kit.weapon_definition == pending_weapon; });
                    if (first != skin_catalog::paint_kits.end()) pending_paint = first->id;
                }
                ImGui::Dummy(px(1.0f, 3.0f));
            }
        });

        layout.add(1, "skin_paints", "Paint kits", "Compatible finishes with game rarity", 0.0f, "skin paint rarity", [&] {
            for (const auto& kit : skin_catalog::paint_kits)
            {
                if (kit.weapon_definition != pending_weapon) continue;
                char id[48]{};
                std::snprintf(id, sizeof(id), "%d_%d", kit.weapon_definition, kit.id);
                if (catalog_row(id, kit.name, pending_paint == kit.id, kit.rarity)) pending_paint = kit.id;
                ImGui::Dummy(px(1.0f, 3.0f));
            }
        });

        layout.add(1, "skin_apply", "Application", "Explicitly rebuild the selected weapon", 0.0f, "apply wear seed stattrak", [&] {
            toggle_row("Skin changer", &settings::skin_changer_enabled);
            slider_row("Seed", &settings::skin_seed, 0.0f, 1000.0f, "%.0f");
            slider_row("Wear", &settings::skin_wear, 0.0001f, 1.0f, "%.4f");
            toggle_row("StatTrak", &settings::skin_stattrak);
            ImGui::Dummy(px(1.0f, 5.0f));
            if (action_button("Apply skin"))
            {
                settings::skin_changer_enabled = true;
                settings::skin_weapon_definition = pending_weapon;
                settings::skin_paint_kit = static_cast<float>(pending_paint);
                ++settings::skin_apply_revision;
            }
        });
    }

    void draw_misc(card_layout& layout)
    {
        static const char* sounds[] = { "Default", "Metal", "Bell", "Neverlose" };
        static const char* scales[] = { "75%", "100%", "125%", "150%" };
        layout.add(0, "interface", "Interface", "On-screen interface elements", 150.0f, "watermark spectators hitmarker", [] {
            toggle_row("Watermark", &watermark);
            toggle_row("Spectator list", &spectator_list);
            toggle_row("Hitmarker", &hitmarker);
        });
        layout.add(1, "sound", "Feedback", "Audio feedback settings", 120.0f, "hit sound type", [] {
            toggle_row("Hit sound", &hit_sound);
            combo_row("Sound", &hit_sound_type, sounds, IM_ARRAYSIZE(sounds));
        });
        layout.add(0, "automation", "Automation", "Small quality-of-life helpers", 120.0f, "auto accept reveal ranks", [] {
            toggle_row("Auto accept", &auto_accept);
            toggle_row("Reveal ranks", &reveal_ranks);
        });
        layout.add(1, "menu_style", "Menu Style", "Appearance of this menu", 148.0f, "opacity accent color", [] {
            slider_row("Opacity", &settings::menu_opacity, 65.0f, 100.0f, "%.0f%%");
            combo_row("Scale", &settings::menu_scale_index, scales, IM_ARRAYSIZE(scales));
            color_row("Accent", &settings::menu_accent);
        });
    }

    void draw_configs(card_layout& layout)
    {
        layout.add(0, "configs_local", "Local Configs", "Saved configuration profiles", 168.0f, "default legit visual load save", [] {
            const auto& profiles = config::profiles();
            for (std::size_t index = 0; index < profiles.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                const ImVec2 row = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::InvisibleButton("##profile_row", ImVec2(width, px(27.0f)));
                if (ImGui::IsItemClicked()) config::select(index);
                const bool selected = config::selected_index() == index;
                const bool hovered = ImGui::IsItemHovered();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                if (selected || hovered)
                    draw->AddRectFilled(row, ImVec2(row.x + width, row.y + px(27.0f)),
                        selected ? accent_color(0.11f) : rgba(255, 255, 255, 6), px(4.0f));
                if (selected)
                    draw->AddRectFilled(ImVec2(row.x + px(3.0f), row.y + px(7.0f)),
                        ImVec2(row.x + px(5.0f), row.y + px(20.0f)), accent_color(), px(1.0f));
                add_text(draw, fonts::regular, 14.0f, ImVec2(row.x + px(11.0f), row.y + px(5.0f)),
                    rgba(255, 255, 255, selected ? 215 : 125), profiles[index].c_str());
                ImGui::PopID();
            }
        });
        layout.add(1, "config_actions", "Actions", "Profiles auto-save after changes", 190.0f, "save load create delete", [] {
            static char new_name[33] = "new_config";
            push_font(fonts::medium, 13.0f);
            if (ImGui::Button("Load selected", ImVec2(-1.0f, px(30.0f)))) config::load();
            if (ImGui::Button("Save selected", ImVec2(-1.0f, px(30.0f)))) config::save();
            if (ImGui::Button("Delete selected", ImVec2(-1.0f, px(30.0f)))) config::remove();
            ImGui::PopFont();

            ImGui::SetNextItemWidth(-1.0f);
            push_font(fonts::regular, 13.0f);
            ImGui::InputTextWithHint("##new_config_name", "Profile name", new_name, sizeof(new_name));
            ImGui::PopFont();
            push_font(fonts::medium, 13.0f);
            if (ImGui::Button("Create profile", ImVec2(-1.0f, px(30.0f)))) config::create(new_name);
            ImGui::PopFont();

            if (!config::status().empty())
            {
                push_font(fonts::regular, 12.0f);
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.35f), "%s", config::status().c_str());
                ImGui::PopFont();
            }
        });
    }

    void draw_scripts(card_layout& layout)
    {
        layout.add(0, "scripts_list", "Scripts", "Loaded extension scripts", 150.0f, "scripts loaded", [] {
            push_font(fonts::regular, 14.0f);
            ImGui::TextColored(ImVec4(1, 1, 1, 0.32f), "No scripts loaded");
            ImGui::Spacing();
            ImGui::Button("Open scripts folder", ImVec2(-1.0f, px(30.0f)));
            ImGui::PopFont();
        });
        layout.add(1, "scripts_settings", "Runtime", "Script runtime settings", 110.0f, "auto reload console", [] {
            static bool auto_reload = true;
            static bool console = false;
            toggle_row("Auto reload", &auto_reload);
            toggle_row("Debug console", &console);
        });
    }

    const category_entry& selected_entry()
    {
        for (const category_entry& entry : categories)
            if (entry.value == selected_category)
                return entry;
        return categories.front();
    }

    void draw_category_button(const category_entry& entry, const ImVec2& position, float width)
    {
        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(static_cast<int>(entry.value));
        ImGui::InvisibleButton("##category", ImVec2(width, px(33.0f)));
        if (ImGui::IsItemClicked())
        {
            if (selected_category != entry.value)
                content_animation = 0.0f;
            selected_category = entry.value;
        }

        const bool hovered = ImGui::IsItemHovered();
        float& animation = category_animation[static_cast<std::size_t>(entry.value)];
        const float target = selected_category == entry.value ? 1.0f : 0.0f;
        animation += (target - animation) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (animation > 0.01f || hovered)
        {
            const float alpha_value = std::max(animation, hovered ? 0.24f : 0.0f);
            draw->AddRectFilled(position, ImVec2(position.x + width, position.y + px(33.0f)), rgba(255, 255, 255, static_cast<int>(8.0f * alpha_value)), px(6.0f));
            draw->AddRect(position, ImVec2(position.x + width, position.y + px(33.0f)), rgba(255, 255, 255, static_cast<int>(12.0f * alpha_value)), px(6.0f));
        }

        const float icon_alpha = 0.35f + 0.65f * animation;
        const float text_alpha = 0.42f + 0.48f * animation;
        add_text(draw, fonts::icons, 17.0f, ImVec2(position.x + px(9.0f), position.y + px(8.0f)), accent_color(icon_alpha), entry.icon);
        add_text(draw, fonts::regular, 15.0f, ImVec2(position.x + px(38.0f), position.y + px(8.0f)), rgba(255, 255, 255, static_cast<int>(255.0f * text_alpha)), entry.name);
        ImGui::PopID();
    }

    void draw_sidebar(const ImVec2& origin, float height)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        add_text(draw, fonts::icons, 31.0f, ImVec2(origin.x + px(61.0f), origin.y + px(9.0f)), accent_color(), icon_logo);

        add_text(draw, fonts::regular, 12.0f, ImVec2(origin.x + px(14.0f), origin.y + px(61.0f)), rgba(255, 255, 255, 95), "Category");
        float y = origin.y + px(78.0f);
        for (const category_entry& entry : categories)
        {
            if (entry.secondary)
                continue;
            draw_category_button(entry, ImVec2(origin.x + px(9.0f), y), px(147.0f));
            y += px(39.0f);
        }

        add_text(draw, fonts::regular, 12.0f, ImVec2(origin.x + px(14.0f), origin.y + px(324.0f)), rgba(255, 255, 255, 95), "Other");
        y = origin.y + px(341.0f);
        for (const category_entry& entry : categories)
        {
            if (!entry.secondary)
                continue;
            draw_category_button(entry, ImVec2(origin.x + px(9.0f), y), px(147.0f));
            y += px(39.0f);
        }

        const ImVec2 profile_position(origin.x + px(10.0f), origin.y + height - px(48.0f));
        ImGui::SetCursorScreenPos(profile_position);
        ImGui::InvisibleButton("##profile", px(145.0f, 38.0f));
        if (ImGui::IsItemClicked())
            ImGui::OpenPopup("profile_popup");

        draw->AddRectFilled(profile_position, ImVec2(profile_position.x + px(30.0f), profile_position.y + px(30.0f)), rgba(255, 255, 255, 18), px(7.0f));
        add_text(draw, fonts::medium, 13.0f, ImVec2(profile_position.x + px(39.0f), profile_position.y + px(4.0f)), rgba(255, 255, 255, 225), "Molly0ne");
        add_text(draw, fonts::regular, 11.0f, ImVec2(profile_position.x + px(39.0f), profile_position.y + px(20.0f)), rgba(255, 255, 255, 85), "till: Never");

        ImGui::SetNextWindowSize(px(230.0f, 120.0f));
        if (ImGui::BeginPopup("profile_popup"))
        {
            push_font(fonts::medium, 14.0f);
            ImGui::TextUnformatted("Menu appearance");
            ImGui::PopFont();
            ImGui::Separator();
            color_row("Main color", &settings::menu_accent);
            slider_row("Opacity", &settings::menu_opacity, 65.0f, 100.0f, "%.0f%%");
            ImGui::EndPopup();
        }
    }

    void draw_header(const ImVec2& origin, float width)
    {
        const category_entry& entry = selected_entry();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        add_text(draw, fonts::icons, 14.0f, ImVec2(origin.x + px(181.0f), origin.y + px(16.0f)), rgba(255, 255, 255, 105), icon_logo);
        add_text(draw, fonts::regular, 13.0f, ImVec2(origin.x + px(202.0f), origin.y + px(17.0f)), rgba(255, 255, 255, 100), "Main  /");
        add_text(draw, fonts::icons, 14.0f, ImVec2(origin.x + px(250.0f), origin.y + px(16.0f)), accent_color(0.65f), entry.icon);
        add_text(draw, fonts::regular, 13.0f, ImVec2(origin.x + px(271.0f), origin.y + px(17.0f)), rgba(255, 255, 255, 165), entry.name);

        const ImVec2 search_position(origin.x + width - px(167.0f), origin.y + px(10.0f));
        draw->AddRectFilled(search_position, ImVec2(search_position.x + px(151.0f), search_position.y + px(28.0f)), rgba(255, 255, 255, 5), px(6.0f));
        draw->AddRect(search_position, ImVec2(search_position.x + px(151.0f), search_position.y + px(28.0f)), rgba(255, 255, 255, 10), px(6.0f));
        add_text(draw, fonts::icons, 14.0f, ImVec2(search_position.x + px(8.0f), search_position.y + px(7.0f)), rgba(255, 255, 255, 75), icon_search);

        ImGui::SetCursorScreenPos(ImVec2(search_position.x + px(29.0f), search_position.y + px(2.0f)));
        ImGui::SetNextItemWidth(px(114.0f));
        push_font(fonts::regular, 13.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32_BLACK_TRANS);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(0.0f, 6.0f));
        ImGui::InputTextWithHint("##search", "Search", search_text, sizeof(search_text));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void draw_content(const ImVec2& origin, float width, float height)
    {
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);

        if (ImGui::BeginChild("##content", ImVec2(width, height), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus))
        {
            content_animation += (1.0f - content_animation) * std::min(1.0f, ImGui::GetIO().DeltaTime * 5.0f);
            const float eased = 1.0f - std::pow(1.0f - content_animation, 3.0f);
            draw_alpha = eased;
            const float column_width = (width - px(13.0f)) * 0.5f;
            card_layout layout{ column_width };
            layout.y[0] = layout.y[1] = (1.0f - eased) * px(8.0f);

            switch (selected_category)
            {
            case category::combat: draw_combat(layout); break;
            case category::movement: draw_movement(layout); break;
            case category::visuals: draw_visuals(layout); break;
            case category::player: draw_player(layout); break;
            case category::skins: draw_skins(layout); break;
            case category::misc: draw_misc(layout); break;
            case category::configs: draw_configs(layout); break;
            case category::scripts: draw_scripts(layout); break;
            default: break;
            }

            ImGui::SetCursorPos(ImVec2(0.0f, layout.bottom()));
            ImGui::Dummy(px(1.0f, 1.0f));
            draw_alpha = 1.0f;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

}

namespace menu
{
    bool is_open()
    {
        return open.load(std::memory_order_relaxed);
    }

    void toggle()
    {
        open.store(!open.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void close()
    {
        open.store(false, std::memory_order_relaxed);
    }

    void draw()
    {
        if (!is_open())
            return;

        interface_scale = settings::menu_scale();
        const ImVec2 window_size = px(690.0f, 510.0f);
        const float sidebar_width = px(165.0f);
        const float header_height = px(48.0f);
        const float content_padding = px(14.0f);

        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, px(0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, px(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(8.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32_BLACK_TRANS);
        push_font(fonts::regular, 16.0f);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus;

        if (ImGui::Begin("##priority_cs2", nullptr, flags))
        {
            const ImVec2 origin = ImGui::GetWindowPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const int background_alpha = static_cast<int>(255.0f * std::clamp(settings::menu_opacity / 100.0f, 0.65f, 1.0f));

            draw->AddRectFilled(origin, ImVec2(origin.x + window_size.x, origin.y + window_size.y), rgba(11, 11, 13, background_alpha), px(10.0f));
            draw->AddRectFilled(origin, ImVec2(origin.x + sidebar_width, origin.y + window_size.y), rgba(255, 255, 255, 3), px(10.0f), ImDrawFlags_RoundCornersLeft);
            draw->AddLine(ImVec2(origin.x + sidebar_width, origin.y), ImVec2(origin.x + sidebar_width, origin.y + window_size.y), rgba(255, 255, 255, 11), px(1.0f));
            draw->AddLine(ImVec2(origin.x + sidebar_width, origin.y + header_height), ImVec2(origin.x + window_size.x, origin.y + header_height), rgba(255, 255, 255, 11), px(1.0f));
            draw->AddLine(ImVec2(origin.x + px(9.0f), origin.y + header_height), ImVec2(origin.x + sidebar_width - px(9.0f), origin.y + header_height), rgba(255, 255, 255, 11), px(1.0f));

            draw_sidebar(origin, window_size.y);
            draw_header(origin, window_size.x);
            draw_content(
                ImVec2(origin.x + sidebar_width + content_padding, origin.y + header_height + content_padding),
                window_size.x - sidebar_width - content_padding * 2.0f,
                window_size.y - header_height - content_padding * 2.0f);
        }
        ImGui::End();
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(6);
    }
}
