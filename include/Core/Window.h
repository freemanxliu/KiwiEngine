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
        void SetKeyDown(uint8_t vk) { m_Keys[vk] = true; }
        void SetKeyUp(uint8_t vk)   { m_Keys[vk] = false; }
        bool IsKeyDown(uint8_t vk) const { return m_Keys[vk]; }
    private:
        std::array<bool, 256> m_Keys = {};
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

        bool ShouldClose() const { return m_ShouldClose; }
        void SetShouldClose(bool close) { m_ShouldClose = close; }

#if defined(_WIN32)
        HWND GetHWND() const { return m_Hwnd; }
        void* GetNativeHandle() const { return m_Hwnd; }
#else
        void* GetHWND() const { return m_View; }
        void* GetNativeHandle() const { return m_View; }
#endif
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        void NotifyClose() { m_ShouldClose = true; }
        void NotifyResize(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return;
            m_Width = width;
            m_Height = height;
            if (m_OnResize)
                m_OnResize(width, height);
        }
        void NotifyMouseMove(int32_t x, int32_t y)
        {
            m_Mouse.X = x;
            m_Mouse.Y = y;
        }
        void NotifyMouseButton(int button, bool down)
        {
            if (button == 0)
            {
                if (down && !m_Mouse.LeftDown)
                    m_Mouse.LeftClicked = true;
                m_Mouse.LeftDown = down;
            }
            else if (button == 1)
            {
                m_Mouse.RightDown = down;
            }
        }
        void NotifyKey(uint8_t key, bool down)
        {
            if (key == 0)
                return;
            if (down)
                m_Keys.SetKeyDown(key);
            else
                m_Keys.SetKeyUp(key);
        }

        // Mouse
        const MouseState& GetMouseState() const { return m_Mouse; }
        void ResetFrameState() { m_Mouse.LeftClicked = false; }

        // Keyboard
        const KeyState& GetKeyState() const { return m_Keys; }
        bool IsKeyDown(uint8_t vk) const { return m_Keys.IsKeyDown(vk); }

        // Resize callback
        using ResizeCallback = std::function<void(uint32_t, uint32_t)>;
        void SetResizeCallback(ResizeCallback callback) { m_OnResize = callback; }

    private:
#if defined(_WIN32)
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND         m_Hwnd = nullptr;
        WNDCLASSW    m_WndClass = {};
#else
        void* m_Window = nullptr;
        void* m_View = nullptr;
        void* m_Delegate = nullptr;
#endif
        uint32_t     m_Width;
        uint32_t     m_Height;
        bool         m_ShouldClose = false;
        ResizeCallback m_OnResize;
        MouseState   m_Mouse;
        KeyState     m_Keys;
    };

} // namespace Kiwi
