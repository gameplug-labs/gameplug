#pragma once
#include <vulkan/vulkan.h>
#include <set>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "framework_export.h"

namespace GamePlug {

struct ImageInfo {
    VkImage image;
    VkFormat format;
    VkExtent3D extent;
    VkImageUsageFlags usage;
    std::string name;
};

class FRAMEWORK_API ImageTracker {
public:
    static ImageTracker& Get() {
        static ImageTracker instance;
        return instance;
    }

    void TrackImage(VkImage image, const VkImageCreateInfo* pCreateInfo);
    void UntrackImage(VkImage image);

    void TrackImageView(VkImageView view, VkImage image);
    void UntrackImageView(VkImageView view);

    void TrackFramebuffer(VkFramebuffer fb, uint32_t count, const VkImageView* pViews);
    void UntrackFramebuffer(VkFramebuffer fb);

    void SetScreenDimensions(uint32_t width, uint32_t height) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_screenWidth != width || m_screenHeight != height) {
            m_screenWidth = width;
            m_screenHeight = height;
            m_bestDepthScore = -1.0f;
            m_bestMVScore = -1.0f;
        }
    }

    uint32_t GetScreenWidth() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_screenWidth;
    }

    uint32_t GetScreenHeight() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_screenHeight;
    }

    void ResetScores() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_bestDepthScore = -1.0f;
        m_bestMVScore = -1.0f;
    }

    void SetCurrentDepthBuffer(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentDepthBuffer = image;
    }

    VkImage GetCurrentDepthBuffer(uint32_t w = 0, uint32_t h = 0) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (w > 0 && h > 0) {
            uint64_t key = ((uint64_t)w << 32) | h;
            if (m_bestDepthPerRes.find(key) != m_bestDepthPerRes.end()) return m_bestDepthPerRes.at(key);
        }
        return m_currentDepthBuffer;
    }

    void SetCurrentMotionVectors(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentMVBuffer = image;
    }

    VkImage GetCurrentMotionVectors(uint32_t w = 0, uint32_t h = 0) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (w > 0 && h > 0) {
            uint64_t key = ((uint64_t)w << 32) | h;
            if (m_bestMVPerRes.find(key) != m_bestMVPerRes.end()) return m_bestMVPerRes.at(key);
        }
        return m_currentMVBuffer;
    }

    ImageInfo GetCurrentDepthInfo(uint32_t w = 0, uint32_t h = 0) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        VkImage img = GetCurrentDepthBuffer(w, h);
        if (img != VK_NULL_HANDLE) return m_images[img];
        return {};
    }

    ImageInfo GetCurrentMVInfo(uint32_t w = 0, uint32_t h = 0) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        VkImage img = GetCurrentMotionVectors(w, h);
        if (img != VK_NULL_HANDLE) return m_images[img];
        return {};
    }

    ImageInfo GetImageInfo(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_images.find(image) != m_images.end()) {
            return m_images[image];
        }
        return {};
    }

    VkImageView GetMainView(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_mainViews.find(image) != m_mainViews.end()) {
            return m_mainViews[image];
        }
        // Fallback: search all views
        for (auto const& [view, img] : m_views) {
            if (img == image) {
                m_mainViews[image] = view; // Cache it
                return view;
            }
        }
        return VK_NULL_HANDLE;
    }


    // Resolves all images associated with a framebuffer
    std::vector<VkImage> GetFramebufferAttachments(VkFramebuffer fb) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_framebuffers.find(fb) != m_framebuffers.end()) {
            return m_framebuffers[fb];
        }
        return {};
    }


    bool IsSceneFramebuffer(VkFramebuffer fb, uint32_t renderW, uint32_t renderH);
    bool IsSwapchainFramebuffer(VkFramebuffer fb);
    VkImage GetSwapchainImageFromFramebuffer(VkFramebuffer fb);
    VkImage GetBestSceneSourceFromFramebuffer(VkFramebuffer fb, uint32_t renderW, uint32_t renderH);
    VkImage GetColorAttachment(VkFramebuffer fb, uint32_t renderW, uint32_t renderH);

    void RegisterSwapchainImage(VkImage image);
    void SetSwapchainFormat(VkFormat format);
    VkFormat GetSwapchainFormat() const;

    void SaveSceneImage(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_lastSceneSource = image;
    }
    
    VkImage GetLastSceneImage() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_lastSceneSource;
    }

private:
    ImageTracker() = default;
    std::map<VkImage, ImageInfo> m_images;
    std::set<VkImage> m_swapchainImages;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    std::map<VkImageView, VkImage> m_views;
    std::map<VkImage, VkImageView> m_mainViews;
    std::map<VkFramebuffer, std::vector<VkImage>> m_framebuffers;

    // Per-resolution tracking
    std::map<uint64_t, VkImage> m_bestDepthPerRes;
    std::map<uint64_t, VkImage> m_bestMVPerRes;
    std::map<uint64_t, float> m_bestDepthScorePerRes;
    std::map<uint64_t, float> m_bestMVScorePerRes;

    VkImage m_currentDepthBuffer = VK_NULL_HANDLE;
    VkImage m_currentMVBuffer = VK_NULL_HANDLE;
    VkImage m_lastSceneSource = VK_NULL_HANDLE;
    float m_bestDepthScore = -1.0f;
    float m_bestMVScore = -1.0f;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    mutable std::recursive_mutex m_mutex;

    uint64_t GetResKey(uint32_t w, uint32_t h) const {
        return ((uint64_t)w << 32) | h;
    }
};

} // namespace GamePlug
