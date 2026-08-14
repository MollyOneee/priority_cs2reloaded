#include "pch.h"

#include "fonts.hpp"
#include "resource.h"

#include <imgui.h>

#include <cstdio>

namespace
{
    struct resource_view
    {
        void* data = nullptr;
        int size = 0;
    };

    resource_view get_resource(HMODULE module, int identifier)
    {
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(identifier), RT_RCDATA);
        if (!resource)
            return {};

        const HGLOBAL loaded = LoadResource(module, resource);
        if (!loaded)
            return {};

        return {
            LockResource(loaded),
            static_cast<int>(SizeofResource(module, resource))
        };
    }

    ImFont* load_font(HMODULE module, int identifier, float size, const char* name)
    {
        const resource_view resource = get_resource(module, identifier);
        if (!resource.data || resource.size <= 0)
            return nullptr;

        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        std::snprintf(config.Name, sizeof(config.Name), "%s, %.0fpx", name, size);

        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            resource.data,
            resource.size,
            size,
            &config,
            ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
    }

    ImFont* load_icon_font(HMODULE module)
    {
        static const ImWchar ranges[] = {
            0x0020, 0x00FF,
            0x0390, 0x04FF,
            0x2000, 0x27FF,
            0xE000, 0xE196,
            0
        };

        const resource_view resource = get_resource(module, IDR_FONT_ICONISHE);
        if (!resource.data || resource.size <= 0)
            return nullptr;

        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.GlyphMinAdvanceX = 40.0f;
        std::snprintf(config.Name, sizeof(config.Name), "Iconishe, 40px");

        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            resource.data,
            resource.size,
            40.0f,
            &config,
            ranges);
    }
}

namespace fonts
{
    bool initialize(HMODULE module)
    {
        // Rasterize at 2x and render down to the requested UI size. This keeps 125/150% crisp
        // instead of stretching a 16 px atlas after the frame has already been generated.
        regular = load_font(module, IDR_FONT_SFPRO_REGULAR, 32.0f, "SF Pro Display Regular");
        medium = load_font(module, IDR_FONT_SFPRO_MEDIUM, 32.0f, "SF Pro Display Medium");
        bold = load_font(module, IDR_FONT_SFPRO_BOLD, 42.0f, "SF Pro Display Bold");
        icons = load_icon_font(module);

        if (!regular || !medium || !bold)
        {
            ImFont* fallback = ImGui::GetIO().Fonts->AddFontDefault();
            regular = regular ? regular : fallback;
            medium = medium ? medium : fallback;
            bold = bold ? bold : fallback;
        }

        if (!icons)
            icons = regular;

        ImGui::GetIO().FontDefault = regular;
        return regular && medium && bold && icons;
    }

    void reset()
    {
        regular = nullptr;
        medium = nullptr;
        bold = nullptr;
        icons = nullptr;
    }
}
