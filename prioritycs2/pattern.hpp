#pragma once

#include <Windows.h>
#include <cstdint>
#include <string_view>

namespace pattern
{
    std::uintptr_t find(HMODULE module, std::string_view signature);
    std::uintptr_t find(std::wstring_view module_name, std::string_view signature);
    std::uintptr_t resolve_relative(std::uintptr_t instruction, std::size_t displacement_offset = 1, std::size_t instruction_size = 5);
}
