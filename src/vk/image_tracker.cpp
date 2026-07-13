#include "image_tracker.h"
#include "logger.h"
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace GamePlug {

void ImageTracker::TrackImage(VkImage image, const VkImageCreateInfo* pCreateInfo) {
    if (!pCreateInfo || image == VK_NULL_HANDLE)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ImageInfo info;
    info.image = image;
    info.format = pCreateInfo->format;
    info.extent = pCreateInfo->extent;
    info.usage = pCreateInfo->usage;

    m_images[image] = info;

    // Reduce logging frequency for images
    static uint32_t trackCount = 0;
    if (trackCount++ < 500) {
        std::stringstream ss;
        ss << "ImageTracker: Tracked Image " << (void*)image << " [" << pCreateInfo->extent.width << "x" << pCreateInfo->extent.height
           << "]"
           << " Format: " << pCreateInfo->format << " Usage: 0x" << std::hex << pCreateInfo->usage;
        Logger::debug(ss.str());
    }

    // Auto-identify Depth with persistence
    if (pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        float score = 0.0f;
        if (m_screenWidth > 0 && pCreateInfo->extent.width == m_screenWidth && pCreateInfo->extent.height == m_screenHeight)
            score += 1000000.0f;
        if (pCreateInfo->extent.width != pCreateInfo->extent.height)
            score += 500000.0f;
        if (pCreateInfo->extent.width >= 640 && pCreateInfo->extent.height >= 360)
            score += 100000.0f;

        // Shadow map penalty
        if (pCreateInfo->extent.width == pCreateInfo->extent.height && m_screenWidth != m_screenHeight)
            score -= 800000.0f;
        // Minimum size filter
        if (pCreateInfo->extent.width < 320 || pCreateInfo->extent.height < 200)
            score = -100.0f;
        // Strongly prefer depth images that can be sampled in shaders.
        // Use a large bonus so samplable images always beat non-samplable ones
        // regardless of which was registered first.
        if (pCreateInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT)
            score += 1500000.0f;

        uint64_t resKey = GetResKey(pCreateInfo->extent.width, pCreateInfo->extent.height);
        if (score >= m_bestDepthScorePerRes[resKey] && score > 0) {
            m_bestDepthPerRes[resKey] = image;
            m_bestDepthScorePerRes[resKey] = score;

            // Maintain legacy pointer for current screen match
            if (pCreateInfo->extent.width == m_screenWidth && pCreateInfo->extent.height == m_screenHeight) {
                m_currentDepthBuffer = image;
                m_bestDepthScore = score;
            }
            Logger::info("ImageTracker: Depth buffer candidate for " + std::to_string(pCreateInfo->extent.width) + "x" +
                         std::to_string(pCreateInfo->extent.height) + " updated (Score=" + std::to_string(score) + ")");
        }
    }
}

void ImageTracker::UntrackImage(VkImage image) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_images.erase(image);
    m_mainViews.erase(image);
    if (m_currentDepthBuffer == image) {
        m_currentDepthBuffer = VK_NULL_HANDLE;
        m_bestDepthScore = -1.0f;
    }
    if (m_currentMVBuffer == image) {
        m_currentMVBuffer = VK_NULL_HANDLE;
        m_bestMVScore = -1.0f;
    }
    if (m_currentExposureBuffer == image) {
        m_currentExposureBuffer = VK_NULL_HANDLE;
    }
    if (m_currentReactiveBuffer == image) {
        m_currentReactiveBuffer = VK_NULL_HANDLE;
    }
    if (m_currentTransparencyBuffer == image) {
        m_currentTransparencyBuffer = VK_NULL_HANDLE;
    }
    if (m_fakeBackBufferImage == image) {
        m_fakeBackBufferImage = VK_NULL_HANDLE;
    }
    if (m_lastSceneSource == image) {
        m_lastSceneSource = VK_NULL_HANDLE;
    }

    // Clean up per-res maps
    for (auto it = m_bestDepthPerRes.begin(); it != m_bestDepthPerRes.end();) {
        if (it->second == image) {
            m_bestDepthScorePerRes[it->first] = -1.0f;
            it = m_bestDepthPerRes.erase(it);
        } else
            ++it;
    }
    for (auto it = m_bestMVPerRes.begin(); it != m_bestMVPerRes.end();) {
        if (it->second == image) {
            m_bestMVScorePerRes[it->first] = -1.0f;
            it = m_bestMVPerRes.erase(it);
        } else
            ++it;
    }

    // Remove all views pointing to this image
    for (auto it = m_views.begin(); it != m_views.end();) {
        if (it->second == image)
            it = m_views.erase(it);
        else
            ++it;
    }
}

void ImageTracker::TrackImageView(VkImageView view, VkImage image) {
    if (view == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_views[view] = image;

    /*
    char buf[256];
    sprintf_s(buf, "ImageTracker: Tracked View 0x%llx for Image 0x%llx", (unsigned long long)view, (unsigned long long)image);
    Logger::debug(buf);
    */

    // Store the first view we see as the main view for debug display
    if (m_mainViews.find(image) == m_mainViews.end()) {
        m_mainViews[image] = view;
    }
}

void ImageTracker::UntrackImageView(VkImageView view) {
    if (view == VK_NULL_HANDLE)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_views.find(view) != m_views.end()) {
        m_views.erase(view);
    }
}

void ImageTracker::TrackFramebuffer(VkFramebuffer fb, uint32_t count, const VkImageView* pViews) {
    if (fb == VK_NULL_HANDLE || !pViews || count == 0)
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<VkImage> resolvedImages;
    for (uint32_t i = 0; i < count; i++) {
        if (m_views.find(pViews[i]) != m_views.end()) {
            resolvedImages.push_back(m_views[pViews[i]]);
        } else {
            resolvedImages.push_back(VK_NULL_HANDLE);
        }
    }
    m_framebuffers[fb] = resolvedImages;

    // Heuristic: If we see a depth buffer and a likely motion vector buffer in the same FB,
    // it's VERY likely the G-Buffer pass.
    VkImage depthCand = VK_NULL_HANDLE;
    VkImage mvCand = VK_NULL_HANDLE;

    // Score all depth attachments in this framebuffer
    for (VkImage img : resolvedImages) {
        if (img == VK_NULL_HANDLE)
            continue;
        auto& info = m_images[img];
        if (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            float score = 0.0f;
            if (m_screenWidth > 0 && info.extent.width == m_screenWidth && info.extent.height == m_screenHeight)
                score += 1000000.0f;
            if (info.extent.width != info.extent.height)
                score += 500000.0f;
            if (info.extent.width >= 640 && info.extent.height >= 360)
                score += 100000.0f;

            // Shadow map penalty
            if (info.extent.width == info.extent.height && m_screenWidth != m_screenHeight)
                score -= 800000.0f;
            // Filter smaller buffers
            if (info.extent.width < 320 || info.extent.height < 200)
                score = -100.0f;

            uint64_t resKey = GetResKey(info.extent.width, info.extent.height);
            if (score >= m_bestDepthScorePerRes[resKey] && score > 0) {
                m_bestDepthPerRes[resKey] = img;
                m_bestDepthScorePerRes[resKey] = score;

                if (info.extent.width == m_screenWidth && info.extent.height == m_screenHeight) {
                    m_currentDepthBuffer = img;
                    m_bestDepthScore = score;
                }
                depthCand = img;
                Logger::info("ImageTracker: Depth buffer best candidate for " + std::to_string(info.extent.width) + "x" +
                             std::to_string(info.extent.height) + " updated (Score=" + std::to_string(score) + ")");
            } else if (img == m_bestDepthPerRes[resKey]) {
                depthCand = img;
            }
        }
    }

    if (depthCand != VK_NULL_HANDLE) {
        auto& dInfo = m_images[depthCand];
        for (VkImage img : resolvedImages) {
            if (img == VK_NULL_HANDLE || img == depthCand)
                continue;
            if (m_images.find(img) == m_images.end())
                continue;

            auto& info = m_images[img];
            bool isMVFormat = (info.format == VK_FORMAT_R16G16_SFLOAT || info.format == VK_FORMAT_R32G32_SFLOAT ||
                               info.format == VK_FORMAT_R16G16B16A16_SFLOAT || info.format == VK_FORMAT_R16G16_SNORM ||
                               info.format == VK_FORMAT_R16G16_UNORM || info.format == VK_FORMAT_R8G8_UNORM ||
                               info.format == VK_FORMAT_R8G8_SNORM);
            if (!isMVFormat)
                continue;

            float score = 0.0f;
            bool sizeMatch = (info.extent.width == dInfo.extent.width && info.extent.height == dInfo.extent.height);
            if (sizeMatch)
                score += 500000.0f;

            // Format scoring
            if (info.format == VK_FORMAT_R16G16_SFLOAT || info.format == VK_FORMAT_R16G16B16A16_SFLOAT)
                score += 200000.0f;
            else if (info.format == VK_FORMAT_R16G16_SNORM || info.format == VK_FORMAT_R16G16_UNORM)
                score += 150000.0f;
            else if (info.format == VK_FORMAT_R8G8_UNORM)
                score += 50000.0f;

            uint64_t resKey = GetResKey(info.extent.width, info.extent.height);
            if (score >= m_bestMVScorePerRes[resKey] && score > 0) {
                m_bestMVPerRes[resKey] = img;
                m_bestMVScorePerRes[resKey] = score;

                if (sizeMatch && info.extent.width == m_screenWidth && info.extent.height == m_screenHeight) {
                    m_currentMVBuffer = img;
                    m_bestMVScore = score;
                }
                mvCand = img;
                Logger::info("ImageTracker: Motion Vector best candidate for " + std::to_string(info.extent.width) + "x" +
                             std::to_string(info.extent.height) + " updated");
            } else if (img == m_bestMVPerRes[resKey]) {
                mvCand = img;
            }
        }
    }
}

void ImageTracker::UntrackFramebuffer(VkFramebuffer fb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_framebuffers.erase(fb);
}

bool ImageTracker::IsSceneFramebuffer(VkFramebuffer fb, uint32_t renderW, uint32_t renderH) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end())
        return false;

    bool hasSwapchain = false;
    bool hasMatch = false;

    static uint32_t logCount = 0;
    bool shouldLog = (logCount++ % 5000 == 0);

    for (VkImage img : m_framebuffers[fb]) {
        if (img == VK_NULL_HANDLE)
            continue;
        auto& info = m_images[img];

        if (m_swapchainImages.find(img) != m_swapchainImages.end()) {
            hasSwapchain = true;
        }

        // Check for exact match with target render resolution
        if (renderW > 0 && info.extent.width == renderW && info.extent.height == renderH) {
            hasMatch = true;
        } else if (shouldLog) {
            // Log resolution mismatches periodically
            Logger::debug("ImageTracker: FB Candidate Mismatch: Expected " + std::to_string(renderW) + "x" + std::to_string(renderH) +
                          " Found " + std::to_string(info.extent.width) + "x" + std::to_string(info.extent.height));
        }
    }

    if (shouldLog && hasMatch && hasSwapchain) {
        Logger::debug("ImageTracker: rejected FB " + std::to_string((uintptr_t)fb) + " because it contains swapchain.");
    }

    // A TRUE Scene Framebuffer for Early Upscaling is one that matches the resolution
    // AND is NOT the swapchain.
    return hasMatch && !hasSwapchain;
}

bool ImageTracker::IsSwapchainFramebuffer(VkFramebuffer fb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end())
        return false;

    for (VkImage img : m_framebuffers[fb]) {
        if (m_swapchainImages.find(img) != m_swapchainImages.end()) {
            return true;
        }
    }
    return false;
}

void ImageTracker::RegisterSwapchainImage(VkImage image) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_swapchainImages.insert(image);
}

void ImageTracker::SetSwapchainFormat(VkFormat format) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_swapchainFormat = format;
}

VkFormat ImageTracker::GetSwapchainFormat() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_swapchainFormat;
}

VkImage ImageTracker::GetSwapchainImageFromFramebuffer(VkFramebuffer fb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end())
        return VK_NULL_HANDLE;

    for (VkImage img : m_framebuffers[fb]) {
        if (m_swapchainImages.find(img) != m_swapchainImages.end()) {
            return img;
        }
    }
    return VK_NULL_HANDLE;
}

VkImage ImageTracker::GetBestSceneSourceFromFramebuffer(VkFramebuffer fb, uint32_t renderW, uint32_t renderH) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end())
        return VK_NULL_HANDLE;

    VkImage bestOffscreen = VK_NULL_HANDLE;
    VkImage swapchainFallback = VK_NULL_HANDLE;

    for (VkImage img : m_framebuffers[fb]) {
        if (img == VK_NULL_HANDLE)
            continue;
        auto& info = m_images[img];

        bool isSwapchain = (m_swapchainImages.find(img) != m_swapchainImages.end());
        bool isColor = (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

        if (!isColor)
            continue;

        if (isSwapchain) {
            swapchainFallback = img;
        } else {
            // 1. Prefer exact match with current render resolution
            if (renderW > 0 && info.extent.width == renderW && info.extent.height == renderH) {
                return img;
            }

            // 2. Secondary match: Large buffer proportions relative to screen
            if (m_screenWidth > 0) {
                float rx = (float)info.extent.width / m_screenWidth;
                if (rx > 0.45f && rx < 1.05f && info.extent.width != info.extent.height) {
                    bestOffscreen = img;
                }
            }
        }
    }

    return bestOffscreen != VK_NULL_HANDLE ? bestOffscreen : swapchainFallback;
}

VkImage ImageTracker::GetColorAttachment(VkFramebuffer fb, uint32_t renderW, uint32_t renderH) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end())
        return VK_NULL_HANDLE;
    for (VkImage img : m_framebuffers[fb]) {
        if (img == VK_NULL_HANDLE)
            continue;
        auto& info = m_images[img];
        if (info.extent.width == renderW && info.extent.height == renderH) {
            if (!(info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                return img;
            }
        }
    }
        return VK_NULL_HANDLE;
}

void ImageTracker::OnBindFramebuffer(VkFramebuffer fb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_framebuffers.find(fb) == m_framebuffers.end()) {
        static std::set<VkFramebuffer> unkFBs;
        if (unkFBs.find(fb) == unkFBs.end()) {
            Logger::warn("ImageTracker: OnBindFramebuffer - bound FB " + std::to_string((uintptr_t)fb) + " is not tracked!");
            unkFBs.insert(fb);
        }
        return;
    }

    for (VkImage img : m_framebuffers[fb]) {
        if (img == VK_NULL_HANDLE)
            continue;
        
        auto it = m_images.find(img);
        if (it == m_images.end())
            continue;

        auto const& info = it->second;
        if (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            // Basic filtering like in scoring: ignore small buffers / shadow maps
            if (info.extent.width >= 320 && info.extent.height >= 200 && info.extent.width != info.extent.height) {
                uint64_t resKey = GetResKey(info.extent.width, info.extent.height);
                m_bestDepthPerRes[resKey] = img;
                
                if (info.extent.width == m_screenWidth && info.extent.height == m_screenHeight) {
                    if (m_currentDepthBuffer != img) {
                        Logger::info("ImageTracker: OnBindFramebuffer - depth buffer changed from " +
                                     std::to_string((uintptr_t)m_currentDepthBuffer) + " to " + std::to_string((uintptr_t)img));
                        m_currentDepthBuffer = img;
                    }
                }
            }
        }

        // Also track motion vectors dynamically if we find one
        bool isMVFormat = (info.format == VK_FORMAT_R16G16_SFLOAT || info.format == VK_FORMAT_R32G32_SFLOAT ||
                           info.format == VK_FORMAT_R16G16B16A16_SFLOAT || info.format == VK_FORMAT_R16G16_SNORM ||
                           info.format == VK_FORMAT_R16G16_UNORM || info.format == VK_FORMAT_R8G8_UNORM ||
                           info.format == VK_FORMAT_R8G8_SNORM);
        if (isMVFormat && ((info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || (info.usage & VK_IMAGE_USAGE_SAMPLED_BIT))) {
            if (info.extent.width >= 320 && info.extent.height >= 200) {
                uint64_t resKey = GetResKey(info.extent.width, info.extent.height);
                m_bestMVPerRes[resKey] = img;
                
                if (info.extent.width == m_screenWidth && info.extent.height == m_screenHeight) {
                    m_currentMVBuffer = img;
                }
            }
        }
    }
}

void ImageTracker::SetFakeBackBufferImage(VkImage image) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fakeBackBufferImage = image;
    Logger::info("ImageTracker: Set Fake Backbuffer Image to " + std::to_string((uintptr_t)image));
}

} // namespace GamePlug
