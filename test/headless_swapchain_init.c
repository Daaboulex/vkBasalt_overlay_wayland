#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>

#define CHECK(call)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        VkResult check_result = (call);                                                              \
        if (check_result != VK_SUCCESS)                                                              \
        {                                                                                            \
            fprintf(stderr, "%s failed: %d\n", #call, check_result);                               \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

static uint32_t clamp_u32(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    if (value < minimum)
        return minimum;
    if (maximum != UINT32_MAX && value > maximum)
        return maximum;
    return value;
}
static uint32_t dimension_from_env(const char* name, uint32_t fallback)
{
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0')
        return fallback;

    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0 || parsed > UINT32_MAX)
    {
        fprintf(stderr, "Ignoring invalid %s=%s\n", name, value);
        return fallback;
    }
    return (uint32_t)parsed;
}

int main(void)
{
    const int use_xlib = getenv("VKBASALT_TEST_XLIB") != NULL
        && strcmp(getenv("VKBASALT_TEST_XLIB"), "1") == 0;
    const uint32_t requested_width = dimension_from_env("VKBASALT_HEADLESS_WIDTH", 640);
    const uint32_t requested_height = dimension_from_env("VKBASALT_HEADLESS_HEIGHT", 360);
    const char* instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        use_xlib ? VK_KHR_XLIB_SURFACE_EXTENSION_NAME : VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
    VkApplicationInfo application = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "vkBasalt headless effect initializer";
    application.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = instance_extensions;

    VkInstance instance = VK_NULL_HANDLE;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    Display* x_display = NULL;
    Window x_window = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (use_xlib)
    {
        x_display = XOpenDisplay(NULL);
        if (x_display == NULL)
        {
            fprintf(stderr, "XOpenDisplay failed for the Xlib effect initializer\n");
            return 1;
        }
        const int screen = DefaultScreen(x_display);
        x_window = XCreateSimpleWindow(
            x_display, RootWindow(x_display, screen), 0, 0,
            requested_width, requested_height, 0,
            BlackPixel(x_display, screen), BlackPixel(x_display, screen));
        const int managed_xlib = getenv("VKBASALT_TEST_XLIB_MANAGED") != NULL
            && strcmp(getenv("VKBASALT_TEST_XLIB_MANAGED"), "1") == 0;
        if (!managed_xlib)
        {
            XSetWindowAttributes attributes = {0};
            attributes.override_redirect = True;
            XChangeWindowAttributes(x_display, x_window, CWOverrideRedirect, &attributes);
        }
        XStoreName(x_display, x_window, "vkBasalt effect initializer");
        XMapWindow(x_display, x_window);
        XSync(x_display, False);

        VkXlibSurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        surface_info.dpy = x_display;
        surface_info.window = x_window;
        CHECK(vkCreateXlibSurfaceKHR(instance, &surface_info, NULL, &surface));
    }
    else
    {
        PFN_vkCreateHeadlessSurfaceEXT create_headless_surface =
            (PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT");
        if (create_headless_surface == NULL)
        {
            fprintf(stderr, "VK_EXT_headless_surface entry point is unavailable\n");
            return 1;
        }

        VkHeadlessSurfaceCreateInfoEXT surface_info = {VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
        CHECK(create_headless_surface(instance, &surface_info, NULL, &surface));
    }

    uint32_t physical_count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL));
    if (physical_count == 0)
    {
        fprintf(stderr, "No Vulkan physical device is available\n");
        return 1;
    }
    VkPhysicalDevice* physical_devices = calloc(physical_count, sizeof(*physical_devices));
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
    for (uint32_t device_index = 0; device_index < physical_count && physical_device == VK_NULL_HANDLE; ++device_index)
    {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[device_index], &family_count, NULL);
        VkQueueFamilyProperties* families = calloc(family_count, sizeof(*families));
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[device_index], &family_count, families);
        for (uint32_t family_index = 0; family_index < family_count; ++family_index)
        {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[device_index], family_index, surface, &present_supported);
            if ((families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_supported)
            {
                physical_device = physical_devices[device_index];
                queue_family = family_index;
                break;
            }
        }
        free(families);
    }
    free(physical_devices);
    if (physical_device == VK_NULL_HANDLE)
    {
        fprintf(stderr, "No graphics queue can present to a headless surface\n");
        return 1;
    }

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);
    VkPhysicalDeviceFeatures enabled_features = {0};
    enabled_features.shaderStorageImageReadWithoutFormat = supported_features.shaderStorageImageReadWithoutFormat;
    enabled_features.shaderStorageImageWriteWithoutFormat = supported_features.shaderStorageImageWriteWithoutFormat;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &enabled_features;

    VkDevice device = VK_NULL_HANDLE;
    CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VkSurfaceCapabilitiesKHR capabilities;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));
    uint32_t format_count = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL));
    VkSurfaceFormatKHR* formats = calloc(format_count, sizeof(*formats));
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats));
    VkSurfaceFormatKHR selected_format = formats[0];
    free(formats);

    VkExtent2D extent = {
        requested_width,
        requested_height,
    };
    if (capabilities.currentExtent.width != UINT32_MAX)
        extent = capabilities.currentExtent;
    else
    {
        extent.width = clamp_u32(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = clamp_u32(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swapchain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = selected_format.format;
    swapchain_info.imageColorSpace = selected_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    uint32_t swapchain_image_count = 0;
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, NULL));
    VkImage* swapchain_images = calloc(swapchain_image_count, sizeof(*swapchain_images));
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images));

    printf("initialized %u effect-wrapped %s swapchain images at %ux%u\n",
           swapchain_image_count, use_xlib ? "Xlib" : "headless", extent.width, extent.height);

    uint32_t frame_count = 0;
    const char* frames_env = getenv("VKBASALT_HEADLESS_FRAMES");
    if (frames_env != NULL)
        frame_count = (uint32_t)strtoul(frames_env, NULL, 10);

    if (frame_count > 0)
    {
        VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family;
        VkCommandPool command_pool = VK_NULL_HANDLE;
        CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

        VkCommandBufferAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate_info.commandPool = command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer));

        VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
        CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
        CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &rendered));
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
        VkBool32* initialized = calloc(swapchain_image_count, sizeof(*initialized));

        for (uint32_t frame = 0; frame < frame_count; ++frame)
        {
            uint32_t image_index = 0;
            CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image_index));
            CHECK(vkResetCommandBuffer(command_buffer, 0));

            VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

            VkImageMemoryBarrier to_clear = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            to_clear.srcAccessMask = 0;
            to_clear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_clear.oldLayout = initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            to_clear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_clear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_clear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_clear.image = swapchain_images[image_index];
            to_clear.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(command_buffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &to_clear);

            // Keep the optional visible Xlib path photosensitivity-safe. A tiny
            // neutral change still gives temporal effects consecutive inputs
            // without flashing saturated red and blue across a large window.
            const float neutral = (frame & 1u) ? 0.12f : 0.10f;
            VkClearColorValue clear = {{neutral, neutral, neutral, 1.0f}};
            vkCmdClearColorImage(command_buffer, swapchain_images[image_index],
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &to_clear.subresourceRange);

            VkImageMemoryBarrier to_present = to_clear;
            to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_present.dstAccessMask = 0;
            to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(command_buffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, NULL, 0, NULL, 1, &to_present);
            CHECK(vkEndCommandBuffer(command_buffer));

            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &acquired;
            submit_info.pWaitDstStageMask = &wait_stage;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &rendered;
            CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));

            VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &rendered;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &swapchain;
            present_info.pImageIndices = &image_index;
            VkResult present_result = vkQueuePresentKHR(queue, &present_info);
            if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR)
            {
                fprintf(stderr, "vkQueuePresentKHR failed: %d\n", present_result);
                return 1;
            }
            CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
            CHECK(vkResetFences(device, 1, &fence));
            initialized[image_index] = VK_TRUE;
        }

        printf("presented %u headless frames through the effect chain\n", frame_count);
        // Waiting for the application's submit fence does not prove the
        // presentation engine has finished consuming its wait semaphore.
        // Complete the queue before destroying frame semaphores so validation
        // can distinguish application teardown from layer lifetime errors.
        CHECK(vkQueueWaitIdle(queue));
        free(initialized);
        vkDestroyFence(device, fence, NULL);
        vkDestroySemaphore(device, rendered, NULL);
        vkDestroySemaphore(device, acquired, NULL);
        vkDestroyCommandPool(device, command_pool, NULL);
    }

    free(swapchain_images);
    vkDeviceWaitIdle(device);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    if (x_display != NULL)
    {
        XDestroyWindow(x_display, x_window);
        XCloseDisplay(x_display);
    }
    return 0;
}
