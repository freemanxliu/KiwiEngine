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
            : Device(device), Buffer(buffer), Memory(memory)
            , Desc(desc), MappedPtr(mappedPtr) {}

        ~VulkanBuffer() override;

        void* GetNativeHandle() const override { return (void*)Buffer; }
        const BufferDesc& GetDesc() const override { return Desc; }

        void* Map(uint32_t subresource = 0) override;
        void  Unmap(uint32_t subresource = 0) override;
        void  UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override;

        VkBuffer GetVkBuffer() const { return Buffer; }
        VkDeviceMemory GetVkMemory() const { return Memory; }

    private:
        VkDevice Device;
        VkBuffer Buffer;
        VkDeviceMemory Memory;
        BufferDesc Desc;
        void* MappedPtr = nullptr;
    };

    class VulkanTexture : public RHITexture
    {
    public:
        VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory,
                      const TextureDesc& desc, bool ownsImage = true)
            : Device(device), Image(image), Memory(memory)
            , Desc(desc), OwnsImage(ownsImage) {}

        ~VulkanTexture() override;

        void* GetNativeHandle() const override { return (void*)Image; }
        const TextureDesc& GetDesc() const override { return Desc; }
        VkImage GetVkImage() const { return Image; }

    private:
        VkDevice Device;
        VkImage Image;
        VkDeviceMemory Memory;
        TextureDesc Desc;
        bool OwnsImage;
    };

    class VulkanTextureView : public RHITextureView
    {
    public:
        VulkanTextureView(VkDevice device, VkImageView view)
            : Device(device), ImageView(view) {}

        ~VulkanTextureView() override;

        void* GetNativeHandle() const override { return (void*)ImageView; }
        VkImageView GetVkImageView() const { return ImageView; }

    private:
        VkDevice Device;
        VkImageView ImageView;
    };

    class VulkanShader : public RHIShader
    {
    public:
        VulkanShader(VkDevice device, VkShaderModule module, EShaderType type,
                     const std::vector<uint32_t>& spirvCode)
            : Device(device), Module(module), Type(type), SPIRVCode(spirvCode) {}

        ~VulkanShader() override;

        void* GetNativeHandle() const override { return (void*)Module; }
        EShaderType GetType() const override { return Type; }
        VkShaderModule GetVkShaderModule() const { return Module; }
        const std::vector<uint32_t>& GetSPIRVCode() const { return SPIRVCode; }

    private:
        VkDevice Device;
        VkShaderModule Module;
        EShaderType Type;
        std::vector<uint32_t> SPIRVCode;
    };

    class VulkanInputLayout : public RHIInputLayout
    {
    public:
        VulkanInputLayout(const std::vector<VkVertexInputAttributeDescription>& attributes,
                          const VkVertexInputBindingDescription& binding)
            : Attributes(attributes), Binding(binding) {}

        void* GetNativeHandle() const override { return (void*)Attributes.data(); }
        const std::vector<VkVertexInputAttributeDescription>& GetAttributes() const { return Attributes; }
        const VkVertexInputBindingDescription& GetBinding() const { return Binding; }

    private:
        std::vector<VkVertexInputAttributeDescription> Attributes;
        VkVertexInputBindingDescription Binding;
    };

    class VulkanPipelineState : public RHIPipelineState
    {
    public:
        VulkanPipelineState(VkDevice device, VkPipeline pipeline)
            : Device(device), Pipeline(pipeline) {}

        ~VulkanPipelineState() override;

        void* GetNativeHandle() const override { return (void*)Pipeline; }
        VkPipeline GetVkPipeline() const { return Pipeline; }

    private:
        VkDevice Device;
        VkPipeline Pipeline;
    };

    class VulkanSampler : public RHISampler
    {
    public:
        VulkanSampler(VkDevice device, VkSampler sampler)
            : Device(device), Sampler(sampler) {}

        ~VulkanSampler() override;

        void* GetNativeHandle() const override { return (void*)Sampler; }

    private:
        VkDevice Device;
        VkSampler Sampler;
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

        void* GetNativeHandle() const override { return (void*)SwapChain; }
        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override;
        RHITexture* GetBackBuffer(uint32_t index) override;
        RHITextureView* GetBackBufferRTV(uint32_t index) override;

        // Vulkan-specific
        VkSwapchainKHR GetVkSwapChain() const { return SwapChain; }
        VkFormat GetSwapChainFormat() const { return SwapChainFormat; }
        VkRenderPass GetRenderPass() const { return RenderPass; }
        VkFramebuffer GetCurrentFramebuffer() const;
        VkExtent2D GetExtent() const { return Extent; }

        // Semaphores for frame sync
        VkSemaphore GetImageAvailableSemaphore() const { return ImageAvailableSemaphore; }
        VkSemaphore GetRenderFinishedSemaphore() const { return RenderFinishedSemaphore; }

        // Acquire next image
        void AcquireNextImage();

    private:
        void CreateSwapChainResources();
        void CleanupSwapChain();
        void CreateRenderPass();

        VkDevice Device;
        VkPhysicalDevice PhysicalDevice;
        VkSurfaceKHR Surface;
        VkQueue PresentQueue;
        SwapChainDesc Desc;

        VkSwapchainKHR SwapChain = VK_NULL_HANDLE;
        VkFormat SwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D Extent = {};
        VkRenderPass RenderPass = VK_NULL_HANDLE;

        uint32_t CurrentImageIndex = 0;

        std::vector<std::unique_ptr<VulkanTexture>>     BackBuffers;
        std::vector<std::unique_ptr<VulkanTextureView>> BackBufferViews;
        std::vector<VkFramebuffer>                      Framebuffers;

        VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore RenderFinishedSemaphore = VK_NULL_HANDLE;
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
        void* GetNativeDevice() const override { return (void*)Device; }
        void* GetImmediateContext() const override { return (void*)GraphicsQueue; }

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

        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            const GraphicsPipelineStateInitializer& initializer) override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        // ImGui integration
        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiUpdateTextures(ImDrawData* DrawData) override;
        void ImGuiRenderDrawData(RHICommandContext* Ctx, ImDrawData* DrawData) override;

        // Compile from pre-compiled SPIR-V
        std::unique_ptr<RHIShader> CompileShaderFromSPIRV(EShaderType type,
            const uint32_t* spirvCode, size_t spirvSize);

        // Vulkan native access
        VkInstance GetVkInstance() const { return Instance; }
        VkPhysicalDevice GetVkPhysicalDevice() const { return PhysicalDevice; }
        VkDevice GetVkDevice() const { return Device; }
        VkQueue GetGraphicsQueue() const { return GraphicsQueue; }
        uint32_t GetGraphicsQueueFamily() const { return GraphicsQueueFamily; }
        VkCommandPool GetCommandPool() const { return CommandPool; }
        VkDescriptorPool GetDescriptorPool() const { return DescriptorPool; }

        // Pipeline layout & descriptor set layout
        VkPipelineLayout GetPipelineLayout() const { return PipelineLayout; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return DescriptorSetLayout; }

        // Get the main render pass (for pipeline creation)
        VkRenderPass GetMainRenderPass() const { return MainRenderPass; }
        void SetMainRenderPass(VkRenderPass rp) { MainRenderPass = rp; }

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

        VkInstance Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkDevice Device = VK_NULL_HANDLE;
        VkQueue GraphicsQueue = VK_NULL_HANDLE;
        uint32_t GraphicsQueueFamily = 0;
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
        VkRenderPass MainRenderPass = VK_NULL_HANDLE;  // cached from SwapChain
        bool EnableDebug;

        VkSurfaceKHR LastSurface = VK_NULL_HANDLE; // cached for queue family check

    public:
        // Expose for SwapChain creation
        void SetSurface(VkSurfaceKHR surface) { LastSurface = surface; }
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

        void* GetNativeHandle() const override { return (void*)CommandBuffer; }

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
        VkCommandBuffer GetCommandBuffer() const { return CommandBuffer; }
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
        VkDevice Device;
        VkCommandPool CommandPool;
        VkQueue GraphicsQueue;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        bool IsRecording = false;
        bool InRenderPass = false;
    };

} // namespace Kiwi
