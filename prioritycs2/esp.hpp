#pragma once

struct ImDrawList;
struct ImVec2;

namespace esp
{
    void draw(ImDrawList* draw_list, const ImVec2& display_size);
    void reset();
}
