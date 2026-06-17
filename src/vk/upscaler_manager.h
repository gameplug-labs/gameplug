#pragma once
#ifdef GAMEPLUG_VULKAN
#include <vulkan/vulkan.h>
#else
#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#endif
#endif
#include "framework_export.h"
#include "upscaler_interface.h"
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace GamePlug {

class FRAMEWORK_API UpscalerManager {
public:
    static UpscalerManager& Get() {
        static UpscalerManager instance;
        return instance;
    }

    ~UpscalerManager();

    bool LoadUpscaler(uintptr_t instance, uintptr_t physDevice, uintptr_t device = 0);
    void UnloadUpscaler();

    void RenderUI(float fps, uint32_t width, uint32_t height);
    void RenderFrame(uintptr_t cmd, uint64_t source, uint64_t target, uint32_t width, uint32_t height);
    void NewFrame() { m_frameUpscaled = false; }
    bool WasUpscaledThisFrame() const { return m_frameUpscaled; }

    bool IsLoaded() const { return m_handle != nullptr; }
    bool IsUpscalingEnabled() const;
    uint32_t GetRenderWidth() const { return m_renderWidth; }
    uint32_t GetRenderHeight() const { return m_renderHeight; }

    void UpdateDimensions(uint32_t width, uint32_t height);
    void GetTargetResolution(uint32_t width, uint32_t height, uint32_t& outW, uint32_t& outH);

private:
    UpscalerManager()
        : m_handle(nullptr)
        , m_pInterface(nullptr) {}

    HMODULE m_handle;
    GamePlugUpscalerInterface* m_pInterface;

#ifdef GAMEPLUG_VULKAN
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memProps = {};

    // Phase 6 Visual Debugging
    bool m_showDepthDebug = false;
    bool m_showMVDebug = false;
    VkImageView m_lastDepthView = VK_NULL_HANDLE;
    VkImageView m_lastMVView = VK_NULL_HANDLE;
    VkDescriptorSet m_depthDebugSet = VK_NULL_HANDLE;
    VkDescriptorSet m_mvDebugSet = VK_NULL_HANDLE;

    // Downsample Compute Pipeline
    VkShaderModule m_downsampleShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_downsampleSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_downsamplePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_downsamplePipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_downsampleDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_downsampleDescriptorSet = VK_NULL_HANDLE;
    VkSampler m_downsampleSampler = VK_NULL_HANDLE;

    VkImage m_downsampledDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_downsampledDepthMemory = VK_NULL_HANDLE;
    VkImageView m_downsampledDepthView = VK_NULL_HANDLE;

    uint32_t m_downsampleW = 0;
    uint32_t m_downsampleH = 0;

    void InitDownsampleResources();
    void CleanupDownsampleResources();
    bool CreateDownsampleTarget(uint32_t w, uint32_t h);
    void PerformDepthDownsamplingVK(VkCommandBuffer cmd, VkImage srcDepth, VkImageView srcView, VkFormat srcFormat, uint32_t srcW, uint32_t srcH);
#else
    uintptr_t m_instance = 0;
    uintptr_t m_physDevice = 0;
    uintptr_t m_device = 0;
#endif

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_frameUpscaled = false;
    bool m_isShuttingDown = false;
};

} // namespace GamePlug
