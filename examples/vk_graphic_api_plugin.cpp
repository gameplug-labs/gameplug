// vk_graphic_api_plugin.cpp
// Example plugin demonstrating all Vulkan hook callbacks defined in plugin_vulkan.h.
// Each callback logs a message to the GamePlug console.
#include "plugin_helper.h"
#include "plugin_vulkan.h"
#include "volk.h"
#include <string>
#include <sstream>
#include <iomanip>

class GraphicApiPlugin : public GamePlug::Plugin {
public:
    const char* GetName() const override {
        return "Graphics API Example Plugin";
    }

    void OnGraphicsInit(const GamePlugGraphicsContext* context) override {
        if (!context) {
            GamePlug::Logger::error("Graphics context is null!");
            return;
        }

        m_apiType = context->ApiType;

        if (context->ApiType == GAMEPLUG_API_VULKAN) {
            GamePlug::Logger::info("Graphics API: Vulkan detected!");

            m_vkInstance       = context->Vulkan.Instance;
            m_vkPhysicalDevice = context->Vulkan.PhysicalDevice;
            m_vkDevice         = context->Vulkan.Device;
            m_vkQueue          = context->Vulkan.Queue;
            m_queueFamilyIndex = context->Vulkan.QueueFamilyIndex;
            m_vkInstanceTable  = context->Vulkan.InstanceTable;
            m_vkDeviceTable    = context->Vulkan.DeviceTable;
            m_vkQueueTable     = context->Vulkan.QueueTable;
            m_vkSwapchain      = context->Vulkan.Swapchain;

            // Query GPU name
            auto* vk = (const VolkInstanceTable*)m_vkInstanceTable;
            if (vk && vk->vkGetPhysicalDeviceProperties && m_vkPhysicalDevice) {
                VkPhysicalDeviceProperties props = {};
                vk->vkGetPhysicalDeviceProperties((VkPhysicalDevice)m_vkPhysicalDevice, &props);
                m_deviceName = props.deviceName;
                GamePlug::Logger::info(("Device Name: " + m_deviceName).c_str());
            }

            std::stringstream ss;
            ss << "Vulkan Context:\n"
               << "  Instance:      0x" << std::hex << m_vkInstance << "\n"
               << "  PhysDevice:    0x" << m_vkPhysicalDevice << "\n"
               << "  Device:        0x" << m_vkDevice << "\n"
               << "  Queue:         0x" << m_vkQueue << " (Family " << std::dec << m_queueFamilyIndex << ")\n"
               << "  InstanceTable: 0x" << std::hex << m_vkInstanceTable << "\n"
               << "  DeviceTable:   0x" << m_vkDeviceTable << "\n"
               << "  QueueTable:    0x" << m_vkQueueTable << "\n"
               << "  Swapchain:     0x" << m_vkSwapchain;
            GamePlug::Logger::info(ss.str().c_str());
        } else {
            GamePlug::Logger::info("Graphics API: Non-Vulkan API detected.");
        }
    }

    void OnCreateImage(void* device, void* pCreateInfo, const void* pAllocator, void** pImage) override {
        GamePlug::Logger::info("OnCreateImage: called.");
    }

    void OnDestroyImage(void* device, void* image, const void* pAllocator) override {
        std::stringstream ss;
        ss << "OnDestroyImage: image=0x" << std::hex << image;
        GamePlug::Logger::info(ss.str().c_str());
    }


    // -------------------------------------------------------------------------
    // ImGui UI
    // -------------------------------------------------------------------------
    void OnImGuiRender() override {
        ImGui::Begin("Graphics API Example");
        if (m_apiType == GAMEPLUG_API_VULKAN) {
            ImGui::Text("API: Vulkan");
            ImGui::Text("Device Name: %s", m_deviceName.c_str());
            ImGui::Separator();
            ImGui::Text("VkInstance:      0x%p", m_vkInstance);
            ImGui::Text("VkPhysDevice:    0x%p", m_vkPhysicalDevice);
            ImGui::Text("VkDevice:        0x%p", m_vkDevice);
            ImGui::Text("VkQueue:         0x%p (Family %u)", m_vkQueue, m_queueFamilyIndex);
            ImGui::Separator();
            ImGui::Text("InstanceTable:   0x%p", m_vkInstanceTable);
            ImGui::Text("DeviceTable:     0x%p", m_vkDeviceTable);
            ImGui::Text("QueueTable:      0x%p", m_vkQueueTable);
            ImGui::Separator();
            ImGui::Text("VkSwapchainKHR:  0x%p", m_vkSwapchain);
        } else if (m_apiType == GAMEPLUG_API_NONE) {
            ImGui::Text("API: None / Not Initialized");
        } else {
            ImGui::Text("API: Other (%d)", m_apiType);
        }
        ImGui::End();
    }

private:
    int          m_apiType          = GAMEPLUG_API_NONE;
    void*        m_vkInstance       = nullptr;
    void*        m_vkPhysicalDevice = nullptr;
    void*        m_vkDevice         = nullptr;
    void*        m_vkQueue          = nullptr;
    void*        m_vkInstanceTable  = nullptr;
    void*        m_vkDeviceTable    = nullptr;
    void*        m_vkQueueTable     = nullptr;
    void*        m_vkSwapchain      = nullptr;
    unsigned int m_queueFamilyIndex = 0;
    std::string  m_deviceName       = "Unknown Vulkan Device";
};

REGISTER_GAMEPLUG_PLUGIN(GraphicApiPlugin)
