#include "graphicsRuntimeInternal.h"

#include "runtimeError.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool extension_available(const VkExtensionProperties *extensions, uint32_t count, const char *name) {
	for (uint32_t index = 0; index < count; ++index) {
		if (strcmp(extensions[index].extensionName, name) == 0)
			return true;
	}
	return false;
}

static bool extension_enabled(const char *const *extensions, uint32_t count, const char *name) {
	for (uint32_t index = 0; index < count; ++index) {
		if (strcmp(extensions[index], name) == 0)
			return true;
	}
	return false;
}

void dynlex_graphics_set_vulkan_error(const char *operation, VkResult result) {
	char message[512];
	snprintf(message, sizeof(message), "%s failed (Vulkan result %d)", operation, result);
	dynlex_runtime_set_error(message);
}

bool dynlex_graphics_find_memory_type(
	DynlexGraphics *graphics, uint32_t type_bits, VkMemoryPropertyFlags properties, uint32_t *type_index
) {
	VkPhysicalDeviceMemoryProperties memory;
	vkGetPhysicalDeviceMemoryProperties(graphics->physical_device, &memory);
	for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
		if ((type_bits & (1u << index)) != 0 && (memory.memoryTypes[index].propertyFlags & properties) == properties) {
			*type_index = index;
			return true;
		}
	}
	dynlex_runtime_set_error("the Vulkan device has no compatible memory type");
	return false;
}

bool dynlex_graphics_create_buffer(
	DynlexGraphics *graphics, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer,
	VkDeviceMemory *memory
) {
	VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkResult result = vkCreateBuffer(graphics->device, &buffer_info, NULL, buffer);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a graphics buffer", result);
		return false;
	}
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(graphics->device, *buffer, &requirements);
	uint32_t memory_type;
	if (!dynlex_graphics_find_memory_type(graphics, requirements.memoryTypeBits, properties, &memory_type)) {
		vkDestroyBuffer(graphics->device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE;
		return false;
	}
	VkMemoryAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memory_type,
	};
	result = vkAllocateMemory(graphics->device, &allocation, NULL, memory);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating graphics buffer memory", result);
		vkDestroyBuffer(graphics->device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE;
		return false;
	}
	result = vkBindBufferMemory(graphics->device, *buffer, *memory, 0);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("binding graphics buffer memory", result);
		vkFreeMemory(graphics->device, *memory, NULL);
		vkDestroyBuffer(graphics->device, *buffer, NULL);
		*memory = VK_NULL_HANDLE;
		*buffer = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

static bool create_instance(DynlexGraphics *graphics) {
	PFN_vkEnumerateInstanceVersion enumerate_version =
		(PFN_vkEnumerateInstanceVersion)glfwGetInstanceProcAddress(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
	uint32_t loader_version = VK_API_VERSION_1_0;
	if (enumerate_version == NULL || enumerate_version(&loader_version) != VK_SUCCESS ||
		VK_API_VERSION_MAJOR(loader_version) < 1 ||
		(VK_API_VERSION_MAJOR(loader_version) == 1 && VK_API_VERSION_MINOR(loader_version) < 3)) {
		dynlex_runtime_set_error("DynLex graphics requires a Vulkan 1.3 or newer loader");
		return false;
	}
	uint32_t available_count = 0;
	VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &available_count, NULL);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("enumerating Vulkan instance extensions", result);
		return false;
	}
	VkExtensionProperties *available = calloc(available_count, sizeof(*available));
	if (available == NULL) {
		dynlex_runtime_set_error("failed to allocate Vulkan instance extension information");
		return false;
	}
	result = vkEnumerateInstanceExtensionProperties(NULL, &available_count, available);
	if (result != VK_SUCCESS) {
		free(available);
		dynlex_graphics_set_vulkan_error("enumerating Vulkan instance extensions", result);
		return false;
	}
	uint32_t glfw_count = 0;
	const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_count);
	if (glfw_extensions == NULL || glfw_count == 0) {
		free(available);
		dynlex_runtime_set_error("the window system did not provide required Vulkan surface extensions");
		return false;
	}
	const char **enabled = calloc(glfw_count + 1, sizeof(*enabled));
	if (enabled == NULL) {
		free(available);
		dynlex_runtime_set_error("failed to allocate required Vulkan extension names");
		return false;
	}
	for (uint32_t index = 0; index < glfw_count; ++index) {
		if (!extension_available(available, available_count, glfw_extensions[index])) {
			char message[512];
			snprintf(message, sizeof(message), "required Vulkan instance extension is unavailable: %s", glfw_extensions[index]);
			dynlex_runtime_set_error(message);
			free(enabled);
			free(available);
			return false;
		}
		enabled[index] = glfw_extensions[index];
	}
	bool portability_enumeration =
		extension_available(available, available_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#ifdef __APPLE__
	if (!portability_enumeration) {
		dynlex_runtime_set_error("MoltenVK requires VK_KHR_portability_enumeration, but the loader does not provide it");
		free(enabled);
		free(available);
		return false;
	}
#endif
	VkInstanceCreateFlags flags = 0;
	if (portability_enumeration) {
		if (!extension_enabled(enabled, glfw_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
			enabled[glfw_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
		flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}
	VkApplicationInfo application = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "DynLex program",
		.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.pEngineName = "DynLex",
		.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};
	VkInstanceCreateInfo instance_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.flags = flags,
		.pApplicationInfo = &application,
		.enabledExtensionCount = glfw_count,
		.ppEnabledExtensionNames = enabled,
	};
	result = vkCreateInstance(&instance_info, NULL, &graphics->instance);
	free(enabled);
	free(available);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan instance", result);
		return false;
	}
	result = glfwCreateWindowSurface(graphics->instance, graphics->window, NULL, &graphics->surface);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan window surface", result);
		return false;
	}
	return true;
}

static bool device_extensions_supported(VkPhysicalDevice device, bool *portability_subset) {
	uint32_t count = 0;
	if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS)
		return false;
	VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
	if (extensions == NULL)
		return false;
	VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
	bool swapchain = result == VK_SUCCESS && extension_available(extensions, count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	*portability_subset = result == VK_SUCCESS && extension_available(extensions, count, "VK_KHR_portability_subset");
	free(extensions);
	return swapchain;
}

static bool
find_queue_families(DynlexGraphics *graphics, VkPhysicalDevice device, uint32_t *graphics_family, uint32_t *present_family) {
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
	VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
	if (families == NULL)
		return false;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
	bool found_graphics = false;
	bool found_present = false;
	for (uint32_t index = 0; index < count; ++index) {
		if (!found_graphics && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			*graphics_family = index;
			found_graphics = true;
		}
		VkBool32 supports_present = VK_FALSE;
		if (vkGetPhysicalDeviceSurfaceSupportKHR(device, index, graphics->surface, &supports_present) == VK_SUCCESS &&
			!found_present && supports_present == VK_TRUE) {
			*present_family = index;
			found_present = true;
		}
	}
	free(families);
	return found_graphics && found_present;
}

static bool surface_usable(DynlexGraphics *graphics, VkPhysicalDevice device) {
	uint32_t format_count = 0;
	uint32_t mode_count = 0;
	return vkGetPhysicalDeviceSurfaceFormatsKHR(device, graphics->surface, &format_count, NULL) == VK_SUCCESS &&
		   vkGetPhysicalDeviceSurfacePresentModesKHR(device, graphics->surface, &mode_count, NULL) == VK_SUCCESS &&
		   format_count > 0 && mode_count > 0;
}

static bool create_device(DynlexGraphics *graphics) {
	uint32_t count = 0;
	VkResult result = vkEnumeratePhysicalDevices(graphics->instance, &count, NULL);
	if (result != VK_SUCCESS || count == 0) {
		dynlex_graphics_set_vulkan_error("enumerating Vulkan physical devices", result);
		if (result == VK_SUCCESS)
			dynlex_runtime_set_error("no Vulkan physical device is available");
		return false;
	}
	VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
	if (devices == NULL) {
		dynlex_runtime_set_error("failed to allocate Vulkan physical device information");
		return false;
	}
	result = vkEnumeratePhysicalDevices(graphics->instance, &count, devices);
	uint32_t best_score = 0;
	bool selected_portability = false;
	if (result == VK_SUCCESS) {
		for (uint32_t index = 0; index < count; ++index) {
			uint32_t graphics_family;
			uint32_t present_family;
			bool portability_subset;
			if (!find_queue_families(graphics, devices[index], &graphics_family, &present_family) ||
				!device_extensions_supported(devices[index], &portability_subset) || !surface_usable(graphics, devices[index]))
				continue;
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(devices[index], &properties);
			if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
				(VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 3))
				continue;
			uint32_t score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU	   ? 3u
							 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2u
																							   : 1u;
			if (graphics->physical_device == VK_NULL_HANDLE || score > best_score) {
				graphics->physical_device = devices[index];
				graphics->graphics_queue_family = graphics_family;
				graphics->present_queue_family = present_family;
				selected_portability = portability_subset;
				best_score = score;
			}
		}
	}
	free(devices);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("enumerating Vulkan physical devices", result);
		return false;
	}
	if (graphics->physical_device == VK_NULL_HANDLE) {
		dynlex_runtime_set_error("no Vulkan 1.3 device supports graphics, presentation, and swapchains for this window");
		return false;
	}
	float priority = 1.0f;
	uint32_t queue_count = graphics->graphics_queue_family == graphics->present_queue_family ? 1u : 2u;
	VkDeviceQueueCreateInfo queues[2];
	memset(queues, 0, sizeof(queues));
	queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queues[0].queueFamilyIndex = graphics->graphics_queue_family;
	queues[0].queueCount = 1;
	queues[0].pQueuePriorities = &priority;
	if (queue_count == 2) {
		queues[1] = queues[0];
		queues[1].queueFamilyIndex = graphics->present_queue_family;
	}
	const char *extensions[2] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset"};
	VkDeviceCreateInfo device_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = queue_count,
		.pQueueCreateInfos = queues,
		.enabledExtensionCount = selected_portability ? 2u : 1u,
		.ppEnabledExtensionNames = extensions,
	};
	result = vkCreateDevice(graphics->physical_device, &device_info, NULL, &graphics->device);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan logical device", result);
		return false;
	}
	graphics->portability_enabled = selected_portability;
	vkGetPhysicalDeviceProperties(graphics->physical_device, &graphics->physical_properties);
	vkGetDeviceQueue(graphics->device, graphics->graphics_queue_family, 0, &graphics->graphics_queue);
	vkGetDeviceQueue(graphics->device, graphics->present_queue_family, 0, &graphics->present_queue);
	return true;
}

static VkFormat choose_depth_format(DynlexGraphics *graphics) {
	const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
	for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(graphics->physical_device, candidates[index], &properties);
		if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			return candidates[index];
	}
	return VK_FORMAT_UNDEFINED;
}

static void destroy_swapchain_attachments(DynlexGraphics *graphics) {
	for (uint32_t index = 0; index < graphics->swapchain_image_count; ++index) {
		if (graphics->presentation_ready != NULL && graphics->presentation_ready[index] != VK_NULL_HANDLE)
			vkDestroySemaphore(graphics->device, graphics->presentation_ready[index], NULL);
		if (graphics->framebuffers != NULL)
			vkDestroyFramebuffer(graphics->device, graphics->framebuffers[index], NULL);
		if (graphics->depth_views != NULL)
			vkDestroyImageView(graphics->device, graphics->depth_views[index], NULL);
		if (graphics->depth_images != NULL)
			vkDestroyImage(graphics->device, graphics->depth_images[index], NULL);
		if (graphics->depth_memories != NULL)
			vkFreeMemory(graphics->device, graphics->depth_memories[index], NULL);
		if (graphics->swapchain_views != NULL)
			vkDestroyImageView(graphics->device, graphics->swapchain_views[index], NULL);
	}
	free(graphics->framebuffers);
	free(graphics->depth_views);
	free(graphics->depth_images);
	free(graphics->depth_memories);
	free(graphics->swapchain_views);
	free(graphics->swapchain_images);
	free(graphics->presentation_ready);
	graphics->framebuffers = NULL;
	graphics->depth_views = NULL;
	graphics->depth_images = NULL;
	graphics->depth_memories = NULL;
	graphics->swapchain_views = NULL;
	graphics->swapchain_images = NULL;
	graphics->presentation_ready = NULL;
	graphics->swapchain_image_count = 0;
}

static bool create_swapchain(DynlexGraphics *graphics, VkSwapchainKHR old_swapchain) {
	VkSurfaceCapabilitiesKHR capabilities;
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(graphics->physical_device, graphics->surface, &capabilities);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("querying Vulkan surface capabilities", result);
		return false;
	}
	if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
		dynlex_runtime_set_error("the Vulkan surface cannot be used as a color attachment");
		return false;
	}
	uint32_t format_count = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(graphics->physical_device, graphics->surface, &format_count, NULL);
	if (result != VK_SUCCESS || format_count == 0) {
		dynlex_graphics_set_vulkan_error("querying Vulkan surface formats", result);
		return false;
	}
	VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(*formats));
	if (formats == NULL) {
		dynlex_runtime_set_error("failed to allocate Vulkan surface format information");
		return false;
	}
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(graphics->physical_device, graphics->surface, &format_count, formats);
	if (result != VK_SUCCESS) {
		free(formats);
		dynlex_graphics_set_vulkan_error("querying Vulkan surface formats", result);
		return false;
	}
	VkSurfaceFormatKHR chosen = formats[0];
	if (chosen.format == VK_FORMAT_UNDEFINED)
		chosen.format = VK_FORMAT_B8G8R8A8_SRGB;
	for (uint32_t index = 0; index < format_count; ++index) {
		if (formats[index].format == VK_FORMAT_B8G8R8A8_SRGB &&
			formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			chosen = formats[index];
			break;
		}
	}
	free(formats);
	VkExtent2D extent = capabilities.currentExtent;
	if (extent.width == UINT32_MAX) {
		int width;
		int height;
		glfwGetFramebufferSize(graphics->window, &width, &height);
		extent.width = (uint32_t)width;
		extent.height = (uint32_t)height;
		if (extent.width < capabilities.minImageExtent.width)
			extent.width = capabilities.minImageExtent.width;
		if (extent.width > capabilities.maxImageExtent.width)
			extent.width = capabilities.maxImageExtent.width;
		if (extent.height < capabilities.minImageExtent.height)
			extent.height = capabilities.minImageExtent.height;
		if (extent.height > capabilities.maxImageExtent.height)
			extent.height = capabilities.maxImageExtent.height;
	}
	uint32_t image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
		image_count = capabilities.maxImageCount;
	uint32_t families[] = {graphics->graphics_queue_family, graphics->present_queue_family};
	VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if ((capabilities.supportedCompositeAlpha & composite_alpha) == 0) {
		const VkCompositeAlphaFlagBitsKHR candidates[] = {
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
			if ((capabilities.supportedCompositeAlpha & candidates[index]) != 0) {
				composite_alpha = candidates[index];
				break;
			}
		}
	}
	VkSwapchainCreateInfoKHR swapchain_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = graphics->surface,
		.minImageCount = image_count,
		.imageFormat = chosen.format,
		.imageColorSpace = chosen.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = graphics->graphics_queue_family == graphics->present_queue_family ? VK_SHARING_MODE_EXCLUSIVE
																							  : VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = graphics->graphics_queue_family == graphics->present_queue_family ? 0u : 2u,
		.pQueueFamilyIndices = families,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = composite_alpha,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
		.oldSwapchain = old_swapchain,
	};
	result = vkCreateSwapchainKHR(graphics->device, &swapchain_info, NULL, &graphics->swapchain);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan swapchain", result);
		graphics->swapchain = VK_NULL_HANDLE;
		return false;
	}
	graphics->swapchain_format = chosen.format;
	graphics->swapchain_color_space = chosen.colorSpace;
	graphics->swapchain_extent = extent;
	result = vkGetSwapchainImagesKHR(graphics->device, graphics->swapchain, &image_count, NULL);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("querying Vulkan swapchain images", result);
		return false;
	}
	graphics->swapchain_images = calloc(image_count, sizeof(*graphics->swapchain_images));
	graphics->swapchain_views = calloc(image_count, sizeof(*graphics->swapchain_views));
	graphics->depth_images = calloc(image_count, sizeof(*graphics->depth_images));
	graphics->depth_memories = calloc(image_count, sizeof(*graphics->depth_memories));
	graphics->depth_views = calloc(image_count, sizeof(*graphics->depth_views));
	graphics->framebuffers = calloc(image_count, sizeof(*graphics->framebuffers));
	graphics->presentation_ready = calloc(image_count, sizeof(*graphics->presentation_ready));
	if (graphics->swapchain_images == NULL || graphics->swapchain_views == NULL || graphics->depth_images == NULL ||
		graphics->depth_memories == NULL || graphics->depth_views == NULL || graphics->framebuffers == NULL ||
		graphics->presentation_ready == NULL) {
		dynlex_runtime_set_error("failed to allocate Vulkan swapchain resources");
		return false;
	}
	result = vkGetSwapchainImagesKHR(graphics->device, graphics->swapchain, &image_count, graphics->swapchain_images);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("querying Vulkan swapchain images", result);
		return false;
	}
	graphics->swapchain_image_count = image_count;
	return true;
}

static bool create_render_pass(DynlexGraphics *graphics) {
	VkAttachmentDescription attachments[2] = {
		{
			.format = graphics->swapchain_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		},
		{
			.format = graphics->depth_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};
	VkAttachmentReference color = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color,
		.pDepthStencilAttachment = &depth,
	};
	VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	};
	VkRenderPassCreateInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 2,
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency,
	};
	VkResult result = vkCreateRenderPass(graphics->device, &render_pass_info, NULL, &graphics->render_pass);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan render pass", result);
		return false;
	}
	return true;
}

bool dynlex_graphics_create_image(
	DynlexGraphics *graphics, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage *image,
	VkDeviceMemory *memory
) {
	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {width, height, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VkResult result = vkCreateImage(graphics->device, &image_info, NULL, image);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan image", result);
		return false;
	}
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(graphics->device, *image, &requirements);
	uint32_t memory_type;
	if (!dynlex_graphics_find_memory_type(
			graphics, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memory_type
		))
		return false;
	VkMemoryAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = requirements.size,
		.memoryTypeIndex = memory_type,
	};
	result = vkAllocateMemory(graphics->device, &allocation, NULL, memory);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating Vulkan image memory", result);
		return false;
	}
	result = vkBindImageMemory(graphics->device, *image, *memory, 0);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("binding Vulkan image memory", result);
		return false;
	}
	return true;
}

static bool create_swapchain_attachments(DynlexGraphics *graphics) {
	VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	for (uint32_t index = 0; index < graphics->swapchain_image_count; ++index) {
		VkResult result =
			vkCreateSemaphore(graphics->device, &semaphore_info, NULL, &graphics->presentation_ready[index]);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating Vulkan presentation synchronization", result);
			return false;
		}
		VkImageViewCreateInfo color_view = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = graphics->swapchain_images[index],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = graphics->swapchain_format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		result = vkCreateImageView(graphics->device, &color_view, NULL, &graphics->swapchain_views[index]);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan swapchain image view", result);
			return false;
		}
		if (!dynlex_graphics_create_image(
				graphics, graphics->swapchain_extent.width, graphics->swapchain_extent.height, graphics->depth_format,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &graphics->depth_images[index], &graphics->depth_memories[index]
			))
			return false;
		VkImageViewCreateInfo depth_view = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = graphics->depth_images[index],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = graphics->depth_format,
			.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
		};
		result = vkCreateImageView(graphics->device, &depth_view, NULL, &graphics->depth_views[index]);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan depth image view", result);
			return false;
		}
		VkImageView views[] = {graphics->swapchain_views[index], graphics->depth_views[index]};
		VkFramebufferCreateInfo framebuffer_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = graphics->render_pass,
			.attachmentCount = 2,
			.pAttachments = views,
			.width = graphics->swapchain_extent.width,
			.height = graphics->swapchain_extent.height,
			.layers = 1,
		};
		result = vkCreateFramebuffer(graphics->device, &framebuffer_info, NULL, &graphics->framebuffers[index]);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan framebuffer", result);
			return false;
		}
	}
	return true;
}

static bool create_frame_resources(DynlexGraphics *graphics) {
	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = graphics->graphics_queue_family,
	};
	VkResult result = vkCreateCommandPool(graphics->device, &pool_info, NULL, &graphics->command_pool);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan command pool", result);
		return false;
	}
	VkCommandBuffer command_buffers[DYNLEX_GRAPHICS_FRAMES];
	VkCommandBufferAllocateInfo command_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = graphics->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = DYNLEX_GRAPHICS_FRAMES,
	};
	result = vkAllocateCommandBuffers(graphics->device, &command_info, command_buffers);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating Vulkan command buffers", result);
		return false;
	}
	for (uint32_t index = 0; index < DYNLEX_GRAPHICS_FRAMES; ++index) {
		DynlexGraphicsFrame *frame = &graphics->frames[index];
		frame->commands = command_buffers[index];
		VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		VkFenceCreateInfo fence_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		if ((result = vkCreateSemaphore(graphics->device, &semaphore_info, NULL, &frame->image_available)) != VK_SUCCESS ||
			(result = vkCreateFence(graphics->device, &fence_info, NULL, &frame->completed)) != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating Vulkan frame synchronization", result);
			return false;
		}
		if (!dynlex_graphics_create_buffer(
				graphics, DYNLEX_GRAPHICS_UPLOAD_CAPACITY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame->upload_buffer,
				&frame->upload_memory
			))
			return false;
		result =
			vkMapMemory(graphics->device, frame->upload_memory, 0, DYNLEX_GRAPHICS_UPLOAD_CAPACITY, 0, (void **)&frame->upload);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("mapping the Vulkan frame upload buffer", result);
			return false;
		}
	}
	VkDescriptorPoolSize pool_sizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
	};
	VkDescriptorPoolCreateInfo descriptor_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 2048,
		.poolSizeCount = 2,
		.pPoolSizes = pool_sizes,
	};
	result = vkCreateDescriptorPool(graphics->device, &descriptor_info, NULL, &graphics->descriptor_pool);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating the Vulkan descriptor pool", result);
		return false;
	}
	return true;
}

bool dynlex_graphics_recreate_swapchain(DynlexGraphics *graphics) {
	int width;
	int height;
	glfwGetFramebufferSize(graphics->window, &width, &height);
	if (width == 0 || height == 0)
		return true;
	VkResult result = vkDeviceWaitIdle(graphics->device);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("waiting to rebuild the Vulkan swapchain", result);
		return false;
	}
	VkFormat previous_format = graphics->swapchain_format;
	VkSwapchainKHR previous_swapchain = graphics->swapchain;
	destroy_swapchain_attachments(graphics);
	graphics->swapchain = VK_NULL_HANDLE;
	if (!create_swapchain(graphics, previous_swapchain)) {
		vkDestroySwapchainKHR(graphics->device, previous_swapchain, NULL);
		return false;
	}
	vkDestroySwapchainKHR(graphics->device, previous_swapchain, NULL);
	if (previous_format != graphics->swapchain_format) {
		dynlex_graphics_destroy_builtin_pipelines(graphics);
		for (DynlexGraphicsPipeline *pipeline = graphics->pipelines; pipeline != NULL; pipeline = pipeline->next) {
			if (pipeline->pipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(graphics->device, pipeline->pipeline, NULL);
			pipeline->pipeline = VK_NULL_HANDLE;
		}
		vkDestroyRenderPass(graphics->device, graphics->render_pass, NULL);
		graphics->render_pass = VK_NULL_HANDLE;
		if (!create_render_pass(graphics) || !dynlex_graphics_create_builtin_pipelines(graphics) ||
			!dynlex_graphics_rebuild_custom_pipelines(graphics))
			return false;
	}
	if (!create_swapchain_attachments(graphics))
		return false;
	graphics->swapchain_dirty = false;
	return true;
}

bool dynlex_graphics_create_vulkan(DynlexGraphics *graphics) {
	if (glfwVulkanSupported() != GLFW_TRUE) {
		dynlex_runtime_set_error("the installed window system cannot load Vulkan");
		return false;
	}
	if (!create_instance(graphics) || !create_device(graphics))
		return false;
	graphics->depth_format = choose_depth_format(graphics);
	if (graphics->depth_format == VK_FORMAT_UNDEFINED) {
		dynlex_runtime_set_error("the Vulkan device has no supported depth attachment format");
		return false;
	}
	if (!create_swapchain(graphics, VK_NULL_HANDLE) || !create_render_pass(graphics) ||
		!create_swapchain_attachments(graphics) || !create_frame_resources(graphics) ||
		!dynlex_graphics_create_builtin_pipelines(graphics))
		return false;
	return true;
}

void dynlex_graphics_destroy_vulkan(DynlexGraphics *graphics) {
	if (graphics->device != VK_NULL_HANDLE)
		vkDeviceWaitIdle(graphics->device);
	while (graphics->pipelines != NULL) {
		DynlexGraphicsPipeline *pipeline = graphics->pipelines;
		graphics->pipelines = pipeline->next;
		dynlex_graphics_destroy_pipeline_resources(pipeline);
		free(pipeline);
	}
	while (graphics->textures != NULL) {
		DynlexGraphicsTexture *texture = graphics->textures;
		graphics->textures = texture->next;
		dynlex_graphics_destroy_texture_resources(texture);
		free(texture);
	}
	dynlex_graphics_destroy_builtin_pipelines(graphics);
	destroy_swapchain_attachments(graphics);
	if (graphics->render_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(graphics->device, graphics->render_pass, NULL);
	if (graphics->swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(graphics->device, graphics->swapchain, NULL);
	for (uint32_t index = 0; index < DYNLEX_GRAPHICS_FRAMES; ++index) {
		DynlexGraphicsFrame *frame = &graphics->frames[index];
		dynlex_graphics_destroy_retired_textures(graphics, frame);
		if (frame->upload != NULL)
			vkUnmapMemory(graphics->device, frame->upload_memory);
		if (frame->upload_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(graphics->device, frame->upload_buffer, NULL);
		if (frame->upload_memory != VK_NULL_HANDLE)
			vkFreeMemory(graphics->device, frame->upload_memory, NULL);
		if (frame->image_available != VK_NULL_HANDLE)
			vkDestroySemaphore(graphics->device, frame->image_available, NULL);
		if (frame->completed != VK_NULL_HANDLE)
			vkDestroyFence(graphics->device, frame->completed, NULL);
	}
	if (graphics->descriptor_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(graphics->device, graphics->descriptor_pool, NULL);
	if (graphics->command_pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(graphics->device, graphics->command_pool, NULL);
	if (graphics->device != VK_NULL_HANDLE)
		vkDestroyDevice(graphics->device, NULL);
	if (graphics->surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(graphics->instance, graphics->surface, NULL);
	if (graphics->instance != VK_NULL_HANDLE)
		vkDestroyInstance(graphics->instance, NULL);
}

bool dynlex_graphics_copy_buffer_to_image(
	DynlexGraphics *graphics, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height
) {
	VkCommandBufferAllocateInfo allocation = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = graphics->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer commands;
	VkResult result = vkAllocateCommandBuffers(graphics->device, &allocation, &commands);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating a Vulkan transfer command buffer", result);
		return false;
	}
	VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	result = vkBeginCommandBuffer(commands, &begin);
	VkImageMemoryBarrier before = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	if (result == VK_SUCCESS) {
		vkCmdPipelineBarrier(
			commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &before
		);
		VkBufferImageCopy region = {
			.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.imageExtent = {width, height, 1},
		};
		vkCmdCopyBufferToImage(commands, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		VkImageMemoryBarrier after = before;
		after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		after.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(
			commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &after
		);
		result = vkEndCommandBuffer(commands);
	}
	if (result == VK_SUCCESS) {
		VkSubmitInfo submit = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &commands,
		};
		result = vkQueueSubmit(graphics->graphics_queue, 1, &submit, VK_NULL_HANDLE);
	}
	if (result == VK_SUCCESS)
		result = vkQueueWaitIdle(graphics->graphics_queue);
	vkFreeCommandBuffers(graphics->device, graphics->command_pool, 1, &commands);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("uploading a Vulkan texture", result);
		return false;
	}
	return true;
}
