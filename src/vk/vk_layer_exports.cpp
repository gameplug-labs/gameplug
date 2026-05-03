#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <string>
#include <vector>
#include "logger.h"
#include "dispatch.h"
#include "overlay.h"
#include "image_tracker.h"
#include "framework_export.h"

#ifndef VK_LAYER_EXPORT
#ifdef _WIN32
#define VK_LAYER_EXPORT __declspec(dllexport)
#else
#define VK_LAYER_EXPORT
#endif
#endif

extern "C" {

// Global tracking for simplicity in this prototype
static VkInstance g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysDevice = VK_NULL_HANDLE;

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    
    GamePlug::Logger::info("vkCreateInstance intercepted");
    GamePlug::Init();

    VkLayerInstanceCreateInfo* layer_info = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (layer_info && (layer_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layer_info->function != VK_LAYER_LINK_INFO)) {
        layer_info = (VkLayerInstanceCreateInfo*)layer_info->pNext;
    }

    if (!layer_info) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance nextCreateInstance = (PFN_vkCreateInstance)nextGIPA(NULL, "vkCreateInstance");

    layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

    VkResult result = nextCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS) {
        g_Instance = *pInstance;
        GamePlug::DispatchManager::Get().AddInstance(*pInstance, nextGIPA);
    }

    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    
    GamePlug::Logger::info("vkCreateDevice intercepted");
    g_PhysDevice = physicalDevice;

    VkLayerDeviceCreateInfo* layer_info = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (layer_info && (layer_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layer_info->function != VK_LAYER_LINK_INFO)) {
        layer_info = (VkLayerDeviceCreateInfo*)layer_info->pNext;
    }

    if (!layer_info) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = layer_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice nextCreateDevice = (PFN_vkCreateDevice)nextGIPA(NULL, "vkCreateDevice");

    layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

    VkResult result = nextCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS) {
        GamePlug::DispatchManager::Get().AddDevice(*pDevice, nextGDPA);
        
        // We initialize the overlay engine here
        // We'll need a queue. For now, we'll assume the first queue requested is the one.
        // This is a simplification; a real layer would track this more precisely.
    }

    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateWin32SurfaceKHR(
    VkInstance instance,
    const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (!inst_entry) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = inst_entry->table.vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    if (result == VK_SUCCESS) {
        GamePlug::OverlayRenderer::Get().SetWindow(pCreateInfo->hwnd);
    }
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(g_Instance);
    if (!inst_entry) return VK_ERROR_INITIALIZATION_FAILED;

    return inst_entry->table.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
    VkSurfaceCapabilities2KHR* pSurfaceCapabilities) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(g_Instance);
    if (!inst_entry) return VK_ERROR_INITIALIZATION_FAILED;

    return inst_entry->table.vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {
    
    GamePlug::Logger::info("vkCreateSwapchainKHR: Entry. Game Requested Extent=" + std::to_string(pCreateInfo->imageExtent.width) + "x" + std::to_string(pCreateInfo->imageExtent.height));
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    

    // SPOOF: If the game requested 720p because we lied to it, OVERRIDE it to 1080p 
    // so we get a full-res backbuffer for FSR2 output.
    VkSwapchainCreateInfoKHR spoofInfo = *pCreateInfo;
    uint32_t sw = pCreateInfo->imageExtent.width;
    uint32_t sh = pCreateInfo->imageExtent.height;
    
    // Reverse-lookup: What is the target Display resolution?
    // We assume the game is using the 'rw/rh' we gave it.
    // In our prototype, GetTargetResolution(width, height, ...) takes native as input.
    // We'll just use the Tracker's current native size.
    uint32_t nativeW = GamePlug::ImageTracker::Get().GetScreenWidth();
    uint32_t nativeH = GamePlug::ImageTracker::Get().GetScreenHeight();
    if (nativeW > 0 && nativeH > 0 && sw < nativeW) {
        GamePlug::Logger::info("vkCreateSwapchainKHR: OVERRIDING 720p backbuffer to " + std::to_string(nativeW) + "x" + std::to_string(nativeH));
        spoofInfo.imageExtent = { nativeW, nativeH };
    }

    VkResult result = dev_entry->table.vkCreateSwapchainKHR(device, &spoofInfo, pAllocator, pSwapchain);
    GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.2 (Result=" + std::to_string(result) + ")");
    
    if (result == VK_SUCCESS) {
        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.3 (Getting images)");
        uint32_t imageCount = 0;
        dev_entry->table.vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
        std::vector<VkImage> images(imageCount);
        dev_entry->table.vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, images.data());

        // Track swapchain images
        VkImageCreateInfo scCreateInfo = {};
        scCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        scCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        scCreateInfo.format = pCreateInfo->imageFormat;
        scCreateInfo.extent = { pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, 1 };
        scCreateInfo.mipLevels = 1;
        scCreateInfo.arrayLayers = pCreateInfo->imageArrayLayers;
        scCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        scCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        scCreateInfo.usage = pCreateInfo->imageUsage;
        scCreateInfo.sharingMode = pCreateInfo->imageSharingMode;
        scCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        GamePlug::ImageTracker::Get().SetSwapchainFormat(pCreateInfo->imageFormat);
        for (VkImage img : images) {
            GamePlug::ImageTracker::Get().TrackImage(img, &scCreateInfo);
            GamePlug::ImageTracker::Get().RegisterSwapchainImage(img);
        }

        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.4 (Getting queue)");
        VkQueue queue;
        dev_entry->table.vkGetDeviceQueue(device, 0, 0, &queue);
        GamePlug::DispatchManager::Get().AddQueue(queue, device);

        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.5 (SetupDevice)");
        GamePlug::ImageTracker::Get().ResetScores();
        GamePlug::ImageTracker::Get().SetScreenDimensions(pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);
        GamePlug::OverlayRenderer::Get().SetupDevice(g_Instance, g_PhysDevice, device, 0, queue);

        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.6 (SetupSwapchain)");
        GamePlug::OverlayRenderer::Get().SetupSwapchain(*pSwapchain, pCreateInfo->imageFormat, pCreateInfo->imageExtent, imageCount, images);
        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.7 (Done)");
    }
    
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        if (pQueue && *pQueue) {
            GamePlug::DispatchManager::Get().AddQueue(*pQueue, device);
        }
    }
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkGetDeviceQueue2(device, pQueueInfo, pQueue);
        if (pQueue && *pQueue) {
            GamePlug::DispatchManager::Get().AddQueue(*pQueue, device);
        }
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {
    // GamePlug::Logger::debug("Hook: vkCreateImage Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkCreateImage(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImage(*pImage, pCreateInfo);
    }
    // GamePlug::Logger::debug("Hook: vkCreateImage Exit");
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {
    // GamePlug::Logger::debug("Hook: vkDestroyImage Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackImage(image);
        dev_entry->table.vkDestroyImage(device, image, pAllocator);
    }
    // GamePlug::Logger::debug("Hook: vkDestroyImage Exit");
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView) {
    // GamePlug::Logger::debug("Hook: vkCreateImageView Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkCreateImageView(device, pCreateInfo, pAllocator, pView);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImageView(*pView, pCreateInfo->image);
    }
    // GamePlug::Logger::debug("Hook: vkCreateImageView Exit");
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImageView(
    VkDevice device,
    VkImageView imageView,
    const VkAllocationCallbacks* pAllocator) {
    // GamePlug::Logger::debug("Hook: vkDestroyImageView Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackImageView(imageView);
        dev_entry->table.vkDestroyImageView(device, imageView, pAllocator);
    }
    // GamePlug::Logger::debug("Hook: vkDestroyImageView Exit");
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateFramebuffer(
    VkDevice device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer) {
    // GamePlug::Logger::debug("Hook: vkCreateFramebuffer Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackFramebuffer(*pFramebuffer, pCreateInfo->attachmentCount, pCreateInfo->pAttachments);
    }
    // GamePlug::Logger::debug("Hook: vkCreateFramebuffer Exit");
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyFramebuffer(
    VkDevice device,
    VkFramebuffer framebuffer,
    const VkAllocationCallbacks* pAllocator) {
    // GamePlug::Logger::debug("Hook: vkDestroyFramebuffer Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackFramebuffer(framebuffer);
        dev_entry->table.vkDestroyFramebuffer(device, framebuffer, pAllocator);
    }
    // GamePlug::Logger::debug("Hook: vkDestroyFramebuffer Exit");
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory) {
    // GamePlug::Logger::debug("Hook: vkAllocateMemory Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    // GamePlug::Logger::debug("Hook: vkAllocateMemory Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {
    // GamePlug::Logger::debug("Hook: vkBindImageMemory Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkBindImageMemory(device, image, memory, memoryOffset);
    // GamePlug::Logger::debug("Hook: vkBindImageMemory Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateRenderPass(
    VkDevice device,
    const VkRenderPassCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkRenderPass* pRenderPass) {
    // GamePlug::Logger::debug("Hook: vkCreateRenderPass Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    // GamePlug::Logger::debug("Hook: vkCreateRenderPass Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;

    static uint32_t logCount = 0;
    if (logCount < 10) {
        for (uint32_t i = 0; i < createInfoCount; i++) {
            if (pCreateInfos[i].pViewportState) {
                if (pCreateInfos[i].pViewportState->pViewports) {
                    GamePlug::Logger::info("Pipeline " + std::to_string(i) + " has FIXED Viewport: " + 
                        std::to_string(pCreateInfos[i].pViewportState->pViewports[0].width) + "x" + 
                        std::to_string(pCreateInfos[i].pViewportState->pViewports[0].height));
                }
                if (pCreateInfos[i].pViewportState->pScissors) {
                    GamePlug::Logger::info("Pipeline " + std::to_string(i) + " has FIXED Scissor: " + 
                        std::to_string(pCreateInfos[i].pViewportState->pScissors[0].extent.width) + "x" + 
                        std::to_string(pCreateInfos[i].pViewportState->pScissors[0].extent.height));
                }
            }
        }
        logCount++;
    }

    return dev_entry->table.vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return;

    if (GamePlug::OverlayRenderer::IsRenderingOverlay()) {
        dev_entry->table.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
        return;
    }

    // PASSTHROUGH: We removed the viewport scaling hack.
    // The game now natively draw at 720p because of our surface spoofing.
    dev_entry->table.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return;

    if (GamePlug::OverlayRenderer::IsRenderingOverlay()) {
        dev_entry->table.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
        return;
    }

    // PASSTHROUGH: Scissor scaling hack also removed.
    dev_entry->table.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {
    // GamePlug::Logger::debug("Hook: vkAllocateCommandBuffers Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
    if (result == VK_SUCCESS && pCommandBuffers) {
        for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
            GamePlug::DispatchManager::Get().AddCommandBuffer(pCommandBuffers[i], device);
        }
    }
    // GamePlug::Logger::debug("Hook: vkAllocateCommandBuffers Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {
    // GamePlug::Logger::debug("Hook: vkBeginCommandBuffer Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkBeginCommandBuffer(commandBuffer, pBeginInfo);
    if (result != VK_SUCCESS) {
        GamePlug::Logger::error("vkBeginCommandBuffer failed with " + std::to_string(result));
    }
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_EndCommandBuffer( 
    VkCommandBuffer commandBuffer) {
    // GamePlug::Logger::debug("Hook: vkEndCommandBuffer Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkEndCommandBuffer(commandBuffer);
    // GamePlug::Logger::debug("Hook: vkEndCommandBuffer Exit");
    return result;
}

#include <map>
static std::map<VkCommandBuffer, VkFramebuffer> g_ActiveFBs;

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return;

    GamePlug::OverlayRenderer::Get().NewFrame();

    if (pRenderPassBegin) {
        g_ActiveFBs[commandBuffer] = pRenderPassBegin->framebuffer;
        
        static uint32_t logCount = 0;
        if (logCount++ % 500 == 0) {
             GamePlug::Logger::debug("CmdBeginRenderPass: FB=" + std::to_string((uintptr_t)pRenderPassBegin->framebuffer) + " Area=" + std::to_string(pRenderPassBegin->renderArea.extent.width) + "x" + std::to_string(pRenderPassBegin->renderArea.extent.height));
        }

        // Unified Rendering: If this is a swapchain pass, call OverlayRenderer to handle both Upscaling and UI
        if (GamePlug::ImageTracker::Get().IsSwapchainFramebuffer(pRenderPassBegin->framebuffer)) {
            VkImage source = GamePlug::ImageTracker::Get().GetLastSceneImage();
            VkImage target = GamePlug::ImageTracker::Get().GetSwapchainImageFromFramebuffer(pRenderPassBegin->framebuffer);
            
            uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
            uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();

            // Always call Render for UI visibility, even if source/target are NULL.
            // OverlayRenderer::Render now handles NULL handles and identical source/target gracefully.
        }
    }

    dev_entry->table.vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry) return;

    VkFramebuffer fb = g_ActiveFBs[commandBuffer];
    if (fb != VK_NULL_HANDLE) {
    }

    dev_entry->table.vkCmdEndRenderPass(commandBuffer);

    if (fb != VK_NULL_HANDLE) {
        uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
        uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();
        uint32_t rw = sw; 
        uint32_t rh = sh;

        // 1. Capture Scene Source: If this was an offscreen scene pass, save its result.
        if (GamePlug::ImageTracker::Get().IsSceneFramebuffer(fb, rw, rh)) {
            VkImage sceneImg = GamePlug::ImageTracker::Get().GetColorAttachment(fb, rw, rh);
            if (sceneImg != VK_NULL_HANDLE) {
                GamePlug::ImageTracker::Get().SaveSceneImage(sceneImg);
            }
        }

        // 2. Fallback Unified Render: Handle Scaling + UI if we missed it in Begin
        if (GamePlug::ImageTracker::Get().IsSwapchainFramebuffer(fb)){
             VkImage source = GamePlug::ImageTracker::Get().GetLastSceneImage();
             VkImage target = GamePlug::ImageTracker::Get().GetSwapchainImageFromFramebuffer(fb);
             
             if (source != VK_NULL_HANDLE && target != VK_NULL_HANDLE) {
                 GamePlug::OverlayRenderer::Get().Render(commandBuffer, source, target, sw, sh);
             } else {
                 GamePlug::OverlayRenderer::Get().Render(commandBuffer, target, target, sw, sh);
             }
        }
    }
    
    g_ActiveFBs.erase(commandBuffer);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = dev_entry->table.vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        GamePlug::OverlayRenderer::Get().SetCurrentSwapchainImage(*pImageIndex);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::debug("Hook: vkDestroySwapchainKHR Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }
    GamePlug::Logger::debug("Hook: vkDestroySwapchainKHR Exit");
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::info("vkDestroyDevice intercepted - Shutting down GamePlug");
    GamePlug::OverlayRenderer::Get().Shutdown();

    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkDestroyDevice(device, pAllocator);
    }
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::info("vkDestroyInstance intercepted");
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry) {
        inst_entry->table.vkDestroyInstance(instance, pAllocator);
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_DeviceWaitIdle(
    VkDevice device) {
    GamePlug::Logger::debug("Hook: vkDeviceWaitIdle Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkDeviceWaitIdle(device);
    GamePlug::Logger::debug("Hook: vkDeviceWaitIdle Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    auto* queue_dispatch = GamePlug::DispatchManager::Get().GetQueueDispatch(queue);
    
    if (queue_dispatch && pPresentInfo && pPresentInfo->swapchainCount > 0) {
        // Standalone Fallback: Render UI if not already drawn in intercepted render passes
        static uint32_t presentCount = 0;
        bool shouldLog = (presentCount++ % 600 == 0);
        
        bool isVisible = GamePlug::OverlayRenderer::Get().IsVisible();
        bool isRendered = GamePlug::OverlayRenderer::Get().IsUIRendered();

        if (shouldLog) {
            GamePlug::Logger::info("QueuePresentKHR: visible=" + std::string(isVisible ? "YES" : "NO") + 
                                  " rendered=" + std::string(isRendered ? "YES" : "NO"));
        }

        if (isVisible && !isRendered) {
            uint32_t currentIdx = GamePlug::OverlayRenderer::Get().GetCurrentSwapchainImage();
            VkImage target = GamePlug::OverlayRenderer::Get().GetSwapchainImage(currentIdx);
            if (target != VK_NULL_HANDLE) {
                uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
                uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();
                GamePlug::OverlayRenderer::Get().RenderStandalone(target, sw, sh);
            } else if (shouldLog) {
                GamePlug::Logger::warn("QueuePresentKHR: Standalone fallback skipped - No target image");
            }
        }

        VkResult result = queue_dispatch->table.vkQueuePresentKHR(queue, pPresentInfo);
        
        // Reset overlay and upscaler frame lifecycle after presentation
        GamePlug::OverlayRenderer::Get().EndFrame();

        return result;
    }
    
    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetDeviceProcAddr(VkDevice device, const char* pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetInstanceProcAddr(VkInstance instance, const char* pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::string(pName) == "vkGetDeviceProcAddr") return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vkCreateDevice") return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    if (std::string(pName) == "vkGetDeviceQueue") return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2") return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR") return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR") return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR") return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkCreateImage") return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage") return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView") return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView") return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer") return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer") return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory") return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory") return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass") return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines") return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers") return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer") return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer") return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR") return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice") return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance") return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle") return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR") return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport") return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor") return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass") return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass") return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;
    
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) return dev_entry->pfnNextGetDeviceProcAddr(device, pName);
    return nullptr; 
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::string(pName) == "vkGetInstanceProcAddr") return (PFN_vkVoidFunction)GamePlug_GetInstanceProcAddr;
    if (std::string(pName) == "vkGetDeviceProcAddr") return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vk_layerGetPhysicalDeviceProcAddr") return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceProcAddr;
    if (std::string(pName) == "vkCreateInstance") return (PFN_vkVoidFunction)GamePlug_CreateInstance;
    if (std::string(pName) == "vkCreateDevice") return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    if (std::string(pName) == "vkCreateWin32SurfaceKHR") return (PFN_vkVoidFunction)GamePlug_CreateWin32SurfaceKHR;
    if (std::string(pName) == "vkGetDeviceQueue") return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2") return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR") return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR") return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR") return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkCreateImage") return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage") return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView") return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView") return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer") return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer") return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory") return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory") return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass") return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines") return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers") return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer") return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer") return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR") return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice") return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance") return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle") return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR") return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport") return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor") return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass") return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass") return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;
    
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry) return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry) return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetInstanceProcAddr = GamePlug_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = GamePlug_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = GamePlug_GetPhysicalDeviceProcAddr; 
    } else return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

} // extern "C"
