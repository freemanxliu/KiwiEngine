#pragma once

#include "RHI/RHI.h"

#include <mutex>

namespace Kiwi
{
    // Held by the RHI thread while it replays a frame, and by any other thread that touches the device.
    // Backends keep shared state (the Metal encoder, the DX11 immediate context, constant buffer allocators),
    // so resource creation must not interleave with command replay.
    inline std::recursive_mutex& GetRHILock()
    {
        static std::recursive_mutex Lock;
        return Lock;
    }

    // The device the game and render threads see: every call takes the RHI lock and forwards.
    // The RHI thread uses the backend device directly while it already holds the lock.
    class LockedRHIDevice final : public RHIDevice
    {
    public:
        explicit LockedRHIDevice(RHIDevice* InInner) : Inner(InInner) {}

        RHIDevice* GetInner() const { return Inner; }

        RHI_API_TYPE GetApiType() const override { return Inner->GetApiType(); }
        void* GetNativeDevice() const override { return Inner->GetNativeDevice(); }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& Desc) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateSwapChain(Desc); }
        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& Desc, const void* InitialData = nullptr) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateBuffer(Desc, InitialData); }
        std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& Desc, const void* InitialData = nullptr) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateTexture(Desc, InitialData); }
        std::unique_ptr<RHITextureView> CreateTextureView(RHITexture* Texture, EDescriptorHeapType HeapType, EFormat Format = EFormat::Unknown, int MipSlice = -1, int ArraySlice = -1) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateTextureView(Texture, HeapType, Format, MipSlice, ArraySlice); }
        std::unique_ptr<RHIShader> CreateShader(EShaderType Type, const void* ByteCode, size_t ByteCodeSize) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateShader(Type, ByteCode, ByteCodeSize); }
        std::unique_ptr<RHIShader> CompileShader(EShaderType Type, const char* HlslSource, const char* EntryPoint, const char* ShaderModel, const ShaderMacro* Macros = nullptr, uint32_t MacroCount = 0) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CompileShader(Type, HlslSource, EntryPoint, ShaderModel, Macros, MacroCount); }
        std::unique_ptr<RHIInputLayout> CreateInputLayout(const InputElementDesc* Elements, uint32_t ElementCount, RHIShader* VertexShader) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateInputLayout(Elements, ElementCount, VertexShader); }
        std::unique_ptr<RHIPipelineState> CreatePipelineState() override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreatePipelineState(); }
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(const GraphicsPipelineStateInitializer& Initializer) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateGraphicsPipelineState(Initializer); }
        std::unique_ptr<RHISampler> CreateSampler() override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateSampler(); }
        std::unique_ptr<RHISampler> CreateComparisonSampler() override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateComparisonSampler(); }
        std::unique_ptr<RHITextureView> CreateBufferSRV(RHIBuffer* Buffer, uint32_t NumElements, uint32_t StructByteStride) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); return Inner->CreateBufferSRV(Buffer, NumElements, StructByteStride); }

        bool IsFeatureSupported(const char* Feature) const override { return Inner->IsFeatureSupported(Feature); }
        void* GetImmediateContext() const override { return Inner->GetImmediateContext(); }

        void InitImGui(void* WindowHandle) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); Inner->InitImGui(WindowHandle); }
        void ShutdownImGui() override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); Inner->ShutdownImGui(); }
        // Platform input only; taking the lock here would stall the game thread behind a whole RHI frame.
        void ImGuiNewFrame() override { Inner->ImGuiNewFrame(); }
        void ImGuiUpdateTextures(ImDrawData* DrawData) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); Inner->ImGuiUpdateTextures(DrawData); }
        void ImGuiRenderDrawData(RHICommandContext* Ctx, ImDrawData* DrawData) override { std::lock_guard<std::recursive_mutex> Guard(GetRHILock()); Inner->ImGuiRenderDrawData(Ctx, DrawData); }

    private:
        RHIDevice* Inner;
    };
}
