#pragma once

#include "RHI/RHITypes.h"
#if defined(_WIN32)
#include <wrl/client.h>
#endif
#include <dxc/dxcapi.h>   // third_party/dxc, same version as the runtime library
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
#endif

namespace Kiwi
{

    // ============================================================
    // DXCCompiler — Shared DXC (DirectX Shader Compiler) wrapper
    //
    // Loads the vendored DXC runtime (third_party/dxc) at runtime:
    //   Windows: dxcompiler.dll (+ dxil.dll for DXIL signing) next to the executable
    //   macOS:   libdxcompiler.dylib in the app bundle's Contents/Frameworks
    // Targets:
    //   DXIL  — DX12 (SM 6.x)
    //   SPIRV — Vulkan, and the input for SPIRV-Cross (Metal / GL)
    // ============================================================

    // Minimal COM reference holder; WRL's ComPtr does not exist outside Windows.
    template <typename T>
    class DxcRef
    {
    public:
        DxcRef() = default;
        DxcRef(const DxcRef& Other) : Ptr(Other.Ptr) { if (Ptr) Ptr->AddRef(); }
        DxcRef(DxcRef&& Other) noexcept : Ptr(std::exchange(Other.Ptr, nullptr)) {}
        ~DxcRef() { Reset(); }

        DxcRef& operator=(DxcRef Other) noexcept { std::swap(Ptr, Other.Ptr); return *this; }

        T* Get() const { return Ptr; }
        T* operator->() const { return Ptr; }
        explicit operator bool() const { return Ptr != nullptr; }
        // Releases the current reference, so it can be passed straight to IID_PPV_ARGS.
        T** operator&() { Reset(); return &Ptr; }

        void Reset()
        {
            if (Ptr)
                Ptr->Release();
            Ptr = nullptr;
        }

    private:
        T* Ptr = nullptr;
    };

    enum class EDXCTarget
    {
        DXIL,
        SPIRV,
    };

    struct DXCCompileResult
    {
        DxcRef<IDxcBlob> Bytecode;   // Compiled shader bytecode
        std::string      ErrorMsg;   // Error message if compilation failed
        bool             Success = false;
    };

    class DXCCompiler
    {
    public:
        // Get singleton instance (lazy init)
        static DXCCompiler& Get();

        // Check if DXC is available (runtime library loaded successfully)
        bool IsAvailable() const { return Compiler.Get() != nullptr; }

        // Compile HLSL source to shader bytecode
        // shaderModel: e.g. "vs_6_0", "ps_6_0"
        DXCCompileResult Compile(const char* hlslSource, const char* entryPoint, const char* shaderModel, const ShaderMacro* macros = nullptr, uint32_t macroCount = 0, bool debug = false, EDXCTarget Target = EDXCTarget::DXIL);

    private:
        DXCCompiler();
        ~DXCCompiler();

        DXCCompiler(const DXCCompiler&) = delete;
        DXCCompiler& operator=(const DXCCompiler&) = delete;

        void UnloadLibrary();

        void* DxcModule = nullptr;
        DxcRef<IDxcCompiler3> Compiler;
        DxcRef<IDxcUtils>     Utils;
    };

} // namespace Kiwi
