#pragma once

// DX12 相关 Windows 头文件集中管理
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <d3d12.h>
    #include <d3d12sdklayers.h>
    #include <d3dcompiler.h>
    #include <dxgi.h>
    #include <dxgi1_4.h>
    #include <dxgi1_6.h>
    #include <dxgidebug.h>

    // 链接库
    #pragma comment(lib, "d3d12.lib")
    #pragma comment(lib, "d3dcompiler.lib")
    #pragma comment(lib, "dxgi.lib")
    #pragma comment(lib, "dxguid.lib")
#endif

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
