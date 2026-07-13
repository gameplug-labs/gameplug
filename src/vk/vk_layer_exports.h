#pragma once

#include <string>
#include <vector>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#ifndef VK_LAYER_EXPORT
#ifdef _WIN32
#define VK_LAYER_EXPORT __declspec(dllexport)
#else
#define VK_LAYER_EXPORT
#endif
#endif

extern "C" {

// Globals defined in vk_layer_exports.cpp
extern VkInstance g_Instance;
extern VkPhysicalDevice g_PhysDevice;

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateWin32SurfaceKHR(
    VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

/*
VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities);
*/

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImage(
    VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateImageView(
    VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator);

/*
VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BindImageMemory(
    VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
    uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines);
*/

/*
VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetViewport(
    VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdSetScissor(
    VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
*/



VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);

// VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_EndCommandBuffer(VkCommandBuffer commandBuffer);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRenderPass(
    VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderPass(VkCommandBuffer commandBuffer);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRendering(
    VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRendering(VkCommandBuffer commandBuffer);

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdBeginRenderingKHR(
    VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo);
VK_LAYER_EXPORT void VKAPI_CALL GamePlug_CmdEndRenderingKHR(VkCommandBuffer commandBuffer);


VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_AcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);

/*
VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_GetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages);
*/

VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);

/*
VK_LAYER_EXPORT void VKAPI_CALL GamePlug_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator);

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_DeviceWaitIdle(VkDevice device);
*/

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetDeviceProcAddr(VkDevice device, const char* pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetInstanceProcAddr(VkInstance instance, const char* pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);
VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct);

// Hooking support variables (trampolines)
extern PFN_vkGetInstanceProcAddr Original_vkGetInstanceProcAddr;
extern PFN_vkGetDeviceProcAddr Original_vkGetDeviceProcAddr;
extern PFN_vkCreateInstance Original_vkCreateInstance;
extern PFN_vkCreateDevice Original_vkCreateDevice;

extern thread_local bool g_InsideCreateInstance;
extern thread_local bool g_InsideCreateDevice;

void StartVulkanHookSetup();

} // extern "C"
