#include "plugin_manager.h"
#include "config.h"
#include "logger.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

namespace GamePlug {

PluginManager& PluginManager::Get() {
    static PluginManager instance;
    return instance;
}

typedef GamePlugPluginInterface* (*GamePlug_GetPluginInterfaceFn)();

PluginManager::~PluginManager() {
    UnloadPlugins();
}

void PluginLogBridge(GamePlugPluginInterface::PluginLogLevel level, const char* message, void* context) {
    const char* pluginName = (const char*)context;
    std::string formattedMsg = (pluginName ? "[" + std::string(pluginName) + "] " : "") + message;

    switch (level) {
    case GamePlugPluginInterface::LOG_INFO:
        Logger::info(formattedMsg);
        break;
    case GamePlugPluginInterface::LOG_WARN:
        Logger::warn(formattedMsg);
        break;
    case GamePlugPluginInterface::LOG_ERROR:
        Logger::error(formattedMsg);
        break;
    case GamePlugPluginInterface::LOG_DEBUG:
        Logger::debug(formattedMsg);
        break;
    }
}

void PluginManager::LoadPlugins() {
    if (m_searchDone)
        return;

    if (!Config::Get().GetBool("PluginEnabled", true)) {
        return;
    }

    // Try Game EXE Directory first
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path gamePath = std::filesystem::path(buf).parent_path();
    std::filesystem::path pluginsPath = gamePath / "gameplug" / "plugins";

    // Try DLL Directory as fallback
    if (!std::filesystem::exists(pluginsPath)) {
        HMODULE hSelf = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GamePlug::PluginManager::Get, &hSelf);
        if (hSelf) {
            GetModuleFileNameA(hSelf, buf, MAX_PATH);
            std::filesystem::path dllPath = std::filesystem::path(buf).parent_path();
            pluginsPath = dllPath / "gameplug" / "plugins";
        }
    }

    Logger::info("PluginManager: Searching for plugins in " + pluginsPath.string());

    if (!std::filesystem::exists(pluginsPath)) {
        Logger::warn("PluginManager: Plugins directory NOT found: " + pluginsPath.string());
        m_searchDone = true;
        return;
    }

    int loadedCount = 0;
    m_discoveredPlugins.clear();

    for (const auto& entry : std::filesystem::directory_iterator(pluginsPath)) {
        if (entry.path().extension() == ".dll") {
            std::string path = entry.path().string();
            HMODULE hModule = LoadLibraryExA(path.c_str(), NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);

            if (hModule) {
                auto getInterface = (GamePlug_GetPluginInterfaceFn)GetProcAddress(hModule, GamePlug_GET_INTERFACE_NAME);
                if (getInterface) {
                    GamePlugPluginInterface* pInterface = getInterface();
                    if (pInterface) {
                        std::string pluginName = pInterface->GetName();

                        DiscoveredPlugin dp;
                        dp.filename = entry.path().filename().string();
                        dp.name = pluginName;
                        dp.isLoaded = false;

                        if (!Config::Get().IsPluginEnabled(pluginName)) {
                            Logger::info("PluginManager: Plugin '" + pluginName + "' is DISABLED in config. Skipping.");
                            FreeLibrary(hModule);
                            m_discoveredPlugins.push_back(dp);
                            continue;
                        }

                        Logger::info("PluginManager: Loaded plugin '" + pluginName + "' from " + dp.filename + " (v" +
                                     std::to_string(pInterface->InterfaceVersion) + ")");

                        LoadedPlugin plugin;
                        plugin.filename = dp.filename;
                        plugin.name = pluginName;
                        plugin.handle = hModule;
                        plugin.pInterface = pInterface;

                        if (plugin.pInterface->RequestHookInterface) {
                            plugin.vulkanHooks = (GamePlugVulkanHookInterface*)plugin.pInterface->RequestHookInterface(GP_INTERFACE_VULKAN);
                            plugin.d3d11Hooks = (GamePlugD3D11HookInterface*)plugin.pInterface->RequestHookInterface(GP_INTERFACE_D3D11);
                        }

                        if (ImGui::GetCurrentContext()) {
                            if (plugin.pInterface->OnInit) {
                                Logger::info("PluginManager: Calling OnInit for plugin " + std::string(pInterface->GetName()));
                                plugin.pInterface->OnInit(ImGui::GetCurrentContext(), PluginLogBridge, (void*)pInterface->GetName());
                                Logger::info("PluginManager: OnInit complete for plugin " + std::string(pInterface->GetName()));
                            }

                            if (m_hasGraphicsContext && plugin.pInterface->OnGraphicsInit) {
                                Logger::info("PluginManager: Calling OnGraphicsInit (upon load) for plugin " + std::string(pInterface->GetName()));
                                plugin.pInterface->OnGraphicsInit(&m_graphicsContext);
                            }
                        }

                        m_plugins.push_back(plugin);
                        dp.isLoaded = true;
                        m_discoveredPlugins.push_back(dp);
                        loadedCount++;
                    } else {
                        Logger::error("PluginManager: Plugin " + path + " returned null interface.");
                        FreeLibrary(hModule);
                    }
                } else {
                    Logger::error("PluginManager: Plugin " + path + " does not export " + GamePlug_GET_INTERFACE_NAME);
                    FreeLibrary(hModule);
                }
            } else {
                Logger::error("PluginManager: Failed to load DLL " + path + " (Error: " + std::to_string(GetLastError()) + ")");
            }
        }
    }

    Logger::info("PluginManager: Load complete. " + std::to_string(loadedCount) + " plugins loaded.");

    m_searchDone = true;
}

void PluginManager::RenderPlugins() {
    bool enabled = Config::Get().GetBool("PluginEnabled", true);

    if (!enabled) {
        if (!m_plugins.empty()) {
            UnloadPlugins();
        }
        return;
    }

    if (!m_searchDone) {
        ImGui::Text("Searching for plugins...");
        return;
    }

    if (m_plugins.empty()) {
        return;
    }

    bool first = true;
    for (auto& plugin : m_plugins) {
        if (!first) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        first = false;

        if (ImGui::CollapsingHeader(plugin.pInterface->GetName(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(plugin.pInterface->GetName()); // ID Isolation

            // 1. Standard Fields (Grouped by Category)
            if (plugin.pInterface->GetFields) {
                GamePlugPluginInterface::FieldDescriptor* fields = nullptr;
                int count = plugin.pInterface->GetFields(&fields);
                if (count > 0 && fields) {
                    // Grouping by category
                    std::map<std::string, std::vector<GamePlugPluginInterface::FieldDescriptor*>> categories;
                    for (int i = 0; i < count; i++) {
                        std::string cat = fields[i].Category ? fields[i].Category : "General";
                        categories[cat].push_back(&fields[i]);
                    }

                    for (auto& [catName, catFields] : categories) {
                        if (ImGui::TreeNodeEx(catName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                            for (auto* f : catFields) {
                                ImGui::PushID(f->Name);
                                bool changed = false;
                                switch (f->Type) {
                                case GamePlugPluginInterface::TYPE_BOOL:
                                    changed = ImGui::Checkbox(f->Name, (bool*)f->Data);
                                    break;
                                case GamePlugPluginInterface::TYPE_INT:
                                    changed = ImGui::DragInt(f->Name, (int*)f->Data);
                                    break;
                                case GamePlugPluginInterface::TYPE_FLOAT:
                                    changed = ImGui::DragFloat(f->Name, (float*)f->Data);
                                    break;
                                case GamePlugPluginInterface::TYPE_STRING:
                                    changed = ImGui::InputText(f->Name, (char*)f->Data, (size_t)f->DataSize);
                                    break;
                                case GamePlugPluginInterface::TYPE_ENUM:
                                    if (f->Options) {
                                        std::string options(f->Options);
                                        std::vector<std::string> items;
                                        size_t start = 0;
                                        size_t end = options.find(',');
                                        while (end != std::string::npos) {
                                            items.push_back(options.substr(start, end - start));
                                            start = end + 1;
                                            end = options.find(',', start);
                                        }
                                        items.push_back(options.substr(start));

                                        int* current_item = (int*)f->Data;
                                        const char* preview_value = (*current_item >= 0 && *current_item < (int)items.size())
                                                                        ? items[*current_item].c_str()
                                                                        : "Unknown";

                                        if (ImGui::BeginCombo(f->Name, preview_value)) {
                                            for (int n = 0; n < (int)items.size(); n++) {
                                                const bool is_selected = (*current_item == n);
                                                if (ImGui::Selectable(items[n].c_str(), is_selected)) {
                                                    *current_item = n;
                                                    changed = true;
                                                }
                                                if (is_selected) {
                                                    ImGui::SetItemDefaultFocus();
                                                }
                                            }
                                            ImGui::EndCombo();
                                        }
                                    }
                                    break;
                                }

                                if (changed && plugin.pInterface->OnFieldsChanged) {
                                    plugin.pInterface->OnFieldsChanged();
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }

                    if (plugin.pInterface->OnImGuiRender)
                        ImGui::Separator();
                }
            }

            // 2. Custom UI from plugin (Showing below fields)
            if (plugin.pInterface->OnImGuiRender) {
                plugin.pInterface->OnImGuiRender();
            }

            ImGui::PopID();
        }
    }
}

void PluginManager::UnloadPlugins() {
    for (auto& plugin : m_plugins) {
        if (plugin.pInterface->OnShutdown) {
            plugin.pInterface->OnShutdown();
        }
        FreeLibrary(plugin.handle);
    }
    m_plugins.clear();
    m_searchDone = false;
}

bool PluginManager::IsEmpty() const {
    return m_plugins.empty();
}

const std::vector<DiscoveredPlugin>& PluginManager::GetDiscoveredPlugins() const {
    return m_discoveredPlugins;
}

void PluginManager::LoadIndividualPlugin(DiscoveredPlugin& dp) {
    // Basic implementation
}

void PluginManager::UnloadIndividualPlugin(const std::string& name) {
    // Basic implementation
}

void PluginManager::InitializeLoadedPlugins() {
    Logger::info("PluginManager: Initializing loaded plugins...");
    for (auto& plugin : m_plugins) {
        if (plugin.pInterface && plugin.pInterface->OnInit) {
            Logger::info("PluginManager: Calling OnInit for plugin " + std::string(plugin.pInterface->GetName()));
            plugin.pInterface->OnInit(ImGui::GetCurrentContext(), PluginLogBridge, (void*)plugin.pInterface->GetName());
            Logger::info("PluginManager: OnInit complete for plugin " + std::string(plugin.pInterface->GetName()));
        }
    }
}

void PluginManager::SetGraphicsContext(const GamePlugGraphicsContext& context) {
    m_graphicsContext = context;
    m_hasGraphicsContext = true;

    Logger::info("PluginManager: SetGraphicsContext called. API Type = " + std::to_string(context.ApiType));

    for (auto& plugin : m_plugins) {
        if (plugin.pInterface && plugin.pInterface->OnGraphicsInit) {
            Logger::info("PluginManager: Propagating graphics context to plugin " + std::string(plugin.pInterface->GetName()));
            plugin.pInterface->OnGraphicsInit(&m_graphicsContext);
        }
    }
}

void PluginManager::UpdateVulkanSwapchain(void* swapchain) {
    m_graphicsContext.Vulkan.Swapchain = swapchain;

    Logger::info("PluginManager: UpdateVulkanSwapchain called. Swapchain = " + std::to_string((uintptr_t)swapchain));

    for (auto& plugin : m_plugins) {
        if (plugin.pInterface && plugin.pInterface->OnGraphicsInit) {
            plugin.pInterface->OnGraphicsInit(&m_graphicsContext);
        }
    }
}

void PluginManager::DispatchGetDeviceQueue(void* device, unsigned int queueFamilyIndex, unsigned int queueIndex, void** pQueue) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkGetDeviceQueue) {
            plugin.vulkanHooks->vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        }
    }
}

void PluginManager::DispatchGetDeviceQueue2(void* device, const void* pQueueInfo, void** pQueue) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkGetDeviceQueue2) {
            plugin.vulkanHooks->vkGetDeviceQueue2(device, pQueueInfo, pQueue);
        }
    }
}

void PluginManager::DispatchDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkDestroySwapchainKHR) {
            plugin.vulkanHooks->vkDestroySwapchainKHR(device, swapchain, pAllocator);
        }
    }
}

void PluginManager::DispatchQueuePresent(void* queue, const void* pPresentInfo) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkQueuePresentKHR) {
            plugin.vulkanHooks->vkQueuePresentKHR(queue, pPresentInfo);
        }
    }
}

void PluginManager::DispatchAcquireNextImageKHR(void* device, void* swapchain, unsigned long long timeout, void* semaphore, void* fence, unsigned int* pImageIndex) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkAcquireNextImageKHR) {
            plugin.vulkanHooks->vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
        }
    }
}

void PluginManager::DispatchGetSwapchainImagesKHR(void* device, void* swapchain, unsigned int* pSwapchainImageCount, void** pSwapchainImages) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkGetSwapchainImagesKHR) {
            plugin.vulkanHooks->vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
        }
    }
}

void PluginManager::DispatchCreateWin32SurfaceKHR(void* instance, const void* pCreateInfo, const void* pAllocator, void** pSurface) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateWin32SurfaceKHR) {
            plugin.vulkanHooks->vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
        }
    }
}

void PluginManager::DispatchGetPhysicalDeviceSurfaceCapabilitiesKHR(void* physicalDevice, void* surface, void* pSurfaceCapabilities) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
            plugin.vulkanHooks->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
        }
    }
}

void PluginManager::DispatchGetPhysicalDeviceSurfaceCapabilities2KHR(void* physicalDevice, const void* pSurfaceInfo, void* pSurfaceCapabilities) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkGetPhysicalDeviceSurfaceCapabilities2KHR) {
            plugin.vulkanHooks->vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
        }
    }
}

void PluginManager::DispatchCreateImage(void* device, void* pCreateInfo, const void* pAllocator, void** pImage) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateImage) {
            plugin.vulkanHooks->vkCreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }
}

void PluginManager::DispatchDestroyImage(void* device, void* image, const void* pAllocator) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkDestroyImage) {
            plugin.vulkanHooks->vkDestroyImage(device, image, pAllocator);
        }
    }
}

void PluginManager::DispatchCreateImageView(void* device, const void* pCreateInfo, const void* pAllocator, void** pView) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateImageView) {
            plugin.vulkanHooks->vkCreateImageView(device, pCreateInfo, pAllocator, pView);
        }
    }
}

void PluginManager::DispatchDestroyImageView(void* device, void* imageView, const void* pAllocator) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkDestroyImageView) {
            plugin.vulkanHooks->vkDestroyImageView(device, imageView, pAllocator);
        }
    }
}

void PluginManager::DispatchCreateFramebuffer(void* device, const void* pCreateInfo, const void* pAllocator, void** pFramebuffer) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateFramebuffer) {
            plugin.vulkanHooks->vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
        }
    }
}

void PluginManager::DispatchDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkDestroyFramebuffer) {
            plugin.vulkanHooks->vkDestroyFramebuffer(device, framebuffer, pAllocator);
        }
    }
}

void PluginManager::DispatchAllocateMemory(void* device, const void* pAllocateInfo, const void* pAllocator, void** pMemory) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkAllocateMemory) {
            plugin.vulkanHooks->vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
        }
    }
}

void PluginManager::DispatchBindImageMemory(void* device, void* image, void* memory, unsigned long long memoryOffset) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkBindImageMemory) {
            plugin.vulkanHooks->vkBindImageMemory(device, image, memory, memoryOffset);
        }
    }
}

void PluginManager::DispatchCreateRenderPass(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateRenderPass) {
            plugin.vulkanHooks->vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
        }
    }
}

void PluginManager::DispatchCreateGraphicsPipelines(void* device, void* pipelineCache, unsigned int createInfoCount, const void* pCreateInfos, const void* pAllocator, void** pPipelines) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCreateGraphicsPipelines) {
            plugin.vulkanHooks->vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        }
    }
}

void PluginManager::DispatchAllocateCommandBuffers(void* device, const void* pAllocateInfo, void** pCommandBuffers) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkAllocateCommandBuffers) {
            plugin.vulkanHooks->vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
        }
    }
}

void PluginManager::DispatchBeginCommandBuffer(void* commandBuffer, const void* pBeginInfo) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkBeginCommandBuffer) {
            plugin.vulkanHooks->vkBeginCommandBuffer(commandBuffer, pBeginInfo);
        }
    }
}

void PluginManager::DispatchEndCommandBuffer(void* commandBuffer) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkEndCommandBuffer) {
            plugin.vulkanHooks->vkEndCommandBuffer(commandBuffer);
        }
    }
}

void PluginManager::DispatchCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, unsigned int contents) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdBeginRenderPass) {
            plugin.vulkanHooks->vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
        }
    }
}

void PluginManager::DispatchCmdEndRenderPass(void* commandBuffer) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdEndRenderPass) {
            plugin.vulkanHooks->vkCmdEndRenderPass(commandBuffer);
        }
    }
}

void PluginManager::DispatchCmdSetViewport(void* commandBuffer, unsigned int firstViewport, unsigned int viewportCount, const void* pViewports) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdSetViewport) {
            plugin.vulkanHooks->vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
        }
    }
}

void PluginManager::DispatchCmdSetScissor(void* commandBuffer, unsigned int firstScissor, unsigned int scissorCount, const void* pScissors) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdSetScissor) {
            plugin.vulkanHooks->vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
        }
    }
}

void PluginManager::DispatchCmdBeginRendering(void* commandBuffer, const void* pRenderingInfo) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdBeginRendering) {
            plugin.vulkanHooks->vkCmdBeginRendering(commandBuffer, pRenderingInfo);
        }
    }
}

void PluginManager::DispatchCmdEndRendering(void* commandBuffer) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdEndRendering) {
            plugin.vulkanHooks->vkCmdEndRendering(commandBuffer);
        }
    }
}

void PluginManager::DispatchCmdBeginRenderingKHR(void* commandBuffer, const void* pRenderingInfo) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdBeginRenderingKHR) {
            plugin.vulkanHooks->vkCmdBeginRenderingKHR(commandBuffer, pRenderingInfo);
        }
    }
}

void PluginManager::DispatchCmdEndRenderingKHR(void* commandBuffer) {
    for (auto& plugin : m_plugins) {
        if (plugin.vulkanHooks && plugin.vulkanHooks->vkCmdEndRenderingKHR) {
            plugin.vulkanHooks->vkCmdEndRenderingKHR(commandBuffer);
        }
    }
}

} // namespace GamePlug
