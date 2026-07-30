#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        VkResult r_ = (x);                                                                         \
        if (r_ != VK_SUCCESS)                                                                       \
        {                                                                                          \
            fprintf(stderr, "%s failed: %d\n", #x, r_);                                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static VkDevice         device;
static VkQueue          queue;
static VkCommandPool    pool;
static VkBuffer         buffer;
static VkDeviceMemory   memory;
static VkDeviceSize     bufferSize = 64u * 1024u * 1024u;

static VkCommandBuffer record(uint32_t fills)
{
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    for (uint32_t i = 0; i < fills; i++)
        vkCmdFillBuffer(cmd, buffer, 0, bufferSize, i);
    vkEndCommandBuffer(cmd);
    return cmd;
}

static VkResult submit(VkCommandBuffer cmd, VkFence fence)
{
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    return vkQueueSubmit(queue, 1, &si, fence);
}

int main(void)
{
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;

    VkInstance instance;
    CHECK(vkCreateInstance(&ici, NULL, &instance));

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (count == 0)
    {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    VkPhysicalDevice* physicalDevices = calloc(count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &count, physicalDevices);
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    printf("device: %s\n", props.deviceName);

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &families, NULL);
    VkQueueFamilyProperties* familyProps = calloc(families, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &families, familyProps);

    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < families; i++)
        if (familyProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            family = i;
            break;
        }
    if (family == UINT32_MAX)
    {
        fprintf(stderr, "no transfer queue\n");
        return 1;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qci;
    CHECK(vkCreateDevice(physicalDevice, &dci, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);

    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = bufferSize;
    bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if (req.memoryTypeBits & (1u << i))
        {
            typeIndex = i;
            break;
        }

    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = typeIndex;
    CHECK(vkAllocateMemory(device, &mai, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = family;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));

    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fci, NULL, &fence));

    const uint32_t APP_FILLS   = 24;
    const uint32_t LAYER_FILLS = 2;
    const int      RUNS        = 7;

    double scoped[RUNS], drain[RUNS];

    for (int run = 0; run < RUNS; run++)
    {
        vkResetFences(device, 1, &fence);

        VkCommandBuffer appFrame   = record(APP_FILLS);
        VkCommandBuffer layerPass  = record(LAYER_FILLS);
        VkCommandBuffer appNext    = record(APP_FILLS);

        CHECK(submit(appFrame, VK_NULL_HANDLE));
        CHECK(submit(layerPass, fence));
        CHECK(submit(appNext, VK_NULL_HANDLE));

        const double t0 = now_ms();
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
        const double t1 = now_ms();
        CHECK(vkQueueWaitIdle(queue));
        const double t2 = now_ms();

        scoped[run] = t1 - t0;
        drain[run]  = t2 - t0;

        vkFreeCommandBuffers(device, pool, 1, &appFrame);
        vkFreeCommandBuffers(device, pool, 1, &layerPass);
        vkFreeCommandBuffers(device, pool, 1, &appNext);
    }

    double scopedTotal = 0.0, drainTotal = 0.0;
    for (int i = 0; i < RUNS; i++)
    {
        scopedTotal += scoped[i];
        drainTotal += drain[i];
    }

    printf("runs=%d  app-frame-fills=%u  layer-pass-fills=%u  buffer=%lluMiB\n",
           RUNS, APP_FILLS, LAYER_FILLS, (unsigned long long) (bufferSize / (1024 * 1024)));
    printf("wait for our own submission (fence): mean %.2f ms\n", scopedTotal / RUNS);
    printf("wait for the whole queue (QueueWaitIdle): mean %.2f ms\n", drainTotal / RUNS);
    printf("over-wait: mean %.2f ms\n", (drainTotal - scopedTotal) / RUNS);

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    free(physicalDevices);
    free(familyProps);
    return 0;
}
