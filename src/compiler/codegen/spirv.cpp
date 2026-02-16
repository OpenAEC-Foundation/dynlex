#include "spirv.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstring>
#include <fstream>
#include <vector>

// SPIR-V opcodes
static constexpr uint32_t spvOpCapability = 17;
static constexpr uint32_t spvOpEntryPoint = 15;
static constexpr uint32_t spvOpMemoryModel = 14;
static constexpr uint32_t spvOpDecorate = 71;
static constexpr uint32_t spvOpName = 5;
static constexpr uint32_t spvOpVariable = 59;
static constexpr uint32_t spvOpTypePointer = 32;

// SPIR-V constants
static constexpr uint32_t spvCapabilityLinkage = 5;
static constexpr uint32_t spvDecorationLinkageAttributes = 41;
static constexpr uint32_t spvDecorationBuiltIn = 11;
static constexpr uint32_t spvDecorationLocation = 30;
static constexpr uint32_t spvStorageClassInput = 1;
static constexpr uint32_t spvStorageClassOutput = 3;

// ExecutionModel: Vertex=0, Fragment=4
// BuiltIn: Position=0, FragCoord=15

static uint32_t spvOpcode(uint32_t word) { return word & 0xFFFF; }
static uint32_t spvWordCount(uint32_t word) { return word >> 16; }
static uint32_t spvInstWord(uint32_t wordCount, uint32_t opcode) { return (wordCount << 16) | opcode; }

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
	std::string name;		  // LLVM global name (e.g. "gl_FragCoord", "in_Position")
	uint32_t id = 0;		  // SPIR-V result ID (found by scanning OpName)
	uint32_t typeId = 0;	  // pointer type ID used by OpVariable
	uint32_t storageClass;	  // target storage class (Input or Output)
	bool isBuiltIn;			  // true = BuiltIn decoration, false = Location decoration
	uint32_t decorationValue; // BuiltIn ID or Location number
};

// Post-process SPIR-V binary to convert exported functions to shader entry points.
// The LLVM 20 SPIR-V backend emits functions with Export linkage instead of entry points.
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

	// Find IDs by scanning OpName instructions
	uint32_t mainId = 0;
	std::vector<ShaderIoVar> vars = ioVars; // mutable copy to fill in IDs

	size_t pos = 5;
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
			for (auto &v : vars) {
				if (name == v.name)
					v.id = id;
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

		// Insert OpEntryPoint before the first non-header, non-debug instruction
		if (!entryPointInserted && op != spvOpCapability && op != spvOpMemoryModel && op != spvOpName && op != spvOpDecorate) {
			std::vector<uint32_t> nameWords = encodeSpvString("main");
			uint32_t entryWc = 3 + nameWords.size() + vars.size();
			output.push_back(spvInstWord(entryWc, spvOpEntryPoint));
			output.push_back(executionModel);
			output.push_back(mainId);
			output.insert(output.end(), nameWords.begin(), nameWords.end());
			for (const auto &v : vars)
				output.push_back(v.id);
			entryPointInserted = true;
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
			if (!isIoVar)
				output.insert(output.end(), binary.begin() + pos, binary.begin() + pos + wc);
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

	// Add decorations for I/O variables
	std::vector<uint32_t> newDecors;
	for (const auto &v : vars) {
		if (v.isBuiltIn) {
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

	// Fix OpTypePointer storage classes for I/O globals
	// First, find which type IDs the globals use
	for (auto &v : vars) {
		for (pos = 5; pos < output.size();) {
			uint32_t wc2 = spvWordCount(output[pos]);
			uint32_t op2 = spvOpcode(output[pos]);
			if (wc2 == 0)
				break;
			if (op2 == spvOpVariable && wc2 >= 4 && output[pos + 2] == v.id)
				v.typeId = output[pos + 1];
			pos += wc2;
		}
	}

	// Check if any two I/O vars share a pointer type with different storage classes
	// Group vars by typeId
	std::unordered_map<uint32_t, std::vector<ShaderIoVar *>> typeGroups;
	for (auto &v : vars)
		typeGroups[v.typeId].push_back(&v);

	for (auto &[typeId, group] : typeGroups) {
		if (group.size() == 1) {
			// Only one var uses this type — just fix the storage class in-place
			for (pos = 5; pos < output.size();) {
				uint32_t wc2 = spvWordCount(output[pos]);
				uint32_t op2 = spvOpcode(output[pos]);
				if (wc2 == 0)
					break;
				if (op2 == spvOpTypePointer && wc2 >= 4 && output[pos + 1] == typeId)
					output[pos + 2] = group[0]->storageClass;
				pos += wc2;
			}
		} else {
			// Multiple vars share a pointer type — fix the first, create new types for the rest
			// Find the pointee type
			uint32_t pointeeTypeId = 0;
			for (pos = 5; pos < output.size();) {
				uint32_t wc2 = spvWordCount(output[pos]);
				uint32_t op2 = spvOpcode(output[pos]);
				if (wc2 == 0)
					break;
				if (op2 == spvOpTypePointer && wc2 >= 4 && output[pos + 1] == typeId) {
					pointeeTypeId = output[pos + 3];
					output[pos + 2] = group[0]->storageClass; // fix first var's storage class
					break;
				}
				pos += wc2;
			}

			// Create new pointer types for remaining vars
			size_t varInsertPos = output.size();
			for (pos = 5; pos < output.size();) {
				uint32_t wc2 = spvWordCount(output[pos]);
				uint32_t op2 = spvOpcode(output[pos]);
				if (wc2 == 0)
					break;
				if (op2 == spvOpVariable) {
					varInsertPos = pos;
					break;
				}
				pos += wc2;
			}

			for (size_t i = 1; i < group.size(); i++) {
				uint32_t newTypeId = output[3]; // current bound
				output[3] = newTypeId + 1;

				std::vector<uint32_t> newPtrType = {
					spvInstWord(4, spvOpTypePointer), newTypeId, group[i]->storageClass, pointeeTypeId
				};
				output.insert(output.begin() + varInsertPos, newPtrType.begin(), newPtrType.end());
				varInsertPos += newPtrType.size();

				// Fix the OpVariable to use the new type
				for (pos = 5; pos < output.size();) {
					uint32_t wc2 = spvWordCount(output[pos]);
					uint32_t op2 = spvOpcode(output[pos]);
					if (wc2 == 0)
						break;
					if (op2 == spvOpVariable && wc2 >= 4 && output[pos + 2] == group[i]->id)
						output[pos + 1] = newTypeId;
					pos += wc2;
				}
			}
		}
	}

	// Write patched binary
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		errorMsg = "Cannot write patched SPIR-V file: " + path;
		return false;
	}
	out.write(reinterpret_cast<const char *>(output.data()), output.size() * 4);
	return true;
}

bool emitSPIRVModule(ParseContext &context) {
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty())
		outputPath = context.options.inputPath + ".spv";

	LLVMInitializeSPIRVTarget();
	LLVMInitializeSPIRVTargetInfo();
	LLVMInitializeSPIRVTargetMC();
	LLVMInitializeSPIRVAsmPrinter();

	std::string targetTriple = "spirv-unknown-vulkan1.3";
	context.llvmModule->setTargetTriple(targetTriple);

	std::string error;
	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
	if (!target) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "SPIR-V target not available: " + error, Range()));
		return false;
	}

	llvm::TargetOptions options;
	auto targetMachine = target->createTargetMachine(targetTriple, "", "", options, std::nullopt);
	if (!targetMachine) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Failed to create SPIR-V target machine", Range()));
		return false;
	}

	context.llvmModule->setDataLayout(targetMachine->createDataLayout());

	std::error_code ec;
	llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "Could not open SPIR-V output file: " + ec.message(), Range())
		);
		return false;
	}

	llvm::legacy::PassManager passManager;
	if (targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "SPIR-V target cannot emit object file", Range()));
		return false;
	}

	passManager.run(*context.llvmModule);
	dest.flush();
	dest.close();

	// Build I/O variable descriptors based on shader stage
	bool isVertex = context.options.shaderStage == ParseContext::ShaderStage::Vertex;
	uint32_t executionModel = isVertex ? 0 : 4; // Vertex=0, Fragment=4

	std::vector<ShaderIoVar> ioVars;
	if (isVertex) {
		// Vertex: input at Location 0, output at BuiltIn Position
		ioVars.push_back({"in_Position", 0, 0, spvStorageClassInput, false, 0}); // Location 0
		ioVars.push_back({"gl_Position", 0, 0, spvStorageClassOutput, true, 0}); // BuiltIn Position(0)
	} else {
		// Fragment: input at BuiltIn FragCoord, output at Location 0
		ioVars.push_back({"gl_FragCoord", 0, 0, spvStorageClassInput, true, 15});  // BuiltIn FragCoord(15)
		ioVars.push_back({"gl_FragColor", 0, 0, spvStorageClassOutput, false, 0}); // Location 0
	}

	std::string patchError;
	if (!patchShaderBinary(outputPath, executionModel, ioVars, patchError)) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, patchError, Range()));
		return false;
	}

	return true;
}
