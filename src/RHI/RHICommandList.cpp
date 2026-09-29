#include "RHI/RHICommandList.h"

#include <array>
#include <string>
#include <utility>

namespace Kiwi
{

namespace
{
    class BackBufferPlaceholderView final : public RHITextureView
    {
    public:
        void* GetNativeHandle() const override { return nullptr; }
    };

    BackBufferPlaceholderView GBackBufferPlaceholder;
    thread_local RHICommandList* GCurrentCommandList = nullptr;
}

RHITextureView* RHIExecuteContext::Resolve(RHITextureView* View) const
{
    if (View != &GBackBufferPlaceholder)
        return View;
    return SwapChain ? SwapChain->GetBackBufferRTV(SwapChain->GetCurrentBackBufferIndex()) : nullptr;
}

void RHICommandBuffer::Execute(RHIExecuteContext& Context)
{
    for (auto& Command : Commands)
        Command(Context);
    Commands.clear();
    DeferredReleases.clear();
}

void RHIUpdateBuffer(RHIBuffer* Buffer, const void* Data, uint32_t Size, uint32_t Offset)
{
    if (!Buffer || !Data || Size == 0)
        return;
    if (RHICommandList* List = RHICommandList::GetCurrent())
        List->UpdateBuffer(Buffer, Data, Size, Offset);
    else
        Buffer->UpdateData(Data, Size, Offset);
}

// ---- Mode ----

void RHICommandList::SetImmediate(RHICommandContext* InTarget, RHIDevice* InDevice)
{
    Target = InTarget;
    Device = InDevice;
    Recorded = {};
}

void RHICommandList::SetRecording(RHIDevice* InDevice)
{
    Target = nullptr;
    Device = InDevice;
    Recorded = {};
}

RHICommandBuffer RHICommandList::TakeCommands()
{
    RHICommandBuffer Out = std::move(Recorded);
    Recorded = {};
    return Out;
}

RHICommandList* RHICommandList::GetCurrent()
{
    return GCurrentCommandList;
}

void RHICommandList::SetCurrent(RHICommandList* List)
{
    GCurrentCommandList = List;
}

RHITextureView* RHICommandList::GetBackBufferRTV(RHISwapChain* SwapChain) const
{
    if (IsImmediate())
        return SwapChain->GetBackBufferRTV(SwapChain->GetCurrentBackBufferIndex());
    return &GBackBufferPlaceholder;
}

void RHICommandList::Record(std::function<void(RHIExecuteContext&)> Command)
{
    Recorded.Commands.push_back(std::move(Command));
}

// ---- Commands that only exist on the command list ----

void RHICommandList::UpdateBuffer(RHIBuffer* Buffer, const void* Data, uint32_t Size, uint32_t Offset)
{
    if (IsImmediate())
    {
        Buffer->UpdateData(Data, Size, Offset);
        return;
    }
    const uint8_t* Bytes = static_cast<const uint8_t*>(Data);
    Record([Buffer, Copy = std::vector<uint8_t>(Bytes, Bytes + Size), Offset](RHIExecuteContext&) { Buffer->UpdateData(Copy.data(), (uint32_t)Copy.size(), Offset); });
}

void RHICommandList::Present(RHISwapChain* SwapChain, uint32_t SyncInterval)
{
    if (IsImmediate())
    {
        SwapChain->Present(SyncInterval);
        return;
    }
    Record([SwapChain, SyncInterval](RHIExecuteContext&) { SwapChain->Present(SyncInterval); });
}

void RHICommandList::RenderImGui(std::shared_ptr<ImDrawData> DrawData)
{
    if (!DrawData)
        return;
    if (IsImmediate())
    {
        Device->ImGuiRenderDrawData(Target, DrawData.get());
        return;
    }
    Record([DrawData = std::move(DrawData)](RHIExecuteContext& Exec) { Exec.Device->ImGuiRenderDrawData(Exec.Ctx, DrawData.get()); });
}

void RHICommandList::EnqueueLambda(std::function<void(RHIExecuteContext&)> Lambda)
{
    if (IsImmediate())
    {
        RHIExecuteContext Exec;
        Exec.Ctx = Target;
        Exec.Device = Device;
        Lambda(Exec);
        return;
    }
    Record(std::move(Lambda));
}

// ---- RHICommandContext ----

void* RHICommandList::GetNativeHandle() const
{
    return Target ? Target->GetNativeHandle() : nullptr;
}

void RHICommandList::BeginFrame(RHISwapChain* SwapChain)
{
    if (IsImmediate())
        return Target->BeginFrame(SwapChain);
    Record([SwapChain](RHIExecuteContext& Exec) {
        Exec.SwapChain = SwapChain;
        Exec.Ctx->BeginFrame(SwapChain);
    });
}

void RHICommandList::EndFrame(RHISwapChain* SwapChain)
{
    if (IsImmediate())
        return Target->EndFrame(SwapChain);
    Record([SwapChain](RHIExecuteContext& Exec) { Exec.Ctx->EndFrame(SwapChain); });
}

void RHICommandList::BeginEvent(const char* Name)
{
    if (IsImmediate())
        return Target->BeginEvent(Name);
    Record([Label = std::string(Name ? Name : "")](RHIExecuteContext& Exec) { Exec.Ctx->BeginEvent(Label.c_str()); });
}

void RHICommandList::EndEvent()
{
    if (IsImmediate())
        return Target->EndEvent();
    Record([](RHIExecuteContext& Exec) { Exec.Ctx->EndEvent(); });
}

void RHICommandList::SetMarker(const char* Name)
{
    if (IsImmediate())
        return Target->SetMarker(Name);
    Record([Label = std::string(Name ? Name : "")](RHIExecuteContext& Exec) { Exec.Ctx->SetMarker(Label.c_str()); });
}

void RHICommandList::ResourceBarrier(RHITexture* Texture, int StateBefore, int StateAfter)
{
    if (IsImmediate())
        return Target->ResourceBarrier(Texture, StateBefore, StateAfter);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->ResourceBarrier(Texture, StateBefore, StateAfter); });
}

void RHICommandList::SetRenderTargets(RHITextureView** Rtvs, uint32_t RtvCount, RHITextureView* Dsv)
{
    if (IsImmediate())
        return Target->SetRenderTargets(Rtvs, RtvCount, Dsv);
    std::vector<RHITextureView*> Views(Rtvs, Rtvs + RtvCount);
    Record([Views = std::move(Views), Dsv](RHIExecuteContext& Exec) mutable {
        for (RHITextureView*& View : Views)
            View = Exec.Resolve(View);
        Exec.Ctx->SetRenderTargets(Views.data(), (uint32_t)Views.size(), Dsv);
    });
}

void RHICommandList::ClearRenderTargetView(RHITextureView* Rtv, const ClearColorValue& Color)
{
    if (IsImmediate())
        return Target->ClearRenderTargetView(Rtv, Color);
    Record([Rtv, Color](RHIExecuteContext& Exec) { Exec.Ctx->ClearRenderTargetView(Exec.Resolve(Rtv), Color); });
}

void RHICommandList::ClearDepthStencilView(RHITextureView* Dsv, const ClearDepthStencilValue& Value, uint8_t ClearFlags)
{
    if (IsImmediate())
        return Target->ClearDepthStencilView(Dsv, Value, ClearFlags);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->ClearDepthStencilView(Dsv, Value, ClearFlags); });
}

void RHICommandList::SetPipelineState(RHIPipelineState* Pso)
{
    if (IsImmediate())
        return Target->SetPipelineState(Pso);
    Record([Pso](RHIExecuteContext& Exec) { Exec.Ctx->SetPipelineState(Pso); });
}

void RHICommandList::SetCullMode(ECullMode Mode)
{
    if (IsImmediate())
        return Target->SetCullMode(Mode);
    Record([Mode](RHIExecuteContext& Exec) { Exec.Ctx->SetCullMode(Mode); });
}

void RHICommandList::ClearCullModeOverride()
{
    if (IsImmediate())
        return Target->ClearCullModeOverride();
    Record([](RHIExecuteContext& Exec) { Exec.Ctx->ClearCullModeOverride(); });
}

void RHICommandList::SetPrimitiveTopology(EPrimitiveTopology Topology)
{
    if (IsImmediate())
        return Target->SetPrimitiveTopology(Topology);
    Record([Topology](RHIExecuteContext& Exec) { Exec.Ctx->SetPrimitiveTopology(Topology); });
}

void RHICommandList::SetVertexBuffers(uint32_t StartSlot, RHIBuffer* const* Buffers, const VertexBufferView* Views, uint32_t Count)
{
    if (IsImmediate())
        return Target->SetVertexBuffers(StartSlot, Buffers, Views, Count);
    std::vector<RHIBuffer*> BufferCopy(Buffers, Buffers + Count);
    std::vector<VertexBufferView> ViewCopy(Views, Views + Count);
    Record([StartSlot, BufferCopy = std::move(BufferCopy), ViewCopy = std::move(ViewCopy)](RHIExecuteContext& Exec) { Exec.Ctx->SetVertexBuffers(StartSlot, BufferCopy.data(), ViewCopy.data(), (uint32_t)BufferCopy.size()); });
}

void RHICommandList::SetIndexBuffer(RHIBuffer* Buffer, const IndexBufferView* View)
{
    if (IsImmediate())
        return Target->SetIndexBuffer(Buffer, View);
    IndexBufferView ViewCopy = View ? *View : IndexBufferView{};
    const bool bHasView = View != nullptr;
    Record([Buffer, ViewCopy, bHasView](RHIExecuteContext& Exec) { Exec.Ctx->SetIndexBuffer(Buffer, bHasView ? &ViewCopy : nullptr); });
}

void RHICommandList::SetVertexShader(RHIShader* Shader)
{
    if (IsImmediate())
        return Target->SetVertexShader(Shader);
    Record([Shader](RHIExecuteContext& Exec) { Exec.Ctx->SetVertexShader(Shader); });
}

void RHICommandList::SetPixelShader(RHIShader* Shader)
{
    if (IsImmediate())
        return Target->SetPixelShader(Shader);
    Record([Shader](RHIExecuteContext& Exec) { Exec.Ctx->SetPixelShader(Shader); });
}

void RHICommandList::SetGeometryShader(RHIShader* Shader)
{
    if (IsImmediate())
        return Target->SetGeometryShader(Shader);
    Record([Shader](RHIExecuteContext& Exec) { Exec.Ctx->SetGeometryShader(Shader); });
}

void RHICommandList::SetInputLayout(RHIInputLayout* Layout)
{
    if (IsImmediate())
        return Target->SetInputLayout(Layout);
    Record([Layout](RHIExecuteContext& Exec) { Exec.Ctx->SetInputLayout(Layout); });
}

void RHICommandList::SetConstantBuffer(uint32_t Slot, RHIBuffer* Buffer)
{
    if (IsImmediate())
        return Target->SetConstantBuffer(Slot, Buffer);
    Record([Slot, Buffer](RHIExecuteContext& Exec) { Exec.Ctx->SetConstantBuffer(Slot, Buffer); });
}

void RHICommandList::SetConstantBufferOffset(uint32_t Slot, RHIBuffer* Buffer, uint32_t OffsetIn16Constants, uint32_t SizeIn16Constants)
{
    if (IsImmediate())
        return Target->SetConstantBufferOffset(Slot, Buffer, OffsetIn16Constants, SizeIn16Constants);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->SetConstantBufferOffset(Slot, Buffer, OffsetIn16Constants, SizeIn16Constants); });
}

void RHICommandList::SetShaderResourceView(uint32_t Slot, RHITextureView* Srv)
{
    if (IsImmediate())
        return Target->SetShaderResourceView(Slot, Srv);
    Record([Slot, Srv](RHIExecuteContext& Exec) { Exec.Ctx->SetShaderResourceView(Slot, Srv); });
}

void RHICommandList::SetSampler(uint32_t Slot, RHISampler* Sampler)
{
    if (IsImmediate())
        return Target->SetSampler(Slot, Sampler);
    Record([Slot, Sampler](RHIExecuteContext& Exec) { Exec.Ctx->SetSampler(Slot, Sampler); });
}

void RHICommandList::SetViewports(const Viewport* Viewports, uint32_t Count)
{
    if (IsImmediate())
        return Target->SetViewports(Viewports, Count);
    Record([Copy = std::vector<Viewport>(Viewports, Viewports + Count)](RHIExecuteContext& Exec) { Exec.Ctx->SetViewports(Copy.data(), (uint32_t)Copy.size()); });
}

void RHICommandList::SetScissorRects(const ScissorRect* Rects, uint32_t Count)
{
    if (IsImmediate())
        return Target->SetScissorRects(Rects, Count);
    Record([Copy = std::vector<ScissorRect>(Rects, Rects + Count)](RHIExecuteContext& Exec) { Exec.Ctx->SetScissorRects(Copy.data(), (uint32_t)Copy.size()); });
}

void RHICommandList::Draw(uint32_t VertexCount, uint32_t VertexStart)
{
    if (IsImmediate())
        return Target->Draw(VertexCount, VertexStart);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->Draw(VertexCount, VertexStart); });
}

void RHICommandList::DrawIndexed(uint32_t IndexCount, uint32_t IndexStart, int32_t VertexOffset)
{
    if (IsImmediate())
        return Target->DrawIndexed(IndexCount, IndexStart, VertexOffset);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->DrawIndexed(IndexCount, IndexStart, VertexOffset); });
}

void RHICommandList::DrawIndexedInstanced(uint32_t IndexCountPerInstance, uint32_t InstanceCount, uint32_t StartIndex, int32_t BaseVertex, uint32_t StartInstance)
{
    if (IsImmediate())
        return Target->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance);
    Record([=](RHIExecuteContext& Exec) { Exec.Ctx->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance); });
}

void RHICommandList::Flush()
{
    if (IsImmediate())
        return Target->Flush();
    Record([](RHIExecuteContext& Exec) { Exec.Ctx->Flush(); });
}

} // namespace Kiwi
