#include "RHI/DXCCompiler.h"
#include <cstring>
#include <iostream>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace Kiwi
{

    namespace
    {
        using DxcCreateInstanceFn = HRESULT(__stdcall*)(REFCLSID, REFIID, LPVOID*);

#if defined(_WIN32)
        void* LoadDxcLibrary()
        {
            return LoadLibraryW(L"dxcompiler.dll");
        }

        DxcCreateInstanceFn FindCreateInstance(void* Module)
        {
            return reinterpret_cast<DxcCreateInstanceFn>(GetProcAddress(static_cast<HMODULE>(Module), "DxcCreateInstance"));
        }

        void FreeDxcLibrary(void* Module)
        {
            FreeLibrary(static_cast<HMODULE>(Module));
        }
#else
#if defined(__APPLE__)
        constexpr const char* DxcLibraryName = "libdxcompiler.dylib";
#else
        constexpr const char* DxcLibraryName = "libdxcompiler.so";
#endif

        // Only the copy shipped next to the executable is accepted, so a system-wide DXC of another version is never used.
        void* LoadDxcLibrary()
        {
#if defined(__APPLE__)
            std::string Path = std::string("@executable_path/../Frameworks/") + DxcLibraryName;
#else
            std::string Path = std::string("$ORIGIN/") + DxcLibraryName;
#endif
            void* Module = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!Module)
                std::cerr << "[Kiwi DXC] dlopen(" << Path << ") failed: " << dlerror() << std::endl;
            return Module;
        }

        DxcCreateInstanceFn FindCreateInstance(void* Module)
        {
            return reinterpret_cast<DxcCreateInstanceFn>(dlsym(Module, "DxcCreateInstance"));
        }

        void FreeDxcLibrary(void* Module)
        {
            dlclose(Module);
        }
#endif
    }

    DXCCompiler& DXCCompiler::Get()
    {
        static DXCCompiler instance;
        return instance;
    }

    DXCCompiler::DXCCompiler()
    {
        DxcModule = LoadDxcLibrary();
        if (!DxcModule)
        {
            std::cerr << "[Kiwi DXC] DXC runtime not found next to the executable (see third_party/dxc/VERSION.txt), DXC unavailable" << std::endl;
            return;
        }

        DxcCreateInstanceFn createInstance = FindCreateInstance(DxcModule);
        if (!createInstance)
        {
            std::cerr << "[Kiwi DXC] Failed to get DxcCreateInstance" << std::endl;
            UnloadLibrary();
            return;
        }

        // Create compiler and utils
        HRESULT hr = createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler));
        if (FAILED(hr))
        {
            std::cerr << "[Kiwi DXC] Failed to create IDxcCompiler3" << std::endl;
            UnloadLibrary();
            return;
        }

        hr = createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&Utils));
        if (FAILED(hr))
        {
            std::cerr << "[Kiwi DXC] Failed to create IDxcUtils" << std::endl;
            UnloadLibrary();
            return;
        }

        std::cout << "[Kiwi DXC] DXC shader compiler loaded successfully" << std::endl;
    }

    DXCCompiler::~DXCCompiler()
    {
        UnloadLibrary();
    }

    void DXCCompiler::UnloadLibrary()
    {
        Compiler.Reset();
        Utils.Reset();
        if (DxcModule)
        {
            FreeDxcLibrary(DxcModule);
            DxcModule = nullptr;
        }
    }

    DXCCompileResult DXCCompiler::Compile(const char* hlslSource, const char* entryPoint, const char* shaderModel, const ShaderMacro* macros, uint32_t macroCount, bool debug, EDXCTarget Target)
    {
        DXCCompileResult result;

        if (!Compiler || !Utils)
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

        if (Target == EDXCTarget::SPIRV)
        {
            args.push_back(L"-spirv");
            args.push_back(L"-fspv-target-env=vulkan1.1");
            // Keep HLSL cbuffer / StructuredBuffer packing so the C++ structs stay identical across backends.
            args.push_back(L"-fvk-use-dx-layout");
        }

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
        DxcRef<IDxcResult> dxcResult;
        HRESULT hr = Compiler->Compile(&sourceBuffer, args.data(), (UINT32)args.size(), nullptr, IID_PPV_ARGS(&dxcResult));

        if (FAILED(hr))
        {
            result.ErrorMsg = "DXC Compile call failed";
            return result;
        }

        // Check for errors
        DxcRef<IDxcBlobUtf8> errors;
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
        DxcRef<IDxcBlob> bytecode;
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
