#pragma once

#include "Core/Platform.h"
#include <functional>
#include <string>
#include <array>

namespace Kiwi
{

    struct WindowDesc
    {
        const char* Title  = "Kiwi Engine";
        uint32_t    Width  = 1280;
        uint32_t    Height = 720;
        void*       ParentHandle = nullptr;
    };

    // Mouse button state
    struct MouseState
    {
        int32_t X = 0;
        int32_t Y = 0;
        bool    LeftDown = false;
        bool    RightDown = false;
        bool    LeftClicked = false;  // Single frame click
    };

    // Keyboard state (256 virtual key codes)
    class KeyState
    {
    public:
        void SetKeyDown(uint8_t vk) { Keys[vk] = true; }
        void SetKeyUp(uint8_t vk)   { Keys[vk] = false; }
        bool IsKeyDown(uint8_t vk) const { return Keys[vk]; }
    private:
        std::array<bool, 256> Keys = {};
    };

    class Window
    {
    public:
        Window(const WindowDesc& desc);
        ~Window();

        // 禁止拷贝
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void Show();
        void Hide();
        void PumpMessages();
        void SetTitle(const std::string& title);

        bool ShouldClose() const { return bShouldClose; }
        void SetShouldClose(bool close) { bShouldClose = close; }

#if defined(_WIN32)
        HWND GetHWND() const { return Hwnd; }
        void* GetNativeHandle() const { return Hwnd; }
#else
        void* GetHWND() const { return View; }
        void* GetNativeHandle() const { return View; }
#endif
        uint32_t GetWidth() const { return Width; }
        uint32_t GetHeight() const { return Height; }

        void NotifyClose() { bShouldClose = true; }
        void NotifyResize(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return;
            Width = width;
            Height = height;
            if (OnResize)
                OnResize(width, height);
        }
        void NotifyMouseMove(int32_t x, int32_t y)
        {
            Mouse.X = x;
            Mouse.Y = y;
        }
        void NotifyMouseButton(int button, bool down)
        {
            if (button == 0)
            {
                if (down && !Mouse.LeftDown)
                    Mouse.LeftClicked = true;
                Mouse.LeftDown = down;
            }
            else if (button == 1)
            {
                Mouse.RightDown = down;
            }
        }
        void NotifyKey(uint8_t key, bool down)
        {
            if (key == 0)
                return;
            if (down)
                Keys.SetKeyDown(key);
            else
                Keys.SetKeyUp(key);
        }

        // Mouse
        const MouseState& GetMouseState() const { return Mouse; }
        void ResetFrameState() { Mouse.LeftClicked = false; }

        // Keyboard
        const KeyState& GetKeyState() const { return Keys; }
        bool IsKeyDown(uint8_t vk) const { return Keys.IsKeyDown(vk); }

        // Resize callback
        using ResizeCallback = std::function<void(uint32_t, uint32_t)>;
        void SetResizeCallback(ResizeCallback callback) { OnResize = callback; }

    private:
#if defined(_WIN32)
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND         Hwnd = nullptr;
        WNDCLASSW    WndClass = {};
#else
        void* NativeWindow = nullptr;
        void* View = nullptr;
        void* Delegate = nullptr;
#endif
        uint32_t     Width;
        uint32_t     Height;
        bool         bShouldClose = false;
        ResizeCallback OnResize;
        MouseState   Mouse;
        KeyState     Keys;
    };

} // namespace Kiwi
