// SPIR-V opcodes carry signed interpretation even when LLVM's signless integer
// types become unsigned declarations. Make that interpretation explicit in the
// operand/result types as well, so every shader translator preserves it.
static void preserveShaderIntegerSemantics(std::vector<uint32_t> &binary) {
	struct IntegerType {
		uint32_t width;
		uint32_t sign;
		uint32_t base;
		uint32_t count;
	};
	std::unordered_map<uint32_t, IntegerType> integers;
	std::unordered_set<uint32_t> types, glslImports;
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos)) {
		const uint32_t op = spvOpcode(binary[pos]);
		if (op >= 19 && op <= 39)
			types.insert(binary[pos + 1]);
		if (op == spvOpTypeInt)
			integers.emplace(binary[pos + 1], IntegerType{binary[pos + 2], binary[pos + 3], 0, 1});
		if (op == spvOpTypeVector)
			integers.emplace(binary[pos + 1], IntegerType{0, 0, binary[pos + 2], binary[pos + 3]});
		if (op == 11 && readSpvString(binary.data(), pos + 2, pos + spvWordCount(binary[pos])) == "GLSL.std.450")
			glslImports.insert(binary[pos + 1]);
	}
	std::unordered_map<uint32_t, uint32_t> valueTypes;
	bool functions = false;
	for (size_t pos = 5; pos < binary.size(); pos = nextSpvInstruction(binary, pos)) {
		const uint32_t op = spvOpcode(binary[pos]);
		functions = functions || op == spvOpFunction;
		if ((functions || op == spvOpVariable || (op >= 41 && op <= 52)) && spvWordCount(binary[pos]) >= 3 &&
			types.contains(binary[pos + 1]))
			valueTypes.emplace(binary[pos + 2], binary[pos + 1]);
	}
	std::vector<uint32_t> declarations;
	std::unordered_map<uint64_t, uint32_t> convertedTypes;
	std::function<uint32_t(uint32_t, uint32_t)> typeFor = [&](uint32_t original, uint32_t sign) {
		const uint64_t key = (static_cast<uint64_t>(original) << 1) | sign;
		if (auto found = convertedTypes.find(key); found != convertedTypes.end())
			return found->second;
		auto found = integers.find(original);
		requireCompilerInvariant(found != integers.end(), "integer shader operation has a non-integer type");
		const IntegerType source = found->second;
		const IntegerType target =
			source.base ? IntegerType{0, 0, typeFor(source.base, sign), source.count} : IntegerType{source.width, sign, 0, 1};
		for (const auto &[id, type] : integers)
			if (type.width == target.width && type.sign == target.sign && type.base == target.base &&
				type.count == target.count) {
				convertedTypes.emplace(key, id);
				return id;
			}
		const uint32_t id = allocateSpvId(binary);
		if (target.base)
			declarations.insert(declarations.end(), {spvInstWord(4, spvOpTypeVector), id, target.base, target.count});
		else
			declarations.insert(declarations.end(), {spvInstWord(4, spvOpTypeInt), id, target.width, target.sign});
		integers.emplace(id, target);
		convertedTypes.emplace(key, id);
		return id;
	};
	size_t firstFunction = binary.size();
	for (size_t pos = 5; pos < binary.size();) {
		const uint32_t op = spvOpcode(binary[pos]);
		uint32_t length = spvWordCount(binary[pos]);
		if (op == spvOpFunction && firstFunction == binary.size())
			firstFunction = pos;
		int sign = -1;
		bool result = false;
		uint32_t first = 3, count = 0;
		switch (op) {
		case 111:
			sign = 1;
			count = 1;
			break; // ConvertSToF
		case 112:
			sign = 0;
			count = 1;
			break; // ConvertUToF
		case 110:
			sign = 1;
			result = true;
			break; // ConvertFToS
		case 109:
			sign = 0;
			result = true;
			break; // ConvertFToU
		case 114:
			sign = 1;
			result = true;
			count = 1;
			break; // SConvert
		case 113:
			sign = 0;
			result = true;
			count = 1;
			break; // UConvert
		case 126:
			sign = 1;
			result = true;
			count = 1;
			break; // SNegate
		case 135:
		case 138:
		case 139:
			sign = 1;
			result = true;
			count = 2;
			break; // SDiv, SRem, SMod
		case 134:
		case 137:
			sign = 0;
			result = true;
			count = 2;
			break; // UDiv, UMod
		case 195:
			sign = 1;
			result = true;
			count = 1;
			break; // ShiftRightArithmetic
		case 194:
			sign = 0;
			result = true;
			count = 1;
			break; // ShiftRightLogical
		case 173:
		case 175:
		case 177:
		case 179:
			sign = 1;
			count = 2;
			break; // Signed comparisons
		case 172:
		case 174:
		case 176:
		case 178:
			sign = 0;
			count = 2;
			break; // Unsigned comparisons
		case 12:
			if (glslImports.contains(binary[pos + 3])) {
				const uint32_t instruction = binary[pos + 4];
				if (instruction == 5 || instruction == 7 || instruction == 39 || instruction == 42 || instruction == 45)
					sign = 1; // SAbs, SSign, SMin, SMax, SClamp
				if (instruction == 38 || instruction == 41 || instruction == 44)
					sign = 0; // UMin, UMax, UClamp
				if (sign >= 0) {
					result = true;
					first = 5;
					count = length - first;
				}
			}
			break;
		default:
			break;
		}
		if (sign < 0) {
			pos += length;
			continue;
		}
		std::vector<uint32_t> before, after;
		for (uint32_t index = first; index < first + count; ++index) {
			const uint32_t operand = binary[pos + index];
			auto found = valueTypes.find(operand);
			requireCompilerInvariant(found != valueTypes.end(), "integer shader operand has no declared type");
			const uint32_t target = typeFor(found->second, sign);
			if (target != found->second) {
				const uint32_t cast = allocateSpvId(binary);
				before.insert(before.end(), {spvInstWord(4, spvOpBitcast), target, cast, operand});
				binary[pos + index] = cast;
			}
		}
		if (result) {
			const uint32_t original = binary[pos + 1], target = typeFor(original, sign);
			if (original != target) {
				const uint32_t identity = binary[pos + 2], temporary = allocateSpvId(binary);
				binary[pos + 1] = target;
				binary[pos + 2] = temporary;
				after.insert(after.end(), {spvInstWord(4, spvOpBitcast), original, identity, temporary});
			}
		}
		if (op == 138) { // GLSL signed % is not SPIR-V SRem for negative operands.
			const uint32_t type = binary[pos + 1], identity = binary[pos + 2];
			const uint32_t left = binary[pos + 3], right = binary[pos + 4];
			const uint32_t quotient = allocateSpvId(binary), product = allocateSpvId(binary);
			const std::vector<uint32_t> remainder = {
				spvInstWord(5, 135), type, quotient, left,	   right,  // SDiv truncates toward zero.
				spvInstWord(5, 132), type, product,	 quotient, right,  // IMul
				spvInstWord(5, 130), type, identity, left,	   product // ISub
			};
			binary.erase(binary.begin() + pos, binary.begin() + pos + length);
			binary.insert(binary.begin() + pos, remainder.begin(), remainder.end());
			length = remainder.size();
		}
		binary.insert(binary.begin() + pos + length, after.begin(), after.end());
		binary.insert(binary.begin() + pos, before.begin(), before.end());
		pos += before.size() + length + after.size();
	}
	requireCompilerInvariant(firstFunction != binary.size(), "integer shader lowering has no function declarations");
	binary.insert(binary.begin() + firstFunction, declarations.begin(), declarations.end());
}
