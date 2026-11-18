// Retarget derived pointers along with interface roots. LLVM's optimized GEPs
// can load only one matrix row, instead of loading the aggregate as a whole.
static void patchShaderAccessChainStorage(std::vector<uint32_t> &binary) {
	struct PointerType {
		uint32_t storage;
		uint32_t pointee;
	};
	std::unordered_map<uint32_t, PointerType> types;
	std::unordered_map<uint32_t, uint32_t> values;
	std::vector<uint32_t> declarations;
	size_t firstFunction = binary.size();
	auto pointerFor = [&](uint32_t storage, uint32_t pointee) {
		for (const auto &[identity, type] : types)
			if (type.storage == storage && type.pointee == pointee)
				return identity;
		uint32_t identity = allocateSpvId(binary);
		types.emplace(identity, PointerType{storage, pointee});
		declarations.insert(declarations.end(), {spvInstWord(4, spvOpTypePointer), identity, storage, pointee});
		return identity;
	};
	for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
		uint32_t opcode = spvOpcode(binary[position]);
		if (opcode == spvOpTypePointer)
			types.emplace(binary[position + 1], PointerType{binary[position + 2], binary[position + 3]});
		if (opcode == spvOpFunction && firstFunction == binary.size())
			firstFunction = position;
		if ((opcode == spvOpVariable || opcode == 55) && types.contains(binary[position + 1]))
			values.emplace(binary[position + 2], binary[position + 1]);
		if (opcode != spvOpAccessChain && opcode != spvOpInBoundsAccessChain)
			continue;
		auto base = values.find(binary[position + 3]);
		auto result = types.find(binary[position + 1]);
		requireCompilerInvariant(
			base != values.end() && result != types.end(), "shader access chain is missing its pointer type"
		);
		const uint32_t storage = types.at(base->second).storage;
		const uint32_t pointee = result->second.pointee;
		const uint32_t pointer = pointerFor(storage, pointee);
		binary[position + 1] = pointer;
		values.emplace(binary[position + 2], pointer);
	}
	requireCompilerInvariant(firstFunction != binary.size(), "shader module has no function declarations");
	binary.insert(binary.begin() + firstFunction, declarations.begin(), declarations.end());
}
