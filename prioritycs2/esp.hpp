#pragma once

struct ImDrawList;
struct ImVec2;

namespace esp
{
    void draw(ImDrawList* draw_list, const ImVec2& display_size);
    bool has_local_player();
    void reset();
}
