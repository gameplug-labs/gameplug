#include "dispatch.h"
#include "image_tracker.h"
#include "logger.h"
#include "upscaler_manager.h"
#include "vk_layer_exports.h"
static thread_local bool t_creatingFakeBackBuffer = false;

extern "C" {

VK_LAYER_EXPORT void GamePlug_SetCreatingFakeBackBuffer(bool active) {
    t_creatingFakeBackBuffer = active;
}

VK_LAYER_EXPORT bool GamePlug_IsCreatingFakeBackBuffer() {
    static bool (*pfnIsCreatingFakeBackBuffer)() = nullptr;
    static bool resolved = false;
    if (!resolved) {
        const char* dlls[] = { "dinput8.dll", "version.dll" };
        for (const char* dll : dlls) {
            HMODULE hMod = GetModuleHandleA(dll);
            if (hMod) {
                pfnIsCreatingFakeBackBuffer = (bool (*)())GetProcAddress(hMod, "GamePlug_IsCreatingFakeBackBuffer");
                if (pfnIsCreatingFakeBackBuffer) break;
            }
        }
        resolved = true;
    }
    if (pfnIsCreatingFakeBackBuffer) {
        return pfnIsCreatingFakeBackBuffer();
    }
    return t_creatingFakeBackBuffer;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImage(
    VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = dev_entry->table.vkCreateImage(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImage(*pImage, pCreateInfo);

        bool isFakeBackBuffer = GamePlug_IsCreatingFakeBackBuffer();

        if (isFakeBackBuffer) {
            GamePlug::Logger::info("GamePlug_CreateImage: Tracked fake backbuffer image {:p}, Size: {}x{}x{}, Format: {}", (void*)*pImage,
                pCreateInfo->extent.width, pCreateInfo->extent.height, pCreateInfo->extent.depth, (int)pCreateInfo->format);
            GamePlug::ImageTracker::Get().SetFakeBackBufferImage(*pImage);
        }
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
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
    VkResult result = dev_entry->table.vkCreateImageView(device, pCreateInfo, pAllocator, pView);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackImageView(*pView, pCreateInfo->image);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
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
    VkResult result = dev_entry->table.vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    if (result == VK_SUCCESS) {
        GamePlug::ImageTracker::Get().TrackFramebuffer(*pFramebuffer, pCreateInfo->attachmentCount, pCreateInfo->pAttachments);
    }
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) {
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
    VkResult result = dev_entry->table.vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BindImageMemory(
    VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkBindImageMemory(device, image, memory, memoryOffset);
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = dev_entry->table.vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    return result;
}

} // extern "C"
