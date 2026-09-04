#include "graphicsRuntimeInternal.h"

#include "runtimeError.h"

#include <math.h>
#include <string.h>

int32_t dynlex_graphics_begin_frame(DynlexGraphics *graphics, double red, double green, double blue, double alpha) {
	if (graphics == NULL || graphics->frame_active) {
		dynlex_runtime_set_error("beginning a graphics frame requires an idle graphics context");
		return -1;
	}
	int width;
	int height;
	glfwGetFramebufferSize(graphics->window, &width, &height);
	if (width == 0 || height == 0)
		return 0;
	if (graphics->swapchain_dirty && !dynlex_graphics_recreate_swapchain(graphics))
		return -1;
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	VkResult result = vkWaitForFences(graphics->device, 1, &frame->completed, VK_TRUE, UINT64_MAX);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("waiting for a Vulkan frame", result);
		return -1;
	}
	dynlex_graphics_destroy_retired_textures(graphics, frame);
	result = vkAcquireNextImageKHR(
		graphics->device, graphics->swapchain, UINT64_MAX, frame->image_available, VK_NULL_HANDLE, &graphics->image_index
	);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		graphics->swapchain_dirty = true;
		return dynlex_graphics_recreate_swapchain(graphics) ? 0 : -1;
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		dynlex_graphics_set_vulkan_error("acquiring a Vulkan swapchain image", result);
		return -1;
	}
	graphics->swapchain_dirty |= result == VK_SUBOPTIMAL_KHR;
	result = vkResetCommandBuffer(frame->commands, 0);
	VkCommandBufferBeginInfo command_begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	if (result == VK_SUCCESS)
		result = vkBeginCommandBuffer(frame->commands, &command_begin);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("beginning a Vulkan frame command buffer", result);
		return -1;
	}
	VkClearValue clear_values[] = {
		{.color = {{(float)red, (float)green, (float)blue, (float)alpha}}},
		{.depthStencil = {1.0f, 0}},
	};
	VkRenderPassBeginInfo render_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = graphics->render_pass,
		.framebuffer = graphics->framebuffers[graphics->image_index],
		.renderArea = {.offset = {0, 0}, .extent = graphics->swapchain_extent},
		.clearValueCount = 2,
		.pClearValues = clear_values,
	};
	vkCmdBeginRenderPass(frame->commands, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)graphics->swapchain_extent.width,
		.height = (float)graphics->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	VkRect2D scissor = {.offset = {0, 0}, .extent = graphics->swapchain_extent};
	vkCmdSetViewport(frame->commands, 0, 1, &viewport);
	vkCmdSetScissor(frame->commands, 0, 1, &scissor);
	frame->upload_offset = 0;
	graphics->triangle_vertex_count = 0;
	graphics->triangles_active = false;
	graphics->frame_active = true;
	return 1;
}

int32_t dynlex_graphics_end_frame(DynlexGraphics *graphics) {
	if (graphics == NULL || !graphics->frame_active || graphics->triangles_active) {
		dynlex_runtime_set_error("ending a graphics frame requires an active frame with no unfinished triangles");
		return -1;
	}
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	vkCmdEndRenderPass(frame->commands);
	VkResult result = vkEndCommandBuffer(frame->commands);
	if (result != VK_SUCCESS) {
		graphics->frame_active = false;
		dynlex_graphics_set_vulkan_error("ending a Vulkan frame command buffer", result);
		return -1;
	}
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSemaphore presentation_ready = graphics->presentation_ready[graphics->image_index];
	VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frame->image_available,
		.pWaitDstStageMask = &wait_stage,
		.commandBufferCount = 1,
		.pCommandBuffers = &frame->commands,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &presentation_ready,
	};
	result = vkResetFences(graphics->device, 1, &frame->completed);
	if (result != VK_SUCCESS) {
		graphics->frame_active = false;
		dynlex_graphics_set_vulkan_error("resetting Vulkan frame completion", result);
		return -1;
	}
	result = vkQueueSubmit(graphics->graphics_queue, 1, &submit, frame->completed);
	if (result != VK_SUCCESS) {
		graphics->frame_active = false;
		dynlex_graphics_set_vulkan_error("submitting a Vulkan frame", result);
		return -1;
	}
	VkPresentInfoKHR present = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &presentation_ready,
		.swapchainCount = 1,
		.pSwapchains = &graphics->swapchain,
		.pImageIndices = &graphics->image_index,
	};
	result = vkQueuePresentKHR(graphics->present_queue, &present);
	graphics->frame_active = false;
	graphics->frame_index = (graphics->frame_index + 1u) % DYNLEX_GRAPHICS_FRAMES;
	glfwPollEvents();
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		graphics->swapchain_dirty = true;
		return 0;
	}
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("presenting a Vulkan frame", result);
		return -1;
	}
	return 1;
}

int32_t dynlex_graphics_begin_triangles(DynlexGraphics *graphics, DynlexGraphicsTexture *texture) {
	if (graphics == NULL || !graphics->frame_active || graphics->triangles_active) {
		dynlex_runtime_set_error("beginning triangles requires an active graphics frame and no unfinished triangles");
		return 0;
	}
	if (texture != NULL && texture->owner != graphics) {
		dynlex_runtime_set_error("a graphics texture belongs to a different graphics context");
		return 0;
	}
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	graphics->triangle_start = frame->upload_offset;
	graphics->triangle_vertex_count = 0;
	graphics->triangle_texture = texture;
	graphics->triangles_active = true;
	return 1;
}

void dynlex_graphics_set_vertex_color(DynlexGraphics *graphics, int32_t red, int32_t green, int32_t blue, int32_t alpha) {
	graphics->current_vertex.color[0] = (float)red / 255.0f;
	graphics->current_vertex.color[1] = (float)green / 255.0f;
	graphics->current_vertex.color[2] = (float)blue / 255.0f;
	graphics->current_vertex.color[3] = (float)alpha / 255.0f;
}

void dynlex_graphics_set_texture_coordinates(DynlexGraphics *graphics, double horizontal, double vertical) {
	graphics->current_vertex.texture_coordinates[0] = (float)horizontal;
	graphics->current_vertex.texture_coordinates[1] = (float)vertical;
}

int32_t dynlex_graphics_append_vertex_3d(DynlexGraphics *graphics, double horizontal, double vertical, double depth) {
	if (graphics == NULL || !graphics->triangles_active) {
		dynlex_runtime_set_error("appending a vertex requires an active triangle stream");
		return 0;
	}
	if (!(isfinite(horizontal) && isfinite(vertical) && isfinite(depth))) {
		dynlex_runtime_set_error("graphics vertex positions must be finite");
		return 0;
	}
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	if (frame->upload_offset > DYNLEX_GRAPHICS_UPLOAD_CAPACITY - sizeof(DynlexGraphicsVertex)) {
		dynlex_runtime_set_error("the active graphics frame exceeds its transient vertex capacity");
		return 0;
	}
	graphics->current_vertex.position[0] = (float)horizontal;
	graphics->current_vertex.position[1] = (float)vertical;
	graphics->current_vertex.position[2] = (float)depth;
	memcpy(frame->upload + frame->upload_offset, &graphics->current_vertex, sizeof(graphics->current_vertex));
	frame->upload_offset += sizeof(DynlexGraphicsVertex);
	++graphics->triangle_vertex_count;
	return 1;
}

int32_t dynlex_graphics_append_vertex_2d(DynlexGraphics *graphics, double horizontal, double vertical) {
	return dynlex_graphics_append_vertex_3d(graphics, horizontal, vertical, 0.0);
}

bool dynlex_graphics_record_triangles(DynlexGraphics *graphics) {
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	uint32_t builtin_index = (graphics->triangle_texture != NULL ? 2u : 0u) + (graphics->depth_testing ? 1u : 0u);
	DynlexGraphicsBuiltinPipeline *builtin = &graphics->builtins[builtin_index];
	float view_model[16];
	float transform[16];
	dynlex_graphics_matrix_multiply(view_model, graphics->camera, graphics->model);
	dynlex_graphics_matrix_multiply(transform, graphics->projection, view_model);
	vkCmdBindPipeline(frame->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, builtin->pipeline);
	VkDeviceSize offset = graphics->triangle_start;
	vkCmdBindVertexBuffers(frame->commands, 0, 1, &frame->upload_buffer, &offset);
	vkCmdPushConstants(frame->commands, builtin->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(transform), transform);
	if (graphics->triangle_texture != NULL) {
		vkCmdBindDescriptorSets(
			frame->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, builtin->layout, 0, 1, &graphics->triangle_texture->descriptor, 0,
			NULL
		);
	}
	vkCmdDraw(frame->commands, graphics->triangle_vertex_count, 1, 0, 0);
	return true;
}

int32_t dynlex_graphics_finish_triangles(DynlexGraphics *graphics) {
	if (graphics == NULL || !graphics->triangles_active) {
		dynlex_runtime_set_error("finishing triangles requires an active triangle stream");
		return 0;
	}
	if (graphics->triangle_vertex_count == 0 || graphics->triangle_vertex_count % 3 != 0) {
		dynlex_runtime_set_error("a triangle stream requires a positive multiple of three vertices");
		return 0;
	}
	bool recorded = dynlex_graphics_record_triangles(graphics);
	graphics->triangles_active = false;
	graphics->triangle_texture = NULL;
	return recorded;
}

static int32_t finish_rectangle(DynlexGraphics *graphics, double left, double top, double right, double bottom) {
	return dynlex_graphics_append_vertex_2d(graphics, left, top) && dynlex_graphics_append_vertex_2d(graphics, right, top) &&
		   dynlex_graphics_append_vertex_2d(graphics, right, bottom) && dynlex_graphics_append_vertex_2d(graphics, left, top) &&
		   dynlex_graphics_append_vertex_2d(graphics, right, bottom) &&
		   dynlex_graphics_append_vertex_2d(graphics, left, bottom) && dynlex_graphics_finish_triangles(graphics);
}

int32_t dynlex_graphics_draw_rectangle(
	DynlexGraphics *graphics, double horizontal, double vertical, double width, double height, int32_t red, int32_t green,
	int32_t blue, int32_t alpha
) {
	if (!dynlex_graphics_begin_triangles(graphics, NULL))
		return 0;
	dynlex_graphics_set_vertex_color(graphics, red, green, blue, alpha);
	return finish_rectangle(graphics, horizontal, vertical, horizontal + width, vertical + height);
}

int32_t dynlex_graphics_draw_textured_rectangle(
	DynlexGraphics *graphics, DynlexGraphicsTexture *texture, double left, double top, double right, double bottom, int32_t red,
	int32_t green, int32_t blue, int32_t alpha
) {
	if (texture == NULL || !dynlex_graphics_begin_triangles(graphics, texture)) {
		if (texture == NULL)
			dynlex_runtime_set_error("drawing a textured rectangle requires a texture");
		return 0;
	}
	dynlex_graphics_set_vertex_color(graphics, red, green, blue, alpha);
	dynlex_graphics_set_texture_coordinates(graphics, 0.0, 0.0);
	if (!dynlex_graphics_append_vertex_2d(graphics, left, top))
		return 0;
	dynlex_graphics_set_texture_coordinates(graphics, 1.0, 0.0);
	if (!dynlex_graphics_append_vertex_2d(graphics, right, top))
		return 0;
	dynlex_graphics_set_texture_coordinates(graphics, 1.0, 1.0);
	if (!dynlex_graphics_append_vertex_2d(graphics, right, bottom))
		return 0;
	dynlex_graphics_set_texture_coordinates(graphics, 0.0, 0.0);
	if (!dynlex_graphics_append_vertex_2d(graphics, left, top))
		return 0;
	dynlex_graphics_set_texture_coordinates(graphics, 1.0, 1.0);
	if (!dynlex_graphics_append_vertex_2d(graphics, right, bottom))
		return 0;
	dynlex_graphics_set_texture_coordinates(graphics, 0.0, 1.0);
	return dynlex_graphics_append_vertex_2d(graphics, left, bottom) && dynlex_graphics_finish_triangles(graphics);
}

int32_t dynlex_graphics_set_uniform_float(DynlexGraphics *graphics, int32_t binding, double value) {
	if (graphics == NULL || binding < 0 || binding >= (int32_t)DYNLEX_GRAPHICS_MAX_UNIFORMS || !isfinite(value)) {
		dynlex_runtime_set_error("a scalar shader uniform requires a binding from 0 through 31 and a finite value");
		return 0;
	}
	graphics->uniform_values[binding] = (float)value;
	return 1;
}

int32_t dynlex_graphics_draw_fullscreen(DynlexGraphics *graphics) {
	if (graphics == NULL || !graphics->frame_active || graphics->active_pipeline == NULL || graphics->triangles_active) {
		dynlex_runtime_set_error("drawing a fullscreen shader requires an active frame, pipeline, and no unfinished triangles");
		return 0;
	}
	DynlexGraphicsFrame *frame = &graphics->frames[graphics->frame_index];
	static const float positions[16] = {
		-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
	};
	if (frame->upload_offset > DYNLEX_GRAPHICS_UPLOAD_CAPACITY - sizeof(positions)) {
		dynlex_runtime_set_error("the active graphics frame exceeds its transient vertex capacity");
		return 0;
	}
	VkDeviceSize offset = frame->upload_offset;
	memcpy(frame->upload + offset, positions, sizeof(positions));
	frame->upload_offset += sizeof(positions);
	DynlexGraphicsPipeline *pipeline = graphics->active_pipeline;
	for (uint32_t index = 0; index < pipeline->binding_count; ++index) {
		float value = graphics->uniform_values[pipeline->bindings[index]];
		memcpy(pipeline->uniform_maps[graphics->frame_index] + pipeline->uniform_stride * index, &value, sizeof(value));
	}
	vkCmdBindPipeline(frame->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
	vkCmdBindVertexBuffers(frame->commands, 0, 1, &frame->upload_buffer, &offset);
	if (pipeline->binding_count > 0) {
		vkCmdBindDescriptorSets(
			frame->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 1,
			&pipeline->descriptor_sets[graphics->frame_index], 0, NULL
		);
	}
	vkCmdDraw(frame->commands, 4, 1, 0, 0);
	return 1;
}
