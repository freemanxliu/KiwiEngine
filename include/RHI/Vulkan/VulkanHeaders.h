#pragma once

// Vulkan headers
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

// HLSL compilation (for SPIR-V we use d3dcompiler to compile HLSL, then dxc or runtime compilation)
#include <d3dcompiler.h>

#include <vector>
#include <array>
#include <string>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <functional>
