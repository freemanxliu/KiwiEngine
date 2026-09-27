#include "Core/Platform.h"

#include <iostream>

#if defined(_WIN32)
    #include <shellapi.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <unistd.h>
#else
    #include <unistd.h>
#endif

namespace Kiwi
{

    std::string GetExecutablePath()
    {
#if defined(_WIN32)
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        return exePath;
#elif defined(__APPLE__)
        char exePath[MAX_PATH] = {};
        uint32_t size = MAX_PATH;
        if (_NSGetExecutablePath(exePath, &size) != 0)
            return {};
        return exePath;
#else
        char exePath[MAX_PATH] = {};
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len <= 0)
            return {};
        exePath[len] = '\0';
        return exePath;
#endif
    }

    std::string GetExecutableDirectory()
    {
        std::string path = GetExecutablePath();
        size_t slash = path.find_last_of("\\/");
        if (slash == std::string::npos)
            return {};
        return path.substr(0, slash);
    }

    void ShowErrorDialog(const char* title, const char* message)
    {
        std::cerr << "[" << (title ? title : "Kiwi") << "] " << (message ? message : "") << std::endl;
#if defined(_WIN32)
        MessageBoxA(nullptr, message ? message : "", title ? title : "Kiwi Engine", MB_OK | MB_ICONERROR);
#endif
    }

    void OpenFileWithDefaultApp(const char* path)
    {
        if (!path || !path[0])
            return;
#if defined(_WIN32)
        ShellExecuteA(nullptr, "open", path, nullptr, nullptr, SW_SHOW);
#elif defined(__APPLE__)
        std::string cmd = std::string("open \"") + path + "\"";
        system(cmd.c_str());
#else
        std::string cmd = std::string("xdg-open \"") + path + "\"";
        system(cmd.c_str());
#endif
    }

}
