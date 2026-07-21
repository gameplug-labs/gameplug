#pragma once

#include "plugin_interface.h"
#include "plugin_vulkan.h"
#include "plugin_d3d11.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace GamePlug {

/**
 * @brief Base class for GamePlug plugins.
 *
 * Inherit from this class and use REGISTER_GAMEPLUG_PLUGIN(YourClass)
 * to create a C++ friendly plugin.
 */
class Plugin {
public:
    virtual ~Plugin() = default;

    /**
     * @brief Returns the display name of the plugin.
     */
    virtual const char* GetName() const = 0;

    /**
     * @brief Called when the plugin is loaded.
     * Default implementation sets up ImGui context and the GamePlug logger.
     */
    virtual void OnInit(
        ImGuiContext* context, void (*LogFunc)(GamePlugPluginInterface::PluginLogLevel, const char*, void*), void* logContext) {
        ImGui::SetCurrentContext(context);
        GamePlug::Logger::set_log(LogFunc, logContext);
    }

    /**
     * @brief Called every frame when the overlay is active.
     */
    virtual void OnImGuiRender() {}

    /**
     * @brief Called before the plugin is unloaded.
     */
    virtual void OnShutdown() {}

    /**
     * @brief Called when fields are modified in the UI.
     */
    virtual void OnFieldsChanged() {}

    /**
     * @brief Returns the field descriptors for the plugin.
     * @param outFields Pointer to store the address of the fields array.
     * @return Number of fields.
     */
    virtual int GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) {
        if (outFields)
            *outFields = nullptr;
        return 0;
    }

    /**
     * @brief Called when the graphics context is initialized or when the plugin is loaded.
     */
    virtual void OnGraphicsInit(const GamePlugGraphicsContext* context) {}

    // Queue & Swapchain
    virtual void OnGetDeviceQueue(void* device, unsigned int queueFamilyIndex, unsigned int queueIndex, void** pQueue) {}
    virtual void OnGetDeviceQueue2(void* device, const void* pQueueInfo, void** pQueue) {}
    virtual void OnDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator) {}
    virtual void OnQueuePresent(void* queue, const void* pPresentInfo) {}
    virtual void OnAcquireNextImageKHR(void* device, void* swapchain, unsigned long long timeout, void* semaphore, void* fence, unsigned int* pImageIndex) {}
    virtual void OnGetSwapchainImagesKHR(void* device, void* swapchain, unsigned int* pSwapchainImageCount, void** pSwapchainImages) {}

    // Surface capabilities
    virtual void OnCreateWin32SurfaceKHR(void* instance, const void* pCreateInfo, const void* pAllocator, void** pSurface) {}
    virtual void OnGetPhysicalDeviceSurfaceCapabilitiesKHR(void* physicalDevice, void* surface, void* pSurfaceCapabilities) {}
    virtual void OnGetPhysicalDeviceSurfaceCapabilities2KHR(void* physicalDevice, const void* pSurfaceInfo, void* pSurfaceCapabilities) {}

    // Resources
    virtual void OnCreateImage(void* device, void* pCreateInfo, const void* pAllocator, void** pImage) {}
    virtual void OnDestroyImage(void* device, void* image, const void* pAllocator) {}
    virtual void OnCreateImageView(void* device, const void* pCreateInfo, const void* pAllocator, void** pView) {}
    virtual void OnDestroyImageView(void* device, void* imageView, const void* pAllocator) {}
    virtual void OnCreateFramebuffer(void* device, const void* pCreateInfo, const void* pAllocator, void** pFramebuffer) {}
    virtual void OnDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator) {}
    virtual void OnAllocateMemory(void* device, const void* pAllocateInfo, const void* pAllocator, void** pMemory) {}
    virtual void OnBindImageMemory(void* device, void* image, void* memory, unsigned long long memoryOffset) {}
    virtual void OnCreateRenderPass(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {}
    virtual void OnCreateGraphicsPipelines(void* device, void* pipelineCache, unsigned int createInfoCount, const void* pCreateInfos, const void* pAllocator, void** pPipelines) {}

    // Commands
    virtual void OnAllocateCommandBuffers(void* device, const void* pAllocateInfo, void** pCommandBuffers) {}
    virtual void OnBeginCommandBuffer(void* commandBuffer, const void* pBeginInfo) {}
    virtual void OnEndCommandBuffer(void* commandBuffer) {}
    virtual void OnCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, unsigned int contents) {}
    virtual void OnCmdEndRenderPass(void* commandBuffer) {}
    virtual void OnCmdSetViewport(void* commandBuffer, unsigned int firstViewport, unsigned int viewportCount, const void* pViewports) {}
    virtual void OnCmdSetScissor(void* commandBuffer, unsigned int firstScissor, unsigned int scissorCount, const void* pScissors) {}
    virtual void OnCmdBeginRendering(void* commandBuffer, const void* pRenderingInfo) {}
    virtual void OnCmdEndRendering(void* commandBuffer) {}
    virtual void OnCmdBeginRenderingKHR(void* commandBuffer, const void* pRenderingInfo) {}
    virtual void OnCmdEndRenderingKHR(void* commandBuffer) {}

    /**
     * @brief Query the plugin for its graphics API-specific hook interfaces.
     */
    virtual void* RequestHookInterface(GamePlugHookInterfaceId id) { return nullptr; }

    /**
     * @brief Internal helper to populate the C interface struct.
     */
    static GamePlugPluginInterface* GetInterface(Plugin* instance) {
        static Plugin* s_Instance = nullptr;
        static GamePlugPluginInterface s_Interface = {};

        s_Instance = instance;

        s_Interface.InterfaceVersion = 9;
        s_Interface.GetName = []() -> const char* { return s_Instance->GetName(); };
        s_Interface.OnInit = [](ImGuiContext* ctx, void (*log)(GamePlugPluginInterface::PluginLogLevel, const char*, void*), void* lctx) {
            s_Instance->OnInit(ctx, log, lctx);
        };
        s_Interface.OnImGuiRender = []() { s_Instance->OnImGuiRender(); };
        s_Interface.OnShutdown = []() { s_Instance->OnShutdown(); };
        s_Interface.OnFieldsChanged = []() { s_Instance->OnFieldsChanged(); };
        s_Interface.GetFields = [](GamePlugPluginInterface::FieldDescriptor** out) -> int { return s_Instance->GetFields(out); };
        s_Interface.OnGraphicsInit = [](const GamePlugGraphicsContext* ctx) { s_Instance->OnGraphicsInit(ctx); };
        s_Interface.RequestHookInterface = [](GamePlugHookInterfaceId id) -> void* {
            void* custom = s_Instance->RequestHookInterface(id);
            if (custom) {
                return custom;
            }
            if (id == GP_INTERFACE_VULKAN) {
                static GamePlugVulkanHookInterface vkHooks = {
                    .vkGetDeviceQueue = [](void* device, unsigned int queueFamilyIndex, unsigned int queueIndex, void** pQueue) {
                        s_Instance->OnGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
                    },
                    .vkGetDeviceQueue2 = [](void* device, const void* pQueueInfo, void** pQueue) {
                        s_Instance->OnGetDeviceQueue2(device, pQueueInfo, pQueue);
                    },

                    .vkDestroySwapchainKHR = [](void* device, void* swapchain, const void* pAllocator) {
                        s_Instance->OnDestroySwapchainKHR(device, swapchain, pAllocator);
                    },
                    .vkQueuePresentKHR = [](void* queue, const void* pPresentInfo) {
                        s_Instance->OnQueuePresent(queue, pPresentInfo);
                    },
                    .vkAcquireNextImageKHR = [](void* device, void* swapchain, unsigned long long timeout, void* semaphore, void* fence, unsigned int* pImageIndex) {
                        s_Instance->OnAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
                    },
                    .vkGetSwapchainImagesKHR = [](void* device, void* swapchain, unsigned int* pSwapchainImageCount, void** pSwapchainImages) {
                        s_Instance->OnGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
                    },
                    .vkCreateWin32SurfaceKHR = [](void* instance, const void* pCreateInfo, const void* pAllocator, void** pSurface) {
                        s_Instance->OnCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
                    },
                    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = [](void* physicalDevice, void* surface, void* pSurfaceCapabilities) {
                        s_Instance->OnGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
                    },
                    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = [](void* physicalDevice, const void* pSurfaceInfo, void* pSurfaceCapabilities) {
                        s_Instance->OnGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
                    },
                    .vkCreateImage = [](void* device, void* pCreateInfo, const void* pAllocator, void** pImage) {
                        s_Instance->OnCreateImage(device, pCreateInfo, pAllocator, pImage);
                    },
                    .vkDestroyImage = [](void* device, void* image, const void* pAllocator) {
                        s_Instance->OnDestroyImage(device, image, pAllocator);
                    },
                    .vkCreateImageView = [](void* device, const void* pCreateInfo, const void* pAllocator, void** pView) {
                        s_Instance->OnCreateImageView(device, pCreateInfo, pAllocator, pView);
                    },
                    .vkDestroyImageView = [](void* device, void* imageView, const void* pAllocator) {
                        s_Instance->OnDestroyImageView(device, imageView, pAllocator);
                    },
                    .vkCreateFramebuffer = [](void* device, const void* pCreateInfo, const void* pAllocator, void** pFramebuffer) {
                        s_Instance->OnCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
                    },
                    .vkDestroyFramebuffer = [](void* device, void* framebuffer, const void* pAllocator) {
                        s_Instance->OnDestroyFramebuffer(device, framebuffer, pAllocator);
                    },
                    .vkAllocateMemory = [](void* device, const void* pAllocateInfo, const void* pAllocator, void** pMemory) {
                        s_Instance->OnAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
                    },
                    .vkBindImageMemory = [](void* device, void* image, void* memory, unsigned long long memoryOffset) {
                        s_Instance->OnBindImageMemory(device, image, memory, memoryOffset);
                    },
                    .vkCreateRenderPass = [](void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {
                        s_Instance->OnCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
                    },
                    .vkCreateGraphicsPipelines = [](void* device, void* pipelineCache, unsigned int createInfoCount, const void* pCreateInfos, const void* pAllocator, void** pPipelines) {
                        s_Instance->OnCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
                    },
                    .vkAllocateCommandBuffers = [](void* device, const void* pAllocateInfo, void** pCommandBuffers) {
                        s_Instance->OnAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
                    },
                    .vkBeginCommandBuffer = [](void* commandBuffer, const void* pBeginInfo) {
                        s_Instance->OnBeginCommandBuffer(commandBuffer, pBeginInfo);
                    },
                    .vkEndCommandBuffer = [](void* commandBuffer) {
                        s_Instance->OnEndCommandBuffer(commandBuffer);
                    },
                    .vkCmdBeginRenderPass = [](void* commandBuffer, const void* pRenderPassBegin, unsigned int contents) {
                        s_Instance->OnCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
                    },
                    .vkCmdEndRenderPass = [](void* commandBuffer) {
                        s_Instance->OnCmdEndRenderPass(commandBuffer);
                    },
                    .vkCmdSetViewport = [](void* commandBuffer, unsigned int firstViewport, unsigned int viewportCount, const void* pViewports) {
                        s_Instance->OnCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
                    },
                    .vkCmdSetScissor = [](void* commandBuffer, unsigned int firstScissor, unsigned int scissorCount, const void* pScissors) {
                        s_Instance->OnCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
                    },
                    .vkCmdBeginRendering = [](void* commandBuffer, const void* pRenderingInfo) {
                        s_Instance->OnCmdBeginRendering(commandBuffer, pRenderingInfo);
                    },
                    .vkCmdEndRendering = [](void* commandBuffer) {
                        s_Instance->OnCmdEndRendering(commandBuffer);
                    },
                    .vkCmdBeginRenderingKHR = [](void* commandBuffer, const void* pRenderingInfo) {
                        s_Instance->OnCmdBeginRenderingKHR(commandBuffer, pRenderingInfo);
                    },
                    .vkCmdEndRenderingKHR = [](void* commandBuffer) {
                        s_Instance->OnCmdEndRenderingKHR(commandBuffer);
                    }
                };
                return &vkHooks;
            }
            return nullptr;
        };

        return &s_Interface;
    }
};

} // namespace GamePlug

/**
 * @brief Macro to register a GamePlug plugin.
 * Place this at the bottom of your .cpp file.
 */
#define REGISTER_GAMEPLUG_PLUGIN(ClassName)                                                 \
    extern "C" GamePlug_PLUGIN_API GamePlugPluginInterface* GamePlug_GetPluginInterface() { \
        static ClassName instance;                                                          \
        return GamePlug::Plugin::GetInterface(&instance);                                   \
    }
