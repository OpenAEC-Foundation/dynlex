// LLVM integer types are signless. GPU interfaces distinguish signed and
// unsigned fetch formats, so expose the declared type at the interface and
// bitcast its bits to LLVM's internal arithmetic type after each input load.
static uint32_t
getOrCreateVertexInputType(std::vector<uint32_t> &binary, uint32_t opcode, std::initializer_list<uint32_t> operands) {
	for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
		if (spvOpcode(binary[position]) == opcode && spvWordCount(binary[position]) == operands.size() + 2 &&
			std::equal(operands.begin(), operands.end(), binary.begin() + position + 2))
			return binary[position + 1];
	}
	uint32_t identity = allocateSpvId(binary);
	std::vector<uint32_t> declaration = {spvInstWord(operands.size() + 2, opcode), identity};
	declaration.insert(declaration.end(), operands.begin(), operands.end());
	binary.insert(binary.begin() + findFirstSpvVariable(binary), declaration.begin(), declaration.end());
	return identity;
}

static void patchSignedShaderInputs(std::vector<uint32_t> &binary, const std::vector<ShaderIoVar> &variables) {
	for (const auto &variable : variables) {
		if (variable.signedComponents == 0)
			continue;
		uint32_t pointer = 0;
		for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
			if (spvOpcode(binary[position]) == spvOpVariable && binary[position + 2] == variable.id)
				pointer = binary[position + 1];
		}
		uint32_t originalType = 0;
		for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
			if (spvOpcode(binary[position]) == spvOpTypePointer && binary[position + 1] == pointer)
				originalType = binary[position + 3];
		}
		requireCompilerInvariant(originalType != 0, "signed vertex input has no SPIR-V value type");
		uint32_t interfaceType = getOrCreateVertexInputType(binary, spvOpTypeInt, {32, 1});
		if (variable.signedComponents > 1)
			interfaceType = getOrCreateVertexInputType(binary, spvOpTypeVector, {interfaceType, variable.signedComponents});
		if (interfaceType == originalType)
			continue;
		uint32_t interfacePointer =
			getOrCreateVertexInputType(binary, spvOpTypePointer, {variable.storageClass, interfaceType});
		for (size_t position = 5; position < binary.size();) {
			uint32_t length = spvWordCount(binary[position]);
			uint32_t opcode = spvOpcode(binary[position]);
			if (opcode == spvOpVariable && binary[position + 2] == variable.id)
				binary[position + 1] = interfacePointer;
			if (opcode == spvOpLoad && binary[position + 3] == variable.id) {
				requireCompilerInvariant(
					binary[position + 1] == originalType, "vertex input load has a conflicting SPIR-V type"
				);
				uint32_t originalResult = binary[position + 2];
				uint32_t loaded = allocateSpvId(binary);
				binary[position + 1] = interfaceType;
				binary[position + 2] = loaded;
				std::vector<uint32_t> cast = {spvInstWord(4, spvOpBitcast), originalType, originalResult, loaded};
				binary.insert(binary.begin() + position + length, cast.begin(), cast.end());
				position += length + cast.size();
			} else {
				position += length;
			}
		}
	}
}
