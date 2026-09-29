#pragma once

#include "RHI/RHI.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Kiwi
{
    // State a recorded command sees when the RHI thread replays it.
    struct RHIExecuteContext
    {
        RHICommandContext* Ctx = nullptr;
        RHIDevice* Device = nullptr;
        RHISwapChain* SwapChain = nullptr; // set by the recorded BeginFrame

        // Swaps the back buffer placeholder for the swap chain's current back buffer view.
        RHITextureView* Resolve(RHITextureView* View) const;
    };

    // One frame of recorded commands plus the resources they must keep alive until replayed.
    struct RHICommandBuffer
    {
        std::vector<std::function<void(RHIExecuteContext&)>> Commands;
        std::vector<std::shared_ptr<void>> DeferredReleases;

        bool IsEmpty() const { return Commands.empty() && DeferredReleases.empty(); }
        void Execute(RHIExecuteContext& Context);
    };

    // The render thread's command context (UE5 FRHICommandList).
    // Immediate: every call goes straight to the real context. Used without an RHI thread.
    // Recording: calls are stored with copies of their arguments and replayed on the RHI thread.
    class RHICommandList final : public RHICommandContext
    {
    public:
        void SetImmediate(RHICommandContext* InTarget, RHIDevice* InDevice);
        void SetRecording(RHIDevice* InDevice);
        bool IsImmediate() const { return Target != nullptr; }

        // Hands the recorded frame to the caller and starts a new one.
        RHICommandBuffer TakeCommands();

        // The command list that RHIUpdateBuffer and uniform buffer updates on this thread record into.
        static RHICommandList* GetCurrent();
        static void SetCurrent(RHICommandList* List);

        // The swap chain's current back buffer. While recording this is a placeholder resolved at replay,
        // because the back buffer index only advances when the RHI thread presents.
        RHITextureView* GetBackBufferRTV(RHISwapChain* SwapChain) const;

        void UpdateBuffer(RHIBuffer* Buffer, const void* Data, uint32_t Size, uint32_t Offset);
        void Present(RHISwapChain* SwapChain, uint32_t SyncInterval);
        void RenderImGui(std::shared_ptr<ImDrawData> DrawData);
        void EnqueueLambda(std::function<void(RHIExecuteContext&)> Lambda);

        // Destroys Resource once the commands recorded so far have run, since they may still reference it.
        template <typename T>
        void DeferredRelease(std::unique_ptr<T>& Resource)
        {
            if (!Resource)
                return;
            if (IsImmediate())
                Resource.reset();
            else
                Recorded.DeferredReleases.push_back(std::shared_ptr<T>(Resource.release()));
        }

        // ---- RHICommandContext ----
        void* GetNativeHandle() const override;
        void BeginFrame(RHISwapChain* SwapChain) override;
        void EndFrame(RHISwapChain* SwapChain) override;
        void BeginEvent(const char* Name) override;
        void EndEvent() override;
        void SetMarker(const char* Name) override;
        void ResourceBarrier(RHITexture* Texture, int StateBefore, int StateAfter) override;
        void SetRenderTargets(RHITextureView** Rtvs, uint32_t RtvCount, RHITextureView* Dsv = nullptr) override;
        void ClearRenderTargetView(RHITextureView* Rtv, const ClearColorValue& Color) override;
        void ClearDepthStencilView(RHITextureView* Dsv, const ClearDepthStencilValue& Value, uint8_t ClearFlags) override;
        void SetPipelineState(RHIPipelineState* Pso) override;
        void SetCullMode(ECullMode Mode) override;
        void ClearCullModeOverride() override;
        void SetPrimitiveTopology(EPrimitiveTopology Topology) override;
        void SetVertexBuffers(uint32_t StartSlot, RHIBuffer* const* Buffers, const VertexBufferView* Views, uint32_t Count) override;
        void SetIndexBuffer(RHIBuffer* Buffer, const IndexBufferView* View) override;
        void SetVertexShader(RHIShader* Shader) override;
        void SetPixelShader(RHIShader* Shader) override;
        void SetGeometryShader(RHIShader* Shader) override;
        void SetInputLayout(RHIInputLayout* Layout) override;
        void SetConstantBuffer(uint32_t Slot, RHIBuffer* Buffer) override;
        void SetConstantBufferOffset(uint32_t Slot, RHIBuffer* Buffer, uint32_t OffsetIn16Constants, uint32_t SizeIn16Constants) override;
        void SetShaderResourceView(uint32_t Slot, RHITextureView* Srv) override;
        void SetSampler(uint32_t Slot, RHISampler* Sampler) override;
        void SetViewports(const Viewport* Viewports, uint32_t Count) override;
        void SetScissorRects(const ScissorRect* Rects, uint32_t Count) override;
        void Draw(uint32_t VertexCount, uint32_t VertexStart = 0) override;
        void DrawIndexed(uint32_t IndexCount, uint32_t IndexStart = 0, int32_t VertexOffset = 0) override;
        void DrawIndexedInstanced(uint32_t IndexCountPerInstance, uint32_t InstanceCount, uint32_t StartIndex = 0, int32_t BaseVertex = 0, uint32_t StartInstance = 0) override;
        void Flush() override;

    private:
        void Record(std::function<void(RHIExecuteContext&)> Command);

        RHICommandContext* Target = nullptr;
        RHIDevice* Device = nullptr;
        RHICommandBuffer Recorded;
    };

    // Buffer update that follows the calling thread's command list, so it lands between the right draws.
    // Threads without a command list (loading, the game thread while the renderer is idle) write directly.
    void RHIUpdateBuffer(RHIBuffer* Buffer, const void* Data, uint32_t Size, uint32_t Offset = 0);
}
