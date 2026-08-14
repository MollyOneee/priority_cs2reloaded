#include "pch.h"

#include "hooks.hpp"

#include "esp.hpp"
#include "renderer.hpp"
#include "menu.hpp"
#include "pattern.hpp"
#include "settings.hpp"

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    using present_fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using resize_buffers_fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using get_raw_input_data_fn = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
    using get_raw_input_buffer_fn = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
    using get_async_key_state_fn = SHORT(WINAPI*)(int);
    using get_key_state_fn = SHORT(WINAPI*)(int);
    using get_keyboard_state_fn = BOOL(WINAPI*)(PBYTE);
    using override_view_fn = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
    using draw_scene_object_fn = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, int, int, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    struct viewmodel_vector { float x{}, y{}, z{}; };
    using viewmodel_fn = void(__fastcall*)(__int64, viewmodel_vector*, float*);

    present_fn original_present = nullptr;
    resize_buffers_fn original_resize_buffers = nullptr;
    void* present_target = nullptr;
    void* resize_buffers_target = nullptr;
    void* get_raw_input_data_target = nullptr;
    void* get_raw_input_buffer_target = nullptr;
    void* get_async_key_state_target = nullptr;
    void* get_key_state_target = nullptr;
    void* get_keyboard_state_target = nullptr;
    get_raw_input_data_fn original_get_raw_input_data = nullptr;
    get_raw_input_buffer_fn original_get_raw_input_buffer = nullptr;
    get_async_key_state_fn original_get_async_key_state = nullptr;
    get_key_state_fn original_get_key_state = nullptr;
    get_keyboard_state_fn original_get_keyboard_state = nullptr;
    viewmodel_fn original_viewmodel = nullptr;
    void* viewmodel_target = nullptr;
    override_view_fn original_override_view = nullptr;
    void* override_view_target = nullptr;
    draw_scene_object_fn original_draw_scene_object = nullptr;
    void* draw_scene_object_target = nullptr;

    void __fastcall override_view(std::uintptr_t context, std::uintptr_t view_setup)
    {
        original_override_view(context, view_setup);
        if (!settings::third_person_enabled || !esp::has_local_player() || !view_setup)
            return;

        struct vector3 { float x{}, y{}, z{}; };
        auto* origin = reinterpret_cast<vector3*>(view_setup + 0x4a0);
        const auto* angles = reinterpret_cast<const vector3*>(view_setup + 0x4b8);
        if (!std::isfinite(origin->x) || !std::isfinite(origin->y) || !std::isfinite(origin->z) ||
            !std::isfinite(angles->x) || !std::isfinite(angles->y))
            return;
        constexpr float to_radians = 0.017453292519943295f;
        const float pitch = angles->x * to_radians;
        const float yaw = angles->y * to_radians;
        const float cosine_pitch = std::cos(pitch);
        const float distance = std::clamp(settings::third_person_distance, 40.0f, 220.0f);
        origin->x -= cosine_pitch * std::cos(yaw) * distance;
        origin->y -= cosine_pitch * std::sin(yaw) * distance;
        origin->z -= -std::sin(pitch) * distance;
    }

    std::uintptr_t __fastcall draw_scene_object(std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch,
        int batch_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8)
    {
        if (settings::world_modulation_enabled && batch && batch_count > 0 && batch_count < 4096)
        {
            const ImVec4 color = settings::world_modulation_color;
            struct packed_color { std::uint8_t r, g, b, a; };
            const packed_color tint{
                static_cast<std::uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f),
                static_cast<std::uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f),
                static_cast<std::uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f),
                static_cast<std::uint8_t>(std::clamp(color.w, 0.0f, 1.0f) * 255.0f)
            };
            __try
            {
                for (int index = 0; index < batch_count; ++index)
                    *reinterpret_cast<packed_color*>(batch + static_cast<std::uintptr_t>(index) * 0x68 + 0x50) = tint;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return original_draw_scene_object(a1, a2, batch, batch_count, a5, a6, a7, a8);
    }

    void __fastcall viewmodel(__int64 context, viewmodel_vector* position, float* fov)
    {
        original_viewmodel(context, position, fov);
        if (!settings::viewmodel_enabled)
            return;
        if (position)
        {
            position->x = std::clamp(settings::viewmodel_x, -2.0f, 2.5f);
            position->y = std::clamp(settings::viewmodel_y, -2.0f, 2.0f);
            position->z = std::clamp(settings::viewmodel_z, -2.0f, 2.0f);
        }
        if (fov)
            *fov = std::clamp(settings::viewmodel_fov, 60.0f, 68.0f);
    }

    bool is_mouse_key(int key)
    {
        return key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON ||
            key == VK_XBUTTON1 || key == VK_XBUTTON2;
    }

    void sanitize_raw_input(RAWINPUT* input)
    {
        if (!input || input->header.dwType != RIM_TYPEMOUSE)
            return;

        input->data.mouse.usButtonFlags = 0;
        input->data.mouse.usButtonData = 0;
        input->data.mouse.ulRawButtons = 0;
        input->data.mouse.lLastX = 0;
        input->data.mouse.lLastY = 0;
    }

    UINT WINAPI get_raw_input_data(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT header_size)
    {
        const UINT result = original_get_raw_input_data(handle, command, data, size, header_size);
        if (menu::is_open() && command == RID_INPUT && data && result != static_cast<UINT>(-1))
            sanitize_raw_input(static_cast<RAWINPUT*>(data));
        return result;
    }

    UINT WINAPI get_raw_input_buffer(PRAWINPUT data, PUINT size, UINT header_size)
    {
        const UINT result = original_get_raw_input_buffer(data, size, header_size);
        if (!menu::is_open() || !data || result == static_cast<UINT>(-1))
            return result;

        PRAWINPUT current = data;
        for (UINT index = 0; index < result; ++index)
        {
            sanitize_raw_input(current);
            const ULONG_PTR next = reinterpret_cast<ULONG_PTR>(current) + current->header.dwSize;
            const ULONG_PTR aligned = (next + sizeof(ULONG_PTR) - 1) & ~(sizeof(ULONG_PTR) - 1);
            current = reinterpret_cast<PRAWINPUT>(aligned);
        }
        return result;
    }

    SHORT WINAPI get_async_key_state(int key)
    {
        if (menu::is_open() && is_mouse_key(key))
            return 0;
        return original_get_async_key_state(key);
    }

    SHORT WINAPI get_key_state(int key)
    {
        if (menu::is_open() && is_mouse_key(key))
            return 0;
        return original_get_key_state(key);
    }

    BOOL WINAPI get_keyboard_state(PBYTE state)
    {
        const BOOL result = original_get_keyboard_state(state);
        if (result && state && menu::is_open())
        {
            state[VK_LBUTTON] = 0;
            state[VK_RBUTTON] = 0;
            state[VK_MBUTTON] = 0;
            state[VK_XBUTTON1] = 0;
            state[VK_XBUTTON2] = 0;
        }
        return result;
    }

    HRESULT __stdcall present(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags)
    {
        renderer::on_present(swap_chain);
        return original_present(swap_chain, sync_interval, flags);
    }

    HRESULT __stdcall resize_buffers(
        IDXGISwapChain* swap_chain,
        UINT buffer_count,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT swap_chain_flags)
    {
        renderer::on_pre_resize();
        const HRESULT result = original_resize_buffers(
            swap_chain,
            buffer_count,
            width,
            height,
            format,
            swap_chain_flags);

        if (SUCCEEDED(result))
            renderer::on_post_resize(swap_chain);

        return result;
    }

    bool get_swap_chain_methods(void*& present_method, void*& resize_method)
    {
        constexpr wchar_t class_name[] = L"prioritycs2_dummy_window";
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name;

        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        const HWND window = CreateWindowExW(
            0,
            class_name,
            L"prioritycs2",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            100,
            100,
            nullptr,
            nullptr,
            instance,
            nullptr);

        if (!window)
        {
            UnregisterClassW(class_name, instance);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
        swap_chain_desc.BufferCount = 1;
        swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.OutputWindow = window;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.Windowed = TRUE;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swap_chain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;

        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &swap_chain_desc,
            &swap_chain,
            &device,
            nullptr,
            &context);

        if (FAILED(result))
        {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &swap_chain_desc,
                &swap_chain,
                &device,
                nullptr,
                &context);
        }

        if (SUCCEEDED(result))
        {
            void** vtable = *reinterpret_cast<void***>(swap_chain);
            present_method = vtable[8];
            resize_method = vtable[13];
        }

        if (context)
            context->Release();
        if (device)
            device->Release();
        if (swap_chain)
            swap_chain->Release();

        DestroyWindow(window);
        UnregisterClassW(class_name, instance);
        return SUCCEEDED(result);
    }

    bool create_input_hooks()
    {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32)
            return false;

        get_raw_input_data_target = reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputData"));
        get_raw_input_buffer_target = reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputBuffer"));
        get_async_key_state_target = reinterpret_cast<void*>(GetProcAddress(user32, "GetAsyncKeyState"));
        get_key_state_target = reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyState"));
        get_keyboard_state_target = reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyboardState"));

        if (!get_raw_input_data_target || !get_raw_input_buffer_target || !get_async_key_state_target ||
            !get_key_state_target || !get_keyboard_state_target)
            return false;

        return MH_CreateHook(get_raw_input_data_target, &get_raw_input_data, reinterpret_cast<void**>(&original_get_raw_input_data)) == MH_OK &&
            MH_CreateHook(get_raw_input_buffer_target, &get_raw_input_buffer, reinterpret_cast<void**>(&original_get_raw_input_buffer)) == MH_OK &&
            MH_CreateHook(get_async_key_state_target, &get_async_key_state, reinterpret_cast<void**>(&original_get_async_key_state)) == MH_OK &&
            MH_CreateHook(get_key_state_target, &get_key_state, reinterpret_cast<void**>(&original_get_key_state)) == MH_OK &&
            MH_CreateHook(get_keyboard_state_target, &get_keyboard_state, reinterpret_cast<void**>(&original_get_keyboard_state)) == MH_OK;
    }

    bool create_game_hooks()
    {
        viewmodel_target = reinterpret_cast<void*>(pattern::find(L"client.dll", "40 55 53 56 41 56 41 57 48 8B"));
        const auto override_anchor = pattern::find(L"client.dll", "A8 00 00 00 48 8D 05 ? ? ? ? 4C 89 74 24 20");
        const auto override_table = override_anchor ? pattern::resolve_relative(override_anchor + 4, 3, 7) : 0;
        override_view_target = override_table ? *reinterpret_cast<void**>(override_table + 0x78) : nullptr;

        const auto scene_anchor = pattern::find(L"scenesystem.dll", "48 8D 05 ? ? ? ? 48 89 07 48 8B 7C 24 48");
        const auto scene_table = pattern::resolve_relative(scene_anchor, 3, 7);
        draw_scene_object_target = scene_table ? *reinterpret_cast<void**>(scene_table + 0x8) : nullptr;

        return viewmodel_target && override_view_target && draw_scene_object_target &&
            MH_CreateHook(viewmodel_target, &viewmodel, reinterpret_cast<void**>(&original_viewmodel)) == MH_OK &&
            MH_CreateHook(override_view_target, &override_view, reinterpret_cast<void**>(&original_override_view)) == MH_OK &&
            MH_CreateHook(draw_scene_object_target, &draw_scene_object, reinterpret_cast<void**>(&original_draw_scene_object)) == MH_OK;
    }
}

namespace hooks
{
    bool initialize(HMODULE module)
    {
        renderer::set_module(module);

        if (!get_swap_chain_methods(present_target, resize_buffers_target))
            return false;

        if (MH_Initialize() != MH_OK)
            return false;

        if (MH_CreateHook(present_target, &present, reinterpret_cast<void**>(&original_present)) != MH_OK ||
            MH_CreateHook(resize_buffers_target, &resize_buffers, reinterpret_cast<void**>(&original_resize_buffers)) != MH_OK ||
            !create_input_hooks() || !create_game_hooks())
        {
            MH_Uninitialize();
            return false;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        {
            MH_RemoveHook(present_target);
            MH_RemoveHook(resize_buffers_target);
            MH_Uninitialize();
            return false;
        }

        return true;
    }

    void shutdown()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        renderer::shutdown();

        if (present_target)
            MH_RemoveHook(present_target);
        if (resize_buffers_target)
            MH_RemoveHook(resize_buffers_target);

        MH_Uninitialize();
    }
}
