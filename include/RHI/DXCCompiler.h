#pragma once

#include "RHI/RHITypes.h"
#include <wrl/client.h>
#include <dxcapi.h>
#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace Kiwi
{

    // ============================================================
    // DXCCompiler — Shared DXC (DirectX Shader Compiler) wrapper
    //
    // Loads dxcompiler.dll at runtime via LoadLibrary.
    // Used by both DX11 and DX12 backends:
    //   DX11: compiles to SM 5.0 → DXBC output
    //   DX12: compiles to SM 6.0 → DXIL output
    // ============================================================

    struct DXCCompileResult
    {
        ComPtr<IDxcBlob> Bytecode;   // Compiled shader bytecode
        std::string      ErrorMsg;   // Error message if compilation failed
        bool             Success = false;
    };

    class DXCCompiler
    {
    public:
        // Get singleton instance (lazy init)
        static DXCCompiler& Get();

        // Check if DXC is available (dxcompiler.dll loaded successfully)
        bool IsAvailable() const { return m_Compiler != nullptr; }

        // Compile HLSL source to shader bytecode
        // shaderModel: e.g. "vs_5_0", "ps_6_0"
        DXCCompileResult Compile(
            const char* hlslSource,
            const char* entryPoint,
            const char* shaderModel,
            const ShaderMacro* macros = nullptr,
            uint32_t macroCount = 0,
            bool debug = false);

    private:
        DXCCompiler();
        ~DXCCompiler();

        DXCCompiler(const DXCCompiler&) = delete;
        DXCCompiler& operator=(const DXCCompiler&) = delete;

        HMODULE m_DxcModule = nullptr;
        ComPtr<IDxcCompiler3> m_Compiler;
        ComPtr<IDxcUtils>     m_Utils;
    };

} // namespace Kiwi
