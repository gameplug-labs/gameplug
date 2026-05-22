#include "dispatch.h"
#include "framework_export.h"
#include "image_tracker.h"
#include "logger.h"
#include "overlay.h"
#include "vk_layer_exports.h"

extern "C" {

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {

    GamePlug::Logger::info("vkCreateInstance intercepted");
    GamePlug::Init();

    VkLayerInstanceCreateInfo* layer_info = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
    while (
        layer_info && (layer_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layer_info->function != VK_LAYER_LINK_INFO)) {
        layer_info = (VkLayerInstanceCreateInfo*)layer_info->pNext;
    }

    if (!layer_info)
        return VK_ERROR_INITIALIZATION_FAILED;

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
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {

    GamePlug::Logger::info("vkCreateDevice intercepted");
    g_PhysDevice = physicalDevice;

    VkLayerDeviceCreateInfo* layer_info = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
    while (layer_info && (layer_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layer_info->function != VK_LAYER_LINK_INFO)) {
        layer_info = (VkLayerDeviceCreateInfo*)layer_info->pNext;
    }

    if (!layer_info)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGIPA = layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = layer_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice nextCreateDevice = (PFN_vkCreateDevice)nextGIPA(NULL, "vkCreateDevice");

    layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

    VkResult result = nextCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS) {
        GamePlug::DispatchManager::Get().AddDevice(*pDevice, nextGDPA);
    }

    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateWin32SurfaceKHR(
    VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (!inst_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = inst_entry->table.vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    if (result == VK_SUCCESS) {
        GamePlug::OverlayRenderer::Get().SetWindow(pCreateInfo->hwnd);
    }
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(g_Instance);
    if (!inst_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    return inst_entry->table.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(g_Instance);
    if (!inst_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    return inst_entry->table.vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {

    GamePlug::Logger::info("vkCreateSwapchainKHR: Entry. Game Requested Extent=" + std::to_string(pCreateInfo->imageExtent.width) + "x" +
                           std::to_string(pCreateInfo->imageExtent.height));
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);

    VkSwapchainCreateInfoKHR spoofInfo = *pCreateInfo;
    uint32_t sw = pCreateInfo->imageExtent.width;
    uint32_t sh = pCreateInfo->imageExtent.height;

    uint32_t nativeW = GamePlug::ImageTracker::Get().GetScreenWidth();
    uint32_t nativeH = GamePlug::ImageTracker::Get().GetScreenHeight();
    if (nativeW > 0 && nativeH > 0 && sw < nativeW) {
        GamePlug::Logger::info(
            "vkCreateSwapchainKHR: OVERRIDING 720p backbuffer to " + std::to_string(nativeW) + "x" + std::to_string(nativeH));
        spoofInfo.imageExtent = {nativeW, nativeH};
    }

    VkResult result = dev_entry->table.vkCreateSwapchainKHR(device, &spoofInfo, pAllocator, pSwapchain);
    GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.2 (Result=" + std::to_string(result) + ")");

    if (result == VK_SUCCESS) {
        GamePlug::Logger::info("vkCreateSwapchainKHR: Trace 10.3 (Getting images)");
        uint32_t imageCount = 0;
        dev_entry->table.vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
        std::vector<VkImage> images(imageCount);
        dev_entry->table.vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, images.data());

        VkImageCreateInfo scCreateInfo = {};
        scCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        scCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        scCreateInfo.format = pCreateInfo->imageFormat;
        scCreateInfo.extent = {pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, 1};
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
        GamePlug::OverlayRenderer::Get().SetupSwapchain(
            *pSwapchain, pCreateInfo->imageFormat, pCreateInfo->imageExtent, imageCount, images);
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

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = dev_entry->table.vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        GamePlug::OverlayRenderer::Get().SetCurrentSwapchainImage(*pImageIndex);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::debug("Hook: vkDestroySwapchainKHR Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }
    GamePlug::Logger::debug("Hook: vkDestroySwapchainKHR Exit");
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::info("vkDestroyDevice intercepted - Shutting down GamePlug");
    GamePlug::OverlayRenderer::Get().Shutdown();

    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        dev_entry->table.vkDestroyDevice(device, pAllocator);
    }
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    GamePlug::Logger::info("vkDestroyInstance intercepted");
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry) {
        inst_entry->table.vkDestroyInstance(instance, pAllocator);
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_DeviceWaitIdle(VkDevice device) {
    GamePlug::Logger::debug("Hook: vkDeviceWaitIdle Entry");
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkDeviceWaitIdle(device);
    GamePlug::Logger::debug("Hook: vkDeviceWaitIdle Exit");
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    auto* queue_dispatch = GamePlug::DispatchManager::Get().GetQueueDispatch(queue);

    if (queue_dispatch && pPresentInfo && pPresentInfo->swapchainCount > 0) {
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

        GamePlug::OverlayRenderer::Get().EndFrame();

        return result;
    }

    return VK_ERROR_INITIALIZATION_FAILED;
}

} // extern "C"
