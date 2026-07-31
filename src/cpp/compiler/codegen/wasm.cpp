#include "wasm.h"
#include "targetOptions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <fstream>
#include <vector>

namespace {

struct WasmSectionInfo {
	uint8_t id = 0;
	size_t sectionOffset = 0;
	size_t payloadOffset = 0;
	size_t payloadSize = 0;
	size_t sectionEnd = 0;
};

uint32_t readULEB(const std::vector<uint8_t> &bytes, size_t &offset, const char *what, std::string &errorMessage) {
	uint32_t value = 0;
	unsigned shift = 0;
	while (offset < bytes.size()) {
		uint8_t byte = bytes[offset++];
		value |= static_cast<uint32_t>(byte & 0x7F) << shift;
		if ((byte & 0x80) == 0)
			return value;
		shift += 7;
		if (shift >= 32) {
			errorMessage = std::string("WASM ") + what + " uses an unsupported LEB128 width";
			return 0;
		}
	}
	errorMessage = std::string("Unexpected end of WASM while reading ") + what;
	return 0;
}

bool skipString(const std::vector<uint8_t> &bytes, size_t &offset, const char *what, std::string &errorMessage) {
	uint32_t size = readULEB(bytes, offset, what, errorMessage);
	if (!errorMessage.empty())
		return false;
	if (offset + size > bytes.size()) {
		errorMessage = std::string("Unexpected end of WASM while reading ") + what;
		return false;
	}
	offset += size;
	return true;
}

bool readString(
	std::string &out, const std::vector<uint8_t> &bytes, size_t &offset, const char *what, std::string &errorMessage
) {
	uint32_t size = readULEB(bytes, offset, what, errorMessage);
	if (!errorMessage.empty())
		return false;
	if (offset + size > bytes.size()) {
		errorMessage = std::string("Unexpected end of WASM while reading ") + what;
		return false;
	}
	out.assign(reinterpret_cast<const char *>(bytes.data() + offset), size);
	offset += size;
	return true;
}

std::vector<uint8_t> encodeULEB(uint32_t value) {
	std::vector<uint8_t> encoded;
	do {
		uint8_t byte = value & 0x7F;
		value >>= 7;
		if (value != 0)
			byte |= 0x80;
		encoded.push_back(byte);
	} while (value != 0);
	return encoded;
}

void appendULEB(std::vector<uint8_t> &out, uint32_t value) {
	std::vector<uint8_t> encoded = encodeULEB(value);
	out.insert(out.end(), encoded.begin(), encoded.end());
}

void appendName(std::vector<uint8_t> &out, std::string_view name) {
	appendULEB(out, static_cast<uint32_t>(name.size()));
	out.insert(out.end(), name.begin(), name.end());
}

bool collectSections(const std::vector<uint8_t> &bytes, std::vector<WasmSectionInfo> &sections, std::string &errorMessage) {
	if (bytes.size() < 8 || bytes[0] != 0x00 || bytes[1] != 0x61 || bytes[2] != 0x73 || bytes[3] != 0x6D || bytes[4] != 0x01 ||
		bytes[5] != 0x00 || bytes[6] != 0x00 || bytes[7] != 0x00) {
		errorMessage = "Invalid WASM header";
		return false;
	}

	size_t offset = 8;
	while (offset < bytes.size()) {
		WasmSectionInfo section;
		section.sectionOffset = offset;
		section.id = bytes[offset++];
		uint32_t size = readULEB(bytes, offset, "section size", errorMessage);
		if (!errorMessage.empty())
			return false;
		section.payloadOffset = offset;
		section.payloadSize = size;
		section.sectionEnd = offset + size;
		if (section.sectionEnd > bytes.size()) {
			errorMessage = "WASM section exceeds file size";
			return false;
		}
		sections.push_back(section);
		offset = section.sectionEnd;
	}
	return true;
}

bool countImportedFunctions(
	const std::vector<uint8_t> &bytes, const WasmSectionInfo &section, uint32_t &outCount, std::string &errorMessage
) {
	size_t offset = section.payloadOffset;
	size_t end = section.sectionEnd;
	uint32_t importCount = readULEB(bytes, offset, "import count", errorMessage);
	if (!errorMessage.empty())
		return false;

	outCount = 0;
	for (uint32_t importIndex = 0; importIndex < importCount; ++importIndex) {
		if (!skipString(bytes, offset, "import module name", errorMessage) ||
			!skipString(bytes, offset, "import field name", errorMessage)) {
			return false;
		}
		if (offset >= end) {
			errorMessage = "Unexpected end of WASM import section";
			return false;
		}
		uint8_t kind = bytes[offset++];
		switch (kind) {
		case 0x00:
			(void)readULEB(bytes, offset, "imported function type", errorMessage);
			if (!errorMessage.empty())
				return false;
			outCount++;
			break;
		case 0x01: {
			if (offset >= end) {
				errorMessage = "Unexpected end of WASM table import";
				return false;
			}
			offset++; // reference type
			uint32_t limitsFlags = readULEB(bytes, offset, "table limits flags", errorMessage);
			if (!errorMessage.empty())
				return false;
			(void)readULEB(bytes, offset, "table minimum", errorMessage);
			if (!errorMessage.empty())
				return false;
			if ((limitsFlags & 0x01u) != 0) {
				(void)readULEB(bytes, offset, "table maximum", errorMessage);
				if (!errorMessage.empty())
					return false;
			}
			break;
		}
		case 0x02: {
			uint32_t limitsFlags = readULEB(bytes, offset, "memory limits flags", errorMessage);
			if (!errorMessage.empty())
				return false;
			(void)readULEB(bytes, offset, "memory minimum", errorMessage);
			if (!errorMessage.empty())
				return false;
			if ((limitsFlags & 0x01u) != 0) {
				(void)readULEB(bytes, offset, "memory maximum", errorMessage);
				if (!errorMessage.empty())
					return false;
			}
			break;
		}
		case 0x03:
			if (offset + 2 > end) {
				errorMessage = "Unexpected end of WASM global import";
				return false;
			}
			offset += 2; // value type + mutability
			break;
		case 0x04:
			if (offset >= end) {
				errorMessage = "Unexpected end of WASM tag import";
				return false;
			}
			offset++; // attribute
			(void)readULEB(bytes, offset, "tag type index", errorMessage);
			if (!errorMessage.empty())
				return false;
			break;
		default:
			errorMessage = "Unsupported WASM import kind";
			return false;
		}
	}

	if (offset != end) {
		errorMessage = "WASM import section parse did not consume the full payload";
		return false;
	}
	return true;
}

bool readFunctionCount(
	const std::vector<uint8_t> &bytes, const WasmSectionInfo &section, uint32_t &outCount, std::string &errorMessage
) {
	size_t offset = section.payloadOffset;
	outCount = readULEB(bytes, offset, "function count", errorMessage);
	if (!errorMessage.empty())
		return false;
	if (offset > section.sectionEnd || offset + outCount < offset) {
		errorMessage = "Invalid WASM function section";
		return false;
	}
	for (uint32_t i = 0; i < outCount; ++i) {
		(void)readULEB(bytes, offset, "function type index", errorMessage);
		if (!errorMessage.empty())
			return false;
	}
	if (offset != section.sectionEnd) {
		errorMessage = "WASM function section parse did not consume the full payload";
		return false;
	}
	return true;
}

bool parseExportSection(
	const std::vector<uint8_t> &bytes, const WasmSectionInfo &section, bool &hasMainExport,
	std::vector<uint8_t> &existingEntries, uint32_t &existingCount, std::string &errorMessage
) {
	size_t offset = section.payloadOffset;
	existingCount = readULEB(bytes, offset, "export count", errorMessage);
	if (!errorMessage.empty())
		return false;
	existingEntries.assign(bytes.begin() + offset, bytes.begin() + section.sectionEnd);
	hasMainExport = false;

	size_t parseOffset = offset;
	for (uint32_t i = 0; i < existingCount; ++i) {
		uint32_t nameLength = readULEB(bytes, parseOffset, "export name length", errorMessage);
		if (!errorMessage.empty())
			return false;
		if (parseOffset + nameLength > section.sectionEnd) {
			errorMessage = "Unexpected end of WASM export name";
			return false;
		}
		std::string_view name(reinterpret_cast<const char *>(bytes.data() + parseOffset), nameLength);
		parseOffset += nameLength;
		if (parseOffset >= section.sectionEnd) {
			errorMessage = "Unexpected end of WASM export entry";
			return false;
		}
		uint8_t kind = bytes[parseOffset++];
		(void)readULEB(bytes, parseOffset, "export index", errorMessage);
		if (!errorMessage.empty())
			return false;
		if (kind == 0x00 && name == "main")
			hasMainExport = true;
	}

	if (parseOffset != section.sectionEnd) {
		errorMessage = "WASM export section parse did not consume the full payload";
		return false;
	}
	return true;
}

bool shouldStripCustomSection(const std::vector<uint8_t> &bytes, const WasmSectionInfo &section, std::string &errorMessage) {
	if (section.id != 0x00)
		return false;
	size_t offset = section.payloadOffset;
	std::string sectionName;
	if (!readString(sectionName, bytes, offset, "custom section name", errorMessage))
		return false;
	return sectionName == "linking" || sectionName.starts_with("reloc.");
}

bool patchMainExport(const std::string &path, std::string &errorMessage) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		errorMessage = "Cannot open WASM file for patching: " + path;
		return false;
	}
	std::streamsize size = input.tellg();
	if (size < 0) {
		errorMessage = "Invalid WASM file size";
		return false;
	}
	input.seekg(0, std::ios::beg);
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	if (!bytes.empty() && !input.read(reinterpret_cast<char *>(bytes.data()), size)) {
		errorMessage = "Cannot read WASM file for patching: " + path;
		return false;
	}

	std::vector<WasmSectionInfo> sections;
	if (!collectSections(bytes, sections, errorMessage))
		return false;

	uint32_t importedFunctionCount = 0;
	uint32_t definedFunctionCount = 0;
	const WasmSectionInfo *exportSection = nullptr;
	size_t insertOffset = bytes.size();
	for (const WasmSectionInfo &section : sections) {
		if (section.id == 0x02) {
			if (!countImportedFunctions(bytes, section, importedFunctionCount, errorMessage))
				return false;
		} else if (section.id == 0x03) {
			if (!readFunctionCount(bytes, section, definedFunctionCount, errorMessage))
				return false;
		} else if (section.id == 0x07) {
			exportSection = &section;
		}

		if (section.id != 0x00 && section.id > 0x07 && insertOffset == bytes.size())
			insertOffset = section.sectionOffset;
	}

	if (definedFunctionCount == 0) {
		errorMessage = "Cannot export main from a WASM module without defined functions";
		return false;
	}
	uint32_t mainFunctionIndex = importedFunctionCount + definedFunctionCount - 1;

	std::vector<uint8_t> exportEntry;
	appendName(exportEntry, "main");
	exportEntry.push_back(0x00); // function export
	appendULEB(exportEntry, mainFunctionIndex);

	std::vector<uint8_t> newPayload;
	if (exportSection) {
		bool hasMainExport = false;
		std::vector<uint8_t> existingEntries;
		uint32_t existingCount = 0;
		if (!parseExportSection(bytes, *exportSection, hasMainExport, existingEntries, existingCount, errorMessage))
			return false;
		if (hasMainExport)
			return true;
		appendULEB(newPayload, existingCount + 1);
		newPayload.insert(newPayload.end(), existingEntries.begin(), existingEntries.end());
		newPayload.insert(newPayload.end(), exportEntry.begin(), exportEntry.end());
		std::vector<uint8_t> replacement;
		replacement.push_back(0x07);
		appendULEB(replacement, static_cast<uint32_t>(newPayload.size()));
		replacement.insert(replacement.end(), newPayload.begin(), newPayload.end());
		bytes.erase(bytes.begin() + exportSection->sectionOffset, bytes.begin() + exportSection->sectionEnd);
		bytes.insert(bytes.begin() + exportSection->sectionOffset, replacement.begin(), replacement.end());
	} else {
		if (insertOffset == bytes.size())
			insertOffset = bytes.size();
		appendULEB(newPayload, 1);
		newPayload.insert(newPayload.end(), exportEntry.begin(), exportEntry.end());
		std::vector<uint8_t> exportSectionBytes;
		exportSectionBytes.push_back(0x07);
		appendULEB(exportSectionBytes, static_cast<uint32_t>(newPayload.size()));
		exportSectionBytes.insert(exportSectionBytes.end(), newPayload.begin(), newPayload.end());
		bytes.insert(bytes.begin() + insertOffset, exportSectionBytes.begin(), exportSectionBytes.end());
	}

	std::vector<WasmSectionInfo> finalSections;
	errorMessage.clear();
	if (!collectSections(bytes, finalSections, errorMessage))
		return false;

	std::vector<uint8_t> stripped;
	stripped.insert(stripped.end(), bytes.begin(), bytes.begin() + 8);
	for (const WasmSectionInfo &section : finalSections) {
		if (shouldStripCustomSection(bytes, section, errorMessage))
			continue;
		if (!errorMessage.empty())
			return false;
		stripped.insert(stripped.end(), bytes.begin() + section.sectionOffset, bytes.begin() + section.sectionEnd);
	}
	bytes = std::move(stripped);

	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		errorMessage = "Cannot open WASM file for writing: " + path;
		return false;
	}
	output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!output.good()) {
		errorMessage = "Cannot write patched WASM file: " + path;
		return false;
	}
	return true;
}

} // namespace

std::unique_ptr<llvm::TargetMachine> createWASMTargetMachine(ParseContext &context, std::string &errorMessage) {
	LLVMInitializeWebAssemblyTarget();
	LLVMInitializeWebAssemblyTargetInfo();
	LLVMInitializeWebAssemblyTargetMC();
	LLVMInitializeWebAssemblyAsmPrinter();

	llvm::Triple targetTriple("wasm32-unknown-unknown");
	context.llvmModule->setTargetTriple(targetTriple);

	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, errorMessage);
	if (!target)
		return nullptr;

	return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
		targetTriple, "generic", "", llvmTargetOptions(context.options), llvm::Reloc::PIC_, std::nullopt,
		codeGenerationOptimizationLevel(context.options)
	));
}

bool emitWASMModule(ParseContext &context) {
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty())
		outputPath = context.options.inputPath + ".wasm";

	requireCompilerInvariant(context.targetMachine != nullptr, "WASM emission requires an initialized target machine");
	llvm::TargetMachine &targetMachine = *context.targetMachine;

	std::error_code ec;
	llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm output open failed", Range(), "error", ec.message())
		);
		return false;
	}

	llvm::legacy::PassManager passManager;
	if (targetMachine.addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm target cannot emit object file", Range())
		);
		return false;
	}

	passManager.run(*context.llvmModule);
	dest.flush();
	dest.close();

	std::string patchError;
	if (!patchMainExport(outputPath, patchError)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm patch failed", Range(), "error", patchError)
		);
		return false;
	}
	return true;
}
