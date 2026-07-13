#pragma once

#include "framework_export.h"
#include "logger.h"
#include "volk.h"
#include <shared_mutex>
#include <unordered_map>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

namespace GamePlug {

// Structure to store next pointers and private dispatch tables
struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
    VolkInstanceTable table;
    VkInstance instance;
};

struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr pfnNextGetDeviceProcAddr;
    VolkDeviceTable table;
    VkDevice device;
};

class FRAMEWORK_API DispatchManager {
public:
    static DispatchManager& Get();

    void AddInstance(VkInstance instance, PFN_vkGetInstanceProcAddr nextGIPA) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        InstanceDispatch dispatch = {nextGIPA, {}, instance};

        // We need to set the global volk pointer so volkLoadInstanceTable uses it
        // Since we are in a DLL, these are private to our DLL.
        ::vkGetInstanceProcAddr = nextGIPA;
        volkLoadInstanceTable(&dispatch.table, instance);

        m_instances[instance] = dispatch;
    }

    void AddDevice(VkDevice device, PFN_vkGetDeviceProcAddr nextGDPA) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        DeviceDispatch dispatch = {nextGDPA, {}, device};

        // Similarly for device pointers
        ::vkGetDeviceProcAddr = nextGDPA;
        volkLoadDeviceTable(&dispatch.table, device);

        m_devices[device] = dispatch;
    }

    void AddQueue(VkQueue queue, VkDevice device) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_devices.find(device);
        if (it != m_devices.end()) {
            m_queues[queue] = &it->second;
        }
    }

    InstanceDispatch* GetInstance(VkInstance instance) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_instances.find(instance);
        return it != m_instances.end() ? &it->second : nullptr;
    }

    DeviceDispatch* GetDevice(VkDevice device) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_devices.find(device);
        return it != m_devices.end() ? &it->second : nullptr;
    }

    DeviceDispatch* GetQueueDispatch(VkQueue queue) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_queues.find(queue);
        return it != m_queues.end() ? it->second : nullptr;
    }

    void AddCommandBuffer(VkCommandBuffer cb, VkDevice device) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_devices.find(device);
        if (it != m_devices.end()) {
            m_commandBuffers[cb] = &it->second;
        }
    }

    DeviceDispatch* GetDeviceByCommandBuffer(VkCommandBuffer cb) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_commandBuffers.find(cb);
        return it != m_commandBuffers.end() ? it->second : nullptr;
    }

private:
    DispatchManager() = default;
    std::unordered_map<VkInstance, InstanceDispatch> m_instances;
    std::unordered_map<VkDevice, DeviceDispatch> m_devices;
    std::unordered_map<VkQueue, DeviceDispatch*> m_queues;
    std::unordered_map<VkCommandBuffer, DeviceDispatch*> m_commandBuffers;
    std::shared_mutex m_mutex;
};

} // namespace GamePlug
