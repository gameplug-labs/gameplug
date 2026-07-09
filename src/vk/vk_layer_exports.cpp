#include "vk_layer_exports.h"
#include "dispatch.h"
#include "logger.h"
#include "minhook.h"
#include <windows.h>

extern "C" {

// Global tracking definitions
VkInstance g_Instance = VK_NULL_HANDLE;
VkPhysicalDevice g_PhysDevice = VK_NULL_HANDLE;

// Hooking support variables (trampolines)
PFN_vkGetInstanceProcAddr Original_vkGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr Original_vkGetDeviceProcAddr = nullptr;
PFN_vkCreateInstance Original_vkCreateInstance = nullptr;
PFN_vkCreateDevice Original_vkCreateDevice = nullptr;

thread_local bool g_InsideCreateInstance = false;
thread_local bool g_InsideCreateDevice = false;
thread_local int g_GDPA_RecursionDepth = 0;
thread_local int g_GIPA_RecursionDepth = 0;

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (g_GDPA_RecursionDepth > 10) {
        GamePlug::Logger::error("GamePlug_GetDeviceProcAddr: Recursion detected for {}!", pName ? pName : "NULL");
        return nullptr;
    }
    struct DepthGuard {
        DepthGuard() { g_GDPA_RecursionDepth++; }
        ~DepthGuard() { g_GDPA_RecursionDepth--; }
    } guard;
    if (std::string(pName) == "vkGetDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vkCreateDevice")
        return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    if (std::string(pName) == "vkGetDeviceQueue")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR")
        return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR")
        return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkGetSwapchainImagesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetSwapchainImagesKHR;
    if (std::string(pName) == "vkCreateImage")
        return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage")
        return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView")
        return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView")
        return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory")
        return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory")
        return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines")
        return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers")
        return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice")
        return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance")
        return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle")
        return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport")
        return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor")
        return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;
    if (std::string(pName) == "vkCmdBeginRendering")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRendering;
    if (std::string(pName) == "vkCmdEndRendering")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRendering;
    if (std::string(pName) == "vkCmdBeginRenderingKHR")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderingKHR;
    if (std::string(pName) == "vkCmdEndRenderingKHR")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderingKHR;

    auto* dev_entry = GamePlug::DispatchManager::Get().GetDevice(device);
    if (dev_entry)
        return dev_entry->pfnNextGetDeviceProcAddr(device, pName);
    if (Original_vkGetDeviceProcAddr)
        return Original_vkGetDeviceProcAddr(device, pName);
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (g_GIPA_RecursionDepth > 10) {
        GamePlug::Logger::error("GamePlug_GetInstanceProcAddr: Recursion detected for {}!", pName ? pName : "NULL");
        return nullptr;
    }
    struct DepthGuard {
        DepthGuard() { g_GIPA_RecursionDepth++; }
        ~DepthGuard() { g_GIPA_RecursionDepth--; }
    } guard;

    if (std::string(pName) == "vkGetInstanceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetInstanceProcAddr;
    if (std::string(pName) == "vkGetDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceProcAddr;
    if (std::string(pName) == "vk_layerGetPhysicalDeviceProcAddr")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceProcAddr;
    if (std::string(pName) == "vkCreateInstance") {
        if (g_InsideCreateInstance && Original_vkCreateInstance) {
            return (PFN_vkVoidFunction)Original_vkCreateInstance;
        }
        return (PFN_vkVoidFunction)GamePlug_CreateInstance;
    }
    if (std::string(pName) == "vkCreateDevice") {
        if (g_InsideCreateDevice && Original_vkCreateDevice) {
            return (PFN_vkVoidFunction)Original_vkCreateDevice;
        }
        return (PFN_vkVoidFunction)GamePlug_CreateDevice;
    }
    if (std::string(pName) == "vkCreateWin32SurfaceKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateWin32SurfaceKHR;
    if (std::string(pName) == "vkGetDeviceQueue")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue;
    if (std::string(pName) == "vkGetDeviceQueue2")
        return (PFN_vkVoidFunction)GamePlug_GetDeviceQueue2;
    if (std::string(pName) == "vkCreateSwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_CreateSwapchainKHR;
    if (std::string(pName) == "vkQueuePresentKHR")
        return (PFN_vkVoidFunction)GamePlug_QueuePresentKHR;
    if (std::string(pName) == "vkAcquireNextImageKHR")
        return (PFN_vkVoidFunction)GamePlug_AcquireNextImageKHR;
    if (std::string(pName) == "vkGetSwapchainImagesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetSwapchainImagesKHR;
    if (std::string(pName) == "vkCreateImage")
        return (PFN_vkVoidFunction)GamePlug_CreateImage;
    if (std::string(pName) == "vkDestroyImage")
        return (PFN_vkVoidFunction)GamePlug_DestroyImage;
    if (std::string(pName) == "vkCreateImageView")
        return (PFN_vkVoidFunction)GamePlug_CreateImageView;
    if (std::string(pName) == "vkDestroyImageView")
        return (PFN_vkVoidFunction)GamePlug_DestroyImageView;
    if (std::string(pName) == "vkCreateFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_CreateFramebuffer;
    if (std::string(pName) == "vkDestroyFramebuffer")
        return (PFN_vkVoidFunction)GamePlug_DestroyFramebuffer;
    if (std::string(pName) == "vkAllocateMemory")
        return (PFN_vkVoidFunction)GamePlug_AllocateMemory;
    if (std::string(pName) == "vkBindImageMemory")
        return (PFN_vkVoidFunction)GamePlug_BindImageMemory;
    if (std::string(pName) == "vkCreateRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CreateRenderPass;
    if (std::string(pName) == "vkCreateGraphicsPipelines")
        return (PFN_vkVoidFunction)GamePlug_CreateGraphicsPipelines;
    if (std::string(pName) == "vkAllocateCommandBuffers")
        return (PFN_vkVoidFunction)GamePlug_AllocateCommandBuffers;
    if (std::string(pName) == "vkBeginCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_BeginCommandBuffer;
    if (std::string(pName) == "vkEndCommandBuffer")
        return (PFN_vkVoidFunction)GamePlug_EndCommandBuffer;
    if (std::string(pName) == "vkDestroySwapchainKHR")
        return (PFN_vkVoidFunction)GamePlug_DestroySwapchainKHR;
    if (std::string(pName) == "vkDestroyDevice")
        return (PFN_vkVoidFunction)GamePlug_DestroyDevice;
    if (std::string(pName) == "vkDestroyInstance")
        return (PFN_vkVoidFunction)GamePlug_DestroyInstance;
    if (std::string(pName) == "vkDeviceWaitIdle")
        return (PFN_vkVoidFunction)GamePlug_DeviceWaitIdle;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (std::string(pName) == "vkGetPhysicalDeviceSurfaceCapabilities2KHR")
        return (PFN_vkVoidFunction)GamePlug_GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (std::string(pName) == "vkCmdSetViewport")
        return (PFN_vkVoidFunction)GamePlug_CmdSetViewport;
    if (std::string(pName) == "vkCmdSetScissor")
        return (PFN_vkVoidFunction)GamePlug_CmdSetScissor;
    if (std::string(pName) == "vkCmdBeginRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderPass;
    if (std::string(pName) == "vkCmdEndRenderPass")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderPass;
    if (std::string(pName) == "vkCmdBeginRendering")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRendering;
    if (std::string(pName) == "vkCmdEndRendering")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRendering;
    if (std::string(pName) == "vkCmdBeginRenderingKHR")
        return (PFN_vkVoidFunction)GamePlug_CmdBeginRenderingKHR;
    if (std::string(pName) == "vkCmdEndRenderingKHR")
        return (PFN_vkVoidFunction)GamePlug_CmdEndRenderingKHR;

    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry)
        return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    if (Original_vkGetInstanceProcAddr)
        return Original_vkGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL GamePlug_GetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    auto* inst_entry = GamePlug::DispatchManager::Get().GetInstance(instance);
    if (inst_entry)
        return inst_entry->pfnNextGetInstanceProcAddr(instance, pName);
    if (Original_vkGetInstanceProcAddr)
        return Original_vkGetInstanceProcAddr(instance, pName);
    return nullptr;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL GamePlug_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetInstanceProcAddr = GamePlug_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = GamePlug_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = GamePlug_GetPhysicalDeviceProcAddr;
    } else
        return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

extern void Init(); // defined in vk/framework.cpp

typedef HMODULE(WINAPI* PFN_LoadLibraryA)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI* PFN_LoadLibraryW)(LPCWSTR lpLibFileName);
typedef HMODULE(WINAPI* PFN_LoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE(WINAPI* PFN_LoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

static PFN_LoadLibraryA Original_LoadLibraryA = nullptr;
static PFN_LoadLibraryW Original_LoadLibraryW = nullptr;
static PFN_LoadLibraryExA Original_LoadLibraryExA = nullptr;
static PFN_LoadLibraryExW Original_LoadLibraryExW = nullptr;

void InitializeVulkanHooks() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    // Load framework configurations and initialize Logger early
    GamePlug::Init();

    GamePlug::Logger::info("Initializing Vulkan API Hooks...");

    HMODULE hVulkan = GetModuleHandleW(L"vulkan-1.dll");
    if (!hVulkan) {
        GamePlug::Logger::error("InitializeVulkanHooks: vulkan-1.dll not found in memory!");
        return;
    }

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        GamePlug::Logger::error("InitializeVulkanHooks: MinHook failed to initialize");
        return;
    }

    // Resolve original loader functions
    auto pGIPA = (LPVOID)GetProcAddress(hVulkan, "vkGetInstanceProcAddr");
    auto pGDPA = (LPVOID)GetProcAddress(hVulkan, "vkGetDeviceProcAddr");
    auto pCreateInstance = (LPVOID)GetProcAddress(hVulkan, "vkCreateInstance");
    auto pCreateDevice = (LPVOID)GetProcAddress(hVulkan, "vkCreateDevice");

    if (pGIPA) {
        MH_CreateHook(pGIPA, (LPVOID)&GamePlug_GetInstanceProcAddr, (LPVOID*)&Original_vkGetInstanceProcAddr);
        MH_EnableHook(pGIPA);
        GamePlug::Logger::info("InitializeVulkanHooks: Hooked vkGetInstanceProcAddr");
    }
    if (pGDPA) {
        MH_CreateHook(pGDPA, (LPVOID)&GamePlug_GetDeviceProcAddr, (LPVOID*)&Original_vkGetDeviceProcAddr);
        MH_EnableHook(pGDPA);
        GamePlug::Logger::info("InitializeVulkanHooks: Hooked vkGetDeviceProcAddr");
    }
    if (pCreateInstance) {
        MH_CreateHook(pCreateInstance, (LPVOID)&GamePlug_CreateInstance, (LPVOID*)&Original_vkCreateInstance);
        MH_EnableHook(pCreateInstance);
        GamePlug::Logger::info("InitializeVulkanHooks: Hooked vkCreateInstance");
    }
    if (pCreateDevice) {
        MH_CreateHook(pCreateDevice, (LPVOID)&GamePlug_CreateDevice, (LPVOID*)&Original_vkCreateDevice);
        MH_EnableHook(pCreateDevice);
        GamePlug::Logger::info("InitializeVulkanHooks: Hooked vkCreateDevice");
    }
}

static void CheckAndHookVulkan(HMODULE hModule) {
    if (hModule) {
        static bool vulkanHooked = false;
        if (!vulkanHooked && GetProcAddress(hModule, "vkGetInstanceProcAddr") != nullptr) {
            vulkanHooked = true;
            InitializeVulkanHooks();
        }
    }
}

HMODULE WINAPI Hooked_LoadLibraryA(LPCSTR lpLibFileName) {
    HMODULE hModule = Original_LoadLibraryA(lpLibFileName);
    if (hModule && lpLibFileName && (strstr(lpLibFileName, "vulkan-1") != nullptr)) {
        CheckAndHookVulkan(hModule);
    }
    return hModule;
}

HMODULE WINAPI Hooked_LoadLibraryW(LPCWSTR lpLibFileName) {
    HMODULE hModule = Original_LoadLibraryW(lpLibFileName);
    if (hModule && lpLibFileName && (wcsstr(lpLibFileName, L"vulkan-1") != nullptr)) {
        CheckAndHookVulkan(hModule);
    }
    return hModule;
}

HMODULE WINAPI Hooked_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    HMODULE hModule = Original_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
    if (hModule && lpLibFileName && (strstr(lpLibFileName, "vulkan-1") != nullptr)) {
        CheckAndHookVulkan(hModule);
    }
    return hModule;
}

HMODULE WINAPI Hooked_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    HMODULE hModule = Original_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    if (hModule && lpLibFileName && (wcsstr(lpLibFileName, L"vulkan-1") != nullptr)) {
        CheckAndHookVulkan(hModule);
    }
    return hModule;
}

void InitializeLoadLibraryHooks() {
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        return;
    }

    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel) {
        auto pLoadLibraryA = (LPVOID)GetProcAddress(hKernel, "LoadLibraryA");
        auto pLoadLibraryW = (LPVOID)GetProcAddress(hKernel, "LoadLibraryW");
        auto pLoadLibraryExA = (LPVOID)GetProcAddress(hKernel, "LoadLibraryExA");
        auto pLoadLibraryExW = (LPVOID)GetProcAddress(hKernel, "LoadLibraryExW");

        if (pLoadLibraryA) {
            MH_CreateHook(pLoadLibraryA, (LPVOID)&Hooked_LoadLibraryA, (LPVOID*)&Original_LoadLibraryA);
            MH_EnableHook(pLoadLibraryA);
        }
        if (pLoadLibraryW) {
            MH_CreateHook(pLoadLibraryW, (LPVOID)&Hooked_LoadLibraryW, (LPVOID*)&Original_LoadLibraryW);
            MH_EnableHook(pLoadLibraryW);
        }
        if (pLoadLibraryExA) {
            MH_CreateHook(pLoadLibraryExA, (LPVOID)&Hooked_LoadLibraryExA, (LPVOID*)&Original_LoadLibraryExA);
            MH_EnableHook(pLoadLibraryExA);
        }
        if (pLoadLibraryExW) {
            MH_CreateHook(pLoadLibraryExW, (LPVOID)&Hooked_LoadLibraryExW, (LPVOID*)&Original_LoadLibraryExW);
            MH_EnableHook(pLoadLibraryExW);
        }
    }
}

void StartVulkanHookSetup() {
    HMODULE hVulkan = GetModuleHandleW(L"vulkan-1.dll");
    if (hVulkan) {
        InitializeVulkanHooks();
    } else {
        InitializeLoadLibraryHooks();
    }
}

} // extern "C"
