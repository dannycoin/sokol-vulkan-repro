// sokol_gfx: _sg_vk_image_layout() returns the generic
// VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL for color attachments, while the
// VkRenderingAttachmentInfo struct declares VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL.
//
// Intel Haswell (intel_hasvk) silently ignores the load-op clear when the
// barrier transitions to a layout that differs from the one the attachment info
// declares, so a pass that asks for a clear keeps the previous contents.
// lavapipe and the OpenGL backend tolerate the mismatch.
//
// This repro uses plain Vulkan, because sokol_gfx exposes no readback: it runs
// two dynamic-rendering passes on the same image, the first clearing to red and
// the second to blue, and then copies the image out to check the result.
//
//   MATCH=1  barrier layout == attachment info layout   -> blue (correct)
//   MATCH=0  barrier uses the generic ATTACHMENT_OPTIMAL -> red (clear ignored)
//
// Build:
//   cc -O1 -o repro-2 2-attachment-layout.c -lvulkan -lm
//
// Run:
//   VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json ./repro-2

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 32
#define H 32

#define VK_CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    printf("FAILED %s -> %d (line %d)\n", #x, r_, __LINE__); return 1; } } while (0)

int main(void) {
    const int match = getenv("MATCH") ? atoi(getenv("MATCH")) : 1;
    printf("MATCH=%d (%s)\n", match,
        match ? "barrier layout matches the attachment info"
              : "barrier uses the generic ATTACHMENT_OPTIMAL");

    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    ai.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &ai;
    VkInstance inst;
    VK_CHECK(vkCreateInstance(&ici, NULL, &inst));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice devs[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(inst, &n, devs);
    VkPhysicalDevice pd = devs[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    printf("device: %s api=%u.%u.%u\n", props.deviceName,
        VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));

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

    const char* exts[] = { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dr = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    dr.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext = &dr;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = exts;
    VkDevice dev;
    VK_CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, qfamily, 0, &queue);

    // On a Vulkan 1.2 device the core 1.3 names are only promoted by the
    // loader, so resolve them by hand with the KHR spelling as fallback
    // (this is issue 3; without it the core names are NULL here).
    PFN_vkCmdBeginRendering begin_rendering = (PFN_vkCmdBeginRendering)
        vkGetDeviceProcAddr(dev, "vkCmdBeginRendering");
    if (!begin_rendering) begin_rendering = (PFN_vkCmdBeginRendering)
        vkGetDeviceProcAddr(dev, "vkCmdBeginRenderingKHR");
    PFN_vkCmdEndRendering end_rendering = (PFN_vkCmdEndRendering)
        vkGetDeviceProcAddr(dev, "vkCmdEndRendering");
    if (!end_rendering) end_rendering = (PFN_vkCmdEndRendering)
        vkGetDeviceProcAddr(dev, "vkCmdEndRenderingKHR");
    if (!begin_rendering || !end_rendering) {
        printf("dynamic rendering entry points missing\n");
        return 1;
    }

    // color target
    VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
    img_ci.extent.width = W;
    img_ci.extent.height = H;
    img_ci.extent.depth = 1;
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img;
    VK_CHECK(vkCreateImage(dev, &img_ci, NULL, &img));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    uint32_t mtype = 0;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if (mr.memoryTypeBits & (1u << i)) { mtype = i; break; }
    }
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mtype;
    VkDeviceMemory img_mem;
    VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &img_mem));
    VK_CHECK(vkBindImageMemory(dev, img, img_mem, 0));

    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_B8G8R8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    VkImageView view;
    VK_CHECK(vkCreateImageView(dev, &vci, NULL, &view));

    // readback buffer
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = W * H * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf;
    VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
    vkGetBufferMemoryRequirements(dev, buf, &mr);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            mtype = i; break;
        }
    }
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mtype;
    VkDeviceMemory buf_mem;
    VK_CHECK(vkAllocateMemory(dev, &mai, NULL, &buf_mem));
    VK_CHECK(vkBindBufferMemory(dev, buf, buf_mem, 0));

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = qfamily;
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));

    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cb, &cbbi));

    // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL for the first pass
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);

    VkRenderingAttachmentInfo att = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    att.imageView = view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.color.float32[0] = 1.0f;   // red
    att.clearValue.color.float32[3] = 1.0f;

    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.extent.width = W;
    ri.renderArea.extent.height = H;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;

    // pass 1: clear to red
    begin_rendering(cb, &ri);
    end_rendering(cb);

    // barrier into the second pass. When MATCH=1 it stays on
    // COLOR_ATTACHMENT_OPTIMAL; when MATCH=0 it uses the generic
    // ATTACHMENT_OPTIMAL that sokol's _sg_vk_image_layout() returns.
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = match ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        : VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);

    // pass 2: clear to blue; the attachment info still declares
    // COLOR_ATTACHMENT_OPTIMAL, as sokol does
    att.clearValue.color.float32[0] = 0.0f;
    att.clearValue.color.float32[2] = 1.0f;
    begin_rendering(cb, &ri);
    end_rendering(cb);

    // -> TRANSFER_SRC for readback
    b.oldLayout = match ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        : VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = W;
    region.imageExtent.height = H;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cb));

    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    void* data = NULL;
    VK_CHECK(vkMapMemory(dev, buf_mem, 0, VK_WHOLE_SIZE, 0, &data));
    unsigned char* px = (unsigned char*)data;
    const int i = ((H / 2) * W + (W / 2)) * 4;
    printf("centre pixel BGRA = %u %u %u %u\n", px[i], px[i + 1], px[i + 2], px[i + 3]);
    const int blue = px[i] > 200 && px[i + 1] < 50 && px[i + 2] < 50;
    const int red  = px[i] < 50 && px[i + 1] < 50 && px[i + 2] > 200;
    printf(blue ? "RESULT: blue, the clear took effect\n"
                : red ? "RESULT: red, the second clear was ignored\n"
                      : "RESULT: unexpected colour\n");
    vkUnmapMemory(dev, buf_mem);

    vkDestroyCommandPool(dev, pool, NULL);
    vkDestroyBuffer(dev, buf, NULL);
    vkFreeMemory(dev, buf_mem, NULL);
    vkDestroyImageView(dev, view, NULL);
    vkDestroyImage(dev, img, NULL);
    vkFreeMemory(dev, img_mem, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return blue ? 0 : 2;
}
