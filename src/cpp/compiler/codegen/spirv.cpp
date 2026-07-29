#include "spirv.h"
#include "compilerUtils.h"
#include "parseContext.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
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
#include <unordered_set>
#include <vector>

// SPIR-V opcodes
static constexpr uint32_t spvOpCapability = 17;
static constexpr uint32_t spvOpEntryPoint = 15;
static constexpr uint32_t spvOpMemoryModel = 14;
static constexpr uint32_t spvOpDecorate = 71;
static constexpr uint32_t spvOpName = 5;
static constexpr uint32_t spvOpVariable = 59;
static constexpr uint32_t spvOpTypeInt = 21;
static constexpr uint32_t spvOpTypePointer = 32;
static constexpr uint32_t spvOpTypeStruct = 30;
static constexpr uint32_t spvOpConstant = 43;
static constexpr uint32_t spvOpLoad = 61;
static constexpr uint32_t spvOpStore = 62;
static constexpr uint32_t spvOpAccessChain = 65;

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

std::string shaderInterpolantGlobalName(std::string_view interpolantName) {
	static constexpr char hexadecimalDigits[] = "0123456789abcdef";
	std::string globalName = "dynlex_interpolant_";
	globalName.reserve(globalName.size() + interpolantName.size() * 2);
	for (unsigned char byte : interpolantName) {
		globalName.push_back(hexadecimalDigits[byte >> 4]);
		globalName.push_back(hexadecimalDigits[byte & 0x0f]);
	}
	return globalName;
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

static size_t defineShaderOutputVectorSeeds(llvm::Use &use, llvm::Type *vectorType) {
	llvm::Value *value = use.get();
	if (auto *constant = llvm::dyn_cast<llvm::Constant>(value)) {
		if (constant->getType() != vectorType)
			return 0;
		use.set(defineVectorConstant(constant));
		return 1;
	}

	auto *instruction = llvm::dyn_cast<llvm::Instruction>(value);
	if (!instruction || instruction->getType() != vectorType)
		return 0;

	size_t seedCount = 0;
	for (llvm::Use &operand : instruction->operands())
		seedCount += defineShaderOutputVectorSeeds(operand, vectorType);
	return seedCount;
}

static void defineShaderOutputVectorSeeds(llvm::Module &module) {
	for (llvm::Function &function : module) {
		for (llvm::BasicBlock &block : function) {
			for (llvm::Instruction &instruction : block) {
				auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
				if (!store || !store->getMetadata(shaderOutputMetadataName))
					continue;

				llvm::Type *vectorType = store->getValueOperand()->getType();
				size_t seedCount = defineShaderOutputVectorSeeds(store->getOperandUse(0), vectorType);
				requireCompilerInvariant(seedCount > 0, "shader output vector has no constant seed");
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
	for (size_t position = 5; position < binary.size(); position = nextSpvInstruction(binary, position)) {
		uint32_t wordCount = spvWordCount(binary[position]);
		if (spvOpcode(binary[position]) == spvOpTypeInt && wordCount == 4 && binary[position + 2] == 32 &&
			binary[position + 3] == 0) {
			unsignedTypeId = binary[position + 1];
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
	binary.insert(binary.begin() + findFirstSpvVariable(binary), declarations.begin(), declarations.end());
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
	uint32_t binding = 0;	   // UBO binding point
};

// Post-process SPIR-V binary to convert exported functions to shader entry points.
// The LLVM SPIR-V backend emits functions with Export linkage instead of entry points.
static bool patchShaderBinary(
	const std::string &path, uint32_t executionModel, const std::vector<ShaderIoVar> &ioVars, std::string &errorMsg
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

	// First pass: collect all OpVariable result IDs so we only match names against actual variables
	std::unordered_set<uint32_t> variableIds;
	size_t pos = 5;
	while (pos < binary.size()) {
		uint32_t wc = spvWordCount(binary[pos]);
		uint32_t op = spvOpcode(binary[pos]);
		if (wc == 0)
			break;
		if (op == spvOpVariable && wc >= 4)
			variableIds.insert(binary[pos + 2]); // result ID
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
			uint32_t entryWc = 3 + nameWords.size() + vars.size();
			output.push_back(spvInstWord(entryWc, spvOpEntryPoint));
			output.push_back(executionModel);
			output.push_back(mainId);
			output.insert(output.end(), nameWords.begin(), nameWords.end());
			for (const auto &v : vars)
				output.push_back(v.id);
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

	// For UBO vars, wrap the scalar type in a struct and fix all references.
	// The codegen emits a plain float global; we transform it into:
	//   %StructType = OpTypeStruct %float
	//   %PtrUniformStruct = OpTypePointer Uniform %StructType
	//   %PtrUniformFloat = OpTypePointer Uniform %float
	//   %uint_0 = OpConstant %uint 0
	//   %var = OpVariable %PtrUniformStruct Uniform
	//   ...
	//   %ptr = OpAccessChain %PtrUniformFloat %var %uint_0
	//   %val = OpLoad %float %ptr
	const bool hasUniform = std::any_of(vars.begin(), vars.end(), [](const ShaderIoVar &variable) {
		return variable.isUBO;
	});
	const uint32_t uniformIndexId = hasUniform ? getOrCreateSpvUnsignedZero(output) : 0;
	for (auto &v : vars) {
		if (!v.isUBO)
			continue;

		// Find the old pointer type and float type from the variable
		uint32_t oldPtrTypeId = 0;
		uint32_t floatTypeId = 0;
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable && wc2 >= 4 && output[pos + 2] == v.id)
				oldPtrTypeId = output[pos + 1];
			pos += wc2;
		}
		// Get the float type from the old pointer type
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpTypePointer && wc2 >= 4 && output[pos + 1] == oldPtrTypeId)
				floatTypeId = output[pos + 3];
			pos += wc2;
		}

		// Allocate new IDs
		uint32_t structTypeId = allocateSpvId(output);
		uint32_t ptrUniformStructId = allocateSpvId(output);
		uint32_t ptrUniformFloatId = allocateSpvId(output);
		v.structTypeId = structTypeId;

		// Find where to insert new types (before first OpVariable)
		size_t typeInsertPos = output.size();
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable) {
				typeInsertPos = pos;
				break;
			}
			pos += wc2;
		}

		// Insert new type declarations
		std::vector<uint32_t> newTypes;
		// OpTypeStruct structTypeId floatTypeId
		newTypes.push_back(spvInstWord(3, spvOpTypeStruct));
		newTypes.push_back(structTypeId);
		newTypes.push_back(floatTypeId);
		// OpTypePointer ptrUniformStructId Uniform structTypeId
		newTypes.push_back(spvInstWord(4, spvOpTypePointer));
		newTypes.push_back(ptrUniformStructId);
		newTypes.push_back(spvStorageClassUniform);
		newTypes.push_back(structTypeId);
		// OpTypePointer ptrUniformFloatId Uniform floatTypeId
		newTypes.push_back(spvInstWord(4, spvOpTypePointer));
		newTypes.push_back(ptrUniformFloatId);
		newTypes.push_back(spvStorageClassUniform);
		newTypes.push_back(floatTypeId);
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
				replacement.push_back(ptrUniformFloatId);
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

	// Add decorations for I/O variables
	std::vector<uint32_t> newDecors;
	for (const auto &v : vars) {
		if (v.isUBO) {
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

	llvm::TargetOptions options;
	return std::unique_ptr<llvm::TargetMachine>(
		target->createTargetMachine(targetTriple, "", "", options, std::nullopt, std::nullopt, llvm::CodeGenOptLevel::None)
	);
}

bool emitSPIRVModule(ParseContext &context) {
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty())
		outputPath = context.options.inputPath + ".spv";

	std::string error;
	std::unique_ptr<llvm::TargetMachine> targetMachine = createSPIRVTargetMachine(context, error);
	if (!targetMachine) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv target not available", Range(), "error", error)
		);
		return false;
	}
	context.llvmModule->setDataLayout(targetMachine->createDataLayout());
	defineShaderOutputVectorSeeds(*context.llvmModule);

	std::error_code ec;
	llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv output open failed", Range(), "error", ec.message())
		);
		return false;
	}

	llvm::legacy::PassManager passManager;
	if (targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
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
	uint32_t executionModel = isVertex ? 0 : 4; // Vertex=0, Fragment=4

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
		ioVars.push_back(makeIoVar("in_Position", spvStorageClassInput, false, 0));
		ioVars.push_back(makeIoVar("gl_Position", spvStorageClassOutput, true, 0));
	} else {
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

	// Add uniform variables as UBOs (Uniform storage class with Block decoration)
	// SPIR-V shaders in OpenGL require buffer-backed uniforms, not UniformConstant
	uint32_t nextBinding = 0;
	std::vector<std::string> orderedUniformNames = context.shaderUniformNames;
	std::stable_sort(
		orderedUniformNames.begin(), orderedUniformNames.end(),
		[&](const std::string &left, const std::string &right) {
		auto leftIt = context.shaderUniformSourceOrder.find(left);
		auto rightIt = context.shaderUniformSourceOrder.find(right);
		bool leftHasSource = leftIt != context.shaderUniformSourceOrder.end();
		bool rightHasSource = rightIt != context.shaderUniformSourceOrder.end();
		if (leftHasSource != rightHasSource)
			return leftHasSource;
		if (!leftHasSource)
			return false;
		return std::tie(leftIt->second.mergedLineIndex, leftIt->second.column) <
			   std::tie(rightIt->second.mergedLineIndex, rightIt->second.column);
	}
	);
	for (const auto &uniformName : orderedUniformNames) {
		std::string globalName = "ubo_" + uniformName;
		ShaderIoVar uboVar;
		uboVar.name = globalName;
		uboVar.storageClass = spvStorageClassUniform;
		uboVar.isBuiltIn = false;
		uboVar.decorationValue = 0;
		uboVar.isUBO = true;
		uboVar.binding = nextBinding++;
		ioVars.push_back(uboVar);
	}

	std::string patchError;
	if (!patchShaderBinary(outputPath, executionModel, ioVars, patchError)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "spirv patch failed", Range(), "error", patchError)
		);
		return false;
	}

	return true;
}
