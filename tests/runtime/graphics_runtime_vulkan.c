#include "graphicsRuntime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
	fprintf(stderr, "%s\n", message);
	return 1;
}

static int fail_with_graphics_error(const char *message) {
	char detail[512];
	if (dynlex_graphics_error_message(detail, sizeof(detail)) == 0)
		return fail(message);
	fprintf(stderr, "%s: %s\n", message, detail);
	return 1;
}

int main(int argument_count, char **arguments) {
	if (argument_count != 1 && argument_count != 3)
		return fail("usage: graphics_runtime_vulkan [vertex.spv fragment.spv]");
	if (!dynlex_graphics_texture_destroy(NULL) || !dynlex_graphics_pipeline_destroy(NULL))
		return fail("releasing an empty graphics resource was not idempotent");
	if (dynlex_graphics_create(0, 64, 0, 0, "invalid") != NULL)
		return fail("graphics creation accepted a zero width");
	if (dynlex_graphics_create(64, 64, 0, 1, "invalid") != NULL)
		return fail("fullscreen graphics creation accepted windowed dimensions");
	if (dynlex_graphics_sleep_milliseconds(-1))
		return fail("graphics sleep accepted a negative duration");
	char error_message[128];
	const char expected_error[] = "a graphics sleep duration cannot be negative";
	if (dynlex_graphics_error_message(NULL, 0) != strlen(expected_error) ||
		dynlex_graphics_error_message(error_message, sizeof(error_message)) != strlen(expected_error) ||
		strcmp(error_message, expected_error) != 0)
		return fail("a rejected graphics operation did not expose its runtime error");

	DynlexGraphics *graphics = dynlex_graphics_create(64, 64, 0, 0, "DynLex Vulkan runtime test");
	if (graphics == NULL)
		return fail_with_graphics_error("graphics creation failed");
	if (dynlex_graphics_is_visible(graphics))
		return fail("a hidden Vulkan window was reported visible");
	if (dynlex_graphics_api_version(graphics) < 0x00403000u)
		return fail("the selected Vulkan device does not provide Vulkan 1.3");
	char device_name[256];
	size_t device_name_length = dynlex_graphics_device_name(graphics, device_name, sizeof(device_name));
	if (device_name_length == 0 || device_name[0] == '\0')
		return fail("the Vulkan device name is empty");
	char terminator = 'x';
	if (dynlex_graphics_device_name(graphics, &terminator, 1) != device_name_length || terminator != '\0')
		return fail("the Vulkan device name query did not terminate a short buffer");
	double sleep_started = dynlex_graphics_time_since_start(graphics);
	if (!dynlex_graphics_sleep_milliseconds(20))
		return fail("a valid graphics sleep duration was rejected");
	if (dynlex_graphics_time_since_start(graphics) - sleep_started < 0.015)
		return fail("the graphics sleep returned before its duration elapsed");
	if (!dynlex_graphics_set_orthographic_projection(graphics, 0.0, 64.0, 0.0, 64.0, -1.0, 1.0))
		return fail("the orthographic projection was rejected");

	const uint8_t red_pixel[] = {255};
	DynlexGraphicsTexture *texture = dynlex_graphics_texture_create_red8(graphics, 1, 1, red_pixel);
	if (texture == NULL)
		return fail("creating a red Vulkan texture failed");
	const uint8_t rgb_pixel[] = {255, 127, 0};
	DynlexGraphicsTexture *rgb_texture = dynlex_graphics_texture_create_rgb8(graphics, 1, 1, rgb_pixel);
	if (rgb_texture == NULL)
		return fail("creating an RGB Vulkan texture failed");
	int32_t frame = dynlex_graphics_begin_frame(graphics, 0.0, 0.0, 0.0, 1.0);
	if (frame != 1)
		return fail("beginning a hidden Vulkan frame failed");
	if (!dynlex_graphics_begin_triangles(graphics, texture))
		return fail("beginning a Vulkan texture release test failed");
	if (dynlex_graphics_texture_destroy(texture))
		return fail("releasing a texture used by an active triangle stream succeeded");
	for (int32_t index = 0; index < 3; ++index) {
		if (!dynlex_graphics_append_vertex_2d(graphics, (double)index, (double)index))
			return fail("recording a Vulkan texture release test vertex failed");
	}
	if (!dynlex_graphics_finish_triangles(graphics))
		return fail("ending a Vulkan texture release test failed");
	if (!dynlex_graphics_draw_rectangle(graphics, 1.0, 1.0, 10.0, 10.0, 255, 0, 0, 255))
		return fail("recording a Vulkan rectangle failed");
	if (!dynlex_graphics_draw_textured_rectangle(graphics, texture, 12.0, 1.0, 22.0, 11.0, 0, 255, 0, 255))
		return fail("recording a Vulkan textured rectangle failed");
	const uint8_t resized_pixels[] = {0, 255};
	if (!dynlex_graphics_texture_update(texture, 2, 1, resized_pixels))
		return fail("resizing a Vulkan texture during a frame failed");
	if (!dynlex_graphics_draw_textured_rectangle(graphics, texture, 24.0, 1.0, 34.0, 11.0, 0, 0, 255, 255))
		return fail("recording the resized Vulkan texture failed");
	if (!dynlex_graphics_draw_textured_rectangle(graphics, rgb_texture, 36.0, 1.0, 46.0, 11.0, 255, 255, 255, 255))
		return fail("recording an RGB Vulkan texture failed");
	const uint8_t resized_rgb_pixels[] = {0, 127, 255, 255, 255, 255};
	if (!dynlex_graphics_texture_update(rgb_texture, 2, 1, resized_rgb_pixels))
		return fail("resizing an RGB Vulkan texture during a frame failed");
	if (dynlex_graphics_end_frame(graphics) < 0)
		return fail("presenting a hidden Vulkan frame failed");
	for (int32_t index = 0; index < 3; ++index) {
		if (dynlex_graphics_begin_frame(graphics, 0.0, 0.0, 0.0, 1.0) != 1 ||
			!dynlex_graphics_draw_rectangle(graphics, 1.0, 1.0, 2.0, 2.0, 255, 255, 255, 255) ||
			dynlex_graphics_end_frame(graphics) < 0)
			return fail("reusing Vulkan frame resources failed");
	}
	if (argument_count == 3) {
		DynlexGraphicsPipeline *pipeline = dynlex_graphics_pipeline_create(graphics, arguments[1], arguments[2]);
		if (pipeline == NULL)
			return fail("creating a Vulkan pipeline from DynLex SPIR-V failed");
		if (dynlex_graphics_begin_frame(graphics, 0.0, 0.0, 0.0, 1.0) != 1 ||
			!dynlex_graphics_use_pipeline(graphics, pipeline) || !dynlex_graphics_set_uniform_float(graphics, 0, 1.0) ||
			!dynlex_graphics_draw_fullscreen(graphics))
			return fail("drawing with a DynLex SPIR-V pipeline failed");
		if (dynlex_graphics_pipeline_destroy(pipeline))
			return fail("releasing a pipeline during an active frame succeeded");
		if (dynlex_graphics_end_frame(graphics) < 0)
			return fail("presenting a DynLex SPIR-V pipeline frame failed");
		if (!dynlex_graphics_pipeline_destroy(pipeline))
			return fail("releasing an idle Vulkan pipeline failed");
	}

	dynlex_graphics_request_close(graphics);
	if (!dynlex_graphics_should_close(graphics))
		return fail("requesting Vulkan window closure did not update window state");
	if (!dynlex_graphics_texture_destroy(texture) || !dynlex_graphics_texture_destroy(rgb_texture))
		return fail("releasing an idle Vulkan texture failed");
	dynlex_graphics_destroy(graphics);

	DynlexGraphics *fullscreen = dynlex_graphics_create(0, 0, 0, 1, "DynLex Vulkan fullscreen test");
	if (fullscreen == NULL)
		return fail("fullscreen graphics creation rejected zero placeholder dimensions");
	int32_t fullscreen_width = 0;
	int32_t fullscreen_height = 0;
	dynlex_graphics_framebuffer_size(fullscreen, &fullscreen_width, &fullscreen_height);
	if (fullscreen_width <= 0 || fullscreen_height <= 0)
		return fail("fullscreen graphics creation did not use the monitor dimensions");
	dynlex_graphics_destroy(fullscreen);
	return 0;
}
