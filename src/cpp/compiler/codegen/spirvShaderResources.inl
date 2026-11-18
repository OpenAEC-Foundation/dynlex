// Lower numeric resource operations after LLVM emission. Opaque image/sampler
// values remain inside their sampling block and have no runtime string metadata.
static void lowerShaderResources(std::vector<uint32_t> &binary, const ParseContext &context) {
	if (context.shaderResources.empty())
		return;
	uint32_t floatType = 0, zero = 0;
	std::unordered_map<std::string, uint32_t> named;
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos)) {
		if (spvOpcode(binary[pos]) == 22 && binary[pos + 2] == 32)
			floatType = binary[pos + 1];
		if (spvOpcode(binary[pos]) == spvOpName)
			named.emplace(readSpvString(binary.data(), pos + 2, pos + spvWordCount(binary[pos])), binary[pos + 1]);
	}
	requireCompilerInvariant(floatType, "shader resource operation has no float32 type");
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos))
		if (spvOpcode(binary[pos]) == spvOpConstant && binary[pos + 1] == floatType && binary[pos + 3] == 0)
			zero = binary[pos + 2];

	struct ResourceIds {
		uint32_t type, variable, sampled;
	};
	std::unordered_map<uint32_t, ResourceIds> bindings;
	std::map<ShaderResourceKind, ResourceIds> shapes;
	std::vector<uint32_t> names, types, globals;
	for (const auto &resource : context.shaderResources) {
		auto kind = resource.isImage() ? resource.kind : ShaderResourceKind::Sampler;
		auto found = shapes.find(kind);
		ResourceIds shape{};
		if (found == shapes.end()) {
			shape.type = allocateSpvId(binary);
			if (resource.isImage()) {
				types.insert(
					types.end(),
					{spvInstWord(9, 25), shape.type, floatType, resource.kind == ShaderResourceKind::ImageCube ? 3u : 1u,
					 resource.kind == ShaderResourceKind::Depth2D ? 1u : 0u, 0, 0, 1, 0}
				);
				shape.sampled = allocateSpvId(binary);
				types.insert(types.end(), {spvInstWord(3, 27), shape.sampled, shape.type});
			} else {
				types.insert(types.end(), {spvInstWord(2, 26), shape.type});
			}
			shape.variable = allocateSpvId(binary); // UniformConstant pointer for this shape.
			types.insert(types.end(), {spvInstWord(4, spvOpTypePointer), shape.variable, 0, shape.type});
			shapes.emplace(kind, shape);
		} else {
			shape = found->second;
		}
		uint32_t variable = allocateSpvId(binary);
		globals.insert(globals.end(), {spvInstWord(4, spvOpVariable), shape.variable, variable, 0});
		bindings.emplace(resource.binding, ResourceIds{shape.type, variable, shape.sampled});
		auto words = encodeSpvString(shaderResourceGlobalName(resource.name));
		names.insert(names.end(), {spvInstWord(2 + words.size(), spvOpName), variable});
		names.insert(names.end(), words.begin(), words.end());
	}
	if (!zero) {
		zero = allocateSpvId(binary);
		types.insert(types.end(), {spvInstWord(4, spvOpConstant), floatType, zero, 0});
	}
	struct Sample {
		ResourceIds image, sampler;
		bool comparison;
	};
	std::unordered_map<uint32_t, Sample> samples;
	for (const auto &image : context.shaderResources) {
		if (!image.isImage())
			continue;
		for (const auto &sampler : context.shaderResources) {
			if (sampler.isImage())
				continue;
			auto function = named.find(shaderSampleFunctionName(image, sampler));
			if (function != named.end())
				samples.emplace(
					function->second, Sample{
										  bindings.at(image.binding), bindings.at(sampler.binding),
										  sampler.kind == ShaderResourceKind::ComparisonSampler
									  }
				);
		}
	}
	std::unordered_set<uint32_t> removed;
	bool removing = false;
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos)) {
		uint32_t op = spvOpcode(binary[pos]);
		if (op == spvOpFunction)
			removing = samples.contains(binary[pos + 2]);
		if (removing && (op == spvOpFunction || op == 55))
			removed.insert(binary[pos + 2]);
		if (op == 56)
			removing = false;
	}
	std::vector<uint32_t> output(binary.begin(), binary.begin() + 5);
	bool namedResources = false, declaredResources = false;
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos)) {
		uint32_t op = spvOpcode(binary[pos]), count = spvWordCount(binary[pos]);
		if (!namedResources && (op == spvOpDecorate || op == spvOpMemberDecorate || (op >= 19 && op <= 39))) {
			output.insert(output.end(), names.begin(), names.end());
			namedResources = true;
		}
		if (!declaredResources && op == spvOpFunction) {
			output.insert(output.end(), types.begin(), types.end());
			output.insert(output.end(), globals.begin(), globals.end());
			declaredResources = true;
		}
		if (op == spvOpFunction)
			removing = samples.contains(binary[pos + 2]);
		if (removing) {
			if (op == 56)
				removing = false;
			continue;
		}
		if ((op == spvOpName || op == spvOpDecorate) && removed.contains(binary[pos + 1]))
			continue;
		if (op == 57 && samples.contains(binary[pos + 3])) {
			requireCompilerInvariant(count == 6, "shader sampling operation has an invalid argument count");
			const auto &sample = samples.at(binary[pos + 3]);
			uint32_t image = allocateSpvId(binary), sampler = allocateSpvId(binary), combined = allocateSpvId(binary);
			output.insert(
				output.end(), {spvInstWord(4, spvOpLoad), sample.image.type, image, sample.image.variable,
							   spvInstWord(4, spvOpLoad), sample.sampler.type, sampler, sample.sampler.variable,
							   spvInstWord(5, 86), sample.image.sampled, combined, image, sampler}
			);
			if (sample.comparison) {
				output.insert(
					output.end(),
					{spvInstWord(8, 90), binary[pos + 1], binary[pos + 2], combined, binary[pos + 4], binary[pos + 5], 2, zero}
				);
			} else {
				output.insert(
					output.end(),
					{spvInstWord(7, 88), binary[pos + 1], binary[pos + 2], combined, binary[pos + 4], 2, binary[pos + 5]}
				);
			}
			continue;
		}
		output.insert(output.end(), binary.begin() + pos, binary.begin() + pos + count);
	}
	requireCompilerInvariant(
		namedResources && declaredResources && !samples.empty(), "shader resource lowering lost its operations"
	);
	output[3] = binary[3];
	binary = std::move(output);
}
