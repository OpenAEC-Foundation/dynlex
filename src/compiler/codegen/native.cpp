#include "native.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <cstdlib>
#include <filesystem>

bool emitNativeExecutable(ParseContext &context) {
	// Initialize native target
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	std::string targetTriple = llvm::sys::getDefaultTargetTriple();
	context.llvmModule->setTargetTriple(targetTriple);

	// Find target
	std::string error;
	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
	if (!target) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Failed to get target: " + error, Range()));
		return false;
	}

	// Create target machine
	llvm::TargetOptions options;
	auto targetMachine = target->createTargetMachine(
		targetTriple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt,
		context.options.optimizationLevel >= 2 ? llvm::CodeGenOptLevel::Aggressive : llvm::CodeGenOptLevel::Default
	);

	if (!targetMachine) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "Failed to create target machine", Range()));
		return false;
	}

	context.llvmModule->setDataLayout(targetMachine->createDataLayout());

	// Determine output path
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty()) {
		// Remove .dl extension if present
		outputPath = context.options.inputPath;
		if (outputPath.ends_with(".dl")) {
			outputPath = outputPath.substr(0, outputPath.size() - 3);
		}
	}

	// Create object file path
	std::string objectPath = outputPath + ".o";

	// Emit object file
	{
		std::error_code ec;
		llvm::raw_fd_ostream dest(objectPath, ec, llvm::sys::fs::OF_None);
		if (ec) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Could not open object file: " + ec.message(), Range())
			);
			return false;
		}

		llvm::legacy::PassManager passManager;
		if (targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Target machine cannot emit object file", Range())
			);
			return false;
		}

		passManager.run(*context.llvmModule);
	}

	// Link object file to executable using system linker
	std::string linkCommand = "cc " + objectPath + " -o " + outputPath;

	// Add any required libraries
	for (const std::string &lib : context.requiredLibraries) {
		linkCommand += " -l" + lib;
	}

	int linkResult = std::system(linkCommand.c_str());
	if (linkResult != 0) {
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "Linking failed with exit code " + std::to_string(linkResult), Range())
		);
		return false;
	}

	// Clean up object file
	std::filesystem::remove(objectPath);

	return true;
}
