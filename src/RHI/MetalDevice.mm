#include "RHI/Metal/MetalDevice.h"

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
            id<MTLBuffer> constantHeap;
            uint32_t constantCursor = 0;
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
        MetalBuffer(id<MTLBuffer> buffer, const BufferDesc& desc) : m_Buffer(buffer), m_Desc(desc) {}
        void* GetNativeHandle() const override { return (__bridge void*)m_Buffer; }
        const BufferDesc& GetDesc() const override { return m_Desc; }
        id<MTLBuffer> GetBuffer() const { return m_Buffer; }
        void* Map(uint32_t) override { return m_Buffer ? m_Buffer.contents : nullptr; }
        void Unmap(uint32_t) override {}
        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            if (!m_Buffer || !data || offset + size > m_Buffer.length)
                return;
            std::memcpy(static_cast<uint8_t*>(m_Buffer.contents) + offset, data, size);
        }

    private:
        id<MTLBuffer> m_Buffer;
        BufferDesc m_Desc;
    };

    class MetalTexture : public RHITexture
    {
    public:
        MetalTexture(id<MTLTexture> texture, const TextureDesc& desc) : m_Texture(texture), m_Desc(desc) {}
        void* GetNativeHandle() const override { return (__bridge void*)m_Texture; }
        const TextureDesc& GetDesc() const override { return m_Desc; }
        id<MTLTexture> GetTexture() const { return m_Texture; }
        void SetTexture(id<MTLTexture> texture) { m_Texture = texture; }

    private:
        id<MTLTexture> m_Texture;
        TextureDesc m_Desc;
    };

    class MetalTextureView : public RHITextureView
    {
    public:
        explicit MetalTextureView(MetalTexture* texture) : m_Texture(texture) {}
        explicit MetalTextureView(id<MTLBuffer> buffer) : m_Buffer(buffer) {}
        void* GetNativeHandle() const override
        {
            if (m_Buffer)
                return (__bridge void*)m_Buffer;
            return m_Texture ? m_Texture->GetNativeHandle() : nullptr;
        }
        id<MTLTexture> GetTexture() const { return m_Texture ? m_Texture->GetTexture() : nil; }
        id<MTLBuffer> GetBuffer() const { return m_Buffer; }

    private:
        MetalTexture* m_Texture = nullptr;
        id<MTLBuffer> m_Buffer = nil;
    };

    class MetalShader : public RHIShader
    {
    public:
        MetalShader(EShaderType type, id<MTLFunction> function) : m_Type(type), m_Function(function) {}
        void* GetNativeHandle() const override { return (__bridge void*)m_Function; }
        EShaderType GetType() const override { return m_Type; }
        id<MTLFunction> GetFunction() const { return m_Function; }

    private:
        EShaderType m_Type;
        id<MTLFunction> m_Function;
    };

    class MetalInputLayout : public RHIInputLayout
    {
    public:
        MetalInputLayout(const InputElementDesc* elements, uint32_t count)
            : m_Elements(elements, elements + count)
        {
            for (const auto& element : m_Elements)
                m_Stride = std::max(m_Stride, element.AlignedByteOffset + FormatBytes(element.Format));
        }
        void* GetNativeHandle() const override { return nullptr; }
        const std::vector<InputElementDesc>& GetElements() const { return m_Elements; }
        uint32_t GetStride() const { return m_Stride; }

    private:
        std::vector<InputElementDesc> m_Elements;
        uint32_t m_Stride = 0;
    };

    class MetalSampler : public RHISampler
    {
    public:
        explicit MetalSampler(id<MTLSamplerState> sampler) : m_Sampler(sampler) {}
        void* GetNativeHandle() const override { return (__bridge void*)m_Sampler; }
        id<MTLSamplerState> GetSampler() const { return m_Sampler; }

    private:
        id<MTLSamplerState> m_Sampler;
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
        explicit MetalCommandContext(const std::shared_ptr<MetalState>& state) : m_State(state) {}
        void* GetNativeHandle() const override { return (__bridge void*)m_State->commandBuffer; }
        void BeginFrame(RHISwapChain* swapChain) override;
        void EndFrame(RHISwapChain*) override { EndEncoder(); }
        void BeginEvent(const char* name) override;
        void EndEvent() override;
        void SetMarker(const char* name) override;
        void ResourceBarrier(RHITexture*, int, int) override { EndEncoder(); }
        void SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv) override;
        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(RHITextureView*, const ClearDepthStencilValue& value, uint8_t clearFlags) override;
        void SetPipelineState(RHIPipelineState* pso) override { m_PSO = dynamic_cast<MetalPipelineState*>(pso); }
        void SetCullMode(ECullMode mode) override
        {
            m_CullOverride = true;
            m_CullMode = mode;
        }
        void ClearCullModeOverride() override { m_CullOverride = false; }
        void SetPrimitiveTopology(EPrimitiveTopology topology) override { m_Topology = ToPrimitive(topology); }
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
        struct BoundBytes
        {
            bool Valid = false;
            uint32_t Offset = 0;
        };

        void EndEncoder();
        bool EnsureEncoder();
        bool ApplyPipeline();
        void ApplyViewport();
        void BindBytes(uint32_t slot, const void* data, uint32_t size);

        std::shared_ptr<MetalState> m_State;
        MetalPipelineState* m_PSO = nullptr;
        bool m_CullOverride = false;
        ECullMode m_CullMode = ECullMode::Back;
        MTLPrimitiveType m_Topology = MTLPrimitiveTypeTriangle;
        id<MTLBuffer> m_VertexBuffer;
        uint32_t m_VertexStride = 0;
        id<MTLBuffer> m_IndexBuffer;
        MTLIndexType m_IndexType = MTLIndexTypeUInt32;
        uint32_t m_IndexStride = 4;
        BoundBytes m_Constants[kMaxSlots];
        id<MTLBuffer> m_StorageBuffers[kMaxSlots] = {};
        id<MTLTexture> m_Textures[kMaxSlots] = {};
        id<MTLSamplerState> m_Samplers[kMaxSlots] = {};
        bool m_ViewportValid = false;
        MTLViewport m_Viewport = {};
        bool m_ScissorValid = false;
        MTLScissorRect m_Scissor = {};
        bool m_ColorClear[kMaxColors] = {};
        MTLClearColor m_ClearColor[kMaxColors] = {};
        bool m_DepthClear = false;
        float m_ClearDepth = 1.0f;
        std::vector<std::string> m_Events;
    };

    class MetalSwapChain : public RHISwapChain
    {
    public:
        MetalSwapChain(const std::shared_ptr<MetalState>& state, const SwapChainDesc& desc)
            : m_State(state), m_Desc(desc)
        {
            NSView* view = (__bridge NSView*)desc.WindowHandle;
            m_Layer = (CAMetalLayer*)view.layer;
            m_Layer.device = state->device;
            m_Layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            m_Layer.framebufferOnly = YES;
            m_Layer.drawableSize = CGSizeMake(desc.Width, desc.Height);

            TextureDesc textureDesc;
            textureDesc.Width = desc.Width;
            textureDesc.Height = desc.Height;
            textureDesc.Format = EFormat::R8G8B8A8_UNORM;
            textureDesc.BindFlags = TEXTURE_BIND_RENDER_TARGET;
            m_BackBuffer = std::make_unique<MetalTexture>(nil, textureDesc);
            m_BackBufferView = std::make_unique<MetalTextureView>(m_BackBuffer.get());
        }

        void* GetNativeHandle() const override { return (__bridge void*)m_Layer; }
        uint32_t GetCurrentBackBufferIndex() const override { return 0; }
        RHITexture* GetBackBuffer(uint32_t) override { return m_BackBuffer.get(); }
        RHITextureView* GetBackBufferRTV(uint32_t) override { return m_BackBufferView.get(); }

        void ResizeBuffers(uint32_t width, uint32_t height) override
        {
            m_Desc.Width = width;
            m_Desc.Height = height;
            if (m_Layer)
                m_Layer.drawableSize = CGSizeMake(width, height);
        }

        void Acquire()
        {
            if (m_State->inflight)
            {
                [m_State->inflight waitUntilCompleted];
                m_State->inflight = nil;
            }
            m_State->drawable = [m_Layer nextDrawable];
            m_State->commandBuffer = [m_State->queue commandBuffer];
            m_State->commandBuffer.label = @"KiwiFrame";
            id<MTLTexture> texture = m_State->drawable ? m_State->drawable.texture : nil;
            m_BackBuffer->SetTexture(texture);
            if (!texture)
                std::cerr << "[Kiwi Metal] CAMetalLayer did not provide a drawable." << std::endl;
        }

        void Present(uint32_t syncInterval = 0) override
        {
            if (!m_State->commandBuffer)
                return;
            if (m_Layer)
                m_Layer.displaySyncEnabled = syncInterval > 0;
            if (m_State->drawable)
                [m_State->commandBuffer presentDrawable:m_State->drawable];
            [m_State->commandBuffer commit];
            m_State->inflight = m_State->commandBuffer;
            m_State->commandBuffer = nil;
            m_State->drawable = nil;
            m_State->encoder = nil;
        }

    private:
        std::shared_ptr<MetalState> m_State;
        SwapChainDesc m_Desc;
        CAMetalLayer* m_Layer = nil;
        std::unique_ptr<MetalTexture> m_BackBuffer;
        std::unique_ptr<MetalTextureView> m_BackBufferView;
    };

    class MetalDevice : public RHIDevice
    {
    public:
        explicit MetalDevice(bool enableDebug)
            : m_State(std::make_shared<MetalState>())
        {
            if (enableDebug)
            {
                setenv("MTL_DEBUG_LAYER", "1", 0);
                setenv("MTL_SHADER_VALIDATION", "1", 0);
            }
            m_State->device = MTLCreateSystemDefaultDevice();
            if (!m_State->device)
                throw std::runtime_error("Metal device is not available");
            m_State->queue = [m_State->device newCommandQueue];

            auto* depthDesc = [MTLDepthStencilDescriptor new];
            depthDesc.depthCompareFunction = MTLCompareFunctionLess;
            depthDesc.depthWriteEnabled = YES;
            m_State->depthWrite = [m_State->device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthWriteEnabled = NO;
            m_State->depthRead = [m_State->device newDepthStencilStateWithDescriptor:depthDesc];
            depthDesc.depthCompareFunction = MTLCompareFunctionAlways;
            m_State->depthOff = [m_State->device newDepthStencilStateWithDescriptor:depthDesc];
            m_State->constantHeap = [m_State->device newBufferWithLength:2 * 1024 * 1024 options:MTLResourceStorageModeShared];
            m_State->constantHeap.label = @"KiwiConstantHeap";
            std::cout << "[Kiwi Metal] Device: " << m_State->device.name.UTF8String << std::endl;
            (void)enableDebug;
        }

        ~MetalDevice() override
        {
            ShutdownImGui();
            if (m_State->encoder)
            {
                [m_State->encoder endEncoding];
                m_State->encoder = nil;
            }
            if (m_State->commandBuffer)
            {
                [m_State->commandBuffer commit];
                m_State->inflight = m_State->commandBuffer;
                m_State->commandBuffer = nil;
            }
            if (m_State->inflight)
                [m_State->inflight waitUntilCompleted];
        }

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::METAL; }
        void* GetNativeDevice() const override { return (__bridge void*)m_State->device; }
        void* GetImmediateContext() const override { return (__bridge void*)m_State->queue; }
        bool IsFeatureSupported(const char*) const override { return true; }
        std::shared_ptr<MetalState> GetState() const { return m_State; }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override
        {
            return std::make_unique<MetalSwapChain>(m_State, desc);
        }

        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc, const void* initialData) override
        {
            NSUInteger size = desc.SizeInBytes ? desc.SizeInBytes : 16;
            if (desc.BindFlags & BUFFER_USAGE_CONSTANT)
                size = (size + 255u) & ~255u;
            id<MTLBuffer> buffer = [m_State->device newBufferWithLength:size options:MTLResourceStorageModeShared];
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

            id<MTLTexture> texture = [m_State->device newTextureWithDescriptor:textureDesc];
            if (desc.DebugName)
                texture.label = @(desc.DebugName);
            if (initialData && !depth)
            {
                NSUInteger row = (NSUInteger)desc.Width * FormatBytes(desc.Format);
                NSUInteger bytes = row * desc.Height;
                id<MTLBuffer> staging = [m_State->device newBufferWithBytes:initialData length:bytes options:MTLResourceStorageModeShared];
                id<MTLCommandBuffer> upload = [m_State->queue commandBuffer];
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
            id<MTLFunction> function = CompileFunction(m_State->device, type, source, entryPoint, macros, macroCount);
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
            return std::make_unique<MetalSampler>([m_State->device newSamplerStateWithDescriptor:desc]);
        }

        std::unique_ptr<RHISampler> CreateComparisonSampler() override
        {
            auto* desc = [MTLSamplerDescriptor new];
            desc.minFilter = MTLSamplerMinMagFilterLinear;
            desc.magFilter = MTLSamplerMinMagFilterLinear;
            desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.compareFunction = MTLCompareFunctionLessEqual;
            return std::make_unique<MetalSampler>([m_State->device newSamplerStateWithDescriptor:desc]);
        }

        void InitImGui(void* windowHandle) override
        {
            if (m_State->imgui)
                return;
            m_View = (__bridge NSView*)windowHandle;
            ImGui_ImplMetal_Init(m_State->device);
            ImGui_ImplOSX_Init(m_View);
            m_State->imgui = true;
        }

        void ShutdownImGui() override
        {
            if (!m_State->imgui)
                return;
            ImGui_ImplMetal_Shutdown();
            ImGui_ImplOSX_Shutdown();
            m_State->imgui = false;
            m_View = nil;
        }

        void ImGuiNewFrame() override
        {
            if (!m_State->imgui)
                return;
            ImGui_ImplOSX_NewFrame(m_View);
            id<MTLTexture> color = m_State->colorCount > 0 ? m_State->colors[0] : nil;
            if (!color)
                return;
            auto* desc = [MTLRenderPassDescriptor renderPassDescriptor];
            desc.colorAttachments[0].texture = color;
            desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
            desc.colorAttachments[0].storeAction = MTLStoreActionStore;
            ImGui_ImplMetal_NewFrame(desc);
        }

        void ImGuiRenderDrawData(RHICommandContext* ctx) override
        {
            auto* metal = static_cast<MetalCommandContext*>(ctx);
            if (!metal->PrepareEncoder() || !m_State->encoder)
                return;
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), m_State->commandBuffer, m_State->encoder);
        }

    private:
        std::shared_ptr<MetalState> m_State;
        NSView* m_View = nil;
    };

    void MetalCommandContext::BeginFrame(RHISwapChain* swapChain)
    {
        EndEncoder();
        static_cast<MetalSwapChain*>(swapChain)->Acquire();
        m_State->constantCursor = 0;
    }

    void MetalCommandContext::BeginEvent(const char* name)
    {
        m_Events.emplace_back(name ? name : "");
        if (m_State->encoder)
            [m_State->encoder pushDebugGroup:@(name ? name : "")];
    }

    void MetalCommandContext::EndEvent()
    {
        if (!m_Events.empty())
            m_Events.pop_back();
        if (m_State->encoder)
            [m_State->encoder popDebugGroup];
    }

    void MetalCommandContext::SetMarker(const char* name)
    {
        if (m_State->encoder)
            [m_State->encoder insertDebugSignpost:@(name ? name : "")];
    }

    void MetalCommandContext::EndEncoder()
    {
        if (!m_State->encoder)
            return;
        [m_State->encoder endEncoding];
        m_State->encoder = nil;
    }

    void MetalCommandContext::SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv)
    {
        EndEncoder();
        m_State->colorCount = std::min(rtvCount, kMaxColors);
        for (uint32_t i = 0; i < kMaxColors; ++i)
        {
            m_State->colors[i] = nil;
            m_ColorClear[i] = false;
        }
        for (uint32_t i = 0; i < m_State->colorCount; ++i)
        {
            auto* view = rtvs ? static_cast<MetalTextureView*>(rtvs[i]) : nullptr;
            m_State->colors[i] = view ? view->GetTexture() : nil;
        }
        auto* depth = static_cast<MetalTextureView*>(dsv);
        m_State->depth = depth ? depth->GetTexture() : nil;
        m_DepthClear = false;
    }

    void MetalCommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        id<MTLTexture> texture = rtv ? static_cast<MetalTextureView*>(rtv)->GetTexture() : nil;
        for (uint32_t i = 0; i < m_State->colorCount; ++i)
        {
            if (m_State->colors[i] != texture)
                continue;
            if (m_State->encoder)
                EndEncoder();
            m_ColorClear[i] = true;
            m_ClearColor[i] = MTLClearColorMake(color.R, color.G, color.B, color.A);
        }
    }

    void MetalCommandContext::ClearDepthStencilView(RHITextureView*, const ClearDepthStencilValue& value, uint8_t clearFlags)
    {
        if ((clearFlags & 0x1) == 0 || !m_State->depth)
            return;
        if (m_State->encoder)
            EndEncoder();
        m_DepthClear = true;
        m_ClearDepth = value.Depth;
    }

    void MetalCommandContext::SetVertexBuffers(uint32_t, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count)
    {
        m_VertexBuffer = nil;
        m_VertexStride = 0;
        if (!count || !buffers || !buffers[0])
            return;
        m_VertexBuffer = static_cast<MetalBuffer*>(buffers[0])->GetBuffer();
        if (views)
            m_VertexStride = views[0].StrideInBytes;
    }

    void MetalCommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        m_IndexBuffer = buffer ? static_cast<MetalBuffer*>(buffer)->GetBuffer() : nil;
        m_IndexType = (view && view->Format == EFormat::R16_UINT) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        m_IndexStride = m_IndexType == MTLIndexTypeUInt16 ? 2u : 4u;
    }

    void MetalCommandContext::BindBytes(uint32_t slot, const void* data, uint32_t size)
    {
        if (slot >= kMaxSlots || !data || size < 4 || !m_State->constantHeap)
            return;
        size -= size % 4;
        uint32_t aligned = (size + 255u) & ~255u;
        if (m_State->constantCursor + aligned > m_State->constantHeap.length)
        {
            std::cerr << "[Kiwi Metal] Constant heap exhausted." << std::endl;
            return;
        }
        auto* dst = static_cast<uint8_t*>(m_State->constantHeap.contents) + m_State->constantCursor;
        std::memcpy(dst, data, size);
        if (aligned > size)
            std::memset(dst + size, 0, aligned - size);
        m_Constants[slot].Valid = true;
        m_Constants[slot].Offset = m_State->constantCursor;
        m_State->constantCursor += aligned;
    }

    void MetalCommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        if (!buffer)
            return;
        auto* metal = static_cast<MetalBuffer*>(buffer);
        uint32_t size = metal->GetDesc().SizeInBytes;
        if ((NSUInteger)size > metal->GetBuffer().length)
            size = (uint32_t)metal->GetBuffer().length;
        BindBytes(slot, metal->GetBuffer().contents, size);
    }

    void MetalCommandContext::SetConstantBufferOffset(uint32_t slot, RHIBuffer* buffer,
        uint32_t offsetIn16Constants, uint32_t sizeIn16Constants)
    {
        if (!buffer)
            return;
        auto* metal = static_cast<MetalBuffer*>(buffer);
        uint32_t offset = offsetIn16Constants * 16;
        uint32_t size = sizeIn16Constants * 16;
        if ((NSUInteger)offset + size > metal->GetBuffer().length)
            return;
        auto* contents = static_cast<const uint8_t*>(metal->GetBuffer().contents);
        BindBytes(slot, contents + offset, size);
    }

    void MetalCommandContext::SetShaderResourceView(uint32_t slot, RHITextureView* srv)
    {
        if (slot >= kMaxSlots)
            return;
        if (!srv)
        {
            m_Textures[slot] = nil;
            m_StorageBuffers[slot] = nil;
            return;
        }
        auto* view = static_cast<MetalTextureView*>(srv);
        m_Textures[slot] = view->GetTexture();
        m_StorageBuffers[slot] = view->GetBuffer();
    }

    void MetalCommandContext::SetSampler(uint32_t slot, RHISampler* sampler)
    {
        if (slot >= kMaxSlots)
            return;
        m_Samplers[slot] = sampler ? static_cast<MetalSampler*>(sampler)->GetSampler() : nil;
    }

    void MetalCommandContext::SetViewports(const Viewport* viewports, uint32_t count)
    {
        if (!viewports || !count)
            return;
        m_ViewportValid = true;
        m_Viewport.originX = viewports[0].TopLeftX;
        m_Viewport.originY = viewports[0].TopLeftY;
        m_Viewport.width = viewports[0].Width;
        m_Viewport.height = viewports[0].Height;
        m_Viewport.znear = viewports[0].MinDepth;
        m_Viewport.zfar = viewports[0].MaxDepth;
    }

    void MetalCommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        if (!rects || !count)
            return;
        m_ScissorValid = true;
        m_Scissor.x = (NSUInteger)std::max(0, rects[0].Left);
        m_Scissor.y = (NSUInteger)std::max(0, rects[0].Top);
        m_Scissor.width = (NSUInteger)std::max(0, rects[0].Right - rects[0].Left);
        m_Scissor.height = (NSUInteger)std::max(0, rects[0].Bottom - rects[0].Top);
    }

    bool MetalCommandContext::EnsureEncoder()
    {
        if (m_State->encoder)
            return true;
        if (!m_State->commandBuffer)
            return false;

        auto* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        bool any = false;
        for (uint32_t i = 0; i < m_State->colorCount; ++i)
        {
            if (!m_State->colors[i])
                continue;
            any = true;
            desc.colorAttachments[i].texture = m_State->colors[i];
            desc.colorAttachments[i].loadAction = m_ColorClear[i] ? MTLLoadActionClear : MTLLoadActionLoad;
            desc.colorAttachments[i].storeAction = MTLStoreActionStore;
            if (m_ColorClear[i])
                desc.colorAttachments[i].clearColor = m_ClearColor[i];
            m_ColorClear[i] = false;
        }
        if (m_State->depth)
        {
            any = true;
            desc.depthAttachment.texture = m_State->depth;
            desc.depthAttachment.loadAction = m_DepthClear ? MTLLoadActionClear : MTLLoadActionLoad;
            desc.depthAttachment.storeAction = MTLStoreActionStore;
            desc.depthAttachment.clearDepth = m_ClearDepth;
            m_DepthClear = false;
        }
        if (!any)
            return false;

        m_State->encoder = [m_State->commandBuffer renderCommandEncoderWithDescriptor:desc];
        for (const auto& event : m_Events)
            [m_State->encoder pushDebugGroup:@(event.c_str())];
        return m_State->encoder != nil;
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
        for (uint32_t i = 0; i < m_State->colorCount; ++i)
            limitTo(m_State->colors[i]);
        limitTo(m_State->depth);
        if (width == 0) width = 1;
        if (height == 0) height = 1;

        MTLViewport viewport = m_ViewportValid ? m_Viewport : MTLViewport{ 0, 0, (double)width, (double)height, 0.0, 1.0 };
        if (viewport.originX < 0.0) viewport.originX = 0.0;
        if (viewport.originY < 0.0) viewport.originY = 0.0;
        if (viewport.originX + viewport.width > (double)width)
            viewport.width = std::max(1.0, (double)width - viewport.originX);
        if (viewport.originY + viewport.height > (double)height)
            viewport.height = std::max(1.0, (double)height - viewport.originY);
        [m_State->encoder setViewport:viewport];

        MTLScissorRect scissor = m_ScissorValid ? m_Scissor : MTLScissorRect{ 0, 0, width, height };
        if (scissor.x >= width) scissor.x = 0;
        if (scissor.y >= height) scissor.y = 0;
        if (scissor.x + scissor.width > width) scissor.width = width - scissor.x;
        if (scissor.y + scissor.height > height) scissor.height = height - scissor.y;
        if (scissor.width == 0 || scissor.height == 0)
            scissor = MTLScissorRect{ 0, 0, width, height };
        [m_State->encoder setScissorRect:scissor];
    }

    bool MetalCommandContext::ApplyPipeline()
    {
        ApplyViewport();
        if (!m_PSO || !m_PSO->VertexFunction)
            return false;

        MTLPixelFormat colors[kMaxColors] = {};
        for (uint32_t i = 0; i < m_State->colorCount; ++i)
            colors[i] = m_State->colors[i] ? m_State->colors[i].pixelFormat : MTLPixelFormatInvalid;
        MTLPixelFormat depthFormat = m_State->depth ? m_State->depth.pixelFormat : MTLPixelFormatInvalid;
        id<MTLRenderPipelineState> pipeline = m_PSO->GetOrCreate(
            m_State->device, colors, m_State->colorCount, depthFormat,
            m_VertexStride ? m_VertexStride : m_PSO->LayoutStride);
        if (!pipeline)
            return false;

        [m_State->encoder setRenderPipelineState:pipeline];
        const RasterizerStateDesc& raster = m_PSO->Initializer.RasterizerState;
        ERasterizerCullMode winding = MetalDiscardWinding(m_CullOverride ? m_CullMode : raster.CullMode);
        [m_State->encoder setFrontFacingWinding:MTLWindingClockwise];
        MTLCullMode cullMode = MTLCullModeNone;
        switch (winding)
        {
        case ERasterizerCullMode::CW:  cullMode = MTLCullModeFront; break;
        case ERasterizerCullMode::CCW: cullMode = MTLCullModeBack; break;
        default: break;
        }
        [m_State->encoder setCullMode:cullMode];
        [m_State->encoder setTriangleFillMode:raster.FillMode == ERasterizerFillMode::Wireframe
            ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        [m_State->encoder setDepthBias:raster.DepthBias slopeScale:raster.SlopeScaleDepthBias clamp:0.0f];
        if (m_State->depth && m_PSO->Initializer.DepthEnabled)
            [m_State->encoder setDepthStencilState:m_PSO->Initializer.DepthWrite ? m_State->depthWrite : m_State->depthRead];
        else
            [m_State->encoder setDepthStencilState:m_State->depthOff];

        if (m_VertexBuffer)
            [m_State->encoder setVertexBuffer:m_VertexBuffer offset:0 atIndex:kVertexBufferIndex];
        for (uint32_t slot = 0; slot < kMaxSlots; ++slot)
        {
            if (m_Constants[slot].Valid)
            {
                [m_State->encoder setVertexBuffer:m_State->constantHeap offset:m_Constants[slot].Offset atIndex:slot];
                [m_State->encoder setFragmentBuffer:m_State->constantHeap offset:m_Constants[slot].Offset atIndex:slot];
            }
            if (m_StorageBuffers[slot])
                [m_State->encoder setVertexBuffer:m_StorageBuffers[slot] offset:0 atIndex:slot];
            if (m_Textures[slot])
            {
                [m_State->encoder setVertexTexture:m_Textures[slot] atIndex:slot];
                [m_State->encoder setFragmentTexture:m_Textures[slot] atIndex:slot];
            }
            if (m_Samplers[slot])
            {
                [m_State->encoder setVertexSamplerState:m_Samplers[slot] atIndex:slot];
                [m_State->encoder setFragmentSamplerState:m_Samplers[slot] atIndex:slot];
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
        [m_State->encoder drawPrimitives:m_Topology vertexStart:vertexStart vertexCount:vertexCount];
    }

    void MetalCommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        DrawIndexedInstanced(indexCount, 1, indexStart, vertexOffset, 0);
    }

    void MetalCommandContext::DrawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount,
        uint32_t startIndex, int32_t baseVertex, uint32_t startInstance)
    {
        if (!m_IndexBuffer || !instanceCount || !EnsureEncoder() || !ApplyPipeline())
            return;
        [m_State->encoder drawIndexedPrimitives:m_Topology
            indexCount:indexCountPerInstance indexType:m_IndexType indexBuffer:m_IndexBuffer
            indexBufferOffset:(NSUInteger)startIndex * m_IndexStride instanceCount:instanceCount
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
