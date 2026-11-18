static nlohmann::json reflectedShaderResources(
	spirv_cross::Compiler &compiler, const spirv_cross::ShaderResources &resources, const ParseContext &context
) {
	nlohmann::json result = nlohmann::json::array();
	auto append = [&](const auto &entries, bool image) {
		for (const auto &entry : entries) {
			const std::string symbol = compiler.get_name(entry.id);
			auto declared =
				std::find_if(context.shaderResources.begin(), context.shaderResources.end(), [&](const auto &resource) {
				return shaderResourceGlobalName(resource.name) == symbol;
			});
			requireCompilerInvariant(
				declared != context.shaderResources.end() && declared->isImage() == image,
				"sampled shader resource lost its declared interface"
			);
			requireCompilerInvariant(
				compiler.has_decoration(entry.id, spv::DecorationBinding) &&
					compiler.has_decoration(entry.id, spv::DecorationDescriptorSet),
				"sampled shader resource lost its binding"
			);
			const auto group = compiler.get_decoration(entry.id, spv::DecorationDescriptorSet);
			const auto binding = compiler.get_decoration(entry.id, spv::DecorationBinding);
			requireCompilerInvariant(
				group == 0 && binding == declared->binding, "sampled shader resource binding changed during emission"
			);
			nlohmann::json description{{"name", declared->name}, {"group", group}, {"binding", binding}};
			if (image) {
				const auto &type = compiler.get_type(entry.base_type_id);
				bool cube = declared->kind == ShaderResourceKind::ImageCube;
				bool depth = declared->kind == ShaderResourceKind::Depth2D;
				requireCompilerInvariant(
					type.basetype == spirv_cross::SPIRType::Image && type.array.empty() &&
						type.image.dim == (cube ? spv::DimCube : spv::Dim2D) && type.image.depth == depth &&
						!type.image.arrayed && !type.image.ms,
					"sampled shader image type changed during emission"
				);
				description["kind"] = "texture";
				description["dimension"] = cube ? "cube" : "2d";
				description["sampleType"] = depth ? "depth" : "float";
			} else {
				description["kind"] = "sampler";
				description["comparison"] = declared->kind == ShaderResourceKind::ComparisonSampler;
			}
			result.push_back(std::move(description));
		}
	};
	append(resources.separate_images, true);
	append(resources.separate_samplers, false);
	std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
		return left.at("binding") < right.at("binding");
	});
	return result;
}
