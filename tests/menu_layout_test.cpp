#include <imgui.h>
#include <imgui_internal.h>

#include "fonts.hpp"
#include "menu.hpp"
#include "settings.hpp"

#include <cmath>
#include <cstring>
#include <cstdio>

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    ImFont* fallback = io.Fonts->AddFontDefault();
    fonts::regular = fallback;
    fonts::medium = fallback;
    fonts::bold = fallback;
    fonts::icons = fallback;

    unsigned char* pixels = nullptr;
    int atlas_width = 0;
    int atlas_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

    const auto render_frame = [&]
    {
        ImGui::NewFrame();
        menu::draw();
        ImGui::Render();
    };

    const auto verify_no_internal_scroll = [&]
    {
        ImGuiWindow* root = ImGui::FindWindowByName("##priority_cs2");
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
        {
            bool belongs = false;
            for (ImGuiWindow* current = window; current; current = current->ParentWindow)
                belongs = belongs || current == root;

            const bool content_scroller = std::strstr(window->Name, "/##content_") != nullptr;
            if (belongs && window != root && !content_scroller && window->ScrollMax.y > 0.01f)
            {
                std::fprintf(stderr, "unexpected scroll in %s: %.2f\n", window->Name, window->ScrollMax.y);
                return false;
            }
        }
        return true;
    };

    menu::toggle();
    render_frame();

    constexpr float window_x = (1920.0f - 690.0f) * 0.5f;
    constexpr float window_y = (1080.0f - 510.0f) * 0.5f;
    constexpr float category_rows[] = { 78.0f, 117.0f, 156.0f, 195.0f, 234.0f, 302.0f, 341.0f };
    for (const float row : category_rows)
    {
        io.AddMousePosEvent(window_x + 50.0f, window_y + row + 15.0f);
        io.AddMouseButtonEvent(0, true);
        render_frame();
        io.AddMouseButtonEvent(0, false);
        render_frame();
        if (!verify_no_internal_scroll())
            return 2;
    }

    for (int scale = 0; scale < 4; ++scale)
    {
        settings::menu_scale_index = scale;
        render_frame();
        if (!verify_no_internal_scroll())
            return 3;

        constexpr float scale_values[] = { 0.75f, 1.0f, 1.25f, 1.5f };
        const float factor = scale_values[scale];
        const float root_x = (io.DisplaySize.x - 690.0f * factor) * 0.5f;
        const float root_y = (io.DisplaySize.y - 510.0f * factor) * 0.5f;

        // Open Visuals using its physically scaled hitbox.
        io.AddMousePosEvent(root_x + 50.0f * factor, root_y + (156.0f + 15.0f) * factor);
        io.AddMouseButtonEvent(0, true);
        render_frame();
        io.AddMouseButtonEvent(0, false);
        render_frame();

        // Click the first Player ESP boolean. Its hitbox must match the scaled drawing.
        settings::esp_enabled = true;
        io.AddMousePosEvent(root_x + 205.0f * factor, root_y + 126.0f * factor);
        io.AddMouseButtonEvent(0, true);
        render_frame();
        io.AddMouseButtonEvent(0, false);
        render_frame();
        if (settings::esp_enabled)
        {
            std::fprintf(stderr, "scaled setting click failed at %.2f\n", factor);
            return 4;
        }
    }

    menu::close();
    ImGui::DestroyContext();
    return 0;
}
