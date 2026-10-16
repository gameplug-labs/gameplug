#include "dispatch.h"
#include "image_tracker.h"
#include "logger.h"
#include "vk_layer_exports.h"
#include "plugin_manager.h"

extern "C" {

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImage(
    VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchCreateImage((void*)device, (void*)pCreateInfo, (const void*)pAllocator, (void**)pImage);
    VkResult result = dev_entry->table.vkCreateImage(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImage(*pImage, pCreateInfo);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    GamePlug::PluginManager::Get().DispatchDestroyImage((void*)device, (void*)image, (const void*)pAllocator);
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackImage(image);
        dev_entry->table.vkDestroyImage(device, image, pAllocator);
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImageView(
    VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchCreateImageView((void*)device, (const void*)pCreateInfo, (const void*)pAllocator, (void**)pView);
    VkResult result = dev_entry->table.vkCreateImageView(device, pCreateInfo, pAllocator, pView);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImageView(*pView, pCreateInfo->image);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
    GamePlug::PluginManager::Get().DispatchDestroyImageView((void*)device, (void*)imageView, (const void*)pAllocator);
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackImageView(imageView);
        dev_entry->table.vkDestroyImageView(device, imageView, pAllocator);
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchCreateFramebuffer((void*)device, (const void*)pCreateInfo, (const void*)pAllocator, (void**)pFramebuffer);
    VkResult result = dev_entry->table.vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackFramebuffer(*pFramebuffer, pCreateInfo->attachmentCount, pCreateInfo->pAttachments);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) {
    GamePlug::PluginManager::Get().DispatchDestroyFramebuffer((void*)device, (void*)framebuffer, (const void*)pAllocator);
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry) {
        GamePlug::ImageTracker::Get().UntrackFramebuffer(framebuffer);
        dev_entry->table.vkDestroyFramebuffer(device, framebuffer, pAllocator);
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchAllocateMemory((void*)device, (const void*)pAllocateInfo, (const void*)pAllocator, (void**)pMemory);
    VkResult result = dev_entry->table.vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BindImageMemory(
    VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchBindImageMemory((void*)device, (void*)image, (void*)memory, (unsigned long long)memoryOffset);
    VkResult result = dev_entry->table.vkBindImageMemory(device, image, memory, memoryOffset);
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
        auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
        if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    GamePlug::PluginManager::Get().DispatchCreateRenderPass((void*)device, (const void*)pCreateInfo, (const void*)pAllocator, (void**)pRenderPass);
    VkResult result = dev_entry->table.vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    return result;
}

} // extern "C"
