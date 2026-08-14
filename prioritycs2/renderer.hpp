#pragma once

#include <Windows.h>

struct IDXGISwapChain;

namespace renderer
{
    void set_module(HMODULE module);
    void on_present(IDXGISwapChain* swap_chain);
    void on_pre_resize();
    void on_post_resize(IDXGISwapChain* swap_chain);
    void shutdown();
}
