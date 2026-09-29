#include "Core/Window.h"
#include <imgui.h>
#include <stdexcept>

// Forward declare ImGui Win32 WndProc handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Kiwi
{

    Window::Window(const WindowDesc& desc)
        : Width(desc.Width)
        , Height(desc.Height)
    {
        // 注册窗口类
        std::wstring className = L"KiwiEngineWindow";
        WndClass.lpfnWndProc = WindowProc;
        WndClass.hInstance = GetModuleHandleW(nullptr);
        WndClass.lpszClassName = className.c_str();
        WndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        WndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        WndClass.style = CS_HREDRAW | CS_VREDRAW;

        RegisterClassW(&WndClass);

        // 计算窗口大小（包含边框）
        RECT rect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;

        // 创建窗口
        std::wstring title(desc.Title, desc.Title + strlen(desc.Title));
        Hwnd = CreateWindowExW(
            0,
            className.c_str(),
            title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowWidth, windowHeight,
            nullptr,
            nullptr,
            WndClass.hInstance,
            this); // 传入 this 作为用户数据

        if (!Hwnd)
        {
            throw std::runtime_error("Failed to create window");
        }
    }

    Window::~Window()
    {
        if (Hwnd)
        {
            DestroyWindow(Hwnd);
        }
        UnregisterClassW(WndClass.lpszClassName, WndClass.hInstance);
    }

    void Window::Show()
    {
        ShowWindow(Hwnd, SW_SHOW);
        UpdateWindow(Hwnd);
    }

    void Window::Hide()
    {
        ShowWindow(Hwnd, SW_HIDE);
    }

    void Window::PumpMessages()
    {
        // Reset per-frame state
        Mouse.LeftClicked = false;

        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                bShouldClose = true;
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
                window->Width = width;
                window->Height = height;
                if (window->OnResize)
                {
                    window->OnResize(width, height);
                }
            }
            break;
        }

        case WM_MOUSEMOVE:
            window->Mouse.X = (int32_t)(short)LOWORD(lParam);
            window->Mouse.Y = (int32_t)(short)HIWORD(lParam);
            break;

        case WM_LBUTTONDOWN:
            window->Mouse.X = (int32_t)(short)LOWORD(lParam);
            window->Mouse.Y = (int32_t)(short)HIWORD(lParam);
            window->Mouse.LeftDown = true;
            window->Mouse.LeftClicked = true;
            break;

        case WM_LBUTTONUP:
            window->Mouse.LeftDown = false;
            break;

        case WM_RBUTTONDOWN:
            window->Mouse.RightDown = true;
            break;

        case WM_RBUTTONUP:
            window->Mouse.RightDown = false;
            break;

        case WM_KEYDOWN:
            window->Keys.SetKeyDown((uint8_t)wParam);
            if (wParam == VK_ESCAPE)
            {
                window->SetShouldClose(true);
                return 0;
            }
            break;

        case WM_KEYUP:
            window->Keys.SetKeyUp((uint8_t)wParam);
            break;

        default:
            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void Window::SetTitle(const std::string& title)
    {
        std::wstring wide(title.begin(), title.end());
        SetWindowTextW(Hwnd, wide.c_str());
    }

} // namespace Kiwi
