#pragma once

#include <vulkan/vulkan.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_vulkan.h"
#include <vector>
#include <map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <mutex>

#include "framework_export.h"

namespace GamePlug {

struct SwapchainResources {
    VkSwapchainKHR swapchain;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass;
    VkExtent2D extent;
    VkFormat format;
};

class FRAMEWORK_API OverlayRenderer {
public:
    static OverlayRenderer& Get();

    // Initialize ImGui for a specific device. Called once per device.
    void SetupDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, 
                     uint32_t queueFamily, VkQueue queue);
    
    // Set the window handle for interaction
    void SetWindow(HWND hWnd);
    
    // Setup for a specific swapchain. Called during/after vkCreateSwapchainKHR.
    void SetupSwapchain(VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent, 
                        uint32_t imageCount, const std::vector<VkImage>& images);

    // Clean up resources
    void Shutdown();

    // Prepare a new frame. Called at start of present or similar.
    void NewFrame();

    // Finalize frame state. Called at end of frame (present).
    void EndFrame();
    
    // Draw the overlay (Upscale + ImGui) into the current swapchain image
    void Render(VkCommandBuffer cmd, VkImage source, VkImage target, uint32_t width, uint32_t height);

    // Manual fallback rendering for cases where no swapchain pass is intercepted
    void RenderStandalone(VkImage target, uint32_t width, uint32_t height);

    VkSemaphore GetRenderCompleteSemaphore(uint32_t imageIndex) {
        if (imageIndex < m_renderCompleteSemaphores.size()) return m_renderCompleteSemaphores[imageIndex];
        return VK_NULL_HANDLE;
    }

    bool IsInitialized() const { return m_initialized; }
    bool IsVisible() const { return m_visible; }
    HWND GetWindow() const { return m_hWnd; }

    static bool IsRenderingOverlay();
    static void SetIsRenderingOverlay(bool val);
    
    VkExtent2D GetSwapchainExtent() const { return m_swapchainRes.extent; }
    VkImage GetSwapchainImage(uint32_t index) const { 
        if (index < m_swapchainRes.images.size()) return m_swapchainRes.images[index];
        return VK_NULL_HANDLE; 
    }
    
    void SetCurrentSwapchainImage(uint32_t index) { m_currentSwapchainImage = index; }
    uint32_t GetCurrentSwapchainImage() const { return m_currentSwapchainImage; }

    // Visual Debugging Support
    VkDescriptorSet RegisterDebugImage(VkImageView view, VkImageLayout layout);
    void UnregisterDebugImage(VkDescriptorSet set);

private:
    OverlayRenderer() = default;
    ~OverlayRenderer() = default;

    bool m_initialized = false;
    bool m_deviceSetup = false;
    bool m_fontUploaded = false;
    bool m_visible = true;
    bool m_showKeyWasPressed = false;
    
    struct LoaderContext {
        PFN_vkGetInstanceProcAddr nextGIPA;
        VkInstance instance;
        VkDevice device;
    } m_loaderContext = {};

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    
    HWND m_hWnd = NULL;
    WNDPROC m_originalWndProc = NULL;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_renderCompleteSemaphores;
    
    SwapchainResources m_swapchainRes = {};
    
    std::map<VkDescriptorSet, VkSampler> m_debugSamplers;
    std::vector<VkDescriptorSet> m_debugSets;
    
    uint32_t m_currentSwapchainImage = 0;
    bool m_uiRendered = false;
    bool m_frameStarted = false;
    std::chrono::steady_clock::time_point m_lastTime;

public:
    bool IsUIRendered() const { return m_uiRendered; }
    
    mutable std::mutex m_renderMtx;
    void CreateRenderPass(VkFormat format);
    void CreateFramebuffers();
    void CleanupSwapchain();

};

// Global frame hook
void OnFrame();

} // namespace GamePlug
