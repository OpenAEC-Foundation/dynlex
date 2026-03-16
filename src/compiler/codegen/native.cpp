#include "native.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
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
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "failed to get target", Range(), "error", error)
		);
		return false;
	}

	// Create target machine
	llvm::TargetOptions options;
	auto targetMachine = target->createTargetMachine(
		targetTriple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt,
		context.options.optimizationLevel >= 2 ? llvm::CodeGenOptLevel::Aggressive : llvm::CodeGenOptLevel::Default
	);

	if (!targetMachine) {
		context.diagnostics.push_back(Diagnostic(context, Diagnostic::Level::Error, "failed to create target machine", Range())
		);
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
				Diagnostic(context, Diagnostic::Level::Error, "failed to open object file", Range(), "error", ec.message())
			);
			return false;
		}

		llvm::legacy::PassManager passManager;
		if (targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
			context.diagnostics.push_back(
				Diagnostic(context, Diagnostic::Level::Error, "target machine cannot emit object file", Range())
			);
			return false;
		}

		passManager.run(*context.llvmModule);
	}

	// Link object file to executable using system linker
	std::string linkCommand = "cc " + objectPath + " -o " + outputPath;

	// Preserve debug info sections
	if (context.options.emitDebugInfo)
		linkCommand += " -g";

	// Add any required libraries
	for (const std::string &lib : context.requiredLibraries) {
		linkCommand += " -l" + lib;
	}

	// Capture linker output to detect missing libraries
	std::string linkCommandWithRedirect = linkCommand + " 2>&1";
	FILE *pipe = popen(linkCommandWithRedirect.c_str(), "r");
	std::string linkerOutput;
	int linkResult = -1;
	if (pipe) {
		char buffer[256];
		while (fgets(buffer, sizeof(buffer), pipe)) {
			linkerOutput += buffer;
		}
		linkResult = pclose(pipe);
	}
	if (linkResult != 0) {
		Diagnostic diagnostic(
			context, Diagnostic::Level::Error, "linking failed", Range(), "exit_code", std::to_string(linkResult)
		);
		const SyntaxConfig &syntax = syntaxConfigForRange(context, Range());

		// Check which libraries are actually missing
		std::vector<std::string> missingLibs;
		for (const std::string &lib : context.requiredLibraries) {
			if (linkerOutput.find("cannot find -l" + lib) != std::string::npos) {
				missingLibs.push_back(lib);
			}
		}

		if (!missingLibs.empty()) {
			std::string libraries;
			for (size_t i = 0; i < missingLibs.size(); i++) {
				if (i > 0)
					libraries += ", ";
				libraries += missingLibs[i];
			}
			diagnostic.message +=
				"\n" + renderConfiguredMessage(syntax, "linking failed", "missing libraries", {{"libraries", libraries}}) +
				"\n" + renderConfiguredMessage(syntax, "linking failed", "install hint");
		}

		context.diagnostics.push_back(std::move(diagnostic));
		return false;
	}

	// Clean up object file
	std::filesystem::remove(objectPath);

	return true;
}
