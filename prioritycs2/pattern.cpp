#include "pch.h"

#include "pattern.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::vector<int> parse(std::string_view signature)
    {
        std::vector<int> result;
        for (std::size_t index = 0; index < signature.size();)
        {
            if (signature[index] == ' ') { ++index; continue; }
            if (signature[index] == '?')
            {
                result.push_back(-1);
                index += index + 1 < signature.size() && signature[index + 1] == '?' ? 2 : 1;
                continue;
            }
            char pair[3]{ signature[index], index + 1 < signature.size() ? signature[index + 1] : '0', '\0' };
            result.push_back(static_cast<int>(std::strtoul(pair, nullptr, 16)));
            index += 2;
        }
        return result;
    }
}

namespace pattern
{
    std::uintptr_t find(HMODULE module, std::string_view signature)
    {
        if (!module)
            return 0;
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const std::vector<int> bytes = parse(signature);
        if (bytes.empty())
            return 0;
        std::size_t anchor = 0;
        while (anchor < bytes.size() && bytes[anchor] < 0) ++anchor;
        if (anchor == bytes.size())
            return 0;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned section_index = 0; section_index < nt->FileHeader.NumberOfSections; ++section_index, ++section)
        {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const auto* begin = reinterpret_cast<const unsigned char*>(base + section->VirtualAddress);
            const std::size_t size = static_cast<std::size_t>(section->Misc.VirtualSize);
            if (size < bytes.size())
                continue;
            const unsigned char* cursor = begin;
            const unsigned char* end = begin + size - bytes.size() + 1;
            while (cursor < end)
            {
                const auto* anchor_begin = cursor + anchor;
                const auto* anchor_end = end + anchor;
                const void* found = std::memchr(anchor_begin, bytes[anchor], static_cast<std::size_t>(anchor_end - anchor_begin));
                if (!found) break;
                const auto* candidate = static_cast<const unsigned char*>(found) - anchor;
                bool matches = candidate >= begin && candidate < end;
                for (std::size_t byte = 0; matches && byte < bytes.size(); ++byte)
                    matches = bytes[byte] < 0 || candidate[byte] == static_cast<unsigned char>(bytes[byte]);
                if (matches) return reinterpret_cast<std::uintptr_t>(candidate);
                cursor = static_cast<const unsigned char*>(found) + 1;
                if (cursor > begin + anchor) cursor -= anchor;
            }
        }
        return 0;
    }

    std::uintptr_t find(std::wstring_view module_name, std::string_view signature)
    {
        const std::wstring name(module_name);
        return find(GetModuleHandleW(name.c_str()), signature);
    }

    std::uintptr_t resolve_relative(std::uintptr_t instruction, std::size_t displacement_offset, std::size_t instruction_size)
    {
        if (!instruction)
            return 0;
        const auto displacement = *reinterpret_cast<const std::int32_t*>(instruction + displacement_offset);
        return instruction + instruction_size + displacement;
    }
}
