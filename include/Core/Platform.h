#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #ifndef MAX_PATH
        #define MAX_PATH 1024
    #endif
    // Win32 virtual-key values used by editor input.
    constexpr int VK_ESCAPE = 0x1B;
    constexpr int VK_LEFT   = 0x25;
    constexpr int VK_UP     = 0x26;
    constexpr int VK_RIGHT  = 0x27;
    constexpr int VK_DOWN   = 0x28;
#endif

namespace Kiwi
{

    std::string GetExecutablePath();
    std::string GetExecutableDirectory();
    void ShowErrorDialog(const char* title, const char* message);
    void OpenFileWithDefaultApp(const char* path);

}
