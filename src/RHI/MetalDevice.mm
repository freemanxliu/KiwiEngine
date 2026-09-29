#include "RHI/Metal/MetalDevice.h"
#include "RHI/ConstantBufferVersioning.h"
#include "RHI/ImGuiRHI.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <imgui.h>
#include <imgui_impl_metal.h>
#include <imgui_impl_osx.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kiwi
{
    // MTLWindingClockwise: clockwise triangles are front faces.
    static ERasterizerCullMode MetalDiscardWinding(ECullMode mode)
    {
        switch (mode)
        {
        case ECullMode::Front: return ERasterizerCullMode::CW;
        case ECullMode::Back:  return ERasterizerCullMode::CCW;
        default:               return ERasterizerCullMode::None;
        }
    }


    namespace
    {

        constexpr uint32_t kMaxColors = 4;
        constexpr uint32_t kMaxSlots = 16;
        constexpr uint32_t kVertexBufferIndex = 30;

        struct MetalState
        {
            id<MTLDevice> device;
            id<MTLCommandQueue> queue;
            id<MTLCommandBuffer> commandBuffer;
            id<MTLCommandBuffer> inflight;
            id<MTLRenderCommandEncoder> encoder;
            id<CAMetalDrawable> drawable;
            id<MTLDepthStencilState> depthWrite;
            id<MTLDepthStencilState> depthRead;
            id<MTLDepthStencilState> depthOff;
            id<MTLTexture> colors[kMaxColors] = {};
            uint32_t colorCount = 0;
            id<MTLTexture> depth;
            bool imgui = false;
            // Constant-address bindings must be 256-byte aligned on macOS.
            // setVertexBytes does not show up in the shader debugger and fails
            // to reach the shader on some Intel GPUs.
            std::unique_ptr<ConstantUploadAllocator> constantAllocator;
        };

        uint32_t FormatBytes(EFormat format)
        {
            switch (format)
            {
            case EFormat::R32G32B32A32_FLOAT: return 16;
            case EFormat::R32G32B32_FLOAT: return 12;
            case EFormat::R16G16B16A16_FLOAT: return 8;
            case EFormat::R32G32_FLOAT: return 8;
            case EFormat::R16G16_FLOAT:
            case EFormat::R32_FLOAT:
            case EFormat::R32_UINT:
            case EFormat::R8G8B8A8_UNORM: return 4;
            case EFormat::R16_UINT: return 2;
            default: return 4;
            }
        }

        MTLPixelFormat ToPixelFormat(EFormat format, bool depth)
        {
            if (depth || format == EFormat::D32_FLOAT || format == EFormat::R32_TYPELESS)
                return MTLPixelFormatDepth32Float;
            if (format == EFormat::D24_UNORM_S8_UINT)
                return MTLPixelFormatDepth32Float_Stencil8;
            switch (format)
            {
            case EFormat::R16G16B16A16_FLOAT: return MTLPixelFormatRGBA16Float;
            case EFormat::R16G16_FLOAT: return MTLPixelFormatRG16Float;
            case EFormat::R32G32B32A32_FLOAT: return MTLPixelFormatRGBA32Float;
            case EFormat::R32G32_FLOAT: return MTLPixelFormatRG32Float;
            case EFormat::R32_FLOAT: return MTLPixelFormatR32Float;
            case EFormat::R32_UINT: return MTLPixelFormatR32Uint;
            case EFormat::R16_UINT: return MTLPixelFormatR16Uint;
            default: return MTLPixelFormatRGBA8Unorm;
            }
        }

        MTLVertexFormat ToVertexFormat(EFormat format)
        {
            switch (format)
            {
            case EFormat::R32G32B32A32_FLOAT: return MTLVertexFormatFloat4;
            case EFormat::R32G32B32_FLOAT: return MTLVertexFormatFloat3;
            case EFormat::R32G32_FLOAT: return MTLVertexFormatFloat2;
            case EFormat::R32_FLOAT: return MTLVertexFormatFloat;
            default: return MTLVertexFormatFloat4;
            }
        }

        MTLPrimitiveType ToPrimitive(EPrimitiveTopology topology)
        {
            switch (topology)
            {
            case EPrimitiveTopology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
            case EPrimitiveTopology::LineList: return MTLPrimitiveTypeLine;
            case EPrimitiveTopology::LineStrip: return MTLPrimitiveTypeLineStrip;
            case EPrimitiveTopology::PointList: return MTLPrimitiveTypePoint;
            default: return MTLPrimitiveTypeTriangle;
            }
        }

        std::string StageSource(const char* source, EShaderType type, const ShaderMacro* macros, uint32_t macroCount)
        {
            std::string src = source ? source : "";
            const char* marker = type == EShaderType::Vertex ? "//!VERTEX" : "//!FRAGMENT";
            const char* other = type == EShaderType::Vertex ? "//!FRAGMENT" : "//!VERTEX";
            auto pos = src.find(marker);
            if (pos != std::string::npos)
            {
                auto start = src.find('\n', pos);
                if (start == std::string::npos)
                    return {};
                ++start;
                auto end = src.find(other, start);
                src = src.substr(start, (end == std::string::npos ? src.size() : end) - start);
            }

            std::string out = "#include <metal_stdlib>\nusing namespace metal;\n";
            for (uint32_t i = 0; macros && i < macroCount; ++i)
            {
                if (!macros[i].Name)
                    continue;
                out += "#define ";
                out += macros[i].Name;
                out += " ";
                out += macros[i].Definition ? macros[i].Definition : "";
                out += "\n";
            }
            out += src;
            return out;
        }

        id<MTLFunction> CompileFunction(id<MTLDevice> device, EShaderType type, const char* source,
            const char* entryPoint, const ShaderMacro* macros, uint32_t macroCount)
        {
            std::string msl = StageSource(source, type, macros, macroCount);
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:@(msl.c_str()) options:nil error:&error];
            if (!library)
            {
                std::cerr << "[Kiwi Metal] Shader compile failed: "
                          << (error ? error.localizedDescription.UTF8String : "unknown") << std::endl;
                return nil;
            }

            id<MTLFunction> function = nil;
            if (entryPoint && entryPoint[0])
                function = [library newFunctionWithName:@(entryPoint)];
            if (!function)
                function = [library newFunctionWithName:(type == EShaderType::Vertex ? @"kiwi_vertex" : @"kiwi_fragment")];
            if (!function)
                function = [library newFunctionWithName:@"main"];
            if (!function)
                std::cerr << "[Kiwi Metal] Shader entry point was not found." << std::endl;
            return function;
        }

    }

    class MetalBuffer : public RHIBuffer
    {
    public:
        MetalBuffer(id<MTLBuffer> buffer, const BufferDesc& desc) : Buffer(buffer), Desc(desc) {}
        MetalBuffer(ConstantUploadAllocator* allocator, const BufferDesc& desc, const void* initialData)
            : Desc(desc)
            , Constant(std::make_unique<VersionedConstantBuffer>(allocator, std::max(desc.SizeInBytes, 16u), initialData)) {}
        void* GetNativeHandle() const override { return (__bridge void*)Buffer; }
        const BufferDesc& GetDesc() const override { return Desc; }
        id<MTLBuffer> GetBuffer() const { return Buffer; }
        VersionedConstantBuffer* GetConstant() const { return Constant.get(); }
        void* Map(uint32_t) override
        {
            if (Constant)
                return Constant->Map();
            return Buffer ? Buffer.contents : nullptr;
        }
        void Unmap(uint32_t) override
        {
            if (Constant)
                Constant->Unmap();
        }
        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            if (Constant)
            {
                Constant->UpdateData(data, size, offset);
                return;
            }
            if (!Buffer || !data || offset + size > Buffer.length)
                return;
            std::memcpy(static_cast<uint8_t*>(Buffer.contents) + offset, data, size);
        }

    private:
        id<MTLBuffer> Buffer;
        BufferDesc Desc;
        std::unique_ptr<VersionedConstantBuffer> Constant;
    };

    class MetalTexture : public RHITexture
    {
    public:
        MetalTexture(id<MTLTexture> texture, const TextureDesc& desc) : Texture(texture), Desc(desc) {}
        void* GetNativeHandle() const override { return (__bridge void*)Texture; }
        const TextureDesc& GetDesc() const override { return Desc; }
        id<MTLTexture> GetTexture() const { return Texture; }
        void SetTexture(id<MTLTexture> texture) { Texture = texture; }

    private:
        id<MTLTexture> Texture;
        TextureDesc Desc;
    };

    class MetalTextureView : public RHITextureView
    {
    public:
        explicit MetalTextureView(MetalTexture* texture) : Texture(texture) {}
        explicit MetalTextureView(id<MTLBuffer> buffer) : Buffer(buffer) {}
        void* GetNativeHandle() const override
        {
            if (Buffer)
                return (__bridge void*)Buffer;
            return Texture ? Texture->GetNativeHandle() : nullptr;
        }
        id<MTLTexture> GetTexture() const { return Texture ? Texture->GetTexture() : nil; }
        id<MTLBuffer> GetBuffer() const { return Buffer; }

    private:
        MetalTexture* Texture = nullptr;
        id<MTLBuffer> Buffer = nil;
    };

    class MetalShader : public RHIShader
    {
    public:
        MetalShader(EShaderType type, id<MTLFunction> function) : Type(type), Function(function) {}
        void* GetNativeHandle() const override { return (__bridge void*)Function; }
        EShaderType GetType() const override { return Type; }
        id<MTLFunction> GetFunction() const { return Function; }

    private:
        EShaderType Type;
        id<MTLFunction> Function;
    };

    class MetalInputLayout : public RHIInputLayout
    {
    public:
        MetalInputLayout(const InputElementDesc* elements, uint32_t count)
            : Elements(elements, elements + count)
        {
            for (const auto& element : Elements)
                Stride = std::max(Stride, element.AlignedByteOffset + FormatBytes(element.Format));
        }
        void* GetNativeHandle() const override { return nullptr; }
        const std::vector<InputElementDesc>& GetElements() const { return Elements; }
        uint32_t GetStride() const { return Stride; }

    private:
        std::vector<InputElementDesc> Elements;
        uint32_t Stride = 0;
    };

    class MetalSampler : public RHISampler
    {
    public:
        explicit MetalSampler(id<MTLSamplerState> sampler) : Sampler(sampler) {}
        void* GetNativeHandle() const override { return (__bridge void*)Sampler; }
        id<MTLSamplerState> GetSampler() const { return Sampler; }

    private:
        id<MTLSamplerState> Sampler;
    };

    class MetalPipelineState : public RHIPipelineState
    {
    public:
        id<MTLFunction> VertexFunction;
        id<MTLFunction> FragmentFunction;
        GraphicsPipelineStateInitializer Initializer;
        std::vector<InputElementDesc> Elements;
        uint32_t LayoutStride = 0;

        struct CacheEntry
        {
            uint64_t Key = 0;
            id<MTLRenderPipelineState> Pipeline;
        };
        std::vector<CacheEntry> Cache;

        void* GetNativeHandle() const override
        {
            return Cache.empty() ? nullptr : (__bridge void*)Cache.back().Pipeline;
        }

        id<MTLRenderPipelineState> GetOrCreate(id<MTLDevice> device, const MTLPixelFormat* colors,
            uint32_t colorCount, MTLPixelFormat depthFormat, uint32_t stride)
        {
            if (!device || !VertexFunction)
                return nil;

            uint64_t key = stride;
            key = key * 131u + colorCount + ((uint64_t)depthFormat << 8);
            key = key * 131u + (Initializer.AdditiveBlend ? 1u : 0u);
            for (uint32_t i = 0; i < colorCount; ++i)
                key = key * 131u + (uint64_t)colors[i];

            for (const auto& entry : Cache)
            {
                if (entry.Key == key)
                    return entry.Pipeline;
            }

            auto* desc = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction = VertexFunction;
            desc.fragmentFunction = FragmentFunction;
            if (!Elements.empty())
            {
                auto* vertexDesc = [MTLVertexDescriptor new];
                vertexDesc.layouts[kVertexBufferIndex].stride = stride ? stride : LayoutStride;
                vertexDesc.layouts[kVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
                vertexDesc.layouts[kVertexBufferIndex].stepRate = 1;
                for (uint32_t i = 0; i < Elements.size() && i < 16; ++i)
                {
                    vertexDesc.attributes[i].format = ToVertexFormat(Elements[i].Format);
                    vertexDesc.attributes[i].offset = Elements[i].AlignedByteOffset;
                    vertexDesc.attributes[i].bufferIndex = kVertexBufferIndex;
                }
                desc.vertexDescriptor = vertexDesc;
            }

            for (uint32_t i = 0; i < colorCount && i < kMaxColors; ++i)
            {
                auto* attachment = desc.colorAttachments[i];
                attachment.pixelFormat = colors[i];
                if (Initializer.AdditiveBlend)
                {
                    attachment.blendingEnabled = YES;
                    attachment.rgbBlendOperation = MTLBlendOperationAdd;
                    attachment.alphaBlendOperation = MTLBlendOperationAdd;
                    attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
                    attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
                    attachment.destinationRGBBlendFactor = MTLBlendFactorOne;
                    attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
                }
            }
            desc.depthAttachmentPixelFormat = depthFormat;

            NSError* error = nil;
            id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pipeline)
            {
                std::cerr << "[Kiwi Metal] Pipeline creation failed: "
                          << (error ? error.localizedDescription.UTF8String : "unknown") << std::endl;
                return nil;
            }
            Cache.push_back({ key, pipeline });
            return pipeline;
        }
    };

    class MetalSwapChain;

    class MetalCommandContext : public RHICommandContext
    {
    public:
        explicit MetalCommandContext(const std::shared_ptr<MetalState>& state) : State(state) {}
        void* GetNativeHandle() const override { return (__bridge void*)State->commandBuffer; }
        void BeginFrame(RHISwapChain* swapChain) override;
        void EndFrame(RHISwapChain*) override { EndEncoder(); }
        void BeginEvent(const char* name) override;
        void EndEvent() override;
        void SetMarker(const char* name) override;
        void ResourceBarrier(RHITexture*, int, int) override { EndEncoder(); }
        void SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv) override;
        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(RHITextureView*, const ClearDepthStencilValue& value, uint8_t clearFlags) override;
        void SetPipelineState(RHIPipelineState* pso) override { PSO = dynamic_cast<MetalPipelineState*>(pso); }
        void SetCullMode(ECullMode mode) override
        {
            CullOverride = true;
            CullMode = mode;
        }
        void ClearCullModeOverride() override { CullOverride = false; }
        void SetPrimitiveTopology(EPrimitiveTopology topology) override { Topology = ToPrimitive(topology); }
        void SetVertexBuffers(uint32_t, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count) override;
        void SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view) override;
        void SetVertexShader(RHIShader*) override {}
        void SetPixelShader(RHIShader*) override {}
        void SetGeometryShader(RHIShader*) override {}
        void SetInputLayout(RHIInputLayout*) override {}
        void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) override;
        void SetConstantBufferOffset(uint32_t slot, RHIBuffer* buffer, uint32_t offsetIn16Constants, uint32_t sizeIn16Constants) override;
        void SetShaderResourceView(uint32_t slot, RHITextureView* srv) override;
        void SetSampler(uint32_t slot, RHISampler* sampler) override;
        void SetViewports(const Viewport* viewports, uint32_t count) override;
        void SetScissorRects(const ScissorRect* rects, uint32_t count) override;
        void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) override;
        void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) override;
        void DrawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override;
        void Flush() override {}
        bool PrepareEncoder();

    private:
        struct BoundConstant
        {
            id<MTLBuffer> Buffer;
            uint32_t Offset = 0;
        };

        void EndEncoder();
        bool EnsureEncoder();
        bool ApplyPipeline();
        void ApplyViewport();

        std::shared_ptr<MetalState> State;
        MetalPipelineState* PSO = nullptr;
        bool CullOverride = false;
        ECullMode CullMode = ECullMode::Back;
        MTLPrimitiveType Topology = MTLPrimitiveTypeTriangle;
        id<MTLBuffer> VertexBuffer;
        uint32_t VertexStride = 0;
        id<MTLBuffer> IndexBuffer;
        MTLIndexType IndexType = MTLIndexTypeUInt32;
        uint32_t IndexStride = 4;
        BoundConstant Constants[kMaxSlots];
        id<MTLBuffer> StorageBuffers[kMaxSlots] = {};
        id<MTLTexture> Textures[kMaxSlots] = {};
        id<MTLSamplerState> Samplers[kMaxSlots] = {};
        bool ViewportValid = false;
        MTLViewport Viewport = {};
        bool ScissorValid = false;
        MTLScissorRect Scissor = {};
        bool ColorClear[kMaxColors] = {};
        MTLClearColor ClearColor[kMaxColors] = {};
        bool DepthClear = false;
        float ClearDepth = 1.0f;
        std::vector<std::string> Events;
    };

    class MetalSwapChain : public RHISwapChain
    {
    public:
        MetalSwapChain(const std::shared_ptr<MetalState>& state, const SwapChainDesc& desc)
            : State(state), Desc(desc)
        {
            NSView* view = (__bridge NSView*)desc.WindowHandle;
            Layer = (CAMetalLayer*)view.layer;
            Layer.device = state->device;
            Layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            Layer.framebufferOnly = YES;
            Layer.drawableSize = CGSizeMake(desc.Width, desc.Height);

            TextureDesc textureDesc;
            textureDesc.Width = desc.Width;
            textureDesc.Height = desc.Height;
            textureDesc.Format = EFormat::R8G8B8A8_UNORM;
            textureDesc.BindFlags = TEXTURE_BIND_RENDER_TARGET;
            BackBuffer = std::make_unique<MetalTexture>(nil, textureDesc);
            BackBufferView = std::make_unique<MetalTextureView>(BackBuffer.get());
        }

        void* GetNativeHandle() const override { return (__bridge void*)Layer; }
        uint32_t GetCurrentBackBufferIndex() const override { return 0; }
        RHITexture* GetBackBuffer(uint32_t) override { return BackBuffer.get(); }
        RHITextureView* GetBackBufferRTV(uint32_t) override { return BackBufferView.get(); }

        void ResizeBuffers(uint32_t width, uint32_t height) override
        {
            Desc.Width = width;
            Desc.Height = height;
            if (Layer)
                Layer.drawableSize = CGSizeMake(width, height);
        }

        void Acquire()
        {
            if (State->inflight)
            {
                [State->inflight waitUntilCompleted];
                State->inflight = nil;
            }
            State->drawable = [Layer nextDrawable];
            State->commandBuffer = [State->queue commandBuffer];
            State->commandBuffer.label = @"KiwiFrame";
            id<MTLTexture> texture = State->drawable ? State->drawable.texture : nil;
            BackBuffer->SetTexture(texture);
            if (!texture)
                std::cerr << "[Kiwi Metal] CAMetalLayer did not provide a drawable." << std::endl;
        }

        void Present(uint32_t syncInterval = 0) override
        {
            if (!State->commandBuffer)
                return;
            if (Layer)
                Layer.displaySyncEnabled = syncInterval > 0;
            if (State->drawable)
                [State->commandBuffer presentDrawable:State->drawable];
            [State->commandBuffer commit];
            State->inflight = State->commandBuffer;
            State->commandBuffer = nil;
            State->drawable = nil;
            State->encoder = nil;
        }

    private:
        std::shared_ptr<MetalState> State;
        SwapChainDesc Desc;
        CAMetalLayer* Layer = nil;
        std::unique_ptr<MetalTexture> BackBuffer;
        std::unique_ptr<MetalTextureView> BackBufferView;
    };

    class MetalDevice : public RHIDevice
    {
    public:
        explicit MetalDevice(bool enableDebug)
            : State(std::make_shared<MetalState>())
        {
            if (enableDebug)
            {
                setenv("MTL_DEBUG_LAYER", "1", 0);
                setenv("MTL_SHADER_VALIDATION", "1", 0);
            }
            State->device = MTLCreateSystemDefaultDevice();
            if (!State->device)
                throw std::runtime_error("Metal device is not available");
            State->queue = [State->device newCommandQueue];

            auto* depthDesc = [MTLDepthStencilDescriptor new];
            depthDesc.depthCompareFunction = MTLCompareFunctionLess;
            depthDesc.depthWriteEnabled = YES;
            State->depthWrite = [State->device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthWriteEnabled = NO;
            State->depthRead = [State->device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthCompareFunction = MTLCompareFunctionAlways;
            State->depthOff = [State->device newDepthStencilStateWithDescriptor:depthDesc];
            id<MTLDevice> device = State->device;
            State->constantAllocator = std::make_unique<ConstantUploadAllocator>([device](uint32_t size)
            {
                ConstantUploadAllocator::Page page;
                id<MTLBuffer> buffer = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
                if (!buffer)
                    return page;
                buffer.label = @"KiwiConstantPage";
                page.CpuBase = static_cast<uint8_t*>(buffer.contents);
                page.NativeHandle = (__bridge void*)buffer;
                page.Size = size;
                page.Owner = std::shared_ptr<void>((__bridge_retained void*)buffer, [](void* p) { CFRelease(p); });
                return page;
            });
            std::cout << "[Kiwi Metal] Device: " << State->device.name.UTF8String << std::endl;
            (void)enableDebug;
        }

        ~MetalDevice() override
        {
            ShutdownImGui();
            if (State->encoder)
            {
                [State->encoder endEncoding];
                State->encoder = nil;
            }
            if (State->commandBuffer)
            {
                [State->commandBuffer commit];
                State->inflight = State->commandBuffer;
                State->commandBuffer = nil;
            }
            if (State->inflight)
                [State->inflight waitUntilCompleted];
        }

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::METAL; }
        void* GetNativeDevice() const override { return (__bridge void*)State->device; }
        void* GetImmediateContext() const override { return (__bridge void*)State->queue; }
        bool IsFeatureSupported(const char*) const override { return true; }
        std::shared_ptr<MetalState> GetState() const { return State; }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override
        {
            return std::make_unique<MetalSwapChain>(State, desc);
        }

        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc, const void* initialData) override
        {
            if (desc.BindFlags & BUFFER_USAGE_CONSTANT)
                return std::make_unique<MetalBuffer>(State->constantAllocator.get(), desc, initialData);
            NSUInteger size = desc.SizeInBytes ? desc.SizeInBytes : 16;
            id<MTLBuffer> buffer = [State->device newBufferWithLength:size options:MTLResourceStorageModeShared];
            if (initialData && desc.SizeInBytes)
                std::memcpy(buffer.contents, initialData, desc.SizeInBytes);
            if (desc.DebugName)
                buffer.label = @(desc.DebugName);
            return std::make_unique<MetalBuffer>(buffer, desc);
        }

        std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc, const void* initialData) override
        {
            if (desc.Width == 0 || desc.Height == 0)
                return nullptr;
            bool depth = (desc.BindFlags & TEXTURE_HINT_DEPTH_STENCIL) != 0
                || desc.Format == EFormat::D32_FLOAT
                || desc.Format == EFormat::D24_UNORM_S8_UINT
                || desc.Format == EFormat::R32_TYPELESS;

            auto* textureDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:ToPixelFormat(desc.Format, depth)
                width:desc.Width height:desc.Height mipmapped:NO];
            textureDesc.storageMode = MTLStorageModePrivate;
            textureDesc.usage = MTLTextureUsageShaderRead;
            if ((desc.BindFlags & TEXTURE_BIND_RENDER_TARGET) || depth)
                textureDesc.usage |= MTLTextureUsageRenderTarget;
            textureDesc.sampleCount = desc.SampleCount ? desc.SampleCount : 1;

            id<MTLTexture> texture = [State->device newTextureWithDescriptor:textureDesc];
            if (desc.DebugName)
                texture.label = @(desc.DebugName);
            if (initialData && !depth)
            {
                NSUInteger row = (NSUInteger)desc.Width * FormatBytes(desc.Format);
                NSUInteger bytes = row * desc.Height;
                id<MTLBuffer> staging = [State->device newBufferWithBytes:initialData length:bytes options:MTLResourceStorageModeShared];
                id<MTLCommandBuffer> upload = [State->queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [upload blitCommandEncoder];
                [blit copyFromBuffer:staging sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:bytes
                    sourceSize:MTLSizeMake(desc.Width, desc.Height, 1)
                    toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [blit endEncoding];
                [upload commit];
                [upload waitUntilCompleted];
            }
            return std::make_unique<MetalTexture>(texture, desc);
        }

        std::unique_ptr<RHITextureView> CreateTextureView(RHITexture* texture, EDescriptorHeapType, EFormat, int, int) override
        {
            return std::make_unique<MetalTextureView>(static_cast<MetalTexture*>(texture));
        }

        std::unique_ptr<RHITextureView> CreateBufferSRV(RHIBuffer* buffer, uint32_t, uint32_t) override
        {
            if (!buffer)
                return nullptr;
            return std::make_unique<MetalTextureView>(static_cast<MetalBuffer*>(buffer)->GetBuffer());
        }

        std::unique_ptr<RHIShader> CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize) override
        {
            std::string source(static_cast<const char*>(byteCode), byteCodeSize);
            return CompileShader(type, source.c_str(), nullptr, nullptr, nullptr, 0);
        }

        std::unique_ptr<RHIShader> CompileShader(EShaderType type, const char* source, const char* entryPoint,
            const char*, const ShaderMacro* macros, uint32_t macroCount) override
        {
            if (type != EShaderType::Vertex && type != EShaderType::Pixel)
                return nullptr;
            id<MTLFunction> function = CompileFunction(State->device, type, source, entryPoint, macros, macroCount);
            return function ? std::make_unique<MetalShader>(type, function) : nullptr;
        }

        std::unique_ptr<RHIInputLayout> CreateInputLayout(const InputElementDesc* elements, uint32_t elementCount, RHIShader*) override
        {
            return std::make_unique<MetalInputLayout>(elements, elementCount);
        }

        std::unique_ptr<RHIPipelineState> CreatePipelineState() override
        {
            return std::make_unique<MetalPipelineState>();
        }

        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            const GraphicsPipelineStateInitializer& initializer) override
        {
            auto pso = std::make_unique<MetalPipelineState>();
            pso->Initializer = initializer;
            if (auto* shader = dynamic_cast<MetalShader*>(initializer.VertexShader))
                pso->VertexFunction = shader->GetFunction();
            if (auto* shader = dynamic_cast<MetalShader*>(initializer.PixelShader))
                pso->FragmentFunction = shader->GetFunction();
            if (auto* layout = dynamic_cast<MetalInputLayout*>(initializer.VertexDeclaration))
            {
                pso->Elements = layout->GetElements();
                pso->LayoutStride = layout->GetStride();
            }
            return pso;
        }

        std::unique_ptr<RHISampler> CreateSampler() override
        {
            auto* desc = [MTLSamplerDescriptor new];
            desc.minFilter = MTLSamplerMinMagFilterLinear;
            desc.magFilter = MTLSamplerMinMagFilterLinear;
            desc.mipFilter = MTLSamplerMipFilterLinear;
            desc.sAddressMode = MTLSamplerAddressModeRepeat;
            desc.tAddressMode = MTLSamplerAddressModeRepeat;
            return std::make_unique<MetalSampler>([State->device newSamplerStateWithDescriptor:desc]);
        }

        std::unique_ptr<RHISampler> CreateComparisonSampler() override
        {
            auto* desc = [MTLSamplerDescriptor new];
            desc.minFilter = MTLSamplerMinMagFilterLinear;
            desc.magFilter = MTLSamplerMinMagFilterLinear;
            desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.compareFunction = MTLCompareFunctionLessEqual;
            return std::make_unique<MetalSampler>([State->device newSamplerStateWithDescriptor:desc]);
        }

        void InitImGui(void* windowHandle) override
        {
            if (State->imgui)
                return;
            View = (__bridge NSView*)windowHandle;
            ImGui_ImplMetal_Init(State->device);
            ImGui_ImplOSX_Init(View);
            State->imgui = true;
        }

        void ShutdownImGui() override
        {
            if (!State->imgui)
                return;
            ImGui_ImplMetal_Shutdown();
            ImGui_ImplOSX_Shutdown();
            State->imgui = false;
            View = nil;
        }

        void ImGuiNewFrame() override
        {
            if (!State->imgui)
                return;
            ImGui_ImplOSX_NewFrame(View);
        }

        void ImGuiUpdateTextures(ImDrawData* DrawData) override
        {
            if (State->imgui)
                UpdateImGuiTextures(DrawData, ImGui_ImplMetal_UpdateTexture);
        }

        void ImGuiRenderDrawData(RHICommandContext* Ctx, ImDrawData* DrawData) override
        {
            if (!State->imgui || !DrawData)
                return;
            id<MTLTexture> color = State->colorCount > 0 ? State->colors[0] : nil;
            if (!color)
                return;
            auto* desc = [MTLRenderPassDescriptor renderPassDescriptor];
            desc.colorAttachments[0].texture = color;
            desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
            desc.colorAttachments[0].storeAction = MTLStoreActionStore;
            ImGui_ImplMetal_NewFrame(desc);

            auto* metal = static_cast<MetalCommandContext*>(Ctx);
            if (!metal->PrepareEncoder() || !State->encoder)
                return;
            ImGui_ImplMetal_RenderDrawData(DrawData, State->commandBuffer, State->encoder);
        }

    private:
        std::shared_ptr<MetalState> State;
        NSView* View = nil;
    };

    void MetalCommandContext::BeginFrame(RHISwapChain* swapChain)
    {
        EndEncoder();
        static_cast<MetalSwapChain*>(swapChain)->Acquire();
        State->constantAllocator->BeginFrame();
    }

    void MetalCommandContext::BeginEvent(const char* name)
    {
        Events.emplace_back(name ? name : "");
        if (State->encoder)
            [State->encoder pushDebugGroup:@(name ? name : "")];
    }

    void MetalCommandContext::EndEvent()
    {
        if (!Events.empty())
            Events.pop_back();
        if (State->encoder)
            [State->encoder popDebugGroup];
    }

    void MetalCommandContext::SetMarker(const char* name)
    {
        if (State->encoder)
            [State->encoder insertDebugSignpost:@(name ? name : "")];
    }

    void MetalCommandContext::EndEncoder()
    {
        if (!State->encoder)
            return;
        [State->encoder endEncoding];
        State->encoder = nil;
    }

    void MetalCommandContext::SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv)
    {
        EndEncoder();
        State->colorCount = std::min(rtvCount, kMaxColors);
        for (uint32_t i = 0; i < kMaxColors; ++i)
        {
            State->colors[i] = nil;
            ColorClear[i] = false;
        }
        for (uint32_t i = 0; i < State->colorCount; ++i)
        {
            auto* view = rtvs ? static_cast<MetalTextureView*>(rtvs[i]) : nullptr;
            State->colors[i] = view ? view->GetTexture() : nil;
        }
        auto* depth = static_cast<MetalTextureView*>(dsv);
        State->depth = depth ? depth->GetTexture() : nil;
        DepthClear = false;
    }

    void MetalCommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        id<MTLTexture> texture = rtv ? static_cast<MetalTextureView*>(rtv)->GetTexture() : nil;
        for (uint32_t i = 0; i < State->colorCount; ++i)
        {
            if (State->colors[i] != texture)
                continue;
            if (State->encoder)
                EndEncoder();
            ColorClear[i] = true;
            ClearColor[i] = MTLClearColorMake(color.R, color.G, color.B, color.A);
        }
    }

    void MetalCommandContext::ClearDepthStencilView(RHITextureView*, const ClearDepthStencilValue& value, uint8_t clearFlags)
    {
        if ((clearFlags & 0x1) == 0 || !State->depth)
            return;
        if (State->encoder)
            EndEncoder();
        DepthClear = true;
        ClearDepth = value.Depth;
    }

    void MetalCommandContext::SetVertexBuffers(uint32_t, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count)
    {
        VertexBuffer = nil;
        VertexStride = 0;
        if (!count || !buffers || !buffers[0])
            return;
        VertexBuffer = static_cast<MetalBuffer*>(buffers[0])->GetBuffer();
        if (views)
            VertexStride = views[0].StrideInBytes;
    }

    void MetalCommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        IndexBuffer = buffer ? static_cast<MetalBuffer*>(buffer)->GetBuffer() : nil;
        IndexType = (view && view->Format == EFormat::R16_UINT) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        IndexStride = IndexType == MTLIndexTypeUInt16 ? 2u : 4u;
    }

    void MetalCommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        if (!buffer || slot >= kMaxSlots)
            return;
        auto* metal = static_cast<MetalBuffer*>(buffer);
        if (auto* constant = metal->GetConstant())
        {
            const auto& version = constant->GetCurrent();
            Constants[slot].Buffer = (__bridge id<MTLBuffer>)version.NativeHandle;
            Constants[slot].Offset = version.Offset;
        }
        else
        {
            Constants[slot].Buffer = metal->GetBuffer();
            Constants[slot].Offset = 0;
        }
    }

    void MetalCommandContext::SetConstantBufferOffset(uint32_t slot, RHIBuffer* buffer,
        uint32_t offsetIn16Constants, uint32_t sizeIn16Constants)
    {
        if (!buffer || slot >= kMaxSlots)
            return;
        auto* metal = static_cast<MetalBuffer*>(buffer);
        uint32_t offset = offsetIn16Constants * 16;
        uint32_t size = sizeIn16Constants * 16;

        id<MTLBuffer> base = nil;
        uint32_t baseOffset = 0;
        const uint8_t* contents = nullptr;
        NSUInteger length = 0;
        if (auto* constant = metal->GetConstant())
        {
            const auto& version = constant->GetCurrent();
            base = (__bridge id<MTLBuffer>)version.NativeHandle;
            baseOffset = version.Offset;
            contents = version.Cpu;
            length = constant->GetSize();
        }
        else
        {
            base = metal->GetBuffer();
            contents = static_cast<const uint8_t*>(base.contents);
            length = base.length;
        }
        if (!base || size == 0 || (NSUInteger)offset + size > length)
            return;

        if ((baseOffset + offset) % ConstantUploadAllocator::kAlignment == 0)
        {
            Constants[slot].Buffer = base;
            Constants[slot].Offset = baseOffset + offset;
            return;
        }

        ConstantUploadAllocator::Allocation copy;
        if (!State->constantAllocator->Allocate(size, copy))
            return;
        std::memcpy(copy.Cpu, contents + offset, size);
        Constants[slot].Buffer = (__bridge id<MTLBuffer>)copy.NativeHandle;
        Constants[slot].Offset = copy.Offset;
    }

    void MetalCommandContext::SetShaderResourceView(uint32_t slot, RHITextureView* srv)
    {
        if (slot >= kMaxSlots)
            return;
        if (!srv)
        {
            Textures[slot] = nil;
            StorageBuffers[slot] = nil;
            return;
        }
        auto* view = static_cast<MetalTextureView*>(srv);
        Textures[slot] = view->GetTexture();
        StorageBuffers[slot] = view->GetBuffer();
    }

    void MetalCommandContext::SetSampler(uint32_t slot, RHISampler* sampler)
    {
        if (slot >= kMaxSlots)
            return;
        Samplers[slot] = sampler ? static_cast<MetalSampler*>(sampler)->GetSampler() : nil;
    }

    void MetalCommandContext::SetViewports(const Kiwi::Viewport* viewports, uint32_t count)
    {
        if (!viewports || !count)
            return;
        ViewportValid = true;
        Viewport.originX = viewports[0].TopLeftX;
        Viewport.originY = viewports[0].TopLeftY;
        Viewport.width = viewports[0].Width;
        Viewport.height = viewports[0].Height;
        Viewport.znear = viewports[0].MinDepth;
        Viewport.zfar = viewports[0].MaxDepth;
    }

    void MetalCommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        if (!rects || !count)
            return;
        ScissorValid = true;
        Scissor.x = (NSUInteger)std::max(0, rects[0].Left);
        Scissor.y = (NSUInteger)std::max(0, rects[0].Top);
        Scissor.width = (NSUInteger)std::max(0, rects[0].Right - rects[0].Left);
        Scissor.height = (NSUInteger)std::max(0, rects[0].Bottom - rects[0].Top);
    }

    bool MetalCommandContext::EnsureEncoder()
    {
        if (State->encoder)
            return true;
        if (!State->commandBuffer)
            return false;

        auto* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        bool any = false;
        for (uint32_t i = 0; i < State->colorCount; ++i)
        {
            if (!State->colors[i])
                continue;
            any = true;
            desc.colorAttachments[i].texture = State->colors[i];
            desc.colorAttachments[i].loadAction = ColorClear[i] ? MTLLoadActionClear : MTLLoadActionLoad;
            desc.colorAttachments[i].storeAction = MTLStoreActionStore;
            if (ColorClear[i])
                desc.colorAttachments[i].clearColor = ClearColor[i];
            ColorClear[i] = false;
        }
        if (State->depth)
        {
            any = true;
            desc.depthAttachment.texture = State->depth;
            desc.depthAttachment.loadAction = DepthClear ? MTLLoadActionClear : MTLLoadActionLoad;
            desc.depthAttachment.storeAction = MTLStoreActionStore;
            desc.depthAttachment.clearDepth = ClearDepth;
            DepthClear = false;
        }
        if (!any)
            return false;

        State->encoder = [State->commandBuffer renderCommandEncoderWithDescriptor:desc];
        for (const auto& event : Events)
            [State->encoder pushDebugGroup:@(event.c_str())];
        return State->encoder != nil;
    }

    void MetalCommandContext::ApplyViewport()
    {
        NSUInteger width = 0;
        NSUInteger height = 0;
        auto limitTo = [&](id<MTLTexture> texture) {
            if (!texture)
                return;
            if (width == 0 || texture.width < width)
                width = texture.width;
            if (height == 0 || texture.height < height)
                height = texture.height;
        };
        for (uint32_t i = 0; i < State->colorCount; ++i)
            limitTo(State->colors[i]);
        limitTo(State->depth);
        if (width == 0) width = 1;
        if (height == 0) height = 1;

        MTLViewport viewport = ViewportValid ? Viewport : MTLViewport{ 0, 0, (double)width, (double)height, 0.0, 1.0 };
        if (viewport.originX < 0.0) viewport.originX = 0.0;
        if (viewport.originY < 0.0) viewport.originY = 0.0;
        if (viewport.originX + viewport.width > (double)width)
            viewport.width = std::max(1.0, (double)width - viewport.originX);
        if (viewport.originY + viewport.height > (double)height)
            viewport.height = std::max(1.0, (double)height - viewport.originY);
        [State->encoder setViewport:viewport];

        MTLScissorRect scissor = ScissorValid ? Scissor : MTLScissorRect{ 0, 0, width, height };
        if (scissor.x >= width) scissor.x = 0;
        if (scissor.y >= height) scissor.y = 0;
        if (scissor.x + scissor.width > width) scissor.width = width - scissor.x;
        if (scissor.y + scissor.height > height) scissor.height = height - scissor.y;
        if (scissor.width == 0 || scissor.height == 0)
            scissor = MTLScissorRect{ 0, 0, width, height };
        [State->encoder setScissorRect:scissor];
    }

    bool MetalCommandContext::ApplyPipeline()
    {
        ApplyViewport();
        if (!PSO || !PSO->VertexFunction)
            return false;

        MTLPixelFormat colors[kMaxColors] = {};
        for (uint32_t i = 0; i < State->colorCount; ++i)
            colors[i] = State->colors[i] ? State->colors[i].pixelFormat : MTLPixelFormatInvalid;
        MTLPixelFormat depthFormat = State->depth ? State->depth.pixelFormat : MTLPixelFormatInvalid;
        id<MTLRenderPipelineState> pipeline = PSO->GetOrCreate(
            State->device, colors, State->colorCount, depthFormat,
            VertexStride ? VertexStride : PSO->LayoutStride);
        if (!pipeline)
            return false;

        [State->encoder setRenderPipelineState:pipeline];
        const RasterizerStateDesc& raster = PSO->Initializer.RasterizerState;
        ERasterizerCullMode winding = MetalDiscardWinding(CullOverride ? CullMode : raster.CullMode);
        [State->encoder setFrontFacingWinding:MTLWindingClockwise];
        MTLCullMode cullMode = MTLCullModeNone;
        switch (winding)
        {
        case ERasterizerCullMode::CW:  cullMode = MTLCullModeFront; break;
        case ERasterizerCullMode::CCW: cullMode = MTLCullModeBack; break;
        default: break;
        }
        [State->encoder setCullMode:cullMode];
        [State->encoder setTriangleFillMode:raster.FillMode == ERasterizerFillMode::Wireframe
            ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        [State->encoder setDepthBias:raster.DepthBias slopeScale:raster.SlopeScaleDepthBias clamp:0.0f];
        if (State->depth && PSO->Initializer.DepthEnabled)
            [State->encoder setDepthStencilState:PSO->Initializer.DepthWrite ? State->depthWrite : State->depthRead];
        else
            [State->encoder setDepthStencilState:State->depthOff];

        if (VertexBuffer)
            [State->encoder setVertexBuffer:VertexBuffer offset:0 atIndex:kVertexBufferIndex];
        for (uint32_t slot = 0; slot < kMaxSlots; ++slot)
        {
            if (Constants[slot].Buffer)
            {
                [State->encoder setVertexBuffer:Constants[slot].Buffer offset:Constants[slot].Offset atIndex:slot];
                [State->encoder setFragmentBuffer:Constants[slot].Buffer offset:Constants[slot].Offset atIndex:slot];
            }
            if (StorageBuffers[slot])
                [State->encoder setVertexBuffer:StorageBuffers[slot] offset:0 atIndex:slot];
            if (Textures[slot])
            {
                [State->encoder setVertexTexture:Textures[slot] atIndex:slot];
                [State->encoder setFragmentTexture:Textures[slot] atIndex:slot];
            }
            if (Samplers[slot])
            {
                [State->encoder setVertexSamplerState:Samplers[slot] atIndex:slot];
                [State->encoder setFragmentSamplerState:Samplers[slot] atIndex:slot];
            }
        }
        return true;
    }

    bool MetalCommandContext::PrepareEncoder()
    {
        if (!EnsureEncoder())
            return false;
        ApplyViewport();
        return true;
    }

    void MetalCommandContext::Draw(uint32_t vertexCount, uint32_t vertexStart)
    {
        if (!EnsureEncoder() || !ApplyPipeline())
            return;
        [State->encoder drawPrimitives:Topology vertexStart:vertexStart vertexCount:vertexCount];
    }

    void MetalCommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        DrawIndexedInstanced(indexCount, 1, indexStart, vertexOffset, 0);
    }

    void MetalCommandContext::DrawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount,
        uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
    {
        if (!IndexBuffer || !instanceCount || !EnsureEncoder() || !ApplyPipeline())
            return;
        [State->encoder drawIndexedPrimitives:Topology
            indexCount:indexCountPerInstance indexType:IndexType indexBuffer:IndexBuffer
            indexBufferOffset:(NSUInteger)startIndex * IndexStride instanceCount:instanceCount
            baseVertex:baseVertex baseInstance:startInstance];
    }

    void CreateMetalRHI(const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        auto device = std::make_unique<MetalDevice>(params.EnableDebug);
        auto context = std::make_unique<MetalCommandContext>(device->GetState());
        outDevice = std::move(device);
        outContext = std::move(context);
    }

}
