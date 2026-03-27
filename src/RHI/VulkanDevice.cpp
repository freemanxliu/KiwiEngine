#include "RHI/Vulkan/VulkanDevice.h"
#include <stdexcept>
#include <vector>
#include <cstring>
#include <iostream>
#include <set>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_win32.h>

namespace Kiwi
{

    // ============================================================
    // VulkanBuffer
    // ============================================================

    VulkanBuffer::~VulkanBuffer()
    {
        if (m_MappedPtr)
        {
            vkUnmapMemory(m_Device, m_Memory);
            m_MappedPtr = nullptr;
        }
        if (m_Buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(m_Device, m_Buffer, nullptr);
        if (m_Memory != VK_NULL_HANDLE)
            vkFreeMemory(m_Device, m_Memory, nullptr);
    }

    void* VulkanBuffer::Map(uint32_t subresource)
    {
        if (m_MappedPtr) return m_MappedPtr;
        VkResult result = vkMapMemory(m_Device, m_Memory, 0, m_Desc.SizeInBytes, 0, &m_MappedPtr);
        if (result != VK_SUCCESS) return nullptr;
        return m_MappedPtr;
    }

    void VulkanBuffer::Unmap(uint32_t subresource)
    {
        if (m_MappedPtr)
        {
            vkUnmapMemory(m_Device, m_Memory);
            m_MappedPtr = nullptr;
        }
    }

    void VulkanBuffer::UpdateData(const void* data, uint32_t size, uint32_t offset)
    {
        void* mapped = Map();
        if (mapped)
        {
            memcpy((uint8_t*)mapped + offset, data, size);
            Unmap();
        }
    }

    // ============================================================
    // VulkanTexture
    // ============================================================

    VulkanTexture::~VulkanTexture()
    {
        if (m_OwnsImage && m_Image != VK_NULL_HANDLE)
            vkDestroyImage(m_Device, m_Image, nullptr);
        if (m_Memory != VK_NULL_HANDLE)
            vkFreeMemory(m_Device, m_Memory, nullptr);
    }

    // ============================================================
    // VulkanTextureView
    // ============================================================

    VulkanTextureView::~VulkanTextureView()
    {
        if (m_ImageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_Device, m_ImageView, nullptr);
    }

    // ============================================================
    // VulkanShader
    // ============================================================

    VulkanShader::~VulkanShader()
    {
        if (m_Module != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_Device, m_Module, nullptr);
    }

    // ============================================================
    // VulkanPipelineState
    // ============================================================

    VulkanPipelineState::~VulkanPipelineState()
    {
        if (m_Pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    }

    // ============================================================
    // VulkanSampler
    // ============================================================

    VulkanSampler::~VulkanSampler()
    {
        if (m_Sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_Device, m_Sampler, nullptr);
    }

    // ============================================================
    // VulkanSwapChain
    // ============================================================

    VulkanSwapChain::VulkanSwapChain(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkSurfaceKHR surface, VkQueue presentQueue,
                                     const SwapChainDesc& desc)
        : m_Device(device)
        , m_PhysicalDevice(physicalDevice)
        , m_Surface(surface)
        , m_PresentQueue(presentQueue)
        , m_Desc(desc)
    {
        // Create semaphores
        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_ImageAvailableSemaphore);
        vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphore);

        CreateSwapChainResources();
    }

    VulkanSwapChain::~VulkanSwapChain()
    {
        vkDeviceWaitIdle(m_Device);
        CleanupSwapChain();

        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
        if (m_RenderFinishedSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
    }

    void VulkanSwapChain::CreateRenderPass()
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = m_SwapChainFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = nullptr;  // No depth for now

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan render pass");
    }

    void VulkanSwapChain::CreateSwapChainResources()
    {
        // Query surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

        // Choose surface format
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

        m_SwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        for (const auto& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                m_SwapChainFormat = f.format;
                break;
            }
        }

        // Choose present mode (FIFO = VSync)
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

        // Choose extent
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            m_Extent = capabilities.currentExtent;
        }
        else
        {
            m_Extent.width = std::max(capabilities.minImageExtent.width,
                                      std::min(capabilities.maxImageExtent.width, m_Desc.Width));
            m_Extent.height = std::max(capabilities.minImageExtent.height,
                                       std::min(capabilities.maxImageExtent.height, m_Desc.Height));
        }

        uint32_t imageCount = std::max(m_Desc.BufferCount, capabilities.minImageCount);
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = m_SwapChainFormat;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = m_Extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan swap chain");

        // Get swap chain images
        uint32_t swapImageCount;
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &swapImageCount, nullptr);
        std::vector<VkImage> swapImages(swapImageCount);
        vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &swapImageCount, swapImages.data());

        // Create render pass
        CreateRenderPass();

        // Create image views and wrap as textures
        m_BackBuffers.clear();
        m_BackBufferViews.clear();
        m_Framebuffers.clear();

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            TextureDesc texDesc;
            texDesc.Width = m_Extent.width;
            texDesc.Height = m_Extent.height;
            texDesc.Format = EFormat::R8G8B8A8_UNORM;
            m_BackBuffers.push_back(
                std::make_unique<VulkanTexture>(m_Device, swapImages[i], VK_NULL_HANDLE, texDesc, false));

            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_SwapChainFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView imageView;
            if (vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
                throw std::runtime_error("Failed to create Vulkan swap chain image view");

            m_BackBufferViews.push_back(std::make_unique<VulkanTextureView>(m_Device, imageView));
        }

        // Create framebuffers (color only, no depth for now)
        for (uint32_t i = 0; i < swapImageCount; i++)
        {
            VkImageView attachments[] = { static_cast<VulkanTextureView*>(m_BackBufferViews[i].get())->GetVkImageView() };

            VkFramebufferCreateInfo fbInfo = {};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = m_RenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = attachments;
            fbInfo.width = m_Extent.width;
            fbInfo.height = m_Extent.height;
            fbInfo.layers = 1;

            VkFramebuffer framebuffer;
            if (vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS)
                throw std::runtime_error("Failed to create Vulkan framebuffer");
            m_Framebuffers.push_back(framebuffer);
        }
    }

    void VulkanSwapChain::CleanupSwapChain()
    {
        for (auto fb : m_Framebuffers)
        {
            if (fb != VK_NULL_HANDLE)
                vkDestroyFramebuffer(m_Device, fb, nullptr);
        }
        m_Framebuffers.clear();

        m_BackBufferViews.clear();
        m_BackBuffers.clear();

        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        if (m_SwapChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
            m_SwapChain = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapChain::AcquireNextImage()
    {
        vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX,
                              m_ImageAvailableSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);
    }

    void VulkanSwapChain::Present(uint32_t syncInterval)
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_RenderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_SwapChain;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    }

    void VulkanSwapChain::ResizeBuffers(uint32_t width, uint32_t height)
    {
        vkDeviceWaitIdle(m_Device);
        CleanupSwapChain();
        m_Desc.Width = width;
        m_Desc.Height = height;
        CreateSwapChainResources();
    }

    uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const
    {
        return m_CurrentImageIndex;
    }

    RHITexture* VulkanSwapChain::GetBackBuffer(uint32_t index)
    {
        if (index >= m_BackBuffers.size()) return nullptr;
        return m_BackBuffers[index].get();
    }

    RHITextureView* VulkanSwapChain::GetBackBufferRTV(uint32_t index)
    {
        if (index >= m_BackBufferViews.size()) return nullptr;
        return m_BackBufferViews[index].get();
    }

    VkFramebuffer VulkanSwapChain::GetCurrentFramebuffer() const
    {
        if (m_CurrentImageIndex >= m_Framebuffers.size()) return VK_NULL_HANDLE;
        return m_Framebuffers[m_CurrentImageIndex];
    }

    // ============================================================
    // VulkanDevice
    // ============================================================

    VulkanDevice::VulkanDevice(bool enableDebug)
        : m_EnableDebug(enableDebug)
    {
        CreateInstance(enableDebug);
        SelectPhysicalDevice();
        CreateLogicalDevice();
        CreateCommandPool();
        CreateDescriptorPoolAndLayout();
        CreatePipelineLayout();
    }

    VulkanDevice::~VulkanDevice()
    {
        WaitIdle();

        if (m_PipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        if (m_DescriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        if (m_CommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        if (m_Device != VK_NULL_HANDLE)
            vkDestroyDevice(m_Device, nullptr);
        if (m_LastSurface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(m_Instance, m_LastSurface, nullptr);

        // Destroy debug messenger
        if (m_DebugMessenger != VK_NULL_HANDLE)
        {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                m_Instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func) func(m_Instance, m_DebugMessenger, nullptr);
        }

        if (m_Instance != VK_NULL_HANDLE)
            vkDestroyInstance(m_Instance, nullptr);
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << std::endl;
        }
        return VK_FALSE;
    }

    void VulkanDevice::CreateInstance(bool enableDebug)
    {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "KiwiEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        appInfo.pEngineName = "KiwiEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };

        std::vector<const char*> layers;

        if (enableDebug)
        {
            // Check if validation layer is available
            uint32_t layerCount;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            bool validationAvailable = false;
            for (const auto& layer : availableLayers)
            {
                if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                {
                    validationAvailable = true;
                    break;
                }
            }

            if (validationAvailable)
            {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
            else
            {
                std::cout << "[Vulkan] Validation layers not available, skipping." << std::endl;
            }
        }

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = (uint32_t)layers.size();
        createInfo.ppEnabledLayerNames = layers.data();

        if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance");

        // Setup debug messenger
        if (enableDebug && !layers.empty())
        {
            VkDebugUtilsMessengerCreateInfoEXT dbgInfo = {};
            dbgInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dbgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbgInfo.pfnUserCallback = VulkanDebugCallback;

            auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                m_Instance, "vkCreateDebugUtilsMessengerEXT");
            if (createFunc)
                createFunc(m_Instance, &dbgInfo, nullptr, &m_DebugMessenger);
        }
    }

    void VulkanDevice::SelectPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        if (deviceCount == 0)
            throw std::runtime_error("Failed to find GPUs with Vulkan support");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

        // Pick first discrete GPU, or first available
        for (const auto& device : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_PhysicalDevice = device;
                std::cout << "[Vulkan] Selected GPU: " << props.deviceName << std::endl;
                return;
            }
        }

        m_PhysicalDevice = devices[0];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
        std::cout << "[Vulkan] Selected GPU: " << props.deviceName << std::endl;
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        // Find graphics queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

        m_GraphicsQueueFamily = UINT32_MAX;
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_GraphicsQueueFamily = i;
                break;
            }
        }

        if (m_GraphicsQueueFamily == UINT32_MAX)
            throw std::runtime_error("Failed to find a graphics queue family");

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_GraphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.fillModeNonSolid = VK_TRUE;

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan logical device");

        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
    }

    void VulkanDevice::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan command pool");
    }

    void VulkanDevice::CreateDescriptorPoolAndLayout()
    {
        // Descriptor pool
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 200;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan descriptor pool");

        // Descriptor set layout: 1 UBO at binding 0
        VkDescriptorSetLayoutBinding uboBinding = {};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan descriptor set layout");
    }

    void VulkanDevice::CreatePipelineLayout()
    {
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_DescriptorSetLayout;

        if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan pipeline layout");
    }

    VkDescriptorSet VulkanDevice::AllocateDescriptorSet()
    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_DescriptorSetLayout;

        VkDescriptorSet descriptorSet;
        if (vkAllocateDescriptorSets(m_Device, &allocInfo, &descriptorSet) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan descriptor set");

        return descriptorSet;
    }

    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable Vulkan memory type");
    }

    void VulkanDevice::WaitIdle()
    {
        if (m_Device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_Device);
    }

    std::unique_ptr<RHISwapChain> VulkanDevice::CreateSwapChain(const SwapChainDesc& desc)
    {
        // Create Win32 surface
        VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hwnd = (HWND)desc.WindowHandle;
        surfaceInfo.hinstance = GetModuleHandle(nullptr);

        VkSurfaceKHR surface;
        if (vkCreateWin32SurfaceKHR(m_Instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan Win32 surface");

        m_LastSurface = surface;

        // Check present support
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_GraphicsQueueFamily, surface, &presentSupport);
        if (!presentSupport)
            throw std::runtime_error("Graphics queue does not support presentation");

        auto swapChain = std::make_unique<VulkanSwapChain>(m_Device, m_PhysicalDevice, surface, m_GraphicsQueue, desc);

        // Cache the render pass for PSO creation and ImGui
        m_MainRenderPass = swapChain->GetRenderPass();

        return swapChain;
    }

    std::unique_ptr<RHIBuffer> VulkanDevice::CreateBuffer(const BufferDesc& desc, const void* initialData)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.SizeInBytes;
        bufferInfo.usage = 0;

        if (desc.BindFlags & BUFFER_USAGE_VERTEX)   bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (desc.BindFlags & BUFFER_USAGE_INDEX)     bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (desc.BindFlags & BUFFER_USAGE_CONSTANT)  bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (bufferInfo.usage == 0)
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer;
        if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan buffer");

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_Device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory memory;
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan buffer memory");

        vkBindBufferMemory(m_Device, buffer, memory, 0);

        // Copy initial data
        if (initialData)
        {
            void* mapped;
            vkMapMemory(m_Device, memory, 0, desc.SizeInBytes, 0, &mapped);
            memcpy(mapped, initialData, desc.SizeInBytes);
            vkUnmapMemory(m_Device, memory);
        }

        return std::make_unique<VulkanBuffer>(m_Device, buffer, memory, desc);
    }

    std::unique_ptr<RHITexture> VulkanDevice::CreateTexture(const TextureDesc& desc, const void* initialData)
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = desc.Width;
        imageInfo.extent.height = desc.Height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = desc.MipLevels;
        imageInfo.arrayLayers = desc.DepthOrArray;
        imageInfo.format = VulkanToVkFormat(desc.Format);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (imageInfo.format == VK_FORMAT_UNDEFINED)
        {
            std::cerr << "[Vulkan] CreateTexture: unsupported format, skipping" << std::endl;
            return nullptr;
        }

        // Depth formats (including R32_TYPELESS which maps to D32_SFLOAT)
        bool isDepth = (desc.Format == EFormat::D24_UNORM_S8_UINT ||
                        desc.Format == EFormat::D32_FLOAT ||
                        desc.Format == EFormat::R32_TYPELESS);
        if (isDepth)
        {
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        else
        {
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        VkImage image;
        if (vkCreateImage(m_Device, &imageInfo, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan image");

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(m_Device, image, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory memory;
        if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan image memory");

        vkBindImageMemory(m_Device, image, memory, 0);

        return std::make_unique<VulkanTexture>(m_Device, image, memory, desc, true);
    }

    std::unique_ptr<RHITextureView> VulkanDevice::CreateTextureView(
        RHITexture* texture, EDescriptorHeapType heapType,
        EFormat format, int mipSlice, int arraySlice)
    {
        auto vkTexture = static_cast<VulkanTexture*>(texture);

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = vkTexture->GetVkImage();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

        EFormat texFormat = (format != EFormat::Unknown) ? format : texture->GetDesc().Format;
        viewInfo.format = VulkanToVkFormat(texFormat);

        if (heapType == EDescriptorHeapType::DSV)
        {
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (texFormat == EFormat::D24_UNORM_S8_UINT)
                viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        else
        {
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        viewInfo.subresourceRange.baseMipLevel = (mipSlice >= 0) ? mipSlice : 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = (arraySlice >= 0) ? arraySlice : 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan image view");

        return std::make_unique<VulkanTextureView>(m_Device, imageView);
    }

    std::unique_ptr<RHIShader> VulkanDevice::CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize)
    {
        // byteCode is expected to be SPIR-V
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = byteCodeSize;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(byteCode);

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan shader module");

        std::vector<uint32_t> spirv((const uint32_t*)byteCode,
            (const uint32_t*)((const uint8_t*)byteCode + byteCodeSize));

        return std::make_unique<VulkanShader>(m_Device, shaderModule, type, spirv);
    }

    std::unique_ptr<RHIShader> VulkanDevice::CompileShaderFromSPIRV(
        EShaderType type, const uint32_t* spirvCode, size_t spirvSize)
    {
        return CreateShader(type, spirvCode, spirvSize);
    }

    std::unique_ptr<RHIInputLayout> VulkanDevice::CreateInputLayout(
        const InputElementDesc* elements, uint32_t elementCount, RHIShader* vertexShader)
    {
        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Calculate stride from elements
        uint32_t maxOffset = 0;
        uint32_t maxSize = 0;
        std::vector<VkVertexInputAttributeDescription> attributes(elementCount);

        for (uint32_t i = 0; i < elementCount; i++)
        {
            attributes[i].binding = elements[i].InputSlot;
            attributes[i].location = i;
            attributes[i].format = VulkanToVkFormat(elements[i].Format);
            attributes[i].offset = elements[i].AlignedByteOffset;

            uint32_t elemSize = 0;
            switch (elements[i].Format)
            {
            case EFormat::R32G32B32A32_FLOAT: elemSize = 16; break;
            case EFormat::R32G32B32_FLOAT:    elemSize = 12; break;
            case EFormat::R32G32_FLOAT:       elemSize = 8;  break;
            case EFormat::R32_FLOAT:          elemSize = 4;  break;
            default:                          elemSize = 4;  break;
            }

            if (elements[i].AlignedByteOffset + elemSize > maxOffset + maxSize)
            {
                maxOffset = elements[i].AlignedByteOffset;
                maxSize = elemSize;
            }
        }

        binding.stride = maxOffset + maxSize;

        return std::make_unique<VulkanInputLayout>(attributes, binding);
    }

    std::unique_ptr<RHIPipelineState> VulkanDevice::CreatePipelineState()
    {
        // Placeholder - actual pipeline creation in main.cpp
        return std::make_unique<VulkanPipelineState>(m_Device, VK_NULL_HANDLE);
    }

    std::unique_ptr<RHISampler> VulkanDevice::CreateSampler()
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        VkSampler sampler;
        if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan sampler");

        return std::make_unique<VulkanSampler>(m_Device, sampler);
    }

    // ============================================================
    // VulkanCommandContext
    // ============================================================

    VulkanCommandContext::VulkanCommandContext(VkDevice device, VkCommandPool commandPool,
                                               VkQueue graphicsQueue)
        : m_Device(device), m_CommandPool(commandPool), m_GraphicsQueue(graphicsQueue)
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate Vulkan command buffer");

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_Device, &fenceInfo, nullptr, &m_Fence) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan fence");
    }

    VulkanCommandContext::~VulkanCommandContext()
    {
        if (m_Fence != VK_NULL_HANDLE)
            vkDestroyFence(m_Device, m_Fence, nullptr);
        // Command buffer freed when command pool is destroyed
    }

    void VulkanCommandContext::Reset()
    {
        if (m_Fence == VK_NULL_HANDLE || m_CommandBuffer == VK_NULL_HANDLE) return;
        vkWaitForFences(m_Device, 1, &m_Fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_Fence);
        vkResetCommandBuffer(m_CommandBuffer, 0);
        m_IsRecording = false;
        m_InRenderPass = false;
    }

    void VulkanCommandContext::BeginCommandBuffer()
    {
        if (m_IsRecording) return;  // Already recording

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
        if (result != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] vkBeginCommandBuffer failed with code: " << result << std::endl;
            return;
        }
        m_IsRecording = true;
    }

    void VulkanCommandContext::EndCommandBuffer()
    {
        if (m_InRenderPass)
        {
            vkCmdEndRenderPass(m_CommandBuffer);
            m_InRenderPass = false;
        }
        if (m_IsRecording)
        {
            vkEndCommandBuffer(m_CommandBuffer);
            m_IsRecording = false;
        }
    }

    void VulkanCommandContext::Submit()
    {
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_Fence);
    }

    void VulkanCommandContext::ResourceBarrier(RHITexture* texture, int stateBefore, int stateAfter)
    {
        // Vulkan uses pipeline barriers with image layout transitions
        // This is a simplified version - actual implementation would track image layouts
        auto vkTex = static_cast<VulkanTexture*>(texture);

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = (VkImageLayout)stateBefore;
        barrier.newLayout = (VkImageLayout)stateAfter;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vkTex->GetVkImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(m_CommandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void VulkanCommandContext::SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv)
    {
        // In Vulkan, render targets are set via render passes and framebuffers
        // This is handled by BeginRenderPass
    }

    void VulkanCommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        // Clear is done at render pass begin with clear values
    }

    void VulkanCommandContext::ClearDepthStencilView(RHITextureView* dsv, const ClearDepthStencilValue& value, uint8_t clearFlags)
    {
        // Clear is done at render pass begin with clear values
    }

    void VulkanCommandContext::BeginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                                                VkExtent2D extent, const VkClearValue* clearValues, uint32_t clearCount)
    {
        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = renderPass;
        rpInfo.framebuffer = framebuffer;
        rpInfo.renderArea.offset = { 0, 0 };
        rpInfo.renderArea.extent = extent;
        rpInfo.clearValueCount = clearCount;
        rpInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(m_CommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_InRenderPass = true;
    }

    void VulkanCommandContext::EndRenderPass()
    {
        if (m_InRenderPass)
        {
            vkCmdEndRenderPass(m_CommandBuffer);
            m_InRenderPass = false;
        }
    }

    void VulkanCommandContext::SetPipelineState(RHIPipelineState* pso)
    {
        auto vkPSO = static_cast<VulkanPipelineState*>(pso);
        if (vkPSO && vkPSO->GetVkPipeline() != VK_NULL_HANDLE)
            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPSO->GetVkPipeline());
    }

    void VulkanCommandContext::SetPrimitiveTopology(EPrimitiveTopology topology)
    {
        // In Vulkan, topology is part of the pipeline state
        // Dynamic state would require VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY (Vulkan 1.3+)
    }

    void VulkanCommandContext::SetVertexBuffers(uint32_t startSlot, RHIBuffer* const* buffers,
                                                 const VertexBufferView* views, uint32_t count)
    {
        std::vector<VkBuffer> vkBuffers(count);
        std::vector<VkDeviceSize> offsets(count);
        for (uint32_t i = 0; i < count; i++)
        {
            auto vkBuf = static_cast<VulkanBuffer*>(buffers[i]);
            vkBuffers[i] = vkBuf->GetVkBuffer();
            offsets[i] = 0;
        }
        vkCmdBindVertexBuffers(m_CommandBuffer, startSlot, count, vkBuffers.data(), offsets.data());
    }

    void VulkanCommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        if (!buffer || !view) return;
        auto vkBuf = static_cast<VulkanBuffer*>(buffer);
        VkIndexType indexType = (view->Format == EFormat::R16_UINT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(m_CommandBuffer, vkBuf->GetVkBuffer(), 0, indexType);
    }

    // Vulkan shader binding is done via PSO
    void VulkanCommandContext::SetVertexShader(RHIShader* shader) { /* handled by PSO */ }
    void VulkanCommandContext::SetPixelShader(RHIShader* shader) { /* handled by PSO */ }
    void VulkanCommandContext::SetGeometryShader(RHIShader* shader) { /* handled by PSO */ }
    void VulkanCommandContext::SetInputLayout(RHIInputLayout* layout) { /* handled by PSO */ }

    void VulkanCommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        // In Vulkan, constant buffers are bound via descriptor sets
        // This needs to be handled via BindDescriptorSet
    }

    void VulkanCommandContext::SetSampler(uint32_t slot, RHISampler* sampler) { /* via descriptor set */ }

    void VulkanCommandContext::SetViewports(const Viewport* viewports, uint32_t count)
    {
        std::vector<VkViewport> vkViewports(count);
        for (uint32_t i = 0; i < count; i++)
        {
            vkViewports[i].x = viewports[i].TopLeftX;
            vkViewports[i].y = viewports[i].TopLeftY;
            vkViewports[i].width = viewports[i].Width;
            vkViewports[i].height = viewports[i].Height;
            vkViewports[i].minDepth = viewports[i].MinDepth;
            vkViewports[i].maxDepth = viewports[i].MaxDepth;
        }
        vkCmdSetViewport(m_CommandBuffer, 0, count, vkViewports.data());
    }

    void VulkanCommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        std::vector<VkRect2D> vkRects(count);
        for (uint32_t i = 0; i < count; i++)
        {
            vkRects[i].offset = { rects[i].Left, rects[i].Top };
            vkRects[i].extent.width = rects[i].Right - rects[i].Left;
            vkRects[i].extent.height = rects[i].Bottom - rects[i].Top;
        }
        vkCmdSetScissor(m_CommandBuffer, 0, count, vkRects.data());
    }

    void VulkanCommandContext::Draw(uint32_t vertexCount, uint32_t vertexStart)
    {
        vkCmdDraw(m_CommandBuffer, vertexCount, 1, vertexStart, 0);
    }

    void VulkanCommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        vkCmdDrawIndexed(m_CommandBuffer, indexCount, 1, indexStart, vertexOffset, 0);
    }

    void VulkanCommandContext::Flush()
    {
        EndCommandBuffer();
        Submit();
        vkWaitForFences(m_Device, 1, &m_Fence, VK_TRUE, UINT64_MAX);
    }

    void VulkanCommandContext::BindDescriptorSet(VkPipelineLayout layout, VkDescriptorSet descriptorSet)
    {
        vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                layout, 0, 1, &descriptorSet, 0, nullptr);
    }

    void VulkanCommandContext::SetShaderResourceView(uint32_t slot, RHITextureView* srv)
    {
        // In Vulkan, SRV binding is done via descriptor sets — handled externally
        // For forward rendering, this is a no-op; full implementation needs descriptor management
    }

    // ============================================================
    // VulkanCommandContext — Frame Lifecycle
    // ============================================================

    void VulkanCommandContext::BeginFrame(RHISwapChain* swapChain)
    {
        auto* vkSwap = static_cast<VulkanSwapChain*>(swapChain);

        // Wait for previous frame
        Reset();

        // Acquire next swap chain image
        vkSwap->AcquireNextImage();

        // Begin command buffer
        BeginCommandBuffer();

        // Begin render pass
        VkClearValue clearValues[1];
        clearValues[0].color = { { 0.1f, 0.1f, 0.15f, 1.0f } };

        BeginRenderPass(
            vkSwap->GetRenderPass(),
            vkSwap->GetCurrentFramebuffer(),
            vkSwap->GetExtent(),
            clearValues, 1);
    }

    void VulkanCommandContext::EndFrame(RHISwapChain* swapChain)
    {
        auto* vkSwap = static_cast<VulkanSwapChain*>(swapChain);

        // End render pass
        EndRenderPass();

        // End command buffer
        EndCommandBuffer();

        // Submit with semaphore synchronization
        VkSemaphore waitSemaphores[] = { vkSwap->GetImageAvailableSemaphore() };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { vkSwap->GetRenderFinishedSemaphore() };

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_Fence);
    }

    // ============================================================
    // VulkanDevice — CompileShader (GLSL via runtime compilation)
    // ============================================================

    std::unique_ptr<RHIShader> VulkanDevice::CompileShader(
        EShaderType type, const char* source, const char* entryPoint,
        const char* shaderModel, const ShaderMacro* macros, uint32_t macroCount)
    {
        // Vulkan CompileShader expects GLSL source with //!VERTEX / //!FRAGMENT markers
        // (same format as OpenGL backend)
        // We extract the appropriate stage and compile via glslang at runtime
        // For now, use a simplified approach: treat source as GLSL, compile via shaderc or 
        // just create a dummy shader module
        // TODO: Integrate runtime GLSL->SPIR-V compilation (via shaderc or glslang C API)

        std::cerr << "[Vulkan] CompileShader: Runtime GLSL->SPIR-V not yet implemented. "
                  << "Use CreateShader with pre-compiled SPIR-V." << std::endl;

        // Return a null shader module as placeholder
        VkShaderModule module = VK_NULL_HANDLE;
        std::vector<uint32_t> emptySpirv;
        return std::make_unique<VulkanShader>(m_Device, module, type, emptySpirv);
    }

    // ============================================================
    // VulkanDevice — CreateGraphicsPipelineState
    // ============================================================

    std::unique_ptr<RHIPipelineState> VulkanDevice::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout)
    {
        PipelineStateDesc defaultDesc;
        defaultDesc.NumRenderTargets = 1;
        defaultDesc.RTVFormats[0] = EFormat::R8G8B8A8_UNORM;
        defaultDesc.DSVFormat = EFormat::D32_FLOAT;
        return CreateGraphicsPipelineState(vertexShader, pixelShader, inputLayout, defaultDesc);
    }

    std::unique_ptr<RHIPipelineState> VulkanDevice::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader,
        RHIInputLayout* inputLayout, const PipelineStateDesc& pipelineDesc)
    {
        auto* vkVS = static_cast<VulkanShader*>(vertexShader);
        auto* vkPS = pixelShader ? static_cast<VulkanShader*>(pixelShader) : nullptr;
        auto* vkLayout = inputLayout ? static_cast<VulkanInputLayout*>(inputLayout) : nullptr;

        // Check for null shader modules
        if (!vkVS || vkVS->GetVkShaderModule() == VK_NULL_HANDLE)
        {
            std::cerr << "[Vulkan] CreateGraphicsPipelineState: null vertex shader" << std::endl;
            return std::make_unique<VulkanPipelineState>(m_Device, VK_NULL_HANDLE);
        }

        // Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        VkPipelineShaderStageCreateInfo vsStage = {};
        vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vsStage.module = vkVS->GetVkShaderModule();
        vsStage.pName = "main";
        shaderStages.push_back(vsStage);

        if (vkPS && vkPS->GetVkShaderModule() != VK_NULL_HANDLE)
        {
            VkPipelineShaderStageCreateInfo psStage = {};
            psStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            psStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            psStage.module = vkPS->GetVkShaderModule();
            psStage.pName = "main";
            shaderStages.push_back(psStage);
        }

        // Vertex input
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (vkLayout)
        {
            auto binding = vkLayout->GetBinding();
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &binding;
            vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)vkLayout->GetAttributes().size();
            vertexInputInfo.pVertexAttributeDescriptions = vkLayout->GetAttributes().data();
        }

        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport / scissor (dynamic state)
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;  // Left-handed like DX
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = pipelineDesc.DepthEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = pipelineDesc.DepthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending
        std::vector<VkPipelineColorBlendAttachmentState> colorAttachments(pipelineDesc.NumRenderTargets);
        for (uint32_t i = 0; i < pipelineDesc.NumRenderTargets; i++)
        {
            colorAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorAttachments[i].blendEnable = VK_FALSE;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = pipelineDesc.NumRenderTargets;
        colorBlending.pAttachments = colorAttachments.data();

        // Dynamic state (viewport + scissor)
        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        // Use the main render pass from swapchain
        VkRenderPass renderPass = m_MainRenderPass;
        if (renderPass == VK_NULL_HANDLE)
        {
            std::cerr << "[Vulkan] CreateGraphicsPipelineState: no render pass set" << std::endl;
            return std::make_unique<VulkanPipelineState>(m_Device, VK_NULL_HANDLE);
        }

        // Create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = (uint32_t)shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        VkPipeline pipeline;
        if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            std::cerr << "[Vulkan] Failed to create graphics pipeline" << std::endl;
            return std::make_unique<VulkanPipelineState>(m_Device, VK_NULL_HANDLE);
        }

        return std::make_unique<VulkanPipelineState>(m_Device, pipeline);
    }

    // ============================================================
    // VulkanDevice — ImGui Integration
    // ============================================================

    void VulkanDevice::InitImGui(void* windowHandle)
    {
        HWND hwnd = (HWND)windowHandle;

        ImGui_ImplWin32_Init(hwnd);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = VK_API_VERSION_1_2;
        initInfo.Instance = m_Instance;
        initInfo.PhysicalDevice = m_PhysicalDevice;
        initInfo.Device = m_Device;
        initInfo.QueueFamily = m_GraphicsQueueFamily;
        initInfo.Queue = m_GraphicsQueue;
        initInfo.DescriptorPool = m_DescriptorPool;
        initInfo.PipelineInfoMain.RenderPass = m_MainRenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = 2;
        initInfo.PipelineCache = VK_NULL_HANDLE;

        ImGui_ImplVulkan_Init(&initInfo);

        std::cout << "[Vulkan] ImGui Vulkan backend initialized" << std::endl;
    }

    void VulkanDevice::ShutdownImGui()
    {
        WaitIdle();
        if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
            ImGui_ImplVulkan_Shutdown();
        if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData)
            ImGui_ImplWin32_Shutdown();
    }

    void VulkanDevice::ImGuiNewFrame()
    {
        if (ImGui::GetIO().BackendRendererUserData)
            ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
    }

    void VulkanDevice::ImGuiRenderDrawData(RHICommandContext* ctx)
    {
        if (!ImGui::GetIO().BackendRendererUserData) return;
        auto* vkCtx = static_cast<VulkanCommandContext*>(ctx);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCtx->GetCommandBuffer());
    }

    // ============================================================
    // Vulkan RHI Factory
    // ============================================================

    void CreateVulkanRHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        auto device = std::make_unique<VulkanDevice>(params.EnableDebug);
        auto context = std::make_unique<VulkanCommandContext>(
            device->GetVkDevice(), device->GetCommandPool(), device->GetGraphicsQueue());

        outDevice = std::move(device);
        outContext = std::move(context);
    }

} // namespace Kiwi
