#pragma once

struct ImDrawList;
struct ImVec2;

namespace esp
{
    void draw(ImDrawList* draw_list, const ImVec2& display_size);
    void on_frame_stage(int stage);
    bool has_local_player();
    void reset();
}
