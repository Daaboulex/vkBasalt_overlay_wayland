/*
 * mock_framegen -- a minimal Vulkan implicit layer that replicates the
 * STRUCTURAL present-time behavior of a frame-generation layer (lsfg-vk):
 *
 *   on every incoming vkQueuePresentKHR it
 *     1. consumes the caller's wait semaphores in an empty queue submit,
 *     2. calls vkAcquireNextImageKHR ITSELF on the same swapchain,
 *     3. presents the acquired image ("generated frame"),
 *     4. presents the original image last, waiting on the submit's signal.
 *
 * Purpose: regression-test a layer ABOVE it (vkBasalt) against the
 * acquire-inside-present + multi-present + semaphore-consumption pattern
 * without the proprietary frame-generation backend. It does NOT generate
 * frames -- the "generated" present shows a stale image, which is fine for
 * protocol testing.
 *
 * Limitations (test tool, not a product): tracks a single VkDevice; a ring
 * of 8 fence-guarded semaphore slots bounds in-flight reuse.
 *
 * Build (see run-local-matrix.sh):
 *   gcc -shared -fPIC -O2 -o libmock_framegen.so mock_framegen.c
 */

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <stdio.h>
#include <string.h>

#define RING 8

static PFN_vkGetInstanceProcAddr g_next_gipa = NULL;
static PFN_vkGetDeviceProcAddr   g_next_gdpa = NULL;

static VkDevice g_device = VK_NULL_HANDLE;

static PFN_vkCreateInstance       g_CreateInstance   = NULL;
static PFN_vkCreateDevice         g_CreateDevice     = NULL;
static PFN_vkAcquireNextImageKHR  g_AcquireNextImage = NULL;
static PFN_vkQueuePresentKHR      g_QueuePresent     = NULL;
static PFN_vkQueueSubmit          g_QueueSubmit      = NULL;
static PFN_vkCreateSemaphore      g_CreateSemaphore  = NULL;
static PFN_vkCreateFence          g_CreateFence      = NULL;
static PFN_vkWaitForFences        g_WaitForFences    = NULL;
static PFN_vkResetFences          g_ResetFences      = NULL;

typedef struct
{
    VkSemaphore doneSem;   /* signaled by the consuming submit           */
    VkSemaphore acqSem;    /* signaled by our own acquire                */
    VkFence     fence;     /* guards slot reuse                          */
    int         used;
} Slot;

static Slot     g_ring[RING];
static unsigned g_slot   = 0;
static unsigned g_frames = 0;

static void ensureSlots(void)
{
    if (g_ring[0].doneSem != VK_NULL_HANDLE)
        return;
    VkSemaphoreCreateInfo si = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    for (int i = 0; i < RING; i++)
    {
        g_CreateSemaphore(g_device, &si, NULL, &g_ring[i].doneSem);
        g_CreateSemaphore(g_device, &si, NULL, &g_ring[i].acqSem);
        g_CreateFence(g_device, &fi, NULL, &g_ring[i].fence);
        g_ring[i].used = 0;
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    /* Single-swapchain test tool; pass anything else straight through. */
    if (info->swapchainCount != 1 || g_device == VK_NULL_HANDLE)
        return g_QueuePresent(queue, info);

    ensureSlots();
    Slot* s = &g_ring[g_slot];
    g_slot  = (g_slot + 1) % RING;

    if (s->used)
    {
        g_WaitForFences(g_device, 1, &s->fence, VK_TRUE, 2000000000ULL);
        g_ResetFences(g_device, 1, &s->fence);
    }
    s->used = 1;

    /* 1. consume the caller's wait semaphores, signal doneSem (lsfg does
     *    this with its blit submit; empty submit keeps the same protocol). */
    VkPipelineStageFlags stages[8];
    for (uint32_t i = 0; i < info->waitSemaphoreCount && i < 8; i++)
        stages[i] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkSubmitInfo sub = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = info->waitSemaphoreCount,
        .pWaitSemaphores      = info->pWaitSemaphores,
        .pWaitDstStageMask    = stages,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &s->doneSem,
    };
    VkResult r = g_QueueSubmit(queue, 1, &sub, s->fence);
    if (r != VK_SUCCESS)
    {
        fprintf(stderr, "mock_framegen: consume-submit failed: %d\n", r);
        return g_QueuePresent(queue, info);
    }

    /* 2. acquire an extra image ourselves, inside the present call. */
    uint32_t extraIdx = 0;
    r = g_AcquireNextImage(g_device, info->pSwapchains[0], 2000000000ULL, s->acqSem, VK_NULL_HANDLE, &extraIdx);
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
    {
        /* 3. present the "generated" frame. */
        VkPresentInfoKHR gen = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &s->acqSem,
            .swapchainCount     = 1,
            .pSwapchains        = info->pSwapchains,
            .pImageIndices      = &extraIdx,
        };
        r = g_QueuePresent(queue, &gen);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            fprintf(stderr, "mock_framegen: generated present: %d\n", r);
    }
    else
    {
        fprintf(stderr, "mock_framegen: extra acquire failed: %d\n", r);
    }

    /* 4. present the original image last, like lsfg. */
    VkPresentInfoKHR orig = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = info->pNext,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &s->doneSem,
        .swapchainCount     = 1,
        .pSwapchains        = info->pSwapchains,
        .pImageIndices      = info->pImageIndices,
    };
    r = g_QueuePresent(queue, &orig);

    /* Periodic count: comparing this high-water mark against vkBasalt's
     * present-cycle count reveals the chain direction -- if the mock sits
     * ABOVE vkBasalt, vkBasalt sees 2 presents per app frame (generated +
     * original), so its cycles run ~2x the mock's frames. */
    if (++g_frames <= 3 || g_frames % 300 == 0)
        fprintf(stderr, "mock_framegen: frame %u ok (orig idx %u, extra idx %u)\n",
                g_frames, info->pImageIndices[0], extraIdx);
    if (info->pResults)
        info->pResults[0] = r;
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_CreateInstance(const VkInstanceCreateInfo*  info,
                                                          const VkAllocationCallbacks* alloc,
                                                          VkInstance*                  inst)
{
    VkLayerInstanceCreateInfo* lci = (VkLayerInstanceCreateInfo*) info->pNext;
    while (lci
           && (lci->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || lci->function != VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo*) lci->pNext;
    if (!lci)
        return VK_ERROR_INITIALIZATION_FAILED;

    g_next_gipa      = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;

    PFN_vkCreateInstance create = (PFN_vkCreateInstance) g_next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    return create(info, alloc, inst);
}

static VKAPI_ATTR VkResult VKAPI_CALL mock_CreateDevice(VkPhysicalDevice             phys,
                                                        const VkDeviceCreateInfo*    info,
                                                        const VkAllocationCallbacks* alloc,
                                                        VkDevice*                    dev)
{
    VkLayerDeviceCreateInfo* ldi = (VkLayerDeviceCreateInfo*) info->pNext;
    while (ldi
           && (ldi->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || ldi->function != VK_LAYER_LINK_INFO))
        ldi = (VkLayerDeviceCreateInfo*) ldi->pNext;
    if (!ldi)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ldi->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    g_next_gdpa                    = ldi->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    ldi->u.pLayerInfo              = ldi->u.pLayerInfo->pNext;

    PFN_vkCreateDevice create = (PFN_vkCreateDevice) gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult r                = create(phys, info, alloc, dev);
    if (r != VK_SUCCESS)
        return r;

    g_device           = *dev;
    g_AcquireNextImage = (PFN_vkAcquireNextImageKHR) g_next_gdpa(*dev, "vkAcquireNextImageKHR");
    g_QueuePresent     = (PFN_vkQueuePresentKHR) g_next_gdpa(*dev, "vkQueuePresentKHR");
    g_QueueSubmit      = (PFN_vkQueueSubmit) g_next_gdpa(*dev, "vkQueueSubmit");
    g_CreateSemaphore  = (PFN_vkCreateSemaphore) g_next_gdpa(*dev, "vkCreateSemaphore");
    g_CreateFence      = (PFN_vkCreateFence) g_next_gdpa(*dev, "vkCreateFence");
    g_WaitForFences    = (PFN_vkWaitForFences) g_next_gdpa(*dev, "vkWaitForFences");
    g_ResetFences      = (PFN_vkResetFences) g_next_gdpa(*dev, "vkResetFences");
    fprintf(stderr, "mock_framegen: device hooked\n");
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetDeviceProcAddr(VkDevice dev, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetInstanceProcAddr(VkInstance inst, const char* name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetDeviceProcAddr(VkDevice dev, const char* name)
{
    if (!strcmp(name, "vkQueuePresentKHR"))
        return (PFN_vkVoidFunction) mock_QueuePresentKHR;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetDeviceProcAddr;
    return g_next_gdpa ? g_next_gdpa(dev, name) : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mock_GetInstanceProcAddr(VkInstance inst, const char* name)
{
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction) mock_CreateInstance;
    if (!strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction) mock_CreateDevice;
    if (!strcmp(name, "vkQueuePresentKHR"))
        return (PFN_vkVoidFunction) mock_QueuePresentKHR;
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction) mock_GetDeviceProcAddr;
    return g_next_gipa ? g_next_gipa(inst, name) : NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v)
{
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || v->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion  = 2;
    v->pfnGetInstanceProcAddr       = mock_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr         = mock_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
