#pragma once

#include <volk.h>
#include <vulkan/vulkan.h>

#include "common.h"

struct GamePlugContext {
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex;
    VkQueue queue;
};
