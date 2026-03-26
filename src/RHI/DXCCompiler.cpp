#include "RHI/DXCCompiler.h"
#include <iostream>
#include <cstring>

namespace Kiwi
{

    DXCCompiler& DXCCompiler::Get()
    {
        static DXCCompiler instance;
        return instance;
    }

    DXCCompiler::DXCCompiler()
    {
        // Try to load dxcompiler.dll at runtime
        m_DxcModule = LoadLibraryW(L"dxcompiler.dll");
        if (!m_DxcModule)
        {
            std::cerr << "[Kiwi DXC] dxcompiler.dll not found, DXC unavailable" << std::endl;
            return;
        }

        // Get DxcCreateInstance function pointer
        typedef HRESULT(WINAPI* DxcCreateInstanceProc)(REFCLSID, REFIID, LPVOID*);
        auto createInstance = (DxcCreateInstanceProc)GetProcAddress(m_DxcModule, "DxcCreateInstance");
        if (!createInstance)
        {
            std::cerr << "[Kiwi DXC] Failed to get DxcCreateInstance" << std::endl;
            FreeLibrary(m_DxcModule);
            m_DxcModule = nullptr;
            return;
        }

        // Create compiler and utils
        HRESULT hr = createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Compiler));
        if (FAILED(hr))
        {
            std::cerr << "[Kiwi DXC] Failed to create IDxcCompiler3" << std::endl;
            FreeLibrary(m_DxcModule);
            m_DxcModule = nullptr;
            return;
        }

        hr = createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Utils));
        if (FAILED(hr))
        {
            std::cerr << "[Kiwi DXC] Failed to create IDxcUtils" << std::endl;
            m_Compiler.Reset();
            FreeLibrary(m_DxcModule);
            m_DxcModule = nullptr;
            return;
        }

        std::cout << "[Kiwi DXC] DXC shader compiler loaded successfully" << std::endl;
    }

    DXCCompiler::~DXCCompiler()
    {
        m_Compiler.Reset();
        m_Utils.Reset();
        if (m_DxcModule)
        {
            FreeLibrary(m_DxcModule);
            m_DxcModule = nullptr;
        }
    }

    DXCCompileResult DXCCompiler::Compile(
        const char* hlslSource,
        const char* entryPoint,
        const char* shaderModel,
        const ShaderMacro* macros,
        uint32_t macroCount,
        bool debug)
    {
        DXCCompileResult result;

        if (!m_Compiler || !m_Utils)
        {
            result.ErrorMsg = "DXC compiler not available";
            return result;
        }

        // Convert entry point and shader model to wide strings
        std::wstring wEntryPoint(entryPoint, entryPoint + strlen(entryPoint));
        std::wstring wShaderModel(shaderModel, shaderModel + strlen(shaderModel));

        // Build argument list
        std::vector<LPCWSTR> args;
        args.push_back(L"-E");
        args.push_back(wEntryPoint.c_str());
        args.push_back(L"-T");
        args.push_back(wShaderModel.c_str());

        // Row-major matrices (match HLSL default in our engine)
        args.push_back(L"-Zpr");

        // Enable strictness
        args.push_back(L"-HV");
        args.push_back(L"2021");

        if (debug)
        {
            args.push_back(L"-Zi");   // Debug info
            args.push_back(L"-Od");   // Disable optimization
        }
        else
        {
            args.push_back(L"-O3");   // Full optimization
        }

        // Build macro definitions
        std::vector<std::wstring> macroStrings; // Keep alive
        for (uint32_t i = 0; i < macroCount; i++)
        {
            std::wstring def = L"-D";
            std::string name(macros[i].Name);
            def += std::wstring(name.begin(), name.end());
            if (macros[i].Definition && macros[i].Definition[0] != '\0')
            {
                def += L"=";
                std::string val(macros[i].Definition);
                def += std::wstring(val.begin(), val.end());
            }
            macroStrings.push_back(std::move(def));
        }
        for (auto& m : macroStrings)
            args.push_back(m.c_str());

        // Create source blob
        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = hlslSource;
        sourceBuffer.Size = strlen(hlslSource);
        sourceBuffer.Encoding = DXC_CP_UTF8;

        // Compile
        ComPtr<IDxcResult> dxcResult;
        HRESULT hr = m_Compiler->Compile(
            &sourceBuffer,
            args.data(),
            (UINT32)args.size(),
            nullptr,  // No include handler
            IID_PPV_ARGS(&dxcResult));

        if (FAILED(hr))
        {
            result.ErrorMsg = "DXC Compile call failed";
            return result;
        }

        // Check for errors
        ComPtr<IDxcBlobUtf8> errors;
        dxcResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0)
        {
            result.ErrorMsg = std::string(errors->GetStringPointer(), errors->GetStringLength());
        }

        // Check compilation status
        HRESULT status;
        dxcResult->GetStatus(&status);
        if (FAILED(status))
        {
            return result; // ErrorMsg already set
        }

        // Get compiled bytecode
        ComPtr<IDxcBlob> bytecode;
        dxcResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&bytecode), nullptr);
        if (bytecode && bytecode->GetBufferSize() > 0)
        {
            result.Bytecode = bytecode;
            result.Success = true;
        }
        else
        {
            result.ErrorMsg = "DXC produced empty bytecode";
        }

        return result;
    }

} // namespace Kiwi
