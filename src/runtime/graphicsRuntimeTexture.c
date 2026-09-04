#include "graphicsRuntimeInternal.h"

#include "runtimeError.h"

#include <stdlib.h>
#include <string.h>

static void destroy_texture_handles(
	DynlexGraphics *graphics, VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler,
	VkDescriptorSet descriptor
) {
	if (descriptor != VK_NULL_HANDLE)
		vkFreeDescriptorSets(graphics->device, graphics->descriptor_pool, 1, &descriptor);
	if (sampler != VK_NULL_HANDLE)
		vkDestroySampler(graphics->device, sampler, NULL);
	if (view != VK_NULL_HANDLE)
		vkDestroyImageView(graphics->device, view, NULL);
	if (image != VK_NULL_HANDLE)
		vkDestroyImage(graphics->device, image, NULL);
	if (memory != VK_NULL_HANDLE)
		vkFreeMemory(graphics->device, memory, NULL);
}

void dynlex_graphics_destroy_retired_textures(DynlexGraphics *graphics, DynlexGraphicsFrame *frame) {
	while (frame->retired_textures != NULL) {
		DynlexGraphicsRetiredTexture *retired = frame->retired_textures;
		frame->retired_textures = retired->next;
		destroy_texture_handles(
			graphics, retired->image, retired->memory, retired->view, retired->sampler, retired->descriptor
		);
		free(retired);
	}
}

static bool retire_texture_resources(DynlexGraphicsTexture *texture, const char *wait_operation) {
	DynlexGraphics *graphics = texture->owner;
	if (texture->image == VK_NULL_HANDLE)
		return true;
	if (!graphics->frame_active) {
		VkResult result = vkDeviceWaitIdle(graphics->device);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error(wait_operation, result);
			return false;
		}
		destroy_texture_handles(
			graphics, texture->image, texture->memory, texture->view, texture->sampler, texture->descriptor
		);
	} else {
		DynlexGraphicsRetiredTexture *retired = malloc(sizeof(*retired));
		if (retired == NULL) {
			dynlex_runtime_set_error("failed to preserve a texture used by the active graphics frame");
			return false;
		}
		*retired = (DynlexGraphicsRetiredTexture){
			.next = graphics->frames[graphics->frame_index].retired_textures,
			.image = texture->image,
			.memory = texture->memory,
			.view = texture->view,
			.sampler = texture->sampler,
			.descriptor = texture->descriptor,
		};
		graphics->frames[graphics->frame_index].retired_textures = retired;
	}
	texture->image = VK_NULL_HANDLE;
	texture->memory = VK_NULL_HANDLE;
	texture->view = VK_NULL_HANDLE;
	texture->sampler = VK_NULL_HANDLE;
	texture->descriptor = VK_NULL_HANDLE;
	return true;
}

static bool validate_texture_size(int32_t width, int32_t height, uint32_t channels, VkDeviceSize *size) {
	if (width <= 0 || height <= 0) {
		dynlex_runtime_set_error("a graphics texture requires positive dimensions");
		return false;
	}
	uint64_t texels = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height;
	if (texels > SIZE_MAX / channels) {
		dynlex_runtime_set_error("graphics texture dimensions exceed the addressable size");
		return false;
	}
	*size = texels * channels;
	return true;
}

static bool upload_texture(DynlexGraphicsTexture *texture, int32_t width, int32_t height, const void *pixels) {
	DynlexGraphics *graphics = texture->owner;
	VkDeviceSize input_byte_size;
	uint32_t upload_channels = texture->channels == 3 ? 4 : texture->channels;
	VkDeviceSize byte_size;
	if (pixels == NULL || !validate_texture_size(width, height, texture->channels, &input_byte_size) ||
		!validate_texture_size(width, height, upload_channels, &byte_size)) {
		if (pixels == NULL)
			dynlex_runtime_set_error("graphics texture pixels cannot be null");
		return false;
	}
	VkFormatProperties format_properties;
	vkGetPhysicalDeviceFormatProperties(graphics->physical_device, texture->format, &format_properties);
	VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
											 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
											 VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if ((format_properties.optimalTilingFeatures & required_features) != required_features) {
		dynlex_runtime_set_error("the Vulkan device cannot upload and linearly sample the requested texture format");
		return false;
	}
	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory staging_memory = VK_NULL_HANDLE;
	if (!dynlex_graphics_create_buffer(
			graphics, byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, &staging_memory
		))
		return false;
	void *mapped = NULL;
	VkResult result = vkMapMemory(graphics->device, staging_memory, 0, byte_size, 0, &mapped);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("mapping Vulkan texture upload memory", result);
		vkDestroyBuffer(graphics->device, staging, NULL);
		vkFreeMemory(graphics->device, staging_memory, NULL);
		return false;
	}
	if (texture->channels == 3) {
		const uint8_t *source = pixels;
		uint8_t *destination = mapped;
		for (VkDeviceSize source_offset = 0, destination_offset = 0; source_offset < input_byte_size;
			 source_offset += 3, destination_offset += 4) {
			destination[destination_offset] = source[source_offset];
			destination[destination_offset + 1] = source[source_offset + 1];
			destination[destination_offset + 2] = source[source_offset + 2];
			destination[destination_offset + 3] = UINT8_MAX;
		}
	} else {
		memcpy(mapped, pixels, (size_t)byte_size);
	}
	vkUnmapMemory(graphics->device, staging_memory);
	if (!dynlex_graphics_create_image(
			graphics, (uint32_t)width, (uint32_t)height, texture->format,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &texture->image, &texture->memory
		) ||
		!dynlex_graphics_copy_buffer_to_image(graphics, staging, texture->image, (uint32_t)width, (uint32_t)height)) {
		vkDestroyBuffer(graphics->device, staging, NULL);
		vkFreeMemory(graphics->device, staging_memory, NULL);
		return false;
	}
	vkDestroyBuffer(graphics->device, staging, NULL);
	vkFreeMemory(graphics->device, staging_memory, NULL);
	VkComponentMapping components = {
		.r = VK_COMPONENT_SWIZZLE_IDENTITY,
		.g = VK_COMPONENT_SWIZZLE_IDENTITY,
		.b = VK_COMPONENT_SWIZZLE_IDENTITY,
		.a = VK_COMPONENT_SWIZZLE_IDENTITY,
	};
	if (texture->channels == 1) {
		components.r = VK_COMPONENT_SWIZZLE_ONE;
		components.g = VK_COMPONENT_SWIZZLE_ONE;
		components.b = VK_COMPONENT_SWIZZLE_ONE;
		components.a = VK_COMPONENT_SWIZZLE_R;
	}
	VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = texture->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = texture->format,
		.components = components,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	result = vkCreateImageView(graphics->device, &view_info, NULL, &texture->view);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan texture image view", result);
		return false;
	}
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = texture->channels == 1 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = texture->channels == 1 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
	};
	result = vkCreateSampler(graphics->device, &sampler_info, NULL, &texture->sampler);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan texture sampler", result);
		return false;
	}
	VkDescriptorSetAllocateInfo descriptor_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = graphics->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &graphics->builtins[2].descriptor_layout,
	};
	result = vkAllocateDescriptorSets(graphics->device, &descriptor_info, &texture->descriptor);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating a Vulkan texture descriptor", result);
		return false;
	}
	VkDescriptorImageInfo image_info = {
		.sampler = texture->sampler,
		.imageView = texture->view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = texture->descriptor,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};
	vkUpdateDescriptorSets(graphics->device, 1, &write, 0, NULL);
	texture->width = (uint32_t)width;
	texture->height = (uint32_t)height;
	return true;
}

static DynlexGraphicsTexture *create_texture(
	DynlexGraphics *graphics, int32_t width, int32_t height, uint32_t channels, VkFormat format, const void *pixels
) {
	if (graphics == NULL) {
		dynlex_runtime_set_error("creating a graphics texture requires a graphics context");
		return NULL;
	}
	DynlexGraphicsTexture *texture = calloc(1, sizeof(*texture));
	if (texture == NULL) {
		dynlex_runtime_set_error("failed to allocate a graphics texture");
		return NULL;
	}
	texture->owner = graphics;
	texture->channels = channels;
	texture->format = format;
	if (!upload_texture(texture, width, height, pixels)) {
		dynlex_graphics_destroy_texture_resources(texture);
		free(texture);
		return NULL;
	}
	texture->next = graphics->textures;
	graphics->textures = texture;
	return texture;
}

DynlexGraphicsTexture *
dynlex_graphics_texture_create_rgba8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels) {
	return create_texture(graphics, width, height, 4, VK_FORMAT_R8G8B8A8_UNORM, pixels);
}

DynlexGraphicsTexture *
dynlex_graphics_texture_create_rgb8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels) {
	return create_texture(graphics, width, height, 3, VK_FORMAT_R8G8B8A8_UNORM, pixels);
}

DynlexGraphicsTexture *
dynlex_graphics_texture_create_red8(DynlexGraphics *graphics, int32_t width, int32_t height, const void *pixels) {
	return create_texture(graphics, width, height, 1, VK_FORMAT_R8_UNORM, pixels);
}

int32_t dynlex_graphics_texture_update(DynlexGraphicsTexture *texture, int32_t width, int32_t height, const void *pixels) {
	if (texture == NULL) {
		dynlex_runtime_set_error("updating a graphics texture requires a texture");
		return 0;
	}
	DynlexGraphicsTexture replacement = {
		.owner = texture->owner,
		.format = texture->format,
		.channels = texture->channels,
	};
	if (!upload_texture(&replacement, width, height, pixels)) {
		dynlex_graphics_destroy_texture_resources(&replacement);
		return 0;
	}
	if (!retire_texture_resources(texture, "waiting to replace a Vulkan texture")) {
		dynlex_graphics_destroy_texture_resources(&replacement);
		return 0;
	}
	texture->image = replacement.image;
	texture->memory = replacement.memory;
	texture->view = replacement.view;
	texture->sampler = replacement.sampler;
	texture->descriptor = replacement.descriptor;
	texture->width = replacement.width;
	texture->height = replacement.height;
	return 1;
}

void dynlex_graphics_destroy_texture_resources(DynlexGraphicsTexture *texture) {
	if (texture->image == VK_NULL_HANDLE)
		return;
	destroy_texture_handles(
		texture->owner, texture->image, texture->memory, texture->view, texture->sampler, texture->descriptor
	);
	texture->image = VK_NULL_HANDLE;
}

int32_t dynlex_graphics_texture_destroy(DynlexGraphicsTexture *texture) {
	if (texture == NULL)
		return 1;
	DynlexGraphics *graphics = texture->owner;
	if (graphics->triangles_active && graphics->triangle_texture == texture) {
		dynlex_runtime_set_error("a graphics texture cannot be released while its triangle stream is active");
		return 0;
	}
	if (!retire_texture_resources(texture, "waiting to release a Vulkan texture"))
		return 0;
	DynlexGraphicsTexture **cursor = &graphics->textures;
	while (*cursor != texture)
		cursor = &(*cursor)->next;
	*cursor = texture->next;
	free(texture);
	return 1;
}
