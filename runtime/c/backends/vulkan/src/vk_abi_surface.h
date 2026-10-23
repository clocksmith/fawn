/* Linux LP64 WSI ABI from pinned retained vulkan_surface.zig and
 * vk_surface_sync.zig, plus separately verified official release-images ABI.
 * See ABI_PROVENANCE.json; no installed SDK or platform-header dependency. */
#ifndef DOE_VK_ABI_SURFACE_H
#define DOE_VK_ABI_SURFACE_H
#include "vk_abi_render.h"
_Static_assert(sizeof(unsigned long) == 8, "Xlib Window requires Linux LP64");
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkSwapchainKHR;
typedef uint64_t VkSemaphore;
typedef void (*doe_vk_proc)(void);
typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    VkFlags currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;
typedef struct VkSurfaceFormatKHR {
    uint32_t format;
    uint32_t colorSpace;
} VkSurfaceFormatKHR;
typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType sType;
    const void * pNext;
    VkFlags flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    uint32_t imageFormat;
    uint32_t imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkFlags imageUsage;
    uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t * pQueueFamilyIndices;
    VkFlags preTransform;
    VkFlags compositeAlpha;
    uint32_t presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;
typedef struct VkPresentInfoKHR {
    VkStructureType sType;
    const void * pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore * pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR * pSwapchains;
    const uint32_t * pImageIndices;
    VkResult * pResults;
} VkPresentInfoKHR;
typedef struct VkWaylandSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void * pNext;
    VkFlags flags;
    void * display;
    void * surface;
} VkWaylandSurfaceCreateInfoKHR;
typedef struct VkXcbSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void * pNext;
    VkFlags flags;
    void * connection;
    uint32_t window;
} VkXcbSurfaceCreateInfoKHR;
typedef struct VkXlibSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void * pNext;
    VkFlags flags;
    void * dpy;
    uint64_t window;
} VkXlibSurfaceCreateInfoKHR;
typedef struct VkSemaphoreCreateInfo {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
} VkSemaphoreCreateInfo;
typedef struct VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT {
    VkStructureType sType;
    void *pNext;
    VkBool32 swapchainMaintenance1;
} VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT;
typedef struct VkSwapchainPresentFenceInfoEXT {
    VkStructureType sType;
    const void *pNext;
    uint32_t swapchainCount;
    const VkFence *pFences;
} VkSwapchainPresentFenceInfoEXT;
typedef struct VkReleaseSwapchainImagesInfoEXT {
    VkStructureType sType;
    const void *pNext;
    VkSwapchainKHR swapchain;
    uint32_t imageIndexCount;
    const uint32_t *pImageIndices;
} VkReleaseSwapchainImagesInfoEXT;
enum {
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
    VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR = 1000006000,
    VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR = 1000005000,
    VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR = 1000004000,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT = 1000275000,
    VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT = 1000275001,
    VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT = 1000275005,
    VK_SUBOPTIMAL_KHR = 1000001003,
    VK_ERROR_OUT_OF_DATE_KHR = -1000001004,
    VK_ERROR_SURFACE_LOST_KHR = -1000000000
};
#endif
