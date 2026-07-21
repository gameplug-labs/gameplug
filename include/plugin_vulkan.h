#pragma once


struct GamePlugVulkanHookInterface {
    // Queue & Swapchain
    void (*vkGetDeviceQueue)(void* device, unsigned int queueFamilyIndex, unsigned int queueIndex, void** pQueue);
    void (*vkGetDeviceQueue2)(void* device, const void* pQueueInfo, void** pQueue);
    void (*vkDestroySwapchainKHR)(void* device, void* swapchain, const void* pAllocator);
    void (*vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
    void (*vkAcquireNextImageKHR)(void* device, void* swapchain, unsigned long long timeout, void* semaphore, void* fence, unsigned int* pImageIndex);
    void (*vkGetSwapchainImagesKHR)(void* device, void* swapchain, unsigned int* pSwapchainImageCount, void** pSwapchainImages);

    // Surface capabilities
    void (*vkCreateWin32SurfaceKHR)(void* instance, const void* pCreateInfo, const void* pAllocator, void** pSurface);
    void (*vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(void* physicalDevice, void* surface, void* pSurfaceCapabilities);
    void (*vkGetPhysicalDeviceSurfaceCapabilities2KHR)(void* physicalDevice, const void* pSurfaceInfo, void* pSurfaceCapabilities);

    // Resources
    void (*vkCreateImage)(void* device, void* pCreateInfo, const void* pAllocator, void** pImage);
    void (*vkDestroyImage)(void* device, void* image, const void* pAllocator);
    void (*vkCreateImageView)(void* device, const void* pCreateInfo, const void* pAllocator, void** pView);
    void (*vkDestroyImageView)(void* device, void* imageView, const void* pAllocator);
    void (*vkCreateFramebuffer)(void* device, const void* pCreateInfo, const void* pAllocator, void** pFramebuffer);
    void (*vkDestroyFramebuffer)(void* device, void* framebuffer, const void* pAllocator);
    void (*vkAllocateMemory)(void* device, const void* pAllocateInfo, const void* pAllocator, void** pMemory);
    void (*vkBindImageMemory)(void* device, void* image, void* memory, unsigned long long memoryOffset);
    void (*vkCreateRenderPass)(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass);
    void (*vkCreateGraphicsPipelines)(void* device, void* pipelineCache, unsigned int createInfoCount, const void* pCreateInfos, const void* pAllocator, void** pPipelines);

    // Commands
    void (*vkAllocateCommandBuffers)(void* device, const void* pAllocateInfo, void** pCommandBuffers);
    void (*vkBeginCommandBuffer)(void* commandBuffer, const void* pBeginInfo);
    void (*vkEndCommandBuffer)(void* commandBuffer);
    void (*vkCmdBeginRenderPass)(void* commandBuffer, const void* pRenderPassBegin, unsigned int contents);
    void (*vkCmdEndRenderPass)(void* commandBuffer);
    void (*vkCmdSetViewport)(void* commandBuffer, unsigned int firstViewport, unsigned int viewportCount, const void* pViewports);
    void (*vkCmdSetScissor)(void* commandBuffer, unsigned int firstScissor, unsigned int scissorCount, const void* pScissors);
    void (*vkCmdBeginRendering)(void* commandBuffer, const void* pRenderingInfo);
    void (*vkCmdEndRendering)(void* commandBuffer);
    void (*vkCmdBeginRenderingKHR)(void* commandBuffer, const void* pRenderingInfo);
    void (*vkCmdEndRenderingKHR)(void* commandBuffer);
};
