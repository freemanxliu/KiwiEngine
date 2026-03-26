#pragma once

#include "RHI/RHI.h"
#include "RHI/Vulkan/VulkanHeaders.h"
#include <vector>
#include <queue>

namespace Kiwi
{

    // ============================================================
    // Vulkan helper: Format conversion
    // ============================================================

    inline VkFormat VulkanToVkFormat(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:     return VK_FORMAT_R8G8B8A8_UNORM;
        case EFormat::R16G16B16A16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case EFormat::R16G16_FLOAT:        return VK_FORMAT_R16G16_SFLOAT;
        case EFormat::R32G32B32A32_FLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case EFormat::R32G32B32_FLOAT:     return VK_FORMAT_R32G32B32_SFLOAT;
        case EFormat::R32G32_FLOAT:        return VK_FORMAT_R32G32_SFLOAT;
        case EFormat::R32_FLOAT:           return VK_FORMAT_R32_SFLOAT;
        case EFormat::R32_UINT:            return VK_FORMAT_R32_UINT;
        case EFormat::R16_UINT:            return VK_FORMAT_R16_UINT;
        case EFormat::D24_UNORM_S8_UINT:   return VK_FORMAT_D24_UNORM_S8_UINT;
        case EFormat::D32_FLOAT:           return VK_FORMAT_D32_SFLOAT;
        case EFormat::R32_TYPELESS:        return VK_FORMAT_D32_SFLOAT;  // Typeless → depth float
        default:                           return VK_FORMAT_UNDEFINED;
        }
    }

    inline VkPrimitiveTopology VulkanToVkTopology(EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case EPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case EPrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case EPrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case EPrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    // ============================================================
    // Vulkan Resources
    // ============================================================

    class VulkanBuffer : public RHIBuffer
    {
    public:
        VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                     const BufferDesc& desc, void* mappedPtr = nullptr)
            : m_Device(device), m_Buffer(buffer), m_Memory(memory)
            , m_Desc(desc), m_MappedPtr(mappedPtr) {}

        ~VulkanBuffer() override;

        void* GetNativeHandle() const override { return (void*)m_Buffer; }
        const BufferDesc& GetDesc() const override { return m_Desc; }

        void* Map(uint32_t subresource = 0) override;
        void  Unmap(uint32_t subresource = 0) override;
        void  UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override;

        VkBuffer GetVkBuffer() const { return m_Buffer; }
        VkDeviceMemory GetVkMemory() const { return m_Memory; }

    private:
        VkDevice m_Device;
        VkBuffer m_Buffer;
        VkDeviceMemory m_Memory;
        BufferDesc m_Desc;
        void* m_MappedPtr = nullptr;
    };

    class VulkanTexture : public RHITexture
    {
    public:
        VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory,
                      const TextureDesc& desc, bool ownsImage = true)
            : m_Device(device), m_Image(image), m_Memory(memory)
            , m_Desc(desc), m_OwnsImage(ownsImage) {}

        ~VulkanTexture() override;

        void* GetNativeHandle() const override { return (void*)m_Image; }
        const TextureDesc& GetDesc() const override { return m_Desc; }
        VkImage GetVkImage() const { return m_Image; }

    private:
        VkDevice m_Device;
        VkImage m_Image;
        VkDeviceMemory m_Memory;
        TextureDesc m_Desc;
        bool m_OwnsImage;
    };

    class VulkanTextureView : public RHITextureView
    {
    public:
        VulkanTextureView(VkDevice device, VkImageView view)
            : m_Device(device), m_ImageView(view) {}

        ~VulkanTextureView() override;

        void* GetNativeHandle() const override { return (void*)m_ImageView; }
        VkImageView GetVkImageView() const { return m_ImageView; }

    private:
        VkDevice m_Device;
        VkImageView m_ImageView;
    };

    class VulkanShader : public RHIShader
    {
    public:
        VulkanShader(VkDevice device, VkShaderModule module, EShaderType type,
                     const std::vector<uint32_t>& spirvCode)
            : m_Device(device), m_Module(module), m_Type(type), m_SPIRVCode(spirvCode) {}

        ~VulkanShader() override;

        void* GetNativeHandle() const override { return (void*)m_Module; }
        EShaderType GetType() const override { return m_Type; }
        VkShaderModule GetVkShaderModule() const { return m_Module; }
        const std::vector<uint32_t>& GetSPIRVCode() const { return m_SPIRVCode; }

    private:
        VkDevice m_Device;
        VkShaderModule m_Module;
        EShaderType m_Type;
        std::vector<uint32_t> m_SPIRVCode;
    };

    class VulkanInputLayout : public RHIInputLayout
    {
    public:
        VulkanInputLayout(const std::vector<VkVertexInputAttributeDescription>& attributes,
                          const VkVertexInputBindingDescription& binding)
            : m_Attributes(attributes), m_Binding(binding) {}

        void* GetNativeHandle() const override { return (void*)m_Attributes.data(); }
        const std::vector<VkVertexInputAttributeDescription>& GetAttributes() const { return m_Attributes; }
        const VkVertexInputBindingDescription& GetBinding() const { return m_Binding; }

    private:
        std::vector<VkVertexInputAttributeDescription> m_Attributes;
        VkVertexInputBindingDescription m_Binding;
    };

    class VulkanPipelineState : public RHIPipelineState
    {
    public:
        VulkanPipelineState(VkDevice device, VkPipeline pipeline)
            : m_Device(device), m_Pipeline(pipeline) {}

        ~VulkanPipelineState() override;

        void* GetNativeHandle() const override { return (void*)m_Pipeline; }
        VkPipeline GetVkPipeline() const { return m_Pipeline; }

    private:
        VkDevice m_Device;
        VkPipeline m_Pipeline;
    };

    class VulkanSampler : public RHISampler
    {
    public:
        VulkanSampler(VkDevice device, VkSampler sampler)
            : m_Device(device), m_Sampler(sampler) {}

        ~VulkanSampler() override;

        void* GetNativeHandle() const override { return (void*)m_Sampler; }

    private:
        VkDevice m_Device;
        VkSampler m_Sampler;
    };

    // ============================================================
    // Vulkan SwapChain
    // ============================================================

    class VulkanSwapChain : public RHISwapChain
    {
    public:
        VulkanSwapChain(VkDevice device, VkPhysicalDevice physicalDevice,
                        VkSurfaceKHR surface, VkQueue presentQueue,
                        const SwapChainDesc& desc);
        ~VulkanSwapChain() override;

        void* GetNativeHandle() const override { return (void*)m_SwapChain; }
        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override;
        RHITexture* GetBackBuffer(uint32_t index) override;
        RHITextureView* GetBackBufferRTV(uint32_t index) override;

        // Vulkan-specific
        VkSwapchainKHR GetVkSwapChain() const { return m_SwapChain; }
        VkFormat GetSwapChainFormat() const { return m_SwapChainFormat; }
        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkFramebuffer GetCurrentFramebuffer() const;
        VkExtent2D GetExtent() const { return m_Extent; }

        // Semaphores for frame sync
        VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphore; }
        VkSemaphore GetRenderFinishedSemaphore() const { return m_RenderFinishedSemaphore; }

        // Acquire next image
        void AcquireNextImage();

    private:
        void CreateSwapChainResources();
        void CleanupSwapChain();
        void CreateRenderPass();

        VkDevice m_Device;
        VkPhysicalDevice m_PhysicalDevice;
        VkSurfaceKHR m_Surface;
        VkQueue m_PresentQueue;
        SwapChainDesc m_Desc;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        VkFormat m_SwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D m_Extent = {};
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;

        uint32_t m_CurrentImageIndex = 0;

        std::vector<std::unique_ptr<VulkanTexture>>     m_BackBuffers;
        std::vector<std::unique_ptr<VulkanTextureView>> m_BackBufferViews;
        std::vector<VkFramebuffer>                      m_Framebuffers;

        VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    };

    // ============================================================
    // Vulkan Device
    // ============================================================

    class VulkanDevice : public RHIDevice
    {
    public:
        VulkanDevice(bool enableDebug);
        ~VulkanDevice() override;

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::VULKAN; }
        void* GetNativeDevice() const override { return (void*)m_Device; }
        void* GetImmediateContext() const override { return (void*)m_GraphicsQueue; }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITextureView> CreateTextureView(RHITexture* texture, EDescriptorHeapType heapType,
            EFormat format = EFormat::Unknown, int mipSlice = -1, int arraySlice = -1) override;
        std::unique_ptr<RHIShader> CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize) override;
        std::unique_ptr<RHIInputLayout> CreateInputLayout(const InputElementDesc* elements, uint32_t elementCount,
            RHIShader* vertexShader) override;
        std::unique_ptr<RHIPipelineState> CreatePipelineState() override;
        std::unique_ptr<RHISampler> CreateSampler() override;

        // CompileShader — Vulkan uses GLSL source, compiles via glslang-style or treats as SPIR-V passthrough
        std::unique_ptr<RHIShader> CompileShader(
            EShaderType type, const char* source, const char* entryPoint,
            const char* shaderModel, const ShaderMacro* macros = nullptr,
            uint32_t macroCount = 0) override;

        // CreateGraphicsPipelineState — creates real VkPipeline
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader, RHIShader* pixelShader,
            RHIInputLayout* inputLayout) override;

        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader, RHIShader* pixelShader,
            RHIInputLayout* inputLayout,
            const PipelineStateDesc& pipelineDesc) override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        // ImGui integration
        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(RHICommandContext* ctx) override;

        // Compile from pre-compiled SPIR-V
        std::unique_ptr<RHIShader> CompileShaderFromSPIRV(EShaderType type,
            const uint32_t* spirvCode, size_t spirvSize);

        // Vulkan native access
        VkInstance GetVkInstance() const { return m_Instance; }
        VkPhysicalDevice GetVkPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetVkDevice() const { return m_Device; }
        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }

        // Pipeline layout & descriptor set layout
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

        // Get the main render pass (for pipeline creation)
        VkRenderPass GetMainRenderPass() const { return m_MainRenderPass; }
        void SetMainRenderPass(VkRenderPass rp) { m_MainRenderPass = rp; }

        // Allocate descriptor set for constant buffer
        VkDescriptorSet AllocateDescriptorSet();

        // Memory helpers
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        // Wait idle
        void WaitIdle();

    private:
        void CreateInstance(bool enableDebug);
        void SelectPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandPool();
        void CreateDescriptorPoolAndLayout();
        void CreatePipelineLayout();

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamily = 0;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkRenderPass m_MainRenderPass = VK_NULL_HANDLE;  // cached from SwapChain
        bool m_EnableDebug;

        VkSurfaceKHR m_LastSurface = VK_NULL_HANDLE; // cached for queue family check

    public:
        // Expose for SwapChain creation
        void SetSurface(VkSurfaceKHR surface) { m_LastSurface = surface; }
    };

    // ============================================================
    // Vulkan Command Context
    // ============================================================

    class VulkanCommandContext : public RHICommandContext
    {
    public:
        VulkanCommandContext(VkDevice device, VkCommandPool commandPool,
                            VkQueue graphicsQueue);
        ~VulkanCommandContext() override;

        void* GetNativeHandle() const override { return (void*)m_CommandBuffer; }

        // Frame lifecycle
        void BeginFrame(RHISwapChain* swapChain) override;
        void EndFrame(RHISwapChain* swapChain) override;

        // Resource barriers
        void ResourceBarrier(RHITexture* texture, int stateBefore, int stateAfter) override;

        // Render Targets
        void SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv = nullptr) override;
        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(RHITextureView* dsv, const ClearDepthStencilValue& value, uint8_t clearFlags) override;

        // Pipeline State
        void SetPipelineState(RHIPipelineState* pso) override;

        // Graphics Pipeline
        void SetPrimitiveTopology(EPrimitiveTopology topology) override;
        void SetVertexBuffers(uint32_t startSlot, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count) override;
        void SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view) override;
        void SetVertexShader(RHIShader* shader) override;
        void SetPixelShader(RHIShader* shader) override;
        void SetGeometryShader(RHIShader* shader) override;
        void SetInputLayout(RHIInputLayout* layout) override;

        // Constant Buffer
        void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) override;

        // Shader Resource View
        void SetShaderResourceView(uint32_t slot, RHITextureView* srv) override;

        // Sampler
        void SetSampler(uint32_t slot, RHISampler* sampler) override;

        // Viewport and Scissor
        void SetViewports(const Viewport* viewports, uint32_t count) override;
        void SetScissorRects(const ScissorRect* rects, uint32_t count) override;

        // Draw
        void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) override;
        void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) override;

        // Flush (submit & wait)
        void Flush() override;

        // Vulkan-specific
        VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }
        void Reset();
        void BeginCommandBuffer();
        void EndCommandBuffer();
        void Submit();

        // Render pass management
        void BeginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                             VkExtent2D extent, const VkClearValue* clearValues, uint32_t clearCount);
        void EndRenderPass();

        // Descriptor set binding
        void BindDescriptorSet(VkPipelineLayout layout, VkDescriptorSet descriptorSet);

    private:
        VkDevice m_Device;
        VkCommandPool m_CommandPool;
        VkQueue m_GraphicsQueue;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        VkFence m_Fence = VK_NULL_HANDLE;
        bool m_IsRecording = false;
        bool m_InRenderPass = false;
    };

} // namespace Kiwi
