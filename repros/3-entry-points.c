// sokol_gfx: the Vulkan 1.3 entry points are called directly
// (vkCmdBeginRendering, vkCmdEndRendering, vkCmdPipelineBarrier2,
// vkCmdCopyBufferToImage2) instead of being resolved through
// vkGetDeviceProcAddr with a KHR fallback.
//
// The loader promotes the core 1.3 names to the KHR implementations, but a
// validation layer in between stops that promotion: with
// VK_LAYER_KHRONOS_validation enabled, vkGetDeviceProcAddr("vkCmdBeginRendering")
// returns NULL while vkGetDeviceProcAddr("vkCmdBeginRenderingKHR") resolves.
// Calling the null pointer crashes.
//
// This repro needs no sokol: it only queries the two spellings.
//
// Build:
//   cc -O1 -o repro-3 3-entry-points.c -lvulkan
//
// Run without a layer:
//   ./repro-3
// Expected: both names resolve.
//
// Run with the Khronos validation layer:
//   VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./repro-3
// Expected on affected setups: "vkCmdBeginRendering" resolves to NULL while
// "vkCmdBeginRenderingKHR" resolves.

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int query(VkDevice dev, const char* name) {
    void* p = (void*)vkGetDeviceProcAddr(dev, name);
    printf("  %-28s %s\n", name, p ? "resolved" : "NULL");
    return p != NULL;
}

int main(void) {
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &ai;

    // Enable the layer through the instance so the test also works when the
    // layer is only available through VK_INSTANCE_LAYERS.
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    VkLayerProperties* props = NULL;
    if (layer_count > 0) {
        props = (VkLayerProperties*)malloc(sizeof(VkLayerProperties) * layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, props);
        for (uint32_t i = 0; i < layer_count; i++) {
            if (strcmp(props[i].layerName, layers[0]) == 0) {
                ici.enabledLayerCount = 1;
                ici.ppEnabledLayerNames = layers;
                printf("validation layer: enabled\n");
                break;
            }
        }
        free(props);
    }
    if (ici.enabledLayerCount == 0) {
        printf("validation layer: not installed, result is not meaningful\n");
    }

    VkInstance inst;
    VkResult res = vkCreateInstance(&ici, NULL, &inst);
    if (res != VK_SUCCESS) { printf("vkCreateInstance failed: %d\n", res); return 1; }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(inst, &n, devs);

    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props2;
    for (uint32_t i = 0; i < n; i++) {
        vkGetPhysicalDeviceProperties(devs[i], &props2);
        if (props2.apiVersion >= VK_API_VERSION_1_2) { pd = devs[i]; break; }
    }
    if (pd == VK_NULL_HANDLE) { printf("no Vulkan 1.2 device\n"); return 1; }
    vkGetPhysicalDeviceProperties(pd, &props2);
    printf("device: %s api=%u.%u.%u\n", props2.deviceName,
        VK_VERSION_MAJOR(props2.apiVersion), VK_VERSION_MINOR(props2.apiVersion),
        VK_VERSION_PATCH(props2.apiVersion));

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, NULL);
    VkQueueFamilyProperties qf[8];
    if (qn > 8) qn = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf);
    uint32_t qfamily = 0;
    for (uint32_t i = 0; i < qn; i++) {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfamily = i; break; }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    // dynamic rendering + synchronization2 as KHR extensions: that is what a
    // Vulkan 1.2 device has, and what the backend enables for such devices.
    const char* exts[] = {
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
    };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    dr.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceSynchronization2FeaturesKHR s2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
    s2.pNext = &dr;
    s2.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &s2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 3;
    dci.ppEnabledExtensionNames = exts;
    VkDevice dev;
    res = vkCreateDevice(pd, &dci, NULL, &dev);
    if (res != VK_SUCCESS) { printf("vkCreateDevice failed: %d\n", res); return 1; }

    printf("entry points:\n");
    int core_begin = query(dev, "vkCmdBeginRendering");
    int khr_begin  = query(dev, "vkCmdBeginRenderingKHR");
    int core_end   = query(dev, "vkCmdEndRendering");
    int khr_end    = query(dev, "vkCmdEndRenderingKHR");
    int core_bar   = query(dev, "vkCmdPipelineBarrier2");
    int khr_bar    = query(dev, "vkCmdPipelineBarrier2KHR");
    int core_copy  = query(dev, "vkCmdCopyBufferToImage2");
    int khr_copy   = query(dev, "vkCmdCopyBufferToImage2KHR");

    int broken = (!core_begin && khr_begin) || (!core_end && khr_end) ||
                 (!core_bar && khr_bar) || (!core_copy && khr_copy);
    if (broken) {
        printf("RESULT: core 1.3 names are NULL while KHR spellings resolve\n");
        printf("        sokol_gfx calls the core names directly and would crash\n");
    } else if (core_begin && core_end && core_bar && core_copy) {
        printf("RESULT: core 1.3 names resolve\n");
    } else {
        printf("RESULT: neither spelling resolves, device lacks the extensions\n");
    }

    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return broken ? 2 : 0;
}
