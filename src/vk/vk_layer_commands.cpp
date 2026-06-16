#include "dispatch.h"
#include "image_tracker.h"
#include "logger.h"
#include "overlay.h"
#include "upscaler_manager.h"
#include "vk_layer_exports.h"
#include <chrono>
#include <map>

extern "C" {

static std::map<VkCommandBuffer, VkFramebuffer> g_ActiveFBs;

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
    uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (!dev_entry)
        return VK_ERROR_INITIALIZATION_FAILED;

    static uint32_t logCount = 0;
    if (logCount < 10) {
        for (uint32_t i = 0; i < createInfoCount; i++) {
            if (pCreateInfos[i].pViewportState) {
                if (pCreateInfos[i].pViewportState->pViewports) {
                    GamePlug::Logger::info("Pipeline " + std::to_string(i) +
                                           " has FIXED Viewport: " + std::to_string(pCreateInfos[i].pViewportState->pViewports[0].width) +
                                           "x" + std::to_string(pCreateInfos[i].pViewportState->pViewports[0].height));
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

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetViewport(
    VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

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

    dev_entry->table.vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    auto* dev_entry = GamePlug::DispatchManager::Get().GetDeviceByCommandBuffer(commandBuffer);
    if (!dev_entry)
        return;

    VkFramebuffer fb = g_ActiveFBs[commandBuffer];

    dev_entry->table.vkCmdEndRenderPass(commandBuffer);

    if (fb != VK_NULL_HANDLE) {
        uint32_t sw = GamePlug::ImageTracker::Get().GetScreenWidth();
        uint32_t sh = GamePlug::ImageTracker::Get().GetScreenHeight();
        uint32_t rw = GamePlug::UpscalerManager::Get().GetRenderWidth();
        uint32_t rh = GamePlug::UpscalerManager::Get().GetRenderHeight();

        if (GamePlug::ImageTracker::Get().IsSceneFramebuffer(fb, rw, rh)) {
            VkImage sceneImg = GamePlug::ImageTracker::Get().GetColorAttachment(fb, rw, rh);
            if (sceneImg != VK_NULL_HANDLE) {
                GamePlug::ImageTracker::Get().SaveSceneImage(sceneImg);
            }
        }

        if (GamePlug::ImageTracker::Get().IsSwapchainFramebuffer(fb) && !GamePlug::UpscalerManager::Get().WasUpscaledThisFrame()) {
            static auto g_startupTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_startupTime).count();

            VkImage source = VK_NULL_HANDLE;
            if (elapsed >= 2) {
                source = GamePlug::ImageTracker::Get().GetFakeBackBufferImage();
            }
            if (source == VK_NULL_HANDLE) {
                source = GamePlug::ImageTracker::Get().GetLastSceneImage();
            }
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

} // extern "C"
