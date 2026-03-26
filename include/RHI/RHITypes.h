#pragma once

#include <cstdint>
#include <string>

namespace Kiwi
{

    // RHI API 类型枚举，方便扩展 DX12/Vulkan 等
    enum class RHI_API_TYPE
    {
        DX11,
        DX12,
        OPENGL,
        VULKAN,
    };

    // 图形资源格式
    enum class EFormat
    {
        Unknown = 0,
        R8G8B8A8_UNORM,
        R16G16B16A16_FLOAT,   // HDR render targets (G-Buffer, etc.)
        R16G16_FLOAT,          // 2-channel half-float
        R32G32B32A32_FLOAT,
        R32G32B32_FLOAT,
        R32G32_FLOAT,
        R32_FLOAT,
        R32_UINT,
        R16_UINT,
        D24_UNORM_S8_UINT,
        D32_FLOAT,
        R32_TYPELESS,          // Typeless format for depth+SRV dual use (shadow maps)
    };

    // 缓冲绑定标志
    enum EBufferUsageFlags
    {
        BUFFER_USAGE_VERTEX    = 1 << 0,
        BUFFER_USAGE_INDEX     = 1 << 1,
        BUFFER_USAGE_CONSTANT  = 1 << 2,
        BUFFER_USAGE_UNORDERED = 1 << 3,
    };

    // 纹理绑定标志（用于 TextureDesc::BindFlags）
    enum ETextureBindFlags
    {
        TEXTURE_BIND_SHADER_RESOURCE  = 1 << 0,  // 可作为 SRV 绑定
        TEXTURE_BIND_RENDER_TARGET    = 1 << 1,  // 可作为 RTV 绑定
        TEXTURE_BIND_UNORDERED_ACCESS = 1 << 2,  // 可作为 UAV 绑定
    };

    // 资源状态（用于 ResourceBarrier，映射到 D3D12_RESOURCE_STATES）
    enum EResourceState : int
    {
        RESOURCE_STATE_COMMON              = 0,
        RESOURCE_STATE_RENDER_TARGET       = 0x4,
        RESOURCE_STATE_DEPTH_WRITE         = 0x10,
        RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
        RESOURCE_STATE_PRESENT             = 0,
    };

    // 资源使用方式
    enum class EResourceUsage
    {
        Default,
        Immutable,
        Dynamic,
        Staging,
    };

    // 图元拓扑类型
    enum class EPrimitiveTopology
    {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList,
    };

    // 着色器类型
    enum class EShaderType
    {
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute,
    };

    // 描述符堆类型
    enum class EDescriptorHeapType
    {
        CBV_SRV_UAV,
        Sampler,
        RTV,
        DSV,
    };

    // 输入元素描述
    struct InputElementDesc
    {
        const char* SemanticName;
        uint32_t    SemanticIndex;
        EFormat     Format;
        uint32_t    AlignedByteOffset;
        uint32_t    InputSlot;
        uint32_t    InstanceDataStepRate;
    };

    // 顶点缓冲视图
    struct VertexBufferView
    {
        uint64_t BufferLocation;
        uint32_t SizeInBytes;
        uint32_t StrideInBytes;
    };

    // 索引缓冲视图
    struct IndexBufferView
    {
        uint64_t BufferLocation;
        uint32_t SizeInBytes;
        EFormat  Format;
    };

    // 视口
    struct Viewport
    {
        float TopLeftX;
        float TopLeftY;
        float Width;
        float Height;
        float MinDepth;
        float MaxDepth;
    };

    // 裁剪矩形
    struct ScissorRect
    {
        int32_t Left;
        int32_t Top;
        int32_t Right;
        int32_t Bottom;
    };

    // 纹理描述
    struct TextureDesc
    {
        uint32_t      Width          = 0;
        uint32_t      Height         = 0;
        uint32_t      DepthOrArray   = 1;
        uint32_t      MipLevels      = 1;
        EFormat       Format         = EFormat::Unknown;
        uint32_t      BindFlags      = 0;
        EResourceUsage Usage          = EResourceUsage::Default;
        uint32_t      SampleCount    = 1;
        const char*   DebugName      = nullptr;  // GPU debug name (RenderDoc / PIX)
    };

    // 缓冲描述
    struct BufferDesc
    {
        uint32_t      SizeInBytes    = 0;
        uint32_t      BindFlags      = 0;
        EResourceUsage Usage          = EResourceUsage::Default;
        uint32_t      StructByteStride = 0;
        const char*   DebugName      = nullptr;  // GPU debug name (RenderDoc / PIX)
    };

    // 着色器宏定义
    struct ShaderMacro
    {
        const char* Name;
        const char* Definition;
    };

    // 清除颜色
    struct ClearColorValue
    {
        float R, G, B, A;
    };

    // 深度清除值
    struct ClearDepthStencilValue
    {
        float Depth;
        uint8_t Stencil;
    };

    // 管线状态描述（用于 MRT PSO 创建）
    struct PipelineStateDesc
    {
        uint32_t NumRenderTargets = 1;
        EFormat RTVFormats[8] = { EFormat::R8G8B8A8_UNORM };
        EFormat DSVFormat = EFormat::D32_FLOAT;
        bool DepthEnabled = true;
        bool DepthWrite = true;
    };

} // namespace Kiwi
