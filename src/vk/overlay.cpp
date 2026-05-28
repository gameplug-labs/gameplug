#include "overlay.h"
#include "config.h"
#include "dispatch.h"
#include "image_tracker.h"
#include "imgui_overlay_shared.h"
#include "logger.h"
#include "plugin_manager.h"
#include <algorithm>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "imgui_impl_win32.h"
#include <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {

static thread_local bool g_isRenderingOverlay = false;

bool OverlayRenderer::IsRenderingOverlay() {
    return g_isRenderingOverlay;
}
void OverlayRenderer::SetIsRenderingOverlay(bool val) {
    g_isRenderingOverlay = val;
}

OverlayRenderer& OverlayRenderer::Get() {
    static OverlayRenderer instance;
    return instance;
}

void OverlayRenderer::SetupDevice(
    VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue queue) {
    if (m_deviceSetup)
        return;

    Logger::info("OverlayRenderer: SetupDevice started");
    m_instance = instance;
    m_physDevice = physicalDevice;
    m_device = device;
    m_queue = queue;
    m_queueFamily = queueFamily;

    auto* dev_entry = DispatchManager::Get().GetDevice(device);
    auto* inst_entry = DispatchManager::Get().GetInstance(instance);
    if (!dev_entry || !inst_entry) {
        Logger::error("OverlayRenderer: Failed to get dispatch entries");
        return;
    }

    // Save Loader Context for ImGui
    m_loaderContext.instance = instance;
    m_loaderContext.device = device;
    m_loaderContext.nextGIPA = inst_entry->pfnNextGetInstanceProcAddr;

    // Create Descriptor Pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (dev_entry->table.vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Logger::error("OverlayRenderer: Failed to create descriptor pool");
        return;
    }

    // Create Command Pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = queueFamily;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (dev_entry->table.vkCreateCommandPool(device, &cmd_pool_info, nullptr, &m_commandPool) != VK_SUCCESS) {
        Logger::error("OverlayRenderer: Failed to create command pool");
        return;
    }

    // Initialize ImGui context
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ImGui Vulkan Load Functions - Using m_loaderContext to prevent recursion
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_1,
        [](const char* function_name, void* user_data) {
            auto* ctx = (LoaderContext*)user_data;
            auto* dev_entry = DispatchManager::Get().GetDevice(ctx->device);
            PFN_vkVoidFunction func = nullptr;
            if (dev_entry)
                func = dev_entry->pfnNextGetDeviceProcAddr(ctx->device, function_name);
            if (!func)
                func = ctx->nextGIPA(ctx->instance, function_name);
            return func;
        },
        &m_loaderContext);

    m_lastTime = std::chrono::steady_clock::now();

    if (m_hWnd) {
        ImGui_ImplWin32_Init(m_hWnd);
        m_originalWndProc = (WNDPROC)SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
    }

    m_deviceSetup = true;
    Logger::info("OverlayRenderer: Device setup complete");

    // Load Plugins
    PluginManager::Get().LoadPlugins();
}

void OverlayRenderer::SetupSwapchain(
    VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent, uint32_t imageCount, const std::vector<VkImage>& images) {
    if (!m_deviceSetup) {
        Logger::error("OverlayRenderer: Attempted swapchain setup without device setup");
        return;
    }

    std::lock_guard<std::mutex> lock(m_renderMtx);
    CleanupSwapchain();

    m_swapchainRes.swapchain = swapchain;
    m_swapchainRes.format = format;
    m_swapchainRes.extent = extent;
    m_swapchainRes.images = images;

    // Update ImageTracker with screen dimensions for better heuristic
    ImageTracker::Get().SetScreenDimensions(extent.width, extent.height);

    CreateRenderPass(format);
    CreateFramebuffers();

    // Initialize ImGui Vulkan backend
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_1;
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physDevice;
    init_info.Device = m_device;
    init_info.QueueFamily = m_queueFamily;
    init_info.Queue = m_queue;
    init_info.DescriptorPool = m_descriptorPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.PipelineInfoMain.RenderPass = m_swapchainRes.renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (m_initialized) {
        Logger::info("OverlayRenderer: Re-initializing ImGui Vulkan backend...");
        ImGui_ImplVulkan_Shutdown();
        m_initialized = false;
    }

    if (!m_initialized) {
        Logger::info("OverlayRenderer: Calling ImGui_ImplVulkan_Init...");
        if (!ImGui_ImplVulkan_Init(&init_info)) {
            Logger::error("OverlayRenderer: Failed to init ImGui Vulkan backend");
            return;
        }

        m_initialized = true;
        Logger::info("OverlayRenderer: ImGui Vulkan backend initialized");
    }

    // Allocate Command Buffers and Semaphores
    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    m_commandBuffers.resize(imageCount);
    m_renderCompleteSemaphores.resize(imageCount);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_commandPool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = imageCount;
    dev_entry->table.vkAllocateCommandBuffers(m_device, &alloc_info, m_commandBuffers.data());

    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < imageCount; i++) {
        dev_entry->table.vkCreateSemaphore(m_device, &sem_info, nullptr, &m_renderCompleteSemaphores[i]);
    }

    Logger::info("OverlayRenderer: Swapchain setup complete (" + std::to_string(imageCount) + " images)");
}

void OverlayRenderer::CreateRenderPass(VkFormat format) {
    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);

    VkAttachmentDescription attachment = {};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments = &attachment;
    rp_info.subpassCount = 1;
    rp_info.pSubpasses = &subpass;
    rp_info.dependencyCount = 1;
    rp_info.pDependencies = &dependency;

    dev_entry->table.vkCreateRenderPass(m_device, &rp_info, nullptr, &m_swapchainRes.renderPass);
}

void OverlayRenderer::CreateFramebuffers() {
    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    uint32_t count = (uint32_t)m_swapchainRes.images.size();
    m_swapchainRes.imageViews.resize(count);
    m_swapchainRes.framebuffers.resize(count);

    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = m_swapchainRes.images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = m_swapchainRes.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        dev_entry->table.vkCreateImageView(m_device, &view_info, nullptr, &m_swapchainRes.imageViews[i]);

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = m_swapchainRes.renderPass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &m_swapchainRes.imageViews[i];
        fb_info.width = m_swapchainRes.extent.width;
        fb_info.height = m_swapchainRes.extent.height;
        fb_info.layers = 1;
        dev_entry->table.vkCreateFramebuffer(m_device, &fb_info, nullptr, &m_swapchainRes.framebuffers[i]);
    }
}

void OverlayRenderer::CleanupSwapchain() {
    if (!m_deviceSetup)
        return;
    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    if (!dev_entry)
        return;

    // 🔥 MANDATORY SYNC
    dev_entry->table.vkDeviceWaitIdle(m_device);

    for (auto fb : m_swapchainRes.framebuffers) {
        if (fb != VK_NULL_HANDLE)
            dev_entry->table.vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_swapchainRes.framebuffers.clear();

    for (auto iv : m_swapchainRes.imageViews) {
        if (iv != VK_NULL_HANDLE)
            dev_entry->table.vkDestroyImageView(m_device, iv, nullptr);
    }
    m_swapchainRes.imageViews.clear();

    if (m_swapchainRes.renderPass != VK_NULL_HANDLE) {
        dev_entry->table.vkDestroyRenderPass(m_device, m_swapchainRes.renderPass, nullptr);
        m_swapchainRes.renderPass = VK_NULL_HANDLE;
    }

    m_swapchainRes.swapchain = VK_NULL_HANDLE;
    m_swapchainRes.images.clear();

    if (!m_commandBuffers.empty()) {
        dev_entry->table.vkFreeCommandBuffers(m_device, m_commandPool, (uint32_t)m_commandBuffers.size(), m_commandBuffers.data());
        m_commandBuffers.clear();
    }

    if (!m_renderCompleteSemaphores.empty()) {
        for (auto sem : m_renderCompleteSemaphores) {
            if (sem != VK_NULL_HANDLE)
                dev_entry->table.vkDestroySemaphore(m_device, sem, nullptr);
        }
        m_renderCompleteSemaphores.clear();
    }
}

void OverlayRenderer::NewFrame() {
    if (!m_initialized || m_frameStarted)
        return;

    std::lock_guard<std::mutex> lock(m_renderMtx);
    m_frameStarted = true;
    m_uiRendered = false;

    static uint32_t frameCount = 0;
    if (frameCount++ % 600 == 0) {
        Logger::info("OverlayRenderer: NewFrame started (Logged every 600 frames)");
    }

    // Toggle Visibility with Ctrl + HOME key
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool homePressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    bool keyCurrentlyPressed = ctrlPressed && homePressed;

    if (keyCurrentlyPressed && !m_showKeyWasPressed) {
        m_visible = !m_visible;
        Logger::info("OverlayRenderer: Visibility toggled manually to: " + std::string(m_visible ? "ON" : "OFF"));
    }
    m_showKeyWasPressed = keyCurrentlyPressed;

    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float, std::ratio<1, 1>>(currentTime - m_lastTime).count();
    m_lastTime = currentTime;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime > 0 ? deltaTime : 1.0f / 60.0f;
    io.DisplaySize = ImVec2((float)m_swapchainRes.extent.width, (float)m_swapchainRes.extent.height);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();

    if (m_visible && m_hWnd) {
        POINT cursorPos;
        if (GetCursorPos(&cursorPos)) {
            ScreenToClient(m_hWnd, &cursorPos);
            io.AddMousePosEvent((float)cursorPos.x, (float)cursorPos.y);
        }
        // Poll button state and feed it through the event queue
        io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
        io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
        // Draw ImGui's own cursor on top – mirrors Community Shaders behaviour.
        // The game cursor can be hidden in certain states (in-game, menus with
        // hardware cursor hidden), so ImGui's software cursor is more reliable.
        io.MouseDrawCursor = true;
    } else {
        io.MouseDrawCursor = false;
    }

    if (!m_visible) {
        io.ClearInputKeys();
    }

    ImGui::NewFrame();
}

void OverlayRenderer::EndFrame() {
    std::lock_guard<std::mutex> lock(m_renderMtx);
    m_frameStarted = false;
    m_uiRendered = false; // Reset state for the next frame
}

void OverlayRenderer::Render(VkCommandBuffer cmd, VkImage source, VkImage target, uint32_t width, uint32_t height) {
    if (!m_initialized || !m_visible || m_uiRendered)
        return;

    if (!m_frameStarted) {
        NewFrame();
    }

    static uint32_t renderCount = 0;
    if (renderCount++ % 600 == 0) {
        Logger::info("OverlayRenderer: Render called path (Logged every 600 frames)");
    }

    std::lock_guard<std::mutex> lock(m_renderMtx);

    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    if (!dev_entry)
        return;

    // 2. Draw GamePlug ImGui UI
    m_uiRendered = true;
    g_isRenderingOverlay = true;

    ImGuiOverlayShared::DrawUI(m_swapchainRes.extent.width, m_swapchainRes.extent.height);
    ImGui::Render();

    // Start a temporary render pass for our UI
    // Note: We avoid transitioning to PRESENT if we're in the middle of a command stream
    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = m_swapchainRes.renderPass;

    // Find correctly matching framebuffer for the target image
    uint32_t imageIndex = 0;
    for (uint32_t i = 0; i < m_swapchainRes.images.size(); ++i) {
        if (m_swapchainRes.images[i] == target) {
            imageIndex = i;
            break;
        }
    }

    rp_begin.framebuffer = m_swapchainRes.framebuffers[imageIndex];
    rp_begin.renderArea.extent = m_swapchainRes.extent;

    dev_entry->table.vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    dev_entry->table.vkCmdEndRenderPass(cmd);

    g_isRenderingOverlay = false;
}

void OverlayRenderer::RenderStandalone(VkImage target, uint32_t width, uint32_t height) {
    if (!m_initialized || !m_visible || m_uiRendered)
        return;

    static uint32_t standaloneCount = 0;
    if (standaloneCount++ % 600 == 0) {
        Logger::info("OverlayRenderer: Using Standalone Fallback Render (UI was not rendered in render passes)");
    }

    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    if (!dev_entry)
        return;

    VkCommandBuffer cmd = m_commandBuffers[m_currentSwapchainImage];

    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (dev_entry->table.vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS)
        return;

    // Transition image to COLOR_ATTACHMENT_OPTIMAL for rendering
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Assumption: we are in present state or similar
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.image = target;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    dev_entry->table.vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Call shared render logic
    Render(cmd, VK_NULL_HANDLE, target, width, height);

    // Transition back to PRESENT_SRC_KHR
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dev_entry->table.vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    dev_entry->table.vkEndCommandBuffer(cmd);

    // Submit standalone command buffer
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    dev_entry->table.vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE);
}

void OverlayRenderer::Shutdown() {
    if (!m_deviceSetup)
        return;
    CleanupSwapchain();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    PluginManager::Get().UnloadPlugins();
    ImGui::DestroyContext();
    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    if (m_commandPool)
        dev_entry->table.vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_descriptorPool)
        dev_entry->table.vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_deviceSetup = false;
    m_initialized = false;

    if (m_hWnd && m_originalWndProc) {
        SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
        m_originalWndProc = nullptr;
    }
}

void OnFrame() {
    OverlayRenderer::Get().NewFrame();
}

void OverlayRenderer::SetWindow(HWND hWnd) {
    if (m_hWnd == hWnd)
        return;
    m_hWnd = hWnd;
    if (m_deviceSetup && m_hWnd) {
        ImGui_ImplWin32_Init(m_hWnd);
        m_originalWndProc = (WNDPROC)SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
    }
}

LRESULT CALLBACK OverlayRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& renderer = OverlayRenderer::Get();

    if (renderer.m_visible) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;

        // Handle cursor visibility/capture
        if (ImGui::GetIO().WantCaptureMouse) {
            // ImGui handles the cursor when capturing mouse
        }
    }

    return CallWindowProc(renderer.m_originalWndProc, hWnd, msg, wParam, lParam);
}

VkDescriptorSet OverlayRenderer::RegisterDebugImage(VkImageView view, VkImageLayout layout) {
    if (!m_initialized)
        return VK_NULL_HANDLE;

    auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
    if (!dev_entry)
        return VK_NULL_HANDLE;

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.minLod = -1000;
    sampler_info.maxLod = 1000;
    sampler_info.maxAnisotropy = 1.0f;

    VkSampler sampler;
    if (dev_entry->table.vkCreateSampler(m_device, &sampler_info, nullptr, &sampler) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(sampler, view, layout);
    m_debugSamplers[ds] = sampler;
    return ds;
}

void OverlayRenderer::UnregisterDebugImage(VkDescriptorSet set) {
    if (!m_initialized)
        return;

    auto it = m_debugSamplers.find(set);
    if (it != m_debugSamplers.end()) {
        auto* dev_entry = DispatchManager::Get().GetDevice(m_device);
        if (dev_entry) {
            // Use parentheses to avoid potential macro expansion of vkDestroySampler
            (dev_entry->table.vkDestroySampler)(m_device, it->second, nullptr);
        }
        m_debugSamplers.erase(it);
    }
}

} // namespace GamePlug
