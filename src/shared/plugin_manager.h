#pragma once

#include "plugin_interface.h"
#include "plugin_vulkan.h"
#include "plugin_d3d11.h"
#include <string>
#include <vector>
#include <windows.h>

#include "framework_export.h"

namespace GamePlug {

struct LoadedPlugin {
    std::string filename;
    std::string name;
    HMODULE handle;
    GamePlugPluginInterface* pInterface;

    GamePlugVulkanHookInterface* vulkanHooks;
    GamePlugD3D11HookInterface* d3d11Hooks;

    LoadedPlugin()
        : handle(NULL)
        , pInterface(nullptr)
        , vulkanHooks(nullptr)
        , d3d11Hooks(nullptr) {}
};

struct DiscoveredPlugin {
    std::string filename;
    std::string name;
    bool isLoaded;
};

class FRAMEWORK_API PluginManager {
public:
    static PluginManager& Get();

    // Call once during engine/overlay initialization.
    // Searches for plugins in (ExeDir)/GamePlug/plugins.
    void LoadPlugins();

    // Call during overlay rendering.
    void RenderPlugins();

    bool IsEmpty() const;
    const std::vector<DiscoveredPlugin>& GetDiscoveredPlugins() const;

    void LoadIndividualPlugin(DiscoveredPlugin& dp);
    void UnloadIndividualPlugin(const std::string& name);

    // Call during shutdown.
    void UnloadPlugins();

    // Set the graphics context.
    void SetGraphicsContext(const GamePlugGraphicsContext& context);

    // Update the Vulkan swapchain in the stored graphics context and re-notify plugins.
    void UpdateVulkanSwapchain(void* swapchain);

    // Initialize loaded plugins with ImGui and graphics context.
    void InitializeLoadedPlugins();

    // Dispatch direct Vulkan layer hooks to plugins
    void DispatchGetDeviceQueue(void* device, unsigned int queueFamilyIndex, unsigned int queueIndex, void** pQueue);
    void DispatchGetDeviceQueue2(void* device, const void* pQueueInfo, void** pQueue);
    void DispatchDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator);
    void DispatchQueuePresent(void* queue, const void* pPresentInfo);
    void DispatchAcquireNextImageKHR(void* device, void* swapchain, unsigned long long timeout, void* semaphore, void* fence, unsigned int* pImageIndex);
    void DispatchGetSwapchainImagesKHR(void* device, void* swapchain, unsigned int* pSwapchainImageCount, void** pSwapchainImages);
    void DispatchCreateWin32SurfaceKHR(void* instance, const void* pCreateInfo, const void* pAllocator, void** pSurface);
    void DispatchGetPhysicalDeviceSurfaceCapabilitiesKHR(void* physicalDevice, void* surface, void* pSurfaceCapabilities);
    void DispatchGetPhysicalDeviceSurfaceCapabilities2KHR(void* physicalDevice, const void* pSurfaceInfo, void* pSurfaceCapabilities);
    // void DispatchCreateSwapchain(void* device, void* pCreateInfo, void* pSwapchain);
    void DispatchCreateImage(void* device, void* pCreateInfo, const void* pAllocator, void** pImage);
    void DispatchDestroyImage(void* device, void* image, const void* pAllocator);
    void DispatchCreateImageView(void* device, const void* pCreateInfo, const void* pAllocator, void** pView);
    void DispatchDestroyImageView(void* device, void* imageView, const void* pAllocator);
    void DispatchCreateFramebuffer(void* device, const void* pCreateInfo, const void* pAllocator, void** pFramebuffer);
    void DispatchDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator);
    void DispatchAllocateMemory(void* device, const void* pAllocateInfo, const void* pAllocator, void** pMemory);
    void DispatchBindImageMemory(void* device, void* image, void* memory, unsigned long long memoryOffset);
    void DispatchCreateRenderPass(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass);
    void DispatchCreateGraphicsPipelines(void* device, void* pipelineCache, unsigned int createInfoCount, const void* pCreateInfos, const void* pAllocator, void** pPipelines);
    void DispatchAllocateCommandBuffers(void* device, const void* pAllocateInfo, void** pCommandBuffers);
    void DispatchBeginCommandBuffer(void* commandBuffer, const void* pBeginInfo);
    void DispatchEndCommandBuffer(void* commandBuffer);
    void DispatchCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, unsigned int contents);
    void DispatchCmdEndRenderPass(void* commandBuffer);
    void DispatchCmdSetViewport(void* commandBuffer, unsigned int firstViewport, unsigned int viewportCount, const void* pViewports);
    void DispatchCmdSetScissor(void* commandBuffer, unsigned int firstScissor, unsigned int scissorCount, const void* pScissors);
    void DispatchCmdBeginRendering(void* commandBuffer, const void* pRenderingInfo);
    void DispatchCmdEndRendering(void* commandBuffer);
    void DispatchCmdBeginRenderingKHR(void* commandBuffer, const void* pRenderingInfo);
    void DispatchCmdEndRenderingKHR(void* commandBuffer);

private:
    PluginManager() = default;
    ~PluginManager();

    std::vector<LoadedPlugin> m_plugins;
    std::vector<DiscoveredPlugin> m_discoveredPlugins;
    bool m_searchDone = false;

    GamePlugGraphicsContext m_graphicsContext = {};
    bool m_hasGraphicsContext = false;
};

} // namespace GamePlug
