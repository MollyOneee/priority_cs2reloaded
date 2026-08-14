#include "pch.h"

#include "renderer.hpp"

#include "fonts.hpp"
#include "esp.hpp"
#include "menu.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

namespace
{
    HMODULE module_handle = nullptr;
    HWND window = nullptr;
    WNDPROC original_wnd_proc = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;
    bool initialized = false;
    bool imgui_context_created = false;
    bool win32_backend_initialized = false;
    bool dx11_backend_initialized = false;
    bool cursor_captured = false;
    RECT saved_clip{};
    HCURSOR saved_cursor = nullptr;
    bool saved_cursor_visible = false;
    int show_cursor_adjustments = 0;

    bool is_mouse_message(UINT message)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return true;
        default:
            return false;
        }
    }

    bool is_keyboard_message(UINT message)
    {
        return message == WM_KEYDOWN || message == WM_KEYUP ||
            message == WM_CHAR || message == WM_DEADCHAR || message == WM_UNICHAR;
    }

    void acquire_cursor()
    {
        if (cursor_captured)
            return;

        GetClipCursor(&saved_clip);
        CURSORINFO cursor_info{ sizeof(CURSORINFO) };
        if (GetCursorInfo(&cursor_info))
        {
            saved_cursor = cursor_info.hCursor;
            saved_cursor_visible = (cursor_info.flags & CURSOR_SHOWING) != 0;
        }
        ClipCursor(nullptr);
        ReleaseCapture();

        do
        {
            ++show_cursor_adjustments;
        } while (ShowCursor(TRUE) < 0);

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        cursor_captured = true;
    }

    void release_cursor()
    {
        if (!cursor_captured)
            return;

        ClipCursor(&saved_clip);
        while (show_cursor_adjustments-- > 0)
            ShowCursor(FALSE);
        show_cursor_adjustments = 0;

        SetCursor(saved_cursor_visible ? (saved_cursor ? saved_cursor : LoadCursorW(nullptr, IDC_ARROW)) : nullptr);
        saved_cursor = nullptr;
        saved_cursor_visible = false;
        cursor_captured = false;
    }

    void sync_cursor_state()
    {
        if (menu::is_open())
        {
            acquire_cursor();
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        else
        {
            release_cursor();
        }
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_KEYUP && wparam == VK_INSERT)
        {
            menu::toggle();
            sync_cursor_state();
            return 0;
        }

        if (initialized)
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);

        if (menu::is_open())
        {
            if (message == WM_SETCURSOR)
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }

            if (message == WM_INPUT)
                return DefWindowProcW(hwnd, message, wparam, lparam);

            if (is_mouse_message(message) || is_keyboard_message(message))
                return 0;
        }

        return CallWindowProcW(original_wnd_proc, hwnd, message, wparam, lparam);
    }

    bool create_render_target(IDXGISwapChain* swap_chain)
    {
        ID3D11Texture2D* back_buffer = nullptr;
        const HRESULT result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (FAILED(result))
            return false;

        const HRESULT view_result = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
        back_buffer->Release();
        return SUCCEEDED(view_result);
    }

    void release_render_target()
    {
        if (render_target)
        {
            render_target->Release();
            render_target = nullptr;
        }
    }

    void cleanup_resources()
    {
        menu::close();
        release_cursor();

        if (window && original_wnd_proc)
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc));

        if (dx11_backend_initialized)
            ImGui_ImplDX11_Shutdown();
        if (win32_backend_initialized)
            ImGui_ImplWin32_Shutdown();
        if (imgui_context_created)
            ImGui::DestroyContext();

        fonts::reset();
        esp::reset();
        release_render_target();

        if (device_context)
            device_context->Release();
        if (device)
            device->Release();

        device_context = nullptr;
        device = nullptr;
        original_wnd_proc = nullptr;
        window = nullptr;
        imgui_context_created = false;
        win32_backend_initialized = false;
        dx11_backend_initialized = false;
        initialized = false;
    }

    void apply_style()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 6.0f;
        style.GrabRounding = 5.0f;
        style.TabRounding = 5.0f;
        style.WindowPadding = ImVec2(14.0f, 14.0f);
        style.FramePadding = ImVec2(10.0f, 6.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.059f, 0.071f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.64f, 0.66f, 0.78f, 0.18f);
        colors[ImGuiCol_Header] = ImVec4(0.64f, 0.66f, 0.78f, 0.10f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.64f, 0.66f, 0.78f, 0.16f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.64f, 0.66f, 0.78f, 0.20f);
        colors[ImGuiCol_Tab] = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.64f, 0.66f, 0.78f, 0.14f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.64f, 0.66f, 0.78f, 0.18f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.64f, 0.66f, 0.78f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.64f, 0.66f, 0.78f, 0.85f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.66f, 0.78f, 1.00f);
        colors[ImGuiCol_NavCursor] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    bool initialize(IDXGISwapChain* swap_chain)
    {
        if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
            return false;

        device->GetImmediateContext(&device_context);

        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swap_chain->GetDesc(&description)))
        {
            cleanup_resources();
            return false;
        }

        window = description.OutputWindow;
        if (!window || !create_render_target(swap_chain))
        {
            cleanup_resources();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui_context_created = true;
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        fonts::initialize(module_handle);
        apply_style();

        if (!ImGui_ImplWin32_Init(window))
        {
            cleanup_resources();
            return false;
        }
        win32_backend_initialized = true;

        if (!ImGui_ImplDX11_Init(device, device_context))
        {
            cleanup_resources();
            return false;
        }
        dx11_backend_initialized = true;

        SetLastError(0);
        original_wnd_proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc)));

        if (!original_wnd_proc)
        {
            cleanup_resources();
            return false;
        }

        initialized = true;
        return true;
    }
}

namespace renderer
{
    void set_module(HMODULE module)
    {
        module_handle = module;
    }

    void on_present(IDXGISwapChain* swap_chain)
    {
        if (!initialized && !initialize(swap_chain))
            return;

        sync_cursor_state();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        esp::draw(ImGui::GetBackgroundDrawList(), ImGui::GetIO().DisplaySize);
        menu::draw();
        ImGui::Render();

        device_context->OMSetRenderTargets(1, &render_target, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void on_pre_resize()
    {
        if (!initialized)
            return;

        ImGui_ImplDX11_InvalidateDeviceObjects();
        release_render_target();
    }

    void on_post_resize(IDXGISwapChain* swap_chain)
    {
        if (!initialized)
            return;

        create_render_target(swap_chain);
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    void shutdown()
    {
        cleanup_resources();
    }
}
