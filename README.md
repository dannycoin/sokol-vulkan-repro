# sokol_gfx Vulkan backend: repros

Minimal reproductions for three issues found while getting the sokol Vulkan
backend to run on drivers and hardware other than the ones it was developed on.

Each repro is a single C file. The first one links against sokol from `master`
(download `sokol_gfx.h` and `sokol_log.h` from https://github.com/floooh/sokol
and put them next to it); the other two use plain Vulkan, because they need
control that `sg_*` does not expose.

| File | Issue |
| --- | --- |
| `repros/1-descriptor-size.c` | `sg_make_sampler()` fails when the driver reports more than 256 bytes per descriptor |
| `repros/2-attachment-layout.c` | Haswell ignores the load-op clear when the barrier layout differs from the attachment info layout |
| `repros/3-entry-points.c` | Core 1.3 entry points resolve to null when a validation layer sits between the loader and the driver |

## Results on the machines these were written for

Mesa 23.2.1, Ubuntu 22.04, Vulkan 1.3.255 headers.

**1 — descriptor size.** lavapipe reports 280 bytes for every descriptor type:

```
$ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./repro-1
samplerDescriptorSize=280 storageBuffer=280 uniformBuffer=280 combinedImageSampler=280
sampler id=65537 state=4  (3=VALID, 4=FAILED)
RESULT: sampler FAILED
```

with `_SG_VK_MAX_DESCRIPTOR_DATA_SIZE` raised from 256 to 512, same binary input:

```
sampler id=65537 state=3  (3=VALID, 4=FAILED)
RESULT: sampler VALID
```

**2 — attachment layout.** Intel Iris Pro 5200 (Haswell, `intel_hasvk`, Vulkan 1.2.255):

```
$ MATCH=1 ... ./repro-2    # barrier layout == attachment info layout
centre pixel BGRA = 255 0 0 255
RESULT: blue, the clear took effect

$ MATCH=0 ... ./repro-2    # barrier uses the generic ATTACHMENT_OPTIMAL
centre pixel BGRA = 0 0 255 255
RESULT: red, the second clear was ignored
```

lavapipe tolerates the mismatch (`MATCH=0` still ends up blue), which is why the
bug only shows on the Haswell driver.

**3 — entry points.** With `VK_LAYER_KHRONOS_validation` loaded:

```
vkCmdBeginRendering          NULL
vkCmdBeginRenderingKHR       resolved
vkCmdEndRendering            NULL
vkCmdEndRenderingKHR         resolved
vkCmdPipelineBarrier2        NULL
vkCmdPipelineBarrier2KHR     resolved
vkCmdCopyBufferToImage2      NULL
vkCmdCopyBufferToImage2KHR   resolved
RESULT: core 1.3 names are NULL while KHR spellings resolve
```

Without the layer, both spellings resolve.

## Build

Repro 1 needs `-DNDEBUG`: without it sokol enables `SOKOL_DEBUG`, which requires
`vkSetDebugUtilsObjectNameEXT` on the instance and panics otherwise.

```
curl -O https://raw.githubusercontent.com/floooh/sokol/master/sokol_gfx.h
curl -O https://raw.githubusercontent.com/floooh/sokol/master/sokol_log.h
cc -O1 -DNDEBUG -o repro-1 repros/1-descriptor-size.c -lvulkan -lpthread -ldl -lm
cc -O1 -o repro-2 repros/2-attachment-layout.c -lvulkan -lm
cc -O1 -o repro-3 repros/3-entry-points.c -lvulkan
```

## Run

```
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json       ./repro-1
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_hasvk_icd.x86_64.json ./repro-2
VK_LAYER_PATH=<layer>/share/vulkan/explicit_layer.d ./repro-3
```
