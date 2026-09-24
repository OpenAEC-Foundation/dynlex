#include "graphicsRuntimeInternal.h"

#include "graphicsBuiltinShaders.h"
#include "runtimeError.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool create_shader_module(DynlexGraphics *graphics, const uint32_t *code, size_t word_count, VkShaderModule *module) {
	VkShaderModuleCreateInfo module_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = word_count * sizeof(uint32_t),
		.pCode = code,
	};
	VkResult result = vkCreateShaderModule(graphics->device, &module_info, NULL, module);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan SPIR-V shader module", result);
		return false;
	}
	return true;
}

static bool create_pipeline(
	DynlexGraphics *graphics, const uint32_t *vertex_code, size_t vertex_words, const uint32_t *fragment_code,
	size_t fragment_words, VkPipelineLayout layout, VkPrimitiveTopology topology, bool builtin, bool depth, VkPipeline *pipeline
) {
	VkShaderModule vertex_module = VK_NULL_HANDLE;
	VkShaderModule fragment_module = VK_NULL_HANDLE;
	if (!create_shader_module(graphics, vertex_code, vertex_words, &vertex_module) ||
		!create_shader_module(graphics, fragment_code, fragment_words, &fragment_module)) {
		if (vertex_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(graphics->device, vertex_module, NULL);
		return false;
	}
	VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_module,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_module,
			.pName = "main",
		},
	};
	VkVertexInputBindingDescription binding = {
		.binding = 0,
		.stride = builtin ? sizeof(DynlexGraphicsVertex) : sizeof(float) * 4,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	VkVertexInputAttributeDescription builtin_attributes[] = {
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(DynlexGraphicsVertex, position)},
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(DynlexGraphicsVertex, color)},
		{.location = 2,
		 .binding = 0,
		 .format = VK_FORMAT_R32G32_SFLOAT,
		 .offset = offsetof(DynlexGraphicsVertex, texture_coordinates)},
	};
	VkVertexInputAttributeDescription custom_attribute = {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.offset = 0,
	};
	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = builtin ? 3u : 1u,
		.pVertexAttributeDescriptions = builtin ? builtin_attributes : &custom_attribute,
	};
	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = topology,
		.primitiveRestartEnable = VK_FALSE,
	};
	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	VkPipelineRasterizationStateCreateInfo rasterizer = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo multisampling = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkPipelineDepthStencilStateCreateInfo depth_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = depth,
		.depthWriteEnable = depth,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	VkPipelineColorBlendStateCreateInfo blending = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};
	VkDynamicState dynamic_values[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_values,
	};
	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depth_state,
		.pColorBlendState = &blending,
		.pDynamicState = &dynamic_state,
		.layout = layout,
		.renderPass = graphics->render_pass,
		.subpass = 0,
	};
	VkResult result = vkCreateGraphicsPipelines(graphics->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, pipeline);
	vkDestroyShaderModule(graphics->device, fragment_module, NULL);
	vkDestroyShaderModule(graphics->device, vertex_module, NULL);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan graphics pipeline", result);
		return false;
	}
	return true;
}

static bool create_builtin_layout(DynlexGraphics *graphics, DynlexGraphicsBuiltinPipeline *builtin) {
	if (builtin->textured) {
		VkDescriptorSetLayoutBinding texture_binding = {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};
		VkDescriptorSetLayoutCreateInfo descriptor_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1,
			.pBindings = &texture_binding,
		};
		VkResult result = vkCreateDescriptorSetLayout(graphics->device, &descriptor_info, NULL, &builtin->descriptor_layout);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating the Vulkan texture descriptor layout", result);
			return false;
		}
	}
	VkPushConstantRange push_constants = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(float) * 16,
	};
	VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = builtin->textured ? 1u : 0u,
		.pSetLayouts = builtin->textured ? &builtin->descriptor_layout : NULL,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constants,
	};
	VkResult result = vkCreatePipelineLayout(graphics->device, &layout_info, NULL, &builtin->layout);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a built-in Vulkan pipeline layout", result);
		return false;
	}
	return true;
}

bool dynlex_graphics_create_builtin_pipelines(DynlexGraphics *graphics) {
	for (uint32_t index = 0; index < 4; ++index) {
		DynlexGraphicsBuiltinPipeline *builtin = &graphics->builtins[index];
		builtin->textured = (index & 2u) != 0;
		builtin->depth = (index & 1u) != 0;
		if (!create_builtin_layout(graphics, builtin))
			return false;
		const unsigned char *vertex = builtin->textured ? dynlex_graphics_texture_vertex : dynlex_graphics_color_vertex;
		const unsigned char *fragment = builtin->textured ? dynlex_graphics_texture_fragment : dynlex_graphics_color_fragment;
		size_t vertex_length = builtin->textured ? dynlex_graphics_texture_vertex_length : dynlex_graphics_color_vertex_length;
		size_t fragment_length =
			builtin->textured ? dynlex_graphics_texture_fragment_length : dynlex_graphics_color_fragment_length;
		if (!create_pipeline(
				graphics, (const uint32_t *)(const void *)vertex, vertex_length / sizeof(uint32_t),
				(const uint32_t *)(const void *)fragment, fragment_length / sizeof(uint32_t), builtin->layout,
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, builtin->depth, &builtin->pipeline
			))
			return false;
	}
	return true;
}

void dynlex_graphics_destroy_builtin_pipelines(DynlexGraphics *graphics) {
	for (uint32_t index = 0; index < 4; ++index) {
		DynlexGraphicsBuiltinPipeline *builtin = &graphics->builtins[index];
		if (builtin->pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(graphics->device, builtin->pipeline, NULL);
		if (builtin->layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(graphics->device, builtin->layout, NULL);
		if (builtin->descriptor_layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(graphics->device, builtin->descriptor_layout, NULL);
		builtin->pipeline = VK_NULL_HANDLE;
		builtin->layout = VK_NULL_HANDLE;
		builtin->descriptor_layout = VK_NULL_HANDLE;
	}
}

static uint32_t *read_spirv(const char *path, size_t *word_count) {
	FILE *stream = fopen(path, "rb");
	if (stream == NULL) {
		char message[512];
		snprintf(message, sizeof(message), "failed to open SPIR-V shader: %s", path);
		dynlex_runtime_set_error(message);
		return NULL;
	}
	if (fseek(stream, 0, SEEK_END) != 0) {
		fclose(stream);
		dynlex_runtime_set_error("failed to measure a SPIR-V shader file");
		return NULL;
	}
	long length = ftell(stream);
	if (length < 20 || length % 4 != 0 || fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		dynlex_runtime_set_error("a SPIR-V shader must contain a complete module aligned to 32-bit words");
		return NULL;
	}
	uint32_t *words = malloc((size_t)length);
	if (words == NULL) {
		fclose(stream);
		dynlex_runtime_set_error("failed to allocate a SPIR-V shader module");
		return NULL;
	}
	size_t bytes_read = fread(words, 1, (size_t)length, stream);
	int close_result = fclose(stream);
	if (bytes_read != (size_t)length || close_result != 0) {
		free(words);
		dynlex_runtime_set_error("failed to read a complete SPIR-V shader module");
		return NULL;
	}
	if (words[0] != 0x07230203u) {
		free(words);
		dynlex_runtime_set_error("a shader file does not contain little-endian SPIR-V");
		return NULL;
	}
	*word_count = (size_t)length / sizeof(uint32_t);
	return words;
}

static bool
reflect_bindings(const uint32_t *words, size_t word_count, uint32_t bindings[DYNLEX_GRAPHICS_MAX_UNIFORMS], uint32_t *count) {
	uint32_t bound = words[3];
	uint32_t *sets = malloc(sizeof(*sets) * bound);
	uint32_t *decorated_bindings = malloc(sizeof(*decorated_bindings) * bound);
	if (sets == NULL || decorated_bindings == NULL) {
		free(sets);
		free(decorated_bindings);
		dynlex_runtime_set_error("failed to allocate SPIR-V reflection state");
		return false;
	}
	for (uint32_t index = 0; index < bound; ++index)
		sets[index] = decorated_bindings[index] = UINT32_MAX;
	for (size_t offset = 5; offset < word_count;) {
		uint16_t instruction_words = (uint16_t)(words[offset] >> 16u);
		uint16_t opcode = (uint16_t)words[offset];
		if (instruction_words == 0 || offset + instruction_words > word_count) {
			free(sets);
			free(decorated_bindings);
			dynlex_runtime_set_error("a SPIR-V shader contains an invalid instruction size");
			return false;
		}
		if (opcode == 71u && instruction_words >= 4u && words[offset + 1] < bound) {
			if (words[offset + 2] == 33u)
				decorated_bindings[words[offset + 1]] = words[offset + 3];
			else if (words[offset + 2] == 34u)
				sets[words[offset + 1]] = words[offset + 3];
		}
		offset += instruction_words;
	}
	for (uint32_t identifier = 0; identifier < bound; ++identifier) {
		if (decorated_bindings[identifier] == UINT32_MAX)
			continue;
		if (sets[identifier] != 0) {
			free(sets);
			free(decorated_bindings);
			dynlex_runtime_set_error("DynLex graphics supports SPIR-V resources only in descriptor set 0");
			return false;
		}
		if (decorated_bindings[identifier] >= DYNLEX_GRAPHICS_MAX_UNIFORMS) {
			free(sets);
			free(decorated_bindings);
			dynlex_runtime_set_error("a SPIR-V uniform binding exceeds the DynLex graphics limit");
			return false;
		}
		bool duplicate = false;
		for (uint32_t index = 0; index < *count; ++index)
			duplicate |= bindings[index] == decorated_bindings[identifier];
		if (!duplicate)
			bindings[(*count)++] = decorated_bindings[identifier];
	}
	free(sets);
	free(decorated_bindings);
	return true;
}

static int compare_u32(const void *left, const void *right) {
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;
	return a < b ? -1 : a > b;
}

static bool create_custom_layout(DynlexGraphicsPipeline *pipeline) {
	DynlexGraphics *graphics = pipeline->owner;
	if (!reflect_bindings(pipeline->vertex_code, pipeline->vertex_word_count, pipeline->bindings, &pipeline->binding_count) ||
		!reflect_bindings(pipeline->fragment_code, pipeline->fragment_word_count, pipeline->bindings, &pipeline->binding_count))
		return false;
	qsort(pipeline->bindings, pipeline->binding_count, sizeof(pipeline->bindings[0]), compare_u32);
	VkDescriptorSetLayoutBinding descriptors[DYNLEX_GRAPHICS_MAX_UNIFORMS];
	for (uint32_t index = 0; index < pipeline->binding_count; ++index) {
		descriptors[index] = (VkDescriptorSetLayoutBinding){
			.binding = pipeline->bindings[index],
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		};
	}
	if (pipeline->binding_count > 0) {
		VkDescriptorSetLayoutCreateInfo descriptor_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = pipeline->binding_count,
			.pBindings = descriptors,
		};
		VkResult result = vkCreateDescriptorSetLayout(graphics->device, &descriptor_info, NULL, &pipeline->descriptor_layout);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan uniform descriptor layout", result);
			return false;
		}
	}
	VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = pipeline->binding_count > 0 ? 1u : 0u,
		.pSetLayouts = pipeline->binding_count > 0 ? &pipeline->descriptor_layout : NULL,
	};
	VkResult result = vkCreatePipelineLayout(graphics->device, &layout_info, NULL, &pipeline->layout);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("creating a Vulkan shader pipeline layout", result);
		return false;
	}
	if (pipeline->binding_count == 0)
		return true;
	VkDeviceSize alignment = graphics->physical_properties.limits.minUniformBufferOffsetAlignment;
	pipeline->uniform_stride = alignment > sizeof(float) ? alignment : sizeof(float);
	VkDescriptorSetLayout layouts[DYNLEX_GRAPHICS_FRAMES];
	for (uint32_t frame = 0; frame < DYNLEX_GRAPHICS_FRAMES; ++frame)
		layouts[frame] = pipeline->descriptor_layout;
	VkDescriptorSetAllocateInfo set_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = graphics->descriptor_pool,
		.descriptorSetCount = DYNLEX_GRAPHICS_FRAMES,
		.pSetLayouts = layouts,
	};
	result = vkAllocateDescriptorSets(graphics->device, &set_info, pipeline->descriptor_sets);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("allocating Vulkan uniform descriptor sets", result);
		return false;
	}
	pipeline->descriptor_sets_allocated = true;
	for (uint32_t frame = 0; frame < DYNLEX_GRAPHICS_FRAMES; ++frame) {
		VkDeviceSize size = pipeline->uniform_stride * pipeline->binding_count;
		if (!dynlex_graphics_create_buffer(
				graphics, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &pipeline->uniform_buffers[frame],
				&pipeline->uniform_memory[frame]
			))
			return false;
		result =
			vkMapMemory(graphics->device, pipeline->uniform_memory[frame], 0, size, 0, (void **)&pipeline->uniform_maps[frame]);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("mapping Vulkan uniform storage", result);
			return false;
		}
		VkDescriptorBufferInfo buffers[DYNLEX_GRAPHICS_MAX_UNIFORMS];
		VkWriteDescriptorSet writes[DYNLEX_GRAPHICS_MAX_UNIFORMS];
		for (uint32_t index = 0; index < pipeline->binding_count; ++index) {
			buffers[index] = (VkDescriptorBufferInfo){
				.buffer = pipeline->uniform_buffers[frame],
				.offset = pipeline->uniform_stride * index,
				.range = sizeof(float),
			};
			writes[index] = (VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = pipeline->descriptor_sets[frame],
				.dstBinding = pipeline->bindings[index],
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &buffers[index],
			};
		}
		vkUpdateDescriptorSets(graphics->device, pipeline->binding_count, writes, 0, NULL);
	}
	return true;
}

static bool build_custom_pipeline(DynlexGraphicsPipeline *pipeline) {
	return create_pipeline(
		pipeline->owner, pipeline->vertex_code, pipeline->vertex_word_count, pipeline->fragment_code,
		pipeline->fragment_word_count, pipeline->layout, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, false, false, &pipeline->pipeline
	);
}

DynlexGraphicsPipeline *
dynlex_graphics_pipeline_create(DynlexGraphics *graphics, const char *vertex_path, const char *fragment_path) {
	if (graphics == NULL || vertex_path == NULL || fragment_path == NULL) {
		dynlex_runtime_set_error("creating a graphics pipeline requires a context and two SPIR-V paths");
		return NULL;
	}
	DynlexGraphicsPipeline *pipeline = calloc(1, sizeof(*pipeline));
	if (pipeline == NULL) {
		dynlex_runtime_set_error("failed to allocate a graphics pipeline");
		return NULL;
	}
	pipeline->owner = graphics;
	pipeline->vertex_code = read_spirv(vertex_path, &pipeline->vertex_word_count);
	pipeline->fragment_code = read_spirv(fragment_path, &pipeline->fragment_word_count);
	if (pipeline->vertex_code == NULL || pipeline->fragment_code == NULL || !create_custom_layout(pipeline) ||
		!build_custom_pipeline(pipeline)) {
		dynlex_graphics_destroy_pipeline_resources(pipeline);
		free(pipeline);
		return NULL;
	}
	pipeline->next = graphics->pipelines;
	graphics->pipelines = pipeline;
	return pipeline;
}

void dynlex_graphics_destroy_pipeline_resources(DynlexGraphicsPipeline *pipeline) {
	DynlexGraphics *graphics = pipeline->owner;
	if (pipeline->pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(graphics->device, pipeline->pipeline, NULL);
	if (pipeline->layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(graphics->device, pipeline->layout, NULL);
	if (pipeline->descriptor_sets_allocated)
		vkFreeDescriptorSets(graphics->device, graphics->descriptor_pool, DYNLEX_GRAPHICS_FRAMES, pipeline->descriptor_sets);
	for (uint32_t frame = 0; frame < DYNLEX_GRAPHICS_FRAMES; ++frame) {
		if (pipeline->uniform_maps[frame] != NULL)
			vkUnmapMemory(graphics->device, pipeline->uniform_memory[frame]);
		if (pipeline->uniform_buffers[frame] != VK_NULL_HANDLE)
			vkDestroyBuffer(graphics->device, pipeline->uniform_buffers[frame], NULL);
		if (pipeline->uniform_memory[frame] != VK_NULL_HANDLE)
			vkFreeMemory(graphics->device, pipeline->uniform_memory[frame], NULL);
	}
	if (pipeline->descriptor_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(graphics->device, pipeline->descriptor_layout, NULL);
	free(pipeline->vertex_code);
	free(pipeline->fragment_code);
}

int32_t dynlex_graphics_pipeline_destroy(DynlexGraphicsPipeline *pipeline) {
	if (pipeline == NULL)
		return 1;
	DynlexGraphics *graphics = pipeline->owner;
	if (graphics->frame_active) {
		dynlex_runtime_set_error("a graphics pipeline cannot be released during an active frame");
		return 0;
	}
	VkResult result = vkDeviceWaitIdle(graphics->device);
	if (result != VK_SUCCESS) {
		dynlex_graphics_set_vulkan_error("waiting to release a Vulkan graphics pipeline", result);
		return 0;
	}
	if (graphics->active_pipeline == pipeline)
		graphics->active_pipeline = NULL;
	DynlexGraphicsPipeline **cursor = &graphics->pipelines;
	while (*cursor != pipeline)
		cursor = &(*cursor)->next;
	*cursor = pipeline->next;
	dynlex_graphics_destroy_pipeline_resources(pipeline);
	free(pipeline);
	return 1;
}

int32_t dynlex_graphics_use_pipeline(DynlexGraphics *graphics, DynlexGraphicsPipeline *pipeline) {
	if (pipeline != NULL && pipeline->owner != graphics) {
		dynlex_runtime_set_error("a graphics pipeline belongs to a different graphics context");
		return 0;
	}
	graphics->active_pipeline = pipeline;
	return 1;
}

bool dynlex_graphics_rebuild_custom_pipelines(DynlexGraphics *graphics) {
	for (DynlexGraphicsPipeline *pipeline = graphics->pipelines; pipeline != NULL; pipeline = pipeline->next) {
		if (!build_custom_pipeline(pipeline))
			return false;
	}
	return true;
}

static bool validate_compute_shader(const DynlexGraphics *graphics, const uint32_t *words, size_t word_count) {
	bool compute_entry = false;
	bool requires_float64 = false;
	for (size_t offset = 5; offset < word_count;) {
		uint32_t instruction = words[offset];
		size_t length = instruction >> 16;
		if (length == 0 || length > word_count - offset) {
			dynlex_runtime_set_error("compute shader contains an incomplete SPIR-V instruction");
			return false;
		}
		uint32_t opcode = instruction & 0xffffu;
		if (opcode == 17 && length >= 2 && words[offset + 1] == 10) // OpCapability Float64
			requires_float64 = true;
		if (opcode == 15 && length >= 4 && words[offset + 1] == 5) // OpEntryPoint GLCompute
			compute_entry = true;
		offset += length;
	}
	if (!compute_entry) {
		dynlex_runtime_set_error("shader has no Vulkan compute entry point");
		return false;
	}
	if (requires_float64 && !graphics->compute_float64_enabled) {
		dynlex_runtime_set_error("compute shader requires unavailable Vulkan 64-bit floating-point support");
		return false;
	}
	return true;
}

static int32_t
dispatch_compute(DynlexGraphics *graphics, const char *shader_path, void *buffer_data, size_t buffer_bytes, uint32_t groups_x) {
	if (graphics == NULL || shader_path == NULL || graphics->frame_active || !graphics->compute_supported || groups_x == 0 ||
		groups_x > graphics->physical_properties.limits.maxComputeWorkGroupCount[0] ||
		(buffer_data == NULL && buffer_bytes != 0) ||
		(buffer_data != NULL &&
		 (buffer_bytes == 0 || buffer_bytes > graphics->physical_properties.limits.maxStorageBufferRange))) {
		dynlex_runtime_set_error(
			"compute dispatch requires an idle compute-capable graphics context, a shader path, and a valid workgroup count"
		);
		return 0;
	}
	bool has_buffer = buffer_data != NULL;
	size_t word_count = 0;
	uint32_t *words = read_spirv(shader_path, &word_count);
	if (words == NULL)
		return 0;
	if (!validate_compute_shader(graphics, words, word_count)) {
		free(words);
		return 0;
	}
	VkShaderModule module = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkBuffer storage_buffer = VK_NULL_HANDLE;
	VkDeviceMemory storage_memory = VK_NULL_HANDLE;
	void *storage_map = NULL;
	VkCommandBuffer commands = VK_NULL_HANDLE;
	VkFence completed = VK_NULL_HANDLE;
	VkResult result = VK_SUCCESS;
	bool submitted = false;
	bool success = false;
	do {
		if (!create_shader_module(graphics, words, word_count, &module))
			break;
		if (has_buffer) {
			if (!dynlex_graphics_create_buffer(
					graphics, buffer_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storage_buffer, &storage_memory
				))
				break;
			result = vkMapMemory(graphics->device, storage_memory, 0, buffer_bytes, 0, &storage_map);
			if (result != VK_SUCCESS) {
				dynlex_graphics_set_vulkan_error("mapping a Vulkan compute storage buffer", result);
				break;
			}
			memcpy(storage_map, buffer_data, buffer_bytes);
			VkDescriptorSetLayoutBinding binding = {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			};
			VkDescriptorSetLayoutCreateInfo descriptor_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &binding,
			};
			result = vkCreateDescriptorSetLayout(graphics->device, &descriptor_info, NULL, &descriptor_layout);
			if (result != VK_SUCCESS) {
				dynlex_graphics_set_vulkan_error("creating a Vulkan compute descriptor layout", result);
				break;
			}
			VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
			VkDescriptorPoolCreateInfo pool_info = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = 1,
				.poolSizeCount = 1,
				.pPoolSizes = &pool_size,
			};
			result = vkCreateDescriptorPool(graphics->device, &pool_info, NULL, &descriptor_pool);
			if (result != VK_SUCCESS) {
				dynlex_graphics_set_vulkan_error("creating a Vulkan compute descriptor pool", result);
				break;
			}
			VkDescriptorSetAllocateInfo allocation = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &descriptor_layout,
			};
			result = vkAllocateDescriptorSets(graphics->device, &allocation, &descriptor_set);
			if (result != VK_SUCCESS) {
				dynlex_graphics_set_vulkan_error("allocating a Vulkan compute descriptor", result);
				break;
			}
			VkDescriptorBufferInfo buffer_info = {.buffer = storage_buffer, .offset = 0, .range = buffer_bytes};
			VkWriteDescriptorSet update = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = descriptor_set,
				.dstBinding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &buffer_info,
			};
			vkUpdateDescriptorSets(graphics->device, 1, &update, 0, NULL);
		}
		VkPipelineLayoutCreateInfo layout_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = has_buffer ? 1u : 0u,
			.pSetLayouts = has_buffer ? &descriptor_layout : NULL,
		};
		result = vkCreatePipelineLayout(graphics->device, &layout_info, NULL, &layout);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan compute pipeline layout", result);
			break;
		}
		VkComputePipelineCreateInfo pipeline_info = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage =
				{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_COMPUTE_BIT,
					.module = module,
					.pName = "main",
				},
			.layout = layout,
		};
		result = vkCreateComputePipelines(graphics->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan compute pipeline", result);
			break;
		}
		VkCommandBufferAllocateInfo command_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = graphics->command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		result = vkAllocateCommandBuffers(graphics->device, &command_info, &commands);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("allocating a Vulkan compute command buffer", result);
			break;
		}
		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		result = vkBeginCommandBuffer(commands, &begin_info);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("beginning a Vulkan compute command buffer", result);
			break;
		}
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		if (has_buffer)
			vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptor_set, 0, NULL);
		vkCmdDispatch(commands, groups_x, 1, 1);
		result = vkEndCommandBuffer(commands);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("ending a Vulkan compute command buffer", result);
			break;
		}
		VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		result = vkCreateFence(graphics->device, &fence_info, NULL, &completed);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("creating a Vulkan compute fence", result);
			break;
		}
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &commands,
		};
		result = vkQueueSubmit(graphics->graphics_queue, 1, &submit_info, completed);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("submitting a Vulkan compute dispatch", result);
			break;
		}
		submitted = true;
		result = vkWaitForFences(graphics->device, 1, &completed, VK_TRUE, UINT64_MAX);
		if (result != VK_SUCCESS) {
			dynlex_graphics_set_vulkan_error("waiting for a Vulkan compute dispatch", result);
			break;
		}
		success = true;
	} while (false);
	if (success && has_buffer)
		memcpy(buffer_data, storage_map, buffer_bytes);
	if (submitted && !success)
		vkQueueWaitIdle(graphics->graphics_queue);
	if (completed != VK_NULL_HANDLE)
		vkDestroyFence(graphics->device, completed, NULL);
	if (commands != VK_NULL_HANDLE)
		vkFreeCommandBuffers(graphics->device, graphics->command_pool, 1, &commands);
	if (pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(graphics->device, pipeline, NULL);
	if (layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(graphics->device, layout, NULL);
	if (descriptor_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(graphics->device, descriptor_pool, NULL);
	if (descriptor_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(graphics->device, descriptor_layout, NULL);
	if (storage_map != NULL)
		vkUnmapMemory(graphics->device, storage_memory);
	if (storage_buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(graphics->device, storage_buffer, NULL);
	if (storage_memory != VK_NULL_HANDLE)
		vkFreeMemory(graphics->device, storage_memory, NULL);
	if (module != VK_NULL_HANDLE)
		vkDestroyShaderModule(graphics->device, module, NULL);
	free(words);
	return success;
}

int32_t dynlex_graphics_dispatch_compute(DynlexGraphics *graphics, const char *shader_path, uint32_t groups_x) {
	return dispatch_compute(graphics, shader_path, NULL, 0, groups_x);
}

int32_t dynlex_graphics_dispatch_compute_buffer(
	DynlexGraphics *graphics, const char *shader_path, void *data, size_t byte_count, uint32_t groups_x
) {
	if (data == NULL || byte_count == 0) {
		dynlex_runtime_set_error("compute storage dispatch requires a nonempty buffer");
		return 0;
	}
	return dispatch_compute(graphics, shader_path, data, byte_count, groups_x);
}
