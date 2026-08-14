#pragma once

#include <Windows.h>

struct ImFont;

namespace fonts
{
    inline ImFont* regular = nullptr;
    inline ImFont* medium = nullptr;
    inline ImFont* bold = nullptr;
    inline ImFont* icons = nullptr;

    bool initialize(HMODULE module);
    void reset();
}
