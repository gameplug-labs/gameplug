#include "vk_layer_exports.h"
#include "dispatch.h"

extern "C" {

// Global tracking definitions
VkInstance g_Instance = VK_NULL_HANDLE;
VkPhysicalDevice g_PhysDevice = VK_NULL_HANDLE;

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::string(pName) == "vkGetDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vkCreateDevice")
        return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    if (std::string(pName) == "vkGetDeviceQueue")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR")
        return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR")
        return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkGetSwapchainImagesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetSwapchainImagesKHR;
    if (std::string(pName) == "vkCreateImage")
        return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage")
        return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView")
        return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView")
        return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory")
        return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory")
        return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines")
        return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers")
        return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice")
        return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance")
        return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle")
        return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport")
        return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor")
        return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;

    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry)
        return dev_entry->pfnNextGetDeviceProcAddr(device, pName);
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::string(pName) == "vkGetInstanceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetInstanceProcAddr;
    if (std::string(pName) == "vkGetDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vk_layerGetPhysicalDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceProcAddr;
    if (std::string(pName) == "vkCreateInstance")
        return (PFN_vkVoidFunction)GamePlug_CreateInstance;
    if (std::string(pName) == "vkCreateDevice")
        return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    if (std::string(pName) == "vkCreateWin32SurfaceKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateWin32SurfaceKHR;
    if (std::string(pName) == "vkGetDeviceQueue")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR")
        return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR")
        return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkGetSwapchainImagesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetSwapchainImagesKHR;
    if (std::string(pName) == "vkCreateImage")
        return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage")
        return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView")
        return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView")
        return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory")
        return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory")
        return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines")
        return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers")
        return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice")
        return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance")
        return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle")
        return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport")
        return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor")
        return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;

    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry)
        return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry)
        return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetInstanceProcAddr = GamePlug_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = GamePlug_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = GamePlug_GetPhysicalDeviceProcAddr;
    } else
        return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

} // extern "C"
