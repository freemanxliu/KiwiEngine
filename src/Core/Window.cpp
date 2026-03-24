#include "Core/Window.h"
#include <imgui.h>
#include <stdexcept>

// Forward declare ImGui Win32 WndProc handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Kiwi
{

    Window::Window(const WindowDesc& desc)
        : m_Width(desc.Width)
        , m_Height(desc.Height)
    {
        // 注册窗口类
        std::wstring className = L"KiwiEngineWindow";
        m_WndClass.lpfnWndProc = WindowProc;
        m_WndClass.hInstance = GetModuleHandleW(nullptr);
        m_WndClass.lpszClassName = className.c_str();
        m_WndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        m_WndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        m_WndClass.style = CS_HREDRAW | CS_VREDRAW;

        RegisterClassW(&m_WndClass);

        // 计算窗口大小（包含边框）
        RECT rect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;

        // 创建窗口
        std::wstring title(desc.Title, desc.Title + strlen(desc.Title));
        m_Hwnd = CreateWindowExW(
            0,
            className.c_str(),
            title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowWidth, windowHeight,
            nullptr,
            nullptr,
            m_WndClass.hInstance,
            this); // 传入 this 作为用户数据

        if (!m_Hwnd)
        {
            throw std::runtime_error("Failed to create window");
        }
    }

    Window::~Window()
    {
        if (m_Hwnd)
        {
            DestroyWindow(m_Hwnd);
        }
        UnregisterClassW(m_WndClass.lpszClassName, m_WndClass.hInstance);
    }

    void Window::Show()
    {
        ShowWindow(m_Hwnd, SW_SHOW);
        UpdateWindow(m_Hwnd);
    }

    void Window::Hide()
    {
        ShowWindow(m_Hwnd, SW_HIDE);
    }

    void Window::PumpMessages()
    {
        // Reset per-frame state
        m_Mouse.LeftClicked = false;

        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_ShouldClose = true;
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Let ImGui process first
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return true;

        // 获取 Window 实例指针
        Window* window = nullptr;
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            window = (Window*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        }
        else
        {
            window = (Window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        }

        if (!window)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        switch (msg)
        {
        case WM_CLOSE:
            window->SetShouldClose(true);
            return 0;

        case WM_SIZE:
        {
            uint32_t width = (uint32_t)(LOWORD(lParam));
            uint32_t height = (uint32_t)(HIWORD(lParam));
            if (width > 0 && height > 0)
            {
                window->m_Width = width;
                window->m_Height = height;
                if (window->m_OnResize)
                {
                    window->m_OnResize(width, height);
                }
            }
            break;
        }

        case WM_MOUSEMOVE:
            window->m_Mouse.X = (int32_t)LOWORD(lParam);
            window->m_Mouse.Y = (int32_t)HIWORD(lParam);
            break;

        case WM_LBUTTONDOWN:
            window->m_Mouse.LeftDown = true;
            window->m_Mouse.LeftClicked = true;
            break;

        case WM_LBUTTONUP:
            window->m_Mouse.LeftDown = false;
            break;

        case WM_RBUTTONDOWN:
            window->m_Mouse.RightDown = true;
            break;

        case WM_RBUTTONUP:
            window->m_Mouse.RightDown = false;
            break;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                window->SetShouldClose(true);
                return 0;
            }
            break;

        default:
            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

} // namespace Kiwi
