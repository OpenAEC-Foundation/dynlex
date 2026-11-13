#include "spirv.h"
#include "compilerUtils.h"
#include "parseContext.h"
#include "shaderUniformType.h"
#include "targetOptions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// SPIR-V opcodes
static constexpr uint32_t spvOpCapability = 17;
static constexpr uint32_t spvOpEntryPoint = 15;
static constexpr uint32_t spvOpExecutionMode = 16;
static constexpr uint32_t spvOpMemoryModel = 14;
static constexpr uint32_t spvOpDecorate = 71;
static constexpr uint32_t spvOpName = 5;
static constexpr uint32_t spvOpVariable = 59;
static constexpr uint32_t spvOpFunction = 54;
static constexpr uint32_t spvOpTypeInt = 21;
static constexpr uint32_t spvOpTypeVector = 23;
static constexpr uint32_t spvOpTypePointer = 32;
static constexpr uint32_t spvOpTypeStruct = 30;
static constexpr uint32_t spvOpConstant = 43;
static constexpr uint32_t spvOpLoad = 61;
static constexpr uint32_t spvOpStore = 62;
static constexpr uint32_t spvOpAccessChain = 65;
static constexpr uint32_t spvOpInBoundsAccessChain = 66;
static constexpr uint32_t spvOpBitcast = 124;

// SPIR-V constants
static constexpr uint32_t spvCapabilityLinkage = 5;
static constexpr uint32_t spvDecorationLinkageAttributes = 41;
static constexpr uint32_t spvDecorationBuiltIn = 11;
static constexpr uint32_t spvDecorationLocation = 30;
static constexpr uint32_t spvDecorationBlock = 2;
static constexpr uint32_t spvDecorationBinding = 33;
static constexpr uint32_t spvDecorationDescriptorSet = 34;
static constexpr uint32_t spvDecorationOffset = 35;
static constexpr uint32_t spvOpMemberDecorate = 72;
static constexpr uint32_t spvStorageClassInput = 1;
static constexpr uint32_t spvStorageClassUniform = 2;
static constexpr uint32_t spvStorageClassOutput = 3;

static std::string namedShaderGlobal(std::string_view prefix, std::string_view name) {
	static constexpr char hexadecimalDigits[] = "0123456789abcdef";
	std::string globalName(prefix);
	globalName.reserve(globalName.size() + name.size() * 2);
	for (unsigned char byte : name) {
		globalName.push_back(hexadecimalDigits[byte >> 4]);
		globalName.push_back(hexadecimalDigits[byte & 0x0f]);
	}
	return globalName;
}

std::string shaderInterpolantGlobalName(std::string_view name) { return namedShaderGlobal("dynlex_interpolant_", name); }
std::string shaderResourceGlobalName(std::string_view name) { return namedShaderGlobal("dynlex_resource_", name); }
std::string shaderSampleFunctionName(const ShaderResourceValue &image, const ShaderResourceValue &sampler) {
	return "dynlex_sample_" + std::to_string(image.binding) + "_" + std::to_string(sampler.binding);
}

std::string shaderVertexInputGlobalName(std::string_view name, std::uint32_t location, const DataType &type) {
	const DataType &scalar = type.kind == DataType::Kind::Vector ? type.aggregateElementType() : type;
	const int width = type.kind == DataType::Kind::Vector ? type.arraySize : 1;
	return namedShaderGlobal("dynlex_vertex_", name) + "_" + std::to_string(location) + "_" +
		   std::to_string(static_cast<int>(scalar.kind)) + "_" + std::to_string(scalar.numericSize) + "_" +
		   std::to_string(width);
}

static llvm::Constant *defineVectorConstant(llvm::Constant *constant) {
	auto *vectorType = llvm::dyn_cast<llvm::FixedVectorType>(constant->getType());
	requireCompilerInvariant(vectorType != nullptr, "shader output vector seed must have a fixed vector type");

	std::vector<llvm::Constant *> elements;
	elements.reserve(vectorType->getNumElements());
	for (unsigned index = 0; index < vectorType->getNumElements(); index++) {
		llvm::Constant *element = constant->getAggregateElement(index);
		requireCompilerInvariant(element != nullptr, "shader output vector seed must expose every element");
		if (llvm::isa<llvm::PoisonValue>(element) || llvm::isa<llvm::UndefValue>(element))
			element = llvm::Constant::getNullValue(vectorType->getElementType());
		elements.push_back(element);
	}
	return llvm::ConstantVector::get(elements);
}

static void defineShaderOutputVectorSeeds(
	llvm::Use &use, llvm::Type *vectorType, std::unordered_set<llvm::Instruction *> &visitedInstructions
) {
	llvm::Value *value = use.get();
	if (auto *constant = llvm::dyn_cast<llvm::Constant>(value)) {
		if (constant->getType() != vectorType)
			return;
		use.set(defineVectorConstant(constant));
		return;
	}

	auto *instruction = llvm::dyn_cast<llvm::Instruction>(value);
	if (!instruction || instruction->getType() != vectorType || !visitedInstructions.insert(instruction).second)
		return;

	for (llvm::Use &operand : instruction->operands())
		defineShaderOutputVectorSeeds(operand, vectorType, visitedInstructions);
}

static void defineShaderOutputVectorSeeds(llvm::Module &module) {
	std::unordered_set<llvm::Instruction *> visitedInstructions;
	for (llvm::Function &function : module) {
		for (llvm::BasicBlock &block : function) {
			for (llvm::Instruction &instruction : block) {
				auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
				if (!store || !store->getMetadata(shaderOutputMetadataName))
					continue;

				llvm::Type *vectorType = store->getValueOperand()->getType();
				defineShaderOutputVectorSeeds(store->getOperandUse(0), vectorType, visitedInstructions);
			}
		}
	}
}

// ExecutionModel: Vertex=0, Fragment=4
// BuiltIn: Position=0, FragCoord=15

static uint32_t spvOpcode(uint32_t word) { return word & 0xFFFF; }
static uint32_t spvWordCount(uint32_t word) { return word >> 16; }
static uint32_t spvInstWord(uint32_t wordCount, uint32_t opcode) { return (wordCount << 16) | opcode; }

static size_t nextSpvInstruction(const std::vector<uint32_t> &binary, size_t position) {
	requireCompilerInvariant(position < binary.size(), "SPIR-V instruction starts outside the module");
	uint32_t wordCount = spvWordCount(binary[position]);
	requireCompilerInvariant(wordCount > 0, "SPIR-V instruction has no words");
	requireCompilerInvariant(position + wordCount <= binary.size(), "SPIR-V instruction extends beyond the module");
	return position + wordCount;
}

static uint32_t allocateSpvId(std::vector<uint32_t> &binary) {
	requireCompilerInvariant(binary.size() >= 5, "SPIR-V module has no complete header");
	uint32_t id = binary[3];
	requireCompilerInvariant(id > 0 && id < UINT32_MAX, "SPIR-V ID bound cannot allocate another result");
	binary[3] = id + 1;
	return id;
}

static size_t findFirstSpvVariable(const std::vector<uint32_t> &binary) {
	for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
		if (spvOpcode(binary[position]) == spvOpVariable)
			return position;
	}
	crashCompilerBug("SPIR-V shader has no interface variables");
}

static uint32_t getOrCreateSpvUnsignedZero(std::vector<uint32_t> &binary) {
	uint32_t unsignedTypeId = 0;
	size_t unsignedTypeEnd = 0;
	for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
		uint32_t wordCount = spvWordCount(binary[position]);
		if (spvOpcode(binary[position]) == spvOpTypeInt && wordCount == 4 && binary[position + 2] == 32 &&
			binary[position + 3] == 0) {
			unsignedTypeId = binary[position + 1];
			unsignedTypeEnd = position + wordCount;
			break;
		}
	}

	uint32_t zeroId = 0;
	if (unsignedTypeId) {
		for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
			uint32_t wordCount = spvWordCount(binary[position]);
			if (spvOpcode(binary[position]) == spvOpConstant && wordCount == 4 && binary[position + 1] == unsignedTypeId &&
				binary[position + 3] == 0) {
				zeroId = binary[position + 2];
				break;
			}
		}
	}

	if (zeroId)
		return zeroId;

	std::vector<uint32_t> declarations;
	if (!unsignedTypeId) {
		unsignedTypeId = allocateSpvId(binary);
		declarations.insert(declarations.end(), {spvInstWord(4, spvOpTypeInt), unsignedTypeId, 32, 0});
	}
	zeroId = allocateSpvId(binary);
	declarations.insert(declarations.end(), {spvInstWord(4, spvOpConstant), unsignedTypeId, zeroId, 0});
	// A type used only inside a helper may follow the first global variable.
	// The new constant must follow that type rather than forward-reference it.
	size_t insertion = unsignedTypeEnd ? unsignedTypeEnd : findFirstSpvVariable(binary);
	binary.insert(binary.begin() + insertion, declarations.begin(), declarations.end());
	return zeroId;
}

static std::string readSpvString(const uint32_t *words, size_t startWord, size_t maxWords) {
	std::string result;
	for (size_t i = startWord; i < maxWords; i++) {
		uint32_t w = words[i];
		for (int b = 0; b < 4; b++) {
			char c = (char)((w >> (b * 8)) & 0xFF);
			if (c == '\0')
				return result;
			result += c;
		}
	}
	return result;
}

static std::vector<uint32_t> encodeSpvString(const std::string &str) {
	size_t byteLen = str.size() + 1;
	size_t wordCount = (byteLen + 3) / 4;
	std::vector<uint32_t> words(wordCount, 0);
	std::memcpy(words.data(), str.c_str(), str.size() + 1);
	return words;
}

// Decoration info for a shader I/O variable
struct ShaderIoVar {
	std::string name;		   // LLVM global name (e.g. "gl_FragCoord", "in_Position", "ubo_time")
	uint32_t id = 0;		   // SPIR-V result ID (found by scanning OpName)
	uint32_t structTypeId = 0; // struct type ID (for UBO vars, found via OpTypePointer → OpTypeStruct)
	uint32_t storageClass;	   // target storage class (Input, Output, or Uniform)
	bool isBuiltIn;			   // true = BuiltIn decoration, false = Location decoration
	uint32_t decorationValue;  // BuiltIn ID or Location number
	bool isUBO = false;		   // true = Uniform block (needs Block/Binding/DescriptorSet/Offset decorations)
	bool isResource = false;
	uint32_t binding = 0;		   // UBO binding point
	uint32_t signedComponents = 0; // Signed interface scalar (1) or vector width (2..4).
	uint32_t uniformRows = 0;
	uint32_t uniformColumns = 0;
	uint32_t uniformStorageType = 0;
};

#include "spirvIndexWidths.inl"
#include "spirvIntegerSemantics.inl"
#include "spirvPointerStorage.inl"
#include "spirvShaderResources.inl"
#include "spirvVertexInputs.inl"

// Post-process SPIR-V binary to convert exported functions to shader entry points.
// The LLVM SPIR-V backend emits functions with Export linkage instead of entry points.
static bool patchShaderBinary(
	const std::string &path, uint32_t executionModel, const std::vector<ShaderIoVar> &ioVars, const ParseContext &context,
	std::string &errorMsg
) {
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in) {
		errorMsg = "Cannot open SPIR-V file for patching: " + path;
		return false;
	}
	size_t fileSize = in.tellg();
	if (fileSize % 4 != 0 || fileSize < 20) {
		errorMsg = "Invalid SPIR-V file size";
		return false;
	}
	in.seekg(0);
	std::vector<uint32_t> binary(fileSize / 4);
	in.read(reinterpret_cast<char *>(binary.data()), fileSize);
	in.close();

	if (binary[0] != 0x07230203) {
		errorMsg = "Invalid SPIR-V magic number";
		return false;
	}
	lowerShaderResources(binary, context);

	// Global declarations form the complete SPIR-V 1.4+ entry-point interface.
	// Function-local variables must never appear in that interface.
	std::unordered_set<uint32_t> variableIds;
	std::vector<uint32_t> interfaceIds;
	size_t pos = 5;
	while (pos < binary.size()) {
		uint32_t wc = spvWordCount(binary[pos]);
		uint32_t op = spvOpcode(binary[pos]);
		if (wc == 0)
			break;
		if (op == spvOpFunction)
			break;
		if (op == spvOpVariable && wc >= 4) {
			variableIds.insert(binary[pos + 2]);
			interfaceIds.push_back(binary[pos + 2]);
		}
		pos += wc;
	}

	// Second pass: find IDs by scanning OpName instructions (only match variable IDs for I/O vars)
	uint32_t mainId = 0;
	std::vector<ShaderIoVar> vars = ioVars; // mutable copy to fill in IDs

	pos = 5;
	while (pos < binary.size()) {
		uint32_t wc = spvWordCount(binary[pos]);
		uint32_t op = spvOpcode(binary[pos]);
		if (wc == 0)
			break;
		if (op == spvOpName && wc >= 3) {
			uint32_t id = binary[pos + 1];
			std::string name = readSpvString(binary.data(), pos + 2, pos + wc);
			if (name == "main")
				mainId = id;
			// Only match I/O variable names against actual OpVariable IDs
			if (variableIds.count(id)) {
				for (auto &v : vars) {
					if (name == v.name)
						v.id = id;
				}
			}
		}
		pos += wc;
	}

	if (!mainId) {
		errorMsg = "Could not find main function ID in SPIR-V";
		return false;
	}
	for (const auto &v : vars) {
		if (!v.id) {
			errorMsg = "Could not find SPIR-V ID for " + v.name;
			return false;
		}
	}

	// Build patched output
	std::vector<uint32_t> output;
	output.insert(output.end(), binary.begin(), binary.begin() + 5); // header

	bool entryPointInserted = false;
	pos = 5;
	while (pos < binary.size()) {
		uint32_t wc = spvWordCount(binary[pos]);
		uint32_t op = spvOpcode(binary[pos]);
		if (wc == 0)
			break;

		bool skip = false;

		// Remove OpCapability Linkage
		if (op == spvOpCapability && wc >= 2 && binary[pos + 1] == spvCapabilityLinkage)
			skip = true;

		// Remove OpDecorate ... LinkageAttributes
		if (op == spvOpDecorate && wc >= 3 && binary[pos + 2] == spvDecorationLinkageAttributes)
			skip = true;

		// Insert OpEntryPoint right after OpMemoryModel (SPIR-V logical layout requires:
		// Capability → Extension → ExtInstImport → MemoryModel → EntryPoint → ...)
		if (!entryPointInserted && op == spvOpMemoryModel) {
			// First, emit the MemoryModel instruction itself
			output.insert(output.end(), binary.begin() + pos, binary.begin() + pos + wc);

			// Then emit the EntryPoint
			std::vector<uint32_t> nameWords = encodeSpvString("main");
			uint32_t entryWc = 3 + nameWords.size() + interfaceIds.size();
			output.push_back(spvInstWord(entryWc, spvOpEntryPoint));
			output.push_back(executionModel);
			output.push_back(mainId);
			output.insert(output.end(), nameWords.begin(), nameWords.end());
			output.insert(output.end(), interfaceIds.begin(), interfaceIds.end());
			if (executionModel == 5) { // GLCompute, with a fixed 64-thread workgroup.
				output.insert(output.end(), {spvInstWord(6, spvOpExecutionMode), mainId, 17, 64, 1, 1});
			}
			entryPointInserted = true;
			skip = true; // MemoryModel already emitted above
		}

		if (!skip) {
			// Fix OpVariable storage class and strip initializer for I/O globals
			bool isIoVar = false;
			if (op == spvOpVariable && wc >= 4) {
				uint32_t resultId = binary[pos + 2];
				for (const auto &v : vars) {
					if (resultId == v.id) {
						// Emit 4-word OpVariable (no initializer) with correct storage class
						output.push_back(spvInstWord(4, spvOpVariable));
						output.push_back(binary[pos + 1]); // result type
						output.push_back(binary[pos + 2]); // result id
						output.push_back(v.storageClass);
						isIoVar = true;
						break;
					}
				}
			}
			if (!isIoVar) {
				// Strip Aligned memory operands from OpLoad/OpStore (requires Kernel capability)
				if ((op == spvOpLoad || op == spvOpStore) && wc > (op == spvOpLoad ? 4u : 3u)) {
					uint32_t baseWc = (op == spvOpLoad) ? 4 : 3;
					uint32_t memMask = binary[pos + baseWc];
					if (memMask & 0x2) {
						// Has Aligned — emit without memory operands
						output.push_back(spvInstWord(baseWc, op));
						for (uint32_t w = 1; w < baseWc; w++)
							output.push_back(binary[pos + w]);
					} else {
						output.insert(output.end(), binary.begin() + pos, binary.begin() + pos + wc);
					}
				} else {
					output.insert(output.end(), binary.begin() + pos, binary.begin() + pos + wc);
				}
			}
		}

		pos += wc;
	}

	patchSignedShaderInputs(output, vars);

	// Find insertion point for decorations (before first OpType instruction)
	size_t decorInsertPos = output.size();
	for (pos = 5; pos < output.size();) {
		uint32_t wc2 = spvWordCount(output[pos]);
		uint32_t op2 = spvOpcode(output[pos]);
		if (wc2 == 0)
			break;
		if ((op2 >= 19 && op2 <= 34) || op2 == spvOpVariable || op2 == 43 || op2 == 44) {
			decorInsertPos = pos;
			break;
		}
		pos += wc2;
	}

	// Uniform values live in one-member blocks. Padded row structures preserve
	// matrix layout across Vulkan, WGSL and GLSL without implicit array strides.
	const bool hasUniform = std::any_of(vars.begin(), vars.end(), [](const ShaderIoVar &variable) {
		return variable.isUBO;
	});
	const uint32_t uniformIndexId = hasUniform ? getOrCreateSpvUnsignedZero(output) : 0;
	for (auto &v : vars) {
		if (!v.isUBO)
			continue;

		// Find the old pointer type and value type from the variable
		uint32_t oldPtrTypeId = 0;
		uint32_t valueTypeId = 0;
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable && wc2 >= 4 && output[pos + 2] == v.id)
				oldPtrTypeId = output[pos + 1];
			pos += wc2;
		}
		// Get the value type from the old pointer type
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpTypePointer && wc2 >= 4 && output[pos + 1] == oldPtrTypeId)
				valueTypeId = output[pos + 3];
			pos += wc2;
		}

		// Allocate new IDs
		if (v.uniformRows > 1) {
			bool rowStruct = false;
			const uint32_t fields = v.uniformRows * (v.uniformColumns < 4 ? 2 : 1);
			for (pos = 5; pos < output.size(); pos = nextSpvInstruction(output, pos))
				if (spvOpcode(output[pos]) == spvOpTypeStruct && output[pos + 1] == valueTypeId)
					rowStruct = spvWordCount(output[pos]) == fields + 2;
			requireCompilerInvariant(rowStruct, "matrix uniform is missing its padded SPIR-V row structure");
			v.uniformStorageType = valueTypeId;
		}
		uint32_t structTypeId = allocateSpvId(output);
		uint32_t ptrUniformStructId = allocateSpvId(output);
		uint32_t ptrUniformValueId = allocateSpvId(output);
		v.structTypeId = structTypeId;

		// Insert after the value's types and before this variable.
		size_t typeInsertPos = output.size();
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable && output[pos + 2] == v.id) {
				typeInsertPos = pos;
				break;
			}
			pos += wc2;
		}

		// Insert new type declarations
		std::vector<uint32_t> newTypes;
		// OpTypeStruct structTypeId valueTypeId
		newTypes.push_back(spvInstWord(3, spvOpTypeStruct));
		newTypes.push_back(structTypeId);
		newTypes.push_back(valueTypeId);
		// OpTypePointer ptrUniformStructId Uniform structTypeId
		newTypes.push_back(spvInstWord(4, spvOpTypePointer));
		newTypes.push_back(ptrUniformStructId);
		newTypes.push_back(spvStorageClassUniform);
		newTypes.push_back(structTypeId);
		// OpTypePointer ptrUniformValueId Uniform valueTypeId
		newTypes.push_back(spvInstWord(4, spvOpTypePointer));
		newTypes.push_back(ptrUniformValueId);
		newTypes.push_back(spvStorageClassUniform);
		newTypes.push_back(valueTypeId);
		output.insert(output.begin() + typeInsertPos, newTypes.begin(), newTypes.end());

		// Fix the OpVariable to use the new struct pointer type
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable && wc2 >= 4 && output[pos + 2] == v.id)
				output[pos + 1] = ptrUniformStructId;
			pos += wc2;
		}

		// Replace every OpLoad from this variable with OpAccessChain + OpLoad
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if ((op2 == spvOpAccessChain || op2 == spvOpInBoundsAccessChain) && wc2 >= 5 && output[pos + 3] == v.id) {
				output[pos] = spvInstWord(wc2 + 1, op2);
				output.insert(output.begin() + pos + 4, uniformIndexId);
				pos += wc2 + 1;
				continue;
			}
			if (op2 == spvOpLoad && wc2 >= 4 && output[pos + 3] == v.id) {
				// Original: OpLoad %float %result %var [Aligned N]
				// Replace with: OpAccessChain %ptrUniformFloat %acId %var %uint0
				//               OpLoad %float %result %acId
				uint32_t loadResultType = output[pos + 1];
				uint32_t loadResultId = output[pos + 2];
				uint32_t accessChainId = allocateSpvId(output);
				std::vector<uint32_t> replacement;
				// OpAccessChain
				replacement.push_back(spvInstWord(5, spvOpAccessChain));
				replacement.push_back(ptrUniformValueId);
				replacement.push_back(accessChainId);
				replacement.push_back(v.id);
				replacement.push_back(uniformIndexId);
				// OpLoad (without Aligned)
				replacement.push_back(spvInstWord(4, spvOpLoad));
				replacement.push_back(loadResultType);
				replacement.push_back(loadResultId);
				replacement.push_back(accessChainId);

				output.erase(output.begin() + pos, output.begin() + pos + wc2);
				output.insert(output.begin() + pos, replacement.begin(), replacement.end());
				pos += replacement.size();
				continue;
			}
			pos += wc2;
		}
	}

	patchShaderAccessChainStorage(output);
	preserveShaderIntegerSemantics(output);

	// Add decorations for I/O variables
	std::vector<uint32_t> newDecors;
	std::unordered_set<uint32_t> decoratedUniformStorage;
	for (const auto &v : vars) {
		if (v.isResource) {
			newDecors.insert(
				newDecors.end(), {spvInstWord(4, spvOpDecorate), v.id, spvDecorationBinding, v.binding,
								  spvInstWord(4, spvOpDecorate), v.id, spvDecorationDescriptorSet, 0}
			);
			continue;
		}
		if (v.isUBO) {
			if (v.uniformStorageType && decoratedUniformStorage.insert(v.uniformStorageType).second) {
				const uint32_t step = v.uniformColumns < 4 ? 2 : 1;
				for (uint32_t row = 0; row < v.uniformRows; ++row) {
					newDecors.insert(
						newDecors.end(),
						{spvInstWord(5, spvOpMemberDecorate), v.uniformStorageType, row * step, spvDecorationOffset, row * 16}
					);
					if (step == 2)
						newDecors.insert(
							newDecors.end(), {spvInstWord(5, spvOpMemberDecorate), v.uniformStorageType, row * step + 1,
											  spvDecorationOffset, row * 16 + v.uniformColumns * 4}
						);
				}
			}
			// Block decoration on the struct type
			newDecors.push_back(spvInstWord(3, spvOpDecorate));
			newDecors.push_back(v.structTypeId);
			newDecors.push_back(spvDecorationBlock);
			// MemberDecorate Offset 0 on member 0
			newDecors.push_back(spvInstWord(5, spvOpMemberDecorate));
			newDecors.push_back(v.structTypeId);
			newDecors.push_back(0); // member index
			newDecors.push_back(spvDecorationOffset);
			newDecors.push_back(0); // offset
			// Binding decoration on the variable
			newDecors.push_back(spvInstWord(4, spvOpDecorate));
			newDecors.push_back(v.id);
			newDecors.push_back(spvDecorationBinding);
			newDecors.push_back(v.binding);
			// DescriptorSet 0
			newDecors.push_back(spvInstWord(4, spvOpDecorate));
			newDecors.push_back(v.id);
			newDecors.push_back(spvDecorationDescriptorSet);
			newDecors.push_back(0);
		} else if (v.isBuiltIn) {
			newDecors.push_back(spvInstWord(4, spvOpDecorate));
			newDecors.push_back(v.id);
			newDecors.push_back(spvDecorationBuiltIn);
			newDecors.push_back(v.decorationValue);
		} else {
			newDecors.push_back(spvInstWord(4, spvOpDecorate));
			newDecors.push_back(v.id);
			newDecors.push_back(spvDecorationLocation);
			newDecors.push_back(v.decorationValue);
		}
	}
	if (!newDecors.empty())
		output.insert(output.begin() + decorInsertPos, newDecors.begin(), newDecors.end());

	// Write patched binary
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		errorMsg = "Cannot write patched SPIR-V file: " + path;
		return false;
	}
	out.write(reinterpret_cast<const char *>(output.data()), output.size() * 4);
	return true;
}

std::unique_ptr<llvm::TargetMachine> createSPIRVTargetMachine(ParseContext &context, std::string &errorMessage) {
	LLVMInitializeSPIRVTarget();
	LLVMInitializeSPIRVTargetInfo();
	LLVMInitializeSPIRVTargetMC();
	LLVMInitializeSPIRVAsmPrinter();

	llvm::Triple targetTriple("spirv-unknown-vulkan1.3");
	context.llvmModule->setTargetTriple(targetTriple);

	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, errorMessage);
	if (!target) {
		return nullptr;
	}

	return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
		targetTriple, "", "", llvmTargetOptions(context.options), std::nullopt, std::nullopt, llvm::CodeGenOptLevel::None
	));
}

static bool collectShaderVertexInputs(ParseContext &context, std::vector<ParseContext::ShaderVertexInput> &inputs) {
	for (const auto &input : context.shaderVertexInputs) {
		bool duplicate = false;
		for (const auto &previous : inputs) {
			if (previous.name == input.name) {
				if (previous.location != input.location || previous.type != input.type) {
					context.addDiagnostic(Diagnostic(
						context, Diagnostic::Level::Error, "shader vertex input inconsistent", input.range, "name", input.name
					));
					return false;
				}
				duplicate = true;
				break;
			}
			if (previous.location == input.location) {
				context.addDiagnostic(Diagnostic(
					context, Diagnostic::Level::Error, "shader vertex input duplicate", input.range, "location",
					std::to_string(input.location), "first", previous.name, "second", input.name
				));
				return false;
			}
		}
		if (!duplicate)
			inputs.push_back(input);
	}
	std::sort(inputs.begin(), inputs.end(), [](const auto &left, const auto &right) {
		return left.location < right.location;
	});
	return true;
}

bool emitSPIRVModule(ParseContext &context) {
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty())
		outputPath = context.options.inputPath + ".spv";
	auto shaderUniforms = context.shaderUniforms;
	std::sort(shaderUniforms.begin(), shaderUniforms.end(), [](const auto &left, const auto &right) {
		return left.binding < right.binding;
	});
	std::vector<ParseContext::ShaderVertexInput> vertexInputs;
	if (!collectShaderVertexInputs(context, vertexInputs))
		return false;

	requireCompilerInvariant(context.targetMachine != nullptr, "SPIR-V emission requires an initialized target machine");
	llvm::TargetMachine &targetMachine = *context.targetMachine;
	defineShaderOutputVectorSeeds(*context.llvmModule);
	lowerShaderAddressIndices(*context.llvmModule);

	std::error_code ec;
	llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv output open failed", Range(), "error", ec.message())
		);
		return false;
	}

	llvm::legacy::PassManager passManager;
	if (targetMachine.addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv target cannot emit object file", Range())
		);
		return false;
	}

	passManager.run(*context.llvmModule);
	dest.flush();
	dest.close();

	// Build I/O variable descriptors based on shader stage
	bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;
	bool isCompute = context.options.shaderStage == ParseContext::ShaderStage::Compute;
	uint32_t executionModel = isCompute ? 5 : (isVertex ? 0 : 4); // Vertex=0, Fragment=4, GLCompute=5

	auto makeIoVar = [](const std::string &name, uint32_t sc, bool builtIn, uint32_t decVal) {
		ShaderIoVar v;
		v.name = name;
		v.storageClass = sc;
		v.isBuiltIn = builtIn;
		v.decorationValue = decVal;
		return v;
	};

	std::vector<ShaderIoVar> ioVars;
	if (isVertex) {
		for (const auto &input : vertexInputs) {
			auto variable = makeIoVar(
				shaderVertexInputGlobalName(input.name, input.location, input.type), spvStorageClassInput, false, input.location
			);
			DataType scalar = input.type.kind == DataType::Kind::Vector ? input.type.aggregateElementType() : input.type;
			if (scalar.kind == DataType::Kind::Int)
				variable.signedComponents = input.type.kind == DataType::Kind::Vector ? input.type.arraySize : 1;
			ioVars.push_back(std::move(variable));
		}
		ioVars.push_back(makeIoVar("gl_Position", spvStorageClassOutput, true, 0));
	} else if (!isCompute) {
		ioVars.push_back(makeIoVar("gl_FragCoord", spvStorageClassInput, true, 15));
		ioVars.push_back(makeIoVar("gl_FragColor", spvStorageClassOutput, false, 0));
	}

	std::vector<std::string> orderedInterpolantNames = context.shaderInterpolantNames;
	std::sort(orderedInterpolantNames.begin(), orderedInterpolantNames.end());
	for (size_t location = 0; location < orderedInterpolantNames.size(); location++) {
		ioVars.push_back(makeIoVar(
			shaderInterpolantGlobalName(orderedInterpolantNames[location]),
			isVertex ? spvStorageClassOutput : spvStorageClassInput, false, static_cast<uint32_t>(location)
		));
	}

	// Store shader parameters in descriptor-set-zero uniform buffers shared by Vulkan and WebGPU.
	for (const ParseContext::ShaderUniform &uniform : shaderUniforms) {
		ShaderIoVar uboVar;
		uboVar.name = "ubo_" + uniform.name;
		uboVar.storageClass = spvStorageClassUniform;
		uboVar.isBuiltIn = false;
		uboVar.decorationValue = 0;
		uboVar.isUBO = true;
		uboVar.binding = uniform.binding;
		auto layout = shaderUniformLayout(uniform.type);
		requireCompilerInvariant(layout.has_value(), "uniform codegen lost its validated layout");
		uboVar.uniformRows = layout->rows;
		uboVar.uniformColumns = layout->columns;
		if (uniform.type.kind == DataType::Kind::Int ||
			(uniform.type.kind == DataType::Kind::Vector && uniform.type.aggregateElementType().kind == DataType::Kind::Int))
			uboVar.signedComponents = layout->columns;
		ioVars.push_back(uboVar);
	}

	std::string patchError;
	for (const auto &resource : context.shaderResources) {
		auto variable = makeIoVar(shaderResourceGlobalName(resource.name), 0, false, 0);
		variable.isResource = true;
		variable.binding = resource.binding;
		ioVars.push_back(std::move(variable));
	}
	if (!patchShaderBinary(outputPath, executionModel, ioVars, context, patchError)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv patch failed", Range(), "error", patchError)
		);
		return false;
	}

	return true;
}
