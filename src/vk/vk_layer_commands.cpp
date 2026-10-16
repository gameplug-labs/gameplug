#include "dispatch.h"
#include "image_tracker.h"
#include "logger.h"
#include "overlay.h"
#include "vk_layer_exports.h"
#include "plugin_manager.h"
#include <map>

extern "C" {

static std::map<VkCommandBuffer, VkFramebuffer> g_ActiveFBs;

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
    uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    GamePlug::PluginManager::Get().DispatchCreateGraphicsPipelines((void*)device, (void*)pipelineCache, createInfoCount, (const void*)pCreateInfos, (const void*)pAllocator, (void**)pPipelines);

    VkResult result = dev_entry->table.vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetViewport(
    VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdSetViewport((void*)commandBuffer, firstViewport, viewportCount, (const void*)pViewports);

    if (GamePlug::OverlayRenderer::IsRenderingOverlay()) {
        dev_entry->table.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
        return;
    }

    dev_entry->table.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetScissor(
    VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdSetScissor((void*)commandBuffer, firstScissor, scissorCount, (const void*)pScissors);

    if (GamePlug::OverlayRenderer::IsRenderingOverlay()) {
        dev_entry->table.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
        return;
    }

    dev_entry->table.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    GamePlug::PluginManager::Get().DispatchAllocateCommandBuffers((void*)device, (const void*)pAllocateInfo, (void**)pCommandBuffers);

    VkResult result = dev_entry->table.vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
    if (result == VK_SUCCESS && pCommandBuffers) {
        for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
            GamePlug::DispatchManager::Get().AddCommandBuffer(pCommandBuffers[i], device);
        }
    }
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    GamePlug::PluginManager::Get().DispatchBeginCommandBuffer((void*)commandBuffer, (const void*)pBeginInfo);

    VkResult result = dev_entry->table.vkBeginCommandBuffer(commandBuffer, pBeginInfo);
    if (result != VK_SUCCESS) {
        GamePlug::Logger::error("vkBeginCommandBuffer failed with " + std::to_string(result));
    }
    return result;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_EndCommandBuffer(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    GamePlug::PluginManager::Get().DispatchEndCommandBuffer((void*)commandBuffer);

    VkResult result = dev_entry->table.vkEndCommandBuffer(commandBuffer);
    return result;
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRenderPass(
    VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;
    
    GamePlug::OverlayRenderer::Get().NewFrame();
    
    if (pRenderPassBegin) {
        g_ActiveFBs[commandBuffer] = pRenderPassBegin->framebuffer;
        
        static uint32_t logCount = 0;
        if (logCount++ % 500 == 0) {
            GamePlug::Logger::debug("CmdBeginRenderPass: FB=" + std::to_string((uintptr_t)pRenderPassBegin->framebuffer) +
            " Area=" + std::to_string(pRenderPassBegin->renderArea.extent.width) + "x" +
            std::to_string(pRenderPassBegin->renderArea.extent.height));
        }
        
        if (GamePlug::ImageTracker::Get().IsSwapchainFramebuffer(pRenderPassBegin->framebuffer)) {
            VkImage source = GamePlug::ImageTracker::Get().GetLastSceneImage();
            VkImage target = GamePlug::ImageTracker::Get().GetSwapchainImageFromFramebuffer(pRenderPassBegin->framebuffer);
            
            uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
            uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();
        }
    }
    
    GamePlug::PluginManager::Get().DispatchCmdBeginRenderPass((void*)commandBuffer, (const void*)pRenderPassBegin, (unsigned int)contents);
    dev_entry->table.vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    VkFramebuffer fb = g_ActiveFBs[commandBuffer];
    
    GamePlug::PluginManager::Get().DispatchCmdEndRenderPass((void*)commandBuffer);
    dev_entry->table.vkCmdEndRenderPass(commandBuffer);

    if (fb != VK_NULL_HANDLE) {
        uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
        uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();
        uint32_t rw = sw;
        uint32_t rh = sh;

        if (GamePlug::ImageTracker::Get().IsSceneFramebuffer(fb, rw, rh)) {
            VkImage sceneImg = GamePlug::ImageTracker::Get().GetColorAttachment(fb, rw, rh);
            if (sceneImg != VK_NULL_HANDLE) {
                GamePlug::ImageTracker::Get().SaveSceneImage(sceneImg);
            }
        }

        if (GamePlug::ImageTracker::Get().IsSwapchainFramebuffer(fb)) {
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

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRendering(
    VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdBeginRendering((void*)commandBuffer, (const void*)pRenderingInfo);

    dev_entry->table.vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRendering(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdEndRendering((void*)commandBuffer);

    dev_entry->table.vkCmdEndRendering(commandBuffer);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRenderingKHR(
    VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdBeginRenderingKHR((void*)commandBuffer, (const void*)pRenderingInfo);

    dev_entry->table.vkCmdBeginRenderingKHR(commandBuffer, pRenderingInfo);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderingKHR(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    GamePlug::PluginManager::Get().DispatchCmdEndRenderingKHR((void*)commandBuffer);

    dev_entry->table.vkCmdEndRenderingKHR(commandBuffer);
}

} // extern "C"
