#ifndef DYNLEX_GRAPHICS_RUNTIME_INTERNAL_H
#define DYNLEX_GRAPHICS_RUNTIME_INTERNAL_H

#include "graphicsRuntime.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdbool.h>
#include <stdint.h>

#define DYNLEX_GRAPHICS_FRAMES 2u
#define DYNLEX_GRAPHICS_MAX_UNIFORMS 32u
#define DYNLEX_GRAPHICS_MODEL_STACK 64u
#define DYNLEX_GRAPHICS_UPLOAD_CAPACITY (32u * 1024u * 1024u)

typedef struct DynlexGraphicsVertex {
	float position[3];
	float texture_coordinates[2];
	float color[4];
} DynlexGraphicsVertex;

typedef struct DynlexGraphicsFrame {
	VkCommandBuffer commands;
	VkSemaphore image_available;
	VkFence completed;
	VkBuffer upload_buffer;
	VkDeviceMemory upload_memory;
	unsigned char *upload;
	VkDeviceSize upload_offset;
	struct DynlexGraphicsRetiredTexture *retired_textures;
} DynlexGraphicsFrame;

typedef struct DynlexGraphicsRetiredTexture {
	struct DynlexGraphicsRetiredTexture *next;
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkSampler sampler;
	VkDescriptorSet descriptor;
} DynlexGraphicsRetiredTexture;

struct DynlexGraphicsPipeline {
	DynlexGraphics *owner;
	struct DynlexGraphicsPipeline *next;
	VkPipeline pipeline;
	VkPipelineLayout layout;
	VkDescriptorSetLayout descriptor_layout;
	VkDescriptorSet descriptor_sets[DYNLEX_GRAPHICS_FRAMES];
	bool descriptor_sets_allocated;
	VkBuffer uniform_buffers[DYNLEX_GRAPHICS_FRAMES];
	VkDeviceMemory uniform_memory[DYNLEX_GRAPHICS_FRAMES];
	unsigned char *uniform_maps[DYNLEX_GRAPHICS_FRAMES];
	uint32_t bindings[DYNLEX_GRAPHICS_MAX_UNIFORMS];
	uint32_t binding_count;
	VkDeviceSize uniform_stride;
	uint32_t *vertex_code;
	size_t vertex_word_count;
	uint32_t *fragment_code;
	size_t fragment_word_count;
};

struct DynlexGraphicsTexture {
	DynlexGraphics *owner;
	struct DynlexGraphicsTexture *next;
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkSampler sampler;
	VkDescriptorSet descriptor;
	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t channels;
};

typedef struct DynlexGraphicsBuiltinPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
	VkDescriptorSetLayout descriptor_layout;
	bool textured;
	bool depth;
} DynlexGraphicsBuiltinPipeline;

struct DynlexGraphics {
	GLFWwindow *window;
	VkInstance instance;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical_device;
	VkPhysicalDeviceProperties physical_properties;
	VkDevice device;
	uint32_t graphics_queue_family;
	uint32_t present_queue_family;
	VkQueue graphics_queue;
	VkQueue present_queue;
	VkSwapchainKHR swapchain;
	VkFormat swapchain_format;
	VkColorSpaceKHR swapchain_color_space;
	VkExtent2D swapchain_extent;
	VkImage *swapchain_images;
	VkImageView *swapchain_views;
	VkFramebuffer *framebuffers;
	VkSemaphore *presentation_ready;
	uint32_t swapchain_image_count;
	VkFormat depth_format;
	VkImage *depth_images;
	VkDeviceMemory *depth_memories;
	VkImageView *depth_views;
	VkRenderPass render_pass;
	VkCommandPool command_pool;
	VkDescriptorPool descriptor_pool;
	DynlexGraphicsFrame frames[DYNLEX_GRAPHICS_FRAMES];
	uint32_t frame_index;
	uint32_t image_index;
	bool frame_active;
	bool swapchain_dirty;
	bool portability_enabled;
	bool depth_testing;
	float projection[16];
	float camera[16];
	float model[16];
	float model_stack[DYNLEX_GRAPHICS_MODEL_STACK][16];
	uint32_t model_stack_size;
	DynlexGraphicsVertex current_vertex;
	VkDeviceSize triangle_start;
	uint32_t triangle_vertex_count;
	DynlexGraphicsTexture *triangle_texture;
	bool triangles_active;
	float uniform_values[DYNLEX_GRAPHICS_MAX_UNIFORMS];
	DynlexGraphicsPipeline *active_pipeline;
	DynlexGraphicsPipeline *pipelines;
	DynlexGraphicsTexture *textures;
	DynlexGraphicsBuiltinPipeline builtins[4];
	DynlexGraphicsScrollCallback scroll_callback;
	void *application_state;
};

void dynlex_graphics_set_vulkan_error(const char *operation, VkResult result);
bool dynlex_graphics_create_vulkan(DynlexGraphics *graphics);
void dynlex_graphics_destroy_vulkan(DynlexGraphics *graphics);
bool dynlex_graphics_recreate_swapchain(DynlexGraphics *graphics);
bool dynlex_graphics_create_buffer(
	DynlexGraphics *graphics, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer,
	VkDeviceMemory *memory
);
bool dynlex_graphics_copy_buffer_to_image(
	DynlexGraphics *graphics, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height
);
bool dynlex_graphics_create_image(
	DynlexGraphics *graphics, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage *image,
	VkDeviceMemory *memory
);
bool dynlex_graphics_find_memory_type(
	DynlexGraphics *graphics, uint32_t type_bits, VkMemoryPropertyFlags properties, uint32_t *type_index
);
bool dynlex_graphics_record_triangles(DynlexGraphics *graphics);
bool dynlex_graphics_create_builtin_pipelines(DynlexGraphics *graphics);
void dynlex_graphics_destroy_builtin_pipelines(DynlexGraphics *graphics);
bool dynlex_graphics_rebuild_custom_pipelines(DynlexGraphics *graphics);
void dynlex_graphics_destroy_pipeline_resources(DynlexGraphicsPipeline *pipeline);
void dynlex_graphics_destroy_texture_resources(DynlexGraphicsTexture *texture);
void dynlex_graphics_destroy_retired_textures(DynlexGraphics *graphics, DynlexGraphicsFrame *frame);

void dynlex_graphics_matrix_identity(float matrix[16]);
void dynlex_graphics_matrix_multiply(float result[16], const float left[16], const float right[16]);

#endif
