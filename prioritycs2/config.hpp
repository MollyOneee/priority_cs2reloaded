#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace config
{
    bool initialize();
    void tick();
    void shutdown();

    const std::vector<std::string>& profiles();
    std::size_t selected_index();
    void select(std::size_t index);
    bool create(std::string_view name);
    bool save();
    bool load();
    bool remove();
    const std::string& status();
}
