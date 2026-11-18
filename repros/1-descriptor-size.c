// sokol_gfx: _SG_VK_MAX_DESCRIPTOR_DATA_SIZE (256) is smaller than the
// 280 bytes lavapipe reports, so sg_make_sampler() fails.
//
// Build:
//   curl -O https://raw.githubusercontent.com/floooh/sokol/master/sokol_gfx.h
//   curl -O https://raw.githubusercontent.com/floooh/sokol/master/sokol_log.h
//   cc -O1 -DNDEBUG -o repro-1 1-descriptor-size.c -lvulkan -lpthread -ldl -lm
//
// Run (Mesa lavapipe):
//   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./repro-1
//
// Expected on master:  sampler state 4 (SG_RESOURCESTATE_FAILED)
//                      [sg][error][id:121] VULKAN_SAMPLER_MAX_DESCRIPTOR_SIZE
// Expected with 512:   sampler state 3 (SG_RESOURCESTATE_VALID)
//
// -DNDEBUG is required: without it sokol_gfx enables SOKOL_DEBUG and panics
// because vkSetDebugUtilsObjectNameEXT is not enabled on the instance.

#define SOKOL_IMPL
#define SOKOL_VULKAN
#include "sokol_gfx.h"
#include "sokol_log.h"

#include <vulkan/vulkan.h>
#include <stdio.h>

int main(void) {
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &ai;
    VkInstance inst;
    if (vkCreateInstance(&ici, 0, &inst) != VK_SUCCESS) {
        printf("vkCreateInstance failed\n");
        return 1;
    }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, 0);
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(inst, &n, devs);

    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    for (uint32_t i = 0; i < n; i++) {
        vkGetPhysicalDeviceProperties(devs[i], &props);
        if (props.apiVersion >= VK_API_VERSION_1_3) { pd = devs[i]; break; }
    }
    if (pd == VK_NULL_HANDLE) { printf("no Vulkan 1.3 device\n"); return 1; }
    vkGetPhysicalDeviceProperties(pd, &props);
    printf("device: %s api=%u.%u.%u\n", props.deviceName,
        VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));

    VkPhysicalDeviceDescriptorBufferPropertiesEXT dbp = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &dbp;
    vkGetPhysicalDeviceProperties2(pd, &p2);
    printf("samplerDescriptorSize=%zu storageBuffer=%zu uniformBuffer=%zu combinedImageSampler=%zu\n",
        dbp.samplerDescriptorSize, dbp.storageBufferDescriptorSize,
        dbp.uniformBufferDescriptorSize, dbp.combinedImageSamplerDescriptorSize);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, 0);
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

    const char* exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
    };
    VkPhysicalDeviceDescriptorBufferFeaturesEXT dbf = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT };
    dbf.descriptorBuffer = VK_TRUE;
    VkPhysicalDeviceVulkan13Features v13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    v13.pNext = &dbf;
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features v12 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    v12.pNext = &v13;
    v12.bufferDeviceAddress = VK_TRUE;
    v12.descriptorIndexing = VK_TRUE;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
    eds.pNext = &v12;
    eds.extendedDynamicState = VK_TRUE;

    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &eds;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = exts;
    VkDevice dev;
    if (vkCreateDevice(pd, &dci, 0, &dev) != VK_SUCCESS) {
        printf("vkCreateDevice failed\n");
        return 1;
    }
    VkQueue queue;
    vkGetDeviceQueue(dev, qfamily, 0, &queue);

    sg_desc desc = {0};
    desc.environment.vulkan.instance = inst;
    desc.environment.vulkan.physical_device = pd;
    desc.environment.vulkan.device = dev;
    desc.environment.vulkan.queue = queue;
    desc.environment.vulkan.queue_family_index = qfamily;
    desc.logger.func = slog_func;
    sg_setup(&desc);
    printf("sg_setup done, backend=%d\n", (int)sg_query_backend());

    sg_sampler_desc sd = {0};
    sd.min_filter = SG_FILTER_LINEAR;
    sd.mag_filter = SG_FILTER_LINEAR;
    sd.wrap_u = SG_WRAP_REPEAT;
    sd.wrap_v = SG_WRAP_REPEAT;
    sg_sampler smp = sg_make_sampler(&sd);
    sg_resource_state st = sg_query_sampler_state(smp);
    printf("sampler id=%u state=%d  (3=VALID, 4=FAILED)\n", smp.id, (int)st);
    printf(st == SG_RESOURCESTATE_VALID ? "RESULT: sampler VALID\n"
                                        : "RESULT: sampler FAILED\n");

    sg_shutdown();
    vkDestroyDevice(dev, 0);
    vkDestroyInstance(inst, 0);
    return st == SG_RESOURCESTATE_VALID ? 0 : 2;
}
