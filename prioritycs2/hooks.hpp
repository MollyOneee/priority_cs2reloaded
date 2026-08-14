#pragma once

#include <Windows.h>

namespace hooks
{
    bool initialize(HMODULE module);
    void shutdown();
}
