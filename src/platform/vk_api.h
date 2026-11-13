/* vk_api.h - the part of the Vulkan API the GPU device (gx_vulkan.c) uses,
 * declared from the Vulkan 1.3 specification (Khronos): handles, enums,
 * structures and function types, with our own short header instead of the
 * SDK's. The system's loader (libvulkan.so.1, vulkan-1.dll) is opened at run
 * time, so building needs no Vulkan SDK. Original code for this project. */
#ifndef TL_VK_API_H
#define TL_VK_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define VKAPI __stdcall
#else
#define VKAPI
#endif

typedef uint32_t VkBool32, VkFlags;
typedef uint64_t VkDeviceSize;
typedef int32_t VkResult;
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef uint64_t VkSemaphore, VkFence, VkDeviceMemory, VkBuffer, VkImage, VkImageView, VkShaderModule, VkPipelineLayout,
    VkPipeline, VkDescriptorSetLayout, VkSampler, VkDescriptorPool, VkDescriptorSet, VkCommandPool, VkSurfaceKHR,
    VkSwapchainKHR, VkRenderPass, VkPipelineCache;

enum {
  VK_SUCCESS = 0, VK_NOT_READY = 1, VK_TIMEOUT = 2, VK_INCOMPLETE = 5, VK_SUBOPTIMAL_KHR = 1000001003,
  VK_ERROR_OUT_OF_DATE_KHR = -1000001004, VK_ERROR_SURFACE_LOST_KHR = -1000000000
};
#define VK_API_VERSION_1_3 ((1u << 22) | (3u << 12))
#define VK_WHOLE_SIZE (~0ull)
#define VK_QUEUE_FAMILY_IGNORED (~0u)

enum {
  VK_STRUCTURE_TYPE_APPLICATION_INFO = 0, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
  VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
  VK_STRUCTURE_TYPE_SUBMIT_INFO = 4, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
  VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
  VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
  VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
  VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 19,
  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 20, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
  VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO = 25,
  VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26, VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO = 27,
  VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
  VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO = 31, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
  VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
  VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
  VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
  VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER = 45, VK_STRUCTURE_TYPE_MEMORY_BARRIER = 46,
  VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
  VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR = 1000004000,
  VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR = 1000006000,
  VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR = 1000009000,
  VK_STRUCTURE_TYPE_RENDERING_INFO = 1000044000, VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO = 1000044001,
  VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO = 1000044002,
  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES = 1000044003,
  VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT = 1000217000
};

enum {
  VK_FORMAT_UNDEFINED = 0, VK_FORMAT_R8G8B8A8_UNORM = 37, VK_FORMAT_B8G8R8A8_UNORM = 44,
  VK_FORMAT_R16G16B16A16_SFLOAT = 97, VK_FORMAT_R32G32B32_SFLOAT = 106, VK_FORMAT_R32G32B32A32_SFLOAT = 109,
  VK_FORMAT_D32_SFLOAT = 126
};
enum {
  VK_IMAGE_LAYOUT_UNDEFINED = 0, VK_IMAGE_LAYOUT_GENERAL = 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL = 4,
  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 5, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6,
  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 7, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002
};
enum { VK_IMAGE_ASPECT_COLOR_BIT = 1, VK_IMAGE_ASPECT_DEPTH_BIT = 2 };
enum {
  VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 1, VK_IMAGE_USAGE_TRANSFER_DST_BIT = 2, VK_IMAGE_USAGE_SAMPLED_BIT = 4,
  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x10, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x20
};
enum {
  VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 1, VK_BUFFER_USAGE_TRANSFER_DST_BIT = 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x10,
  VK_BUFFER_USAGE_INDEX_BUFFER_BIT = 0x40, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x80
};
enum {
  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 1, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 2, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 4,
  VK_MEMORY_PROPERTY_HOST_CACHED_BIT = 8
};
enum { VK_QUEUE_GRAPHICS_BIT = 1 };
enum { VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2 };
enum {
  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT = 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 0x400,
  VK_PIPELINE_STAGE_TRANSFER_BIT = 0x1000, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT = 0x2000,
  VK_PIPELINE_STAGE_HOST_BIT = 0x4000, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT = 0x10000
};
enum {
  VK_ACCESS_INDEX_READ_BIT = 2, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT = 4, VK_ACCESS_UNIFORM_READ_BIT = 8,
  VK_ACCESS_SHADER_READ_BIT = 0x20, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT = 0x80, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT = 0x100,
  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT = 0x200, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT = 0x400,
  VK_ACCESS_TRANSFER_READ_BIT = 0x800, VK_ACCESS_TRANSFER_WRITE_BIT = 0x1000, VK_ACCESS_HOST_READ_BIT = 0x2000,
  VK_ACCESS_HOST_WRITE_BIT = 0x4000, VK_ACCESS_MEMORY_READ_BIT = 0x8000, VK_ACCESS_MEMORY_WRITE_BIT = 0x10000
};
enum { VK_SHADER_STAGE_VERTEX_BIT = 1, VK_SHADER_STAGE_FRAGMENT_BIT = 0x10 };
enum { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC = 8 };
enum { VK_ATTACHMENT_LOAD_OP_LOAD = 0, VK_ATTACHMENT_LOAD_OP_CLEAR = 1, VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2 };
enum { VK_ATTACHMENT_STORE_OP_STORE = 0, VK_ATTACHMENT_STORE_OP_DONT_CARE = 1 };
enum { VK_RESOLVE_MODE_NONE = 0, VK_RESOLVE_MODE_AVERAGE_BIT = 2 };
enum { VK_PRIMITIVE_TOPOLOGY_LINE_LIST = 1, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3 };
enum { VK_COMPARE_OP_LESS_OR_EQUAL = 3, VK_COMPARE_OP_GREATER_OR_EQUAL = 6 };
enum { VK_BLEND_FACTOR_ONE = 1, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7 };
enum { VK_DYNAMIC_STATE_VIEWPORT = 0, VK_DYNAMIC_STATE_SCISSOR = 1 };
enum { VK_FILTER_NEAREST = 0, VK_FILTER_LINEAR = 1 };
enum { VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2 };
enum { VK_PRESENT_MODE_IMMEDIATE_KHR = 0, VK_PRESENT_MODE_MAILBOX_KHR = 1, VK_PRESENT_MODE_FIFO_KHR = 2 };
enum { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR = 1, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR = 8 };
enum { VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 2 };
enum { VK_FENCE_CREATE_SIGNALED_BIT = 1 };
enum { VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 1 };

typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y; } VkOffset2D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { float x, y, width, height, minDepth, maxDepth; } VkViewport;

typedef struct {
  int sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion; const char *pEngineName;
  uint32_t engineVersion, apiVersion;
} VkApplicationInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; const VkApplicationInfo *pApplicationInfo; uint32_t enabledLayerCount;
  const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;
typedef struct { char extensionName[256]; uint32_t specVersion; } VkExtensionProperties;
typedef struct { char layerName[256]; uint32_t specVersion, implementationVersion; char description[256]; } VkLayerProperties;
/* VkPhysicalDeviceProperties: only its first fields are read; the call writes it into a buffer of 1024 bytes */
typedef struct {
  uint32_t apiVersion, driverVersion, vendorID, deviceID; int deviceType; char deviceName[256];
} VkPhysicalDevicePropertiesHead;
typedef struct { VkFlags queueFlags; uint32_t queueCount, timestampValidBits; VkExtent3D minImageTransferGranularity; } VkQueueFamilyProperties;
typedef struct { VkFlags propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct {
  uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;
typedef struct {
  VkExtent3D maxExtent; uint32_t maxMipLevels, maxArrayLayers; VkFlags sampleCounts; VkDeviceSize maxResourceSize;
} VkImageFormatProperties;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t queueFamilyIndex, queueCount; const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;
typedef struct {
  int sType; void *pNext; VkBool32 dynamicRendering;
} VkPhysicalDeviceDynamicRenderingFeatures;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo *pQueueCreateInfos;
  uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount;
  const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures;
} VkDeviceCreateInfo;
typedef struct {
  int sType; const void *pNext; uint32_t waitSemaphoreCount; const VkSemaphore *pWaitSemaphores;
  const VkFlags *pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers;
  uint32_t signalSemaphoreCount; const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;
typedef struct { int sType; const void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { int sType; const void *pNext; VkFlags flags; } VkFenceCreateInfo, VkSemaphoreCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkDeviceSize size; VkFlags usage; int sharingMode;
  uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; int imageType; int format; VkExtent3D extent; uint32_t mipLevels, arrayLayers;
  VkFlags samples; int tiling; VkFlags usage; int sharingMode; uint32_t queueFamilyIndexCount;
  const uint32_t *pQueueFamilyIndices; int initialLayout;
} VkImageCreateInfo;
typedef struct { int r, g, b, a; } VkComponentMapping;
typedef struct { VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkImage image; int viewType; int format; VkComponentMapping components;
  VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;
typedef struct { int sType; const void *pNext; VkFlags flags; size_t codeSize; const uint32_t *pCode; } VkShaderModuleCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkFlags stage; VkShaderModule module; const char *pName;
  const void *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;
typedef struct { uint32_t binding, stride; int inputRate; } VkVertexInputBindingDescription;
typedef struct { uint32_t location, binding; int format; uint32_t offset; } VkVertexInputAttributeDescription;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t vertexBindingDescriptionCount;
  const VkVertexInputBindingDescription *pVertexBindingDescriptions; uint32_t vertexAttributeDescriptionCount;
  const VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; int topology; VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t viewportCount; const VkViewport *pViewports; uint32_t scissorCount;
  const VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkBool32 depthClampEnable, rasterizerDiscardEnable; int polygonMode;
  VkFlags cullMode; int frontFace; VkBool32 depthBiasEnable; float depthBiasConstantFactor, depthBiasClamp,
      depthBiasSlopeFactor, lineWidth;
} VkPipelineRasterizationStateCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkFlags rasterizationSamples; VkBool32 sampleShadingEnable;
  float minSampleShading; const uint32_t *pSampleMask; VkBool32 alphaToCoverageEnable, alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;
typedef struct { int failOp, passOp, depthFailOp, compareOp; uint32_t compareMask, writeMask, reference; } VkStencilOpState;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkBool32 depthTestEnable, depthWriteEnable; int depthCompareOp;
  VkBool32 depthBoundsTestEnable, stencilTestEnable; VkStencilOpState front, back; float minDepthBounds, maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;
typedef struct {
  VkBool32 blendEnable; int srcColorBlendFactor, dstColorBlendFactor, colorBlendOp, srcAlphaBlendFactor,
      dstAlphaBlendFactor, alphaBlendOp; VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkBool32 logicOpEnable; int logicOp; uint32_t attachmentCount;
  const VkPipelineColorBlendAttachmentState *pAttachments; float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t dynamicStateCount; const int *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;
typedef struct {
  int sType; const void *pNext; uint32_t viewMask, colorAttachmentCount; const int *pColorAttachmentFormats;
  int depthAttachmentFormat, stencilAttachmentFormat;
} VkPipelineRenderingCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t stageCount; const VkPipelineShaderStageCreateInfo *pStages;
  const VkPipelineVertexInputStateCreateInfo *pVertexInputState;
  const VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState; const void *pTessellationState;
  const VkPipelineViewportStateCreateInfo *pViewportState;
  const VkPipelineRasterizationStateCreateInfo *pRasterizationState;
  const VkPipelineMultisampleStateCreateInfo *pMultisampleState;
  const VkPipelineDepthStencilStateCreateInfo *pDepthStencilState;
  const VkPipelineColorBlendStateCreateInfo *pColorBlendState; const VkPipelineDynamicStateCreateInfo *pDynamicState;
  VkPipelineLayout layout; VkRenderPass renderPass; uint32_t subpass; VkPipeline basePipelineHandle;
  int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;
typedef struct {
  uint32_t binding; int descriptorType; uint32_t descriptorCount; VkFlags stageFlags; const VkSampler *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t bindingCount; const VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t setLayoutCount; const VkDescriptorSetLayout *pSetLayouts;
  uint32_t pushConstantRangeCount; const void *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; int magFilter, minFilter, mipmapMode, addressModeU, addressModeV,
      addressModeW; float mipLodBias; VkBool32 anisotropyEnable; float maxAnisotropy; VkBool32 compareEnable;
  int compareOp; float minLod, maxLod; int borderColor; VkBool32 unnormalizedCoordinates;
} VkSamplerCreateInfo;
typedef struct { int type; uint32_t descriptorCount; } VkDescriptorPoolSize;
typedef struct {
  int sType; const void *pNext; VkFlags flags; uint32_t maxSets, poolSizeCount; const VkDescriptorPoolSize *pPoolSizes;
} VkDescriptorPoolCreateInfo;
typedef struct {
  int sType; const void *pNext; VkDescriptorPool descriptorPool; uint32_t descriptorSetCount;
  const VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo;
typedef struct { VkSampler sampler; VkImageView imageView; int imageLayout; } VkDescriptorImageInfo;
typedef struct { VkBuffer buffer; VkDeviceSize offset, range; } VkDescriptorBufferInfo;
typedef struct {
  int sType; const void *pNext; VkDescriptorSet dstSet; uint32_t dstBinding, dstArrayElement, descriptorCount;
  int descriptorType; const VkDescriptorImageInfo *pImageInfo; const VkDescriptorBufferInfo *pBufferInfo;
  const void *pTexelBufferView;
} VkWriteDescriptorSet;
typedef struct { int sType; const void *pNext; VkFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct {
  int sType; const void *pNext; VkCommandPool commandPool; int level; uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;
typedef struct { int sType; const void *pNext; VkFlags flags; const void *pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct { int sType; const void *pNext; VkFlags srcAccessMask, dstAccessMask; } VkMemoryBarrier;
typedef struct {
  int sType; const void *pNext; VkFlags srcAccessMask, dstAccessMask; int oldLayout, newLayout;
  uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
typedef struct { VkDeviceSize srcOffset, dstOffset, size; } VkBufferCopy;
typedef struct { VkFlags aspectMask; uint32_t mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef struct {
  VkDeviceSize bufferOffset; uint32_t bufferRowLength, bufferImageHeight; VkImageSubresourceLayers imageSubresource;
  VkOffset3D imageOffset; VkExtent3D imageExtent;
} VkBufferImageCopy;
typedef union { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef struct { float depth; uint32_t stencil; } VkClearDepthStencilValue;
typedef union { VkClearColorValue color; VkClearDepthStencilValue depthStencil; } VkClearValue;
typedef struct {
  int sType; const void *pNext; VkImageView imageView; int imageLayout; VkFlags resolveMode; VkImageView resolveImageView;
  int resolveImageLayout; int loadOp, storeOp; VkClearValue clearValue;
} VkRenderingAttachmentInfo;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkRect2D renderArea; uint32_t layerCount, viewMask, colorAttachmentCount;
  const VkRenderingAttachmentInfo *pColorAttachments, *pDepthAttachment, *pStencilAttachment;
} VkRenderingInfo;

typedef struct {
  uint32_t minImageCount, maxImageCount; VkExtent2D currentExtent, minImageExtent, maxImageExtent;
  uint32_t maxImageArrayLayers; VkFlags supportedTransforms, currentTransform, supportedCompositeAlpha, supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;
typedef struct { int format, colorSpace; } VkSurfaceFormatKHR;
typedef struct {
  int sType; const void *pNext; VkFlags flags; VkSurfaceKHR surface; uint32_t minImageCount; int imageFormat,
      imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers; VkFlags imageUsage; int imageSharingMode;
  uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; VkFlags preTransform, compositeAlpha;
  int presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;
typedef struct {
  int sType; const void *pNext; uint32_t waitSemaphoreCount; const VkSemaphore *pWaitSemaphores; uint32_t swapchainCount;
  const VkSwapchainKHR *pSwapchains; const uint32_t *pImageIndices; VkResult *pResults;
} VkPresentInfoKHR;
typedef struct { int sType; const void *pNext; VkFlags flags; void *display, *surface; } VkWaylandSurfaceCreateInfoKHR;
typedef struct { int sType; const void *pNext; VkFlags flags; void *dpy; unsigned long window; } VkXlibSurfaceCreateInfoKHR;
typedef struct { int sType; const void *pNext; VkFlags flags; void *hinstance, *hwnd; } VkWin32SurfaceCreateInfoKHR;

typedef void(VKAPI *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction(VKAPI *PFN_vkGetInstanceProcAddr)(VkInstance, const char *);

/* every call gx_vulkan.c makes, X(name, return, arguments) */
#define VK_GLOBAL_CALLS(X)                                                                                             \
  X(vkCreateInstance, VkResult, (const VkInstanceCreateInfo *, const void *, VkInstance *))                            \
  X(vkEnumerateInstanceExtensionProperties, VkResult, (const char *, uint32_t *, VkExtensionProperties *))             \
  X(vkEnumerateInstanceLayerProperties, VkResult, (uint32_t *, VkLayerProperties *))
#define VK_INSTANCE_CALLS(X)                                                                                           \
  X(vkDestroyInstance, void, (VkInstance, const void *))                                                               \
  X(vkEnumeratePhysicalDevices, VkResult, (VkInstance, uint32_t *, VkPhysicalDevice *))                                \
  X(vkGetPhysicalDeviceProperties, void, (VkPhysicalDevice, void *))                                                   \
  X(vkGetPhysicalDeviceQueueFamilyProperties, void, (VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *))         \
  X(vkGetPhysicalDeviceMemoryProperties, void, (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *))                 \
  X(vkGetPhysicalDeviceImageFormatProperties, VkResult,                                                                \
    (VkPhysicalDevice, int, int, int, VkFlags, VkFlags, VkImageFormatProperties *))                                    \
  X(vkCreateDevice, VkResult, (VkPhysicalDevice, const VkDeviceCreateInfo *, const void *, VkDevice *))                \
  X(vkGetDeviceProcAddr, PFN_vkVoidFunction, (VkDevice, const char *))                                                 \
  X(vkDestroySurfaceKHR, void, (VkInstance, VkSurfaceKHR, const void *))                                               \
  X(vkGetPhysicalDeviceSurfaceSupportKHR, VkResult, (VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *))            \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, VkResult, (VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *)) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR, VkResult, (VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *)) \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR, VkResult, (VkPhysicalDevice, VkSurfaceKHR, uint32_t *, int *))
#define VK_DEVICE_CALLS(X)                                                                                             \
  X(vkDestroyDevice, void, (VkDevice, const void *))                                                                   \
  X(vkGetDeviceQueue, void, (VkDevice, uint32_t, uint32_t, VkQueue *))                                                 \
  X(vkDeviceWaitIdle, VkResult, (VkDevice))                                                                            \
  X(vkQueueSubmit, VkResult, (VkQueue, uint32_t, const VkSubmitInfo *, VkFence))                                       \
  X(vkQueueWaitIdle, VkResult, (VkQueue))                                                                              \
  X(vkAllocateMemory, VkResult, (VkDevice, const VkMemoryAllocateInfo *, const void *, VkDeviceMemory *))              \
  X(vkFreeMemory, void, (VkDevice, VkDeviceMemory, const void *))                                                      \
  X(vkMapMemory, VkResult, (VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void **))                   \
  X(vkUnmapMemory, void, (VkDevice, VkDeviceMemory))                                                                   \
  X(vkCreateBuffer, VkResult, (VkDevice, const VkBufferCreateInfo *, const void *, VkBuffer *))                        \
  X(vkDestroyBuffer, void, (VkDevice, VkBuffer, const void *))                                                         \
  X(vkGetBufferMemoryRequirements, void, (VkDevice, VkBuffer, VkMemoryRequirements *))                                 \
  X(vkBindBufferMemory, VkResult, (VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize))                                  \
  X(vkCreateImage, VkResult, (VkDevice, const VkImageCreateInfo *, const void *, VkImage *))                           \
  X(vkDestroyImage, void, (VkDevice, VkImage, const void *))                                                           \
  X(vkGetImageMemoryRequirements, void, (VkDevice, VkImage, VkMemoryRequirements *))                                   \
  X(vkBindImageMemory, VkResult, (VkDevice, VkImage, VkDeviceMemory, VkDeviceSize))                                    \
  X(vkCreateImageView, VkResult, (VkDevice, const VkImageViewCreateInfo *, const void *, VkImageView *))               \
  X(vkDestroyImageView, void, (VkDevice, VkImageView, const void *))                                                   \
  X(vkCreateSampler, VkResult, (VkDevice, const VkSamplerCreateInfo *, const void *, VkSampler *))                     \
  X(vkDestroySampler, void, (VkDevice, VkSampler, const void *))                                                       \
  X(vkCreateShaderModule, VkResult, (VkDevice, const VkShaderModuleCreateInfo *, const void *, VkShaderModule *))      \
  X(vkDestroyShaderModule, void, (VkDevice, VkShaderModule, const void *))                                             \
  X(vkCreateDescriptorSetLayout, VkResult,                                                                             \
    (VkDevice, const VkDescriptorSetLayoutCreateInfo *, const void *, VkDescriptorSetLayout *))                        \
  X(vkDestroyDescriptorSetLayout, void, (VkDevice, VkDescriptorSetLayout, const void *))                               \
  X(vkCreatePipelineLayout, VkResult, (VkDevice, const VkPipelineLayoutCreateInfo *, const void *, VkPipelineLayout *)) \
  X(vkDestroyPipelineLayout, void, (VkDevice, VkPipelineLayout, const void *))                                         \
  X(vkCreateGraphicsPipelines, VkResult,                                                                               \
    (VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const void *, VkPipeline *))           \
  X(vkDestroyPipeline, void, (VkDevice, VkPipeline, const void *))                                                     \
  X(vkCreateDescriptorPool, VkResult, (VkDevice, const VkDescriptorPoolCreateInfo *, const void *, VkDescriptorPool *)) \
  X(vkDestroyDescriptorPool, void, (VkDevice, VkDescriptorPool, const void *))                                         \
  X(vkResetDescriptorPool, VkResult, (VkDevice, VkDescriptorPool, VkFlags))                                            \
  X(vkAllocateDescriptorSets, VkResult, (VkDevice, const VkDescriptorSetAllocateInfo *, VkDescriptorSet *))            \
  X(vkUpdateDescriptorSets, void, (VkDevice, uint32_t, const VkWriteDescriptorSet *, uint32_t, const void *))          \
  X(vkCreateCommandPool, VkResult, (VkDevice, const VkCommandPoolCreateInfo *, const void *, VkCommandPool *))         \
  X(vkDestroyCommandPool, void, (VkDevice, VkCommandPool, const void *))                                               \
  X(vkAllocateCommandBuffers, VkResult, (VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *))            \
  X(vkResetCommandBuffer, VkResult, (VkCommandBuffer, VkFlags))                                                        \
  X(vkBeginCommandBuffer, VkResult, (VkCommandBuffer, const VkCommandBufferBeginInfo *))                               \
  X(vkEndCommandBuffer, VkResult, (VkCommandBuffer))                                                                   \
  X(vkCreateFence, VkResult, (VkDevice, const VkFenceCreateInfo *, const void *, VkFence *))                           \
  X(vkDestroyFence, void, (VkDevice, VkFence, const void *))                                                           \
  X(vkWaitForFences, VkResult, (VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t))                              \
  X(vkResetFences, VkResult, (VkDevice, uint32_t, const VkFence *))                                                    \
  X(vkCreateSemaphore, VkResult, (VkDevice, const VkSemaphoreCreateInfo *, const void *, VkSemaphore *))               \
  X(vkDestroySemaphore, void, (VkDevice, VkSemaphore, const void *))                                                   \
  X(vkCmdPipelineBarrier, void,                                                                                        \
    (VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const VkMemoryBarrier *, uint32_t, const void *, uint32_t,  \
     const VkImageMemoryBarrier *))                                                                                    \
  X(vkCmdCopyBuffer, void, (VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *))                      \
  X(vkCmdCopyBufferToImage, void, (VkCommandBuffer, VkBuffer, VkImage, int, uint32_t, const VkBufferImageCopy *))      \
  X(vkCmdCopyImageToBuffer, void, (VkCommandBuffer, VkImage, int, VkBuffer, uint32_t, const VkBufferImageCopy *))      \
  X(vkCmdBindPipeline, void, (VkCommandBuffer, int, VkPipeline))                                                       \
  X(vkCmdSetViewport, void, (VkCommandBuffer, uint32_t, uint32_t, const VkViewport *))                                 \
  X(vkCmdSetScissor, void, (VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *))                                    \
  X(vkCmdBindVertexBuffers, void, (VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *, const VkDeviceSize *))       \
  X(vkCmdBindIndexBuffer, void, (VkCommandBuffer, VkBuffer, VkDeviceSize, int))                                        \
  X(vkCmdBindDescriptorSets, void,                                                                                     \
    (VkCommandBuffer, int, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t *)) \
  X(vkCmdDraw, void, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t))                                        \
  X(vkCmdDrawIndexed, void, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t))                        \
  X(vkCmdBeginRendering, void, (VkCommandBuffer, const VkRenderingInfo *))                                             \
  X(vkCmdEndRendering, void, (VkCommandBuffer))                                                                        \
  X(vkCreateSwapchainKHR, VkResult, (VkDevice, const VkSwapchainCreateInfoKHR *, const void *, VkSwapchainKHR *))      \
  X(vkDestroySwapchainKHR, void, (VkDevice, VkSwapchainKHR, const void *))                                             \
  X(vkGetSwapchainImagesKHR, VkResult, (VkDevice, VkSwapchainKHR, uint32_t *, VkImage *))                              \
  X(vkAcquireNextImageKHR, VkResult, (VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *))           \
  X(vkQueuePresentKHR, VkResult, (VkQueue, const VkPresentInfoKHR *))

#endif
