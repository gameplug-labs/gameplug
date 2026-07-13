#pragma once
#include "framework_export.h"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

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
            if (m_bestDepthPerRes.find(key) != m_bestDepthPerRes.end())
                return m_bestDepthPerRes.at(key);
        }
        if (m_currentDepthBuffer != VK_NULL_HANDLE)
            return m_currentDepthBuffer;

        VkImage bestImg = VK_NULL_HANDLE;
        float bestScore = -1.0f;
        for (auto const& [img, info] : m_images) {
            if (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                float score = 0.0f;
                uint32_t targetW = (w > 0) ? w : m_screenWidth;
                uint32_t targetH = (h > 0) ? h : m_screenHeight;

                if (targetW > 0 && info.extent.width == targetW && info.extent.height == targetH)
                    score += 1000000.0f;
                if (info.extent.width != info.extent.height)
                    score += 500000.0f;
                if (info.extent.width >= 640 && info.extent.height >= 360)
                    score += 100000.0f;
                if (info.extent.width == info.extent.height && targetW != targetH)
                    score -= 800000.0f;
                if (info.extent.width < 320 || info.extent.height < 200)
                    score = -100.0f;

                if (score > bestScore && score > 0) {
                    bestScore = score;
                    bestImg = img;
                }
            }
        }
        return bestImg;
    }

    void SetCurrentMotionVectors(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentMVBuffer = image;
    }

    VkImage GetCurrentMotionVectors(uint32_t w = 0, uint32_t h = 0) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (w > 0 && h > 0) {
            uint64_t key = ((uint64_t)w << 32) | h;
            if (m_bestMVPerRes.find(key) != m_bestMVPerRes.end())
                return m_bestMVPerRes.at(key);
        }
        if (m_currentMVBuffer != VK_NULL_HANDLE)
            return m_currentMVBuffer;

        VkImage bestImg = VK_NULL_HANDLE;
        float bestScore = -1.0f;
        for (auto const& [img, info] : m_images) {
            bool isMVFormat = (info.format == VK_FORMAT_R16G16_SFLOAT || info.format == VK_FORMAT_R32G32_SFLOAT ||
                               info.format == VK_FORMAT_R16G16B16A16_SFLOAT || info.format == VK_FORMAT_R16G16_SNORM ||
                               info.format == VK_FORMAT_R16G16_UNORM || info.format == VK_FORMAT_R8G8_UNORM ||
                               info.format == VK_FORMAT_R8G8_SNORM);
            if (!isMVFormat)
                continue;

            if ((info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || (info.usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
                float score = 0.0f;
                uint32_t targetW = (w > 0) ? w : m_screenWidth;
                uint32_t targetH = (h > 0) ? h : m_screenHeight;

                if (targetW > 0 && info.extent.width == targetW && info.extent.height == targetH)
                    score += 500000.0f;
                if (info.format == VK_FORMAT_R16G16_SFLOAT || info.format == VK_FORMAT_R32G32_SFLOAT)
                    score += 200000.0f;

                if (score > bestScore && score > 0) {
                    bestScore = score;
                    bestImg = img;
                }
            }
        }
        return bestImg;
    }

    void SetCurrentExposureBuffer(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentExposureBuffer = image;
    }

    VkImage GetCurrentExposureBuffer() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_currentExposureBuffer != VK_NULL_HANDLE)
            return m_currentExposureBuffer;
        for (auto const& [img, info] : m_images) {
            if (info.extent.width == 1 && info.extent.height == 1) {
                return img;
            }
        }
        return VK_NULL_HANDLE;
    }

    void SetCurrentReactiveBuffer(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentReactiveBuffer = image;
    }

    VkImage GetCurrentReactiveBuffer(uint32_t w = 0, uint32_t h = 0) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_currentReactiveBuffer != VK_NULL_HANDLE)
            return m_currentReactiveBuffer;
        uint32_t targetW = (w > 0) ? w : m_screenWidth;
        uint32_t targetH = (h > 0) ? h : m_screenHeight;
        for (auto const& [img, info] : m_images) {
            if (info.extent.width == targetW && info.extent.height == targetH) {
                if (info.format == VK_FORMAT_R8_UNORM || info.format == VK_FORMAT_R16_SFLOAT) {
                    return img;
                }
            }
        }
        return VK_NULL_HANDLE;
    }

    void SetCurrentTransparencyBuffer(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_currentTransparencyBuffer = image;
    }

    VkImage GetCurrentTransparencyBuffer(uint32_t w = 0, uint32_t h = 0) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_currentTransparencyBuffer != VK_NULL_HANDLE)
            return m_currentTransparencyBuffer;
        return VK_NULL_HANDLE;
    }

    ImageInfo GetCurrentDepthInfo(uint32_t w = 0, uint32_t h = 0) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        VkImage img = GetCurrentDepthBuffer(w, h);
        if (img != VK_NULL_HANDLE)
            return m_images[img];
        return {};
    }

    ImageInfo GetCurrentMVInfo(uint32_t w = 0, uint32_t h = 0) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        VkImage img = GetCurrentMotionVectors(w, h);
        if (img != VK_NULL_HANDLE)
            return m_images[img];
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

    VkImage GetImageFromView(VkImageView view) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_views.find(view) != m_views.end()) {
            return m_views[view];
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
    
    bool IsSwapchainImage(VkImage image) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_swapchainImages.find(image) != m_swapchainImages.end();
    }
    
    VkImage GetSwapchainImageFromFramebuffer(VkFramebuffer fb);
    VkImage GetBestSceneSourceFromFramebuffer(VkFramebuffer fb, uint32_t renderW, uint32_t renderH);
    VkImage GetColorAttachment(VkFramebuffer fb, uint32_t renderW, uint32_t renderH);

    void OnBindFramebuffer(VkFramebuffer fb);

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

    void SetFakeBackBufferImage(VkImage image);

    VkImage GetFakeBackBufferImage() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_fakeBackBufferImage;
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
    VkImage m_currentExposureBuffer = VK_NULL_HANDLE;
    VkImage m_currentReactiveBuffer = VK_NULL_HANDLE;
    VkImage m_currentTransparencyBuffer = VK_NULL_HANDLE;
    VkImage m_lastSceneSource = VK_NULL_HANDLE;
    VkImage m_fakeBackBufferImage = VK_NULL_HANDLE;
    float m_bestDepthScore = -1.0f;
    float m_bestMVScore = -1.0f;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    mutable std::recursive_mutex m_mutex;

    uint64_t GetResKey(uint32_t w, uint32_t h) const { return ((uint64_t)w << 32) | h; }
};

} // namespace GamePlug
