#include "native.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

bool emitNativeExecutable(ParseContext &context) {
	auto pushPlainError = [&](std::string message) {
		Diagnostic diagnostic;
		diagnostic.level = Diagnostic::Level::Error;
		diagnostic.range = Range();
		diagnostic.message = std::move(message);
		context.diagnostics.push_back(std::move(diagnostic));
	};

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

	auto linkerProgram = llvm::sys::findProgramByName("cc");
	if (!linkerProgram) {
		pushPlainError("failed to find linker: " + linkerProgram.getError().message());
		return false;
	}

	llvm::SmallString<128> stdoutPath;
	llvm::SmallString<128> stderrPath;
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_link_stdout", "log", stdoutPath)) {
		pushPlainError("failed to create temporary linker stdout file: " + ec.message());
		return false;
	}
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_link_stderr", "log", stderrPath)) {
		llvm::sys::fs::remove(stdoutPath);
		pushPlainError("failed to create temporary linker stderr file: " + ec.message());
		return false;
	}

	std::vector<std::string> commandStorage;
	commandStorage.reserve(5 + context.requiredLibraries.size());
	commandStorage.push_back(*linkerProgram);
	commandStorage.push_back(objectPath);
	commandStorage.push_back("-o");
	commandStorage.push_back(outputPath);

	if (context.options.emitDebugInfo)
		commandStorage.push_back("-g");

	for (const std::string &lib : context.requiredLibraries) {
		commandStorage.push_back("-l" + lib);
	}

	std::vector<llvm::StringRef> commandArgs;
	commandArgs.reserve(commandStorage.size());
	for (const std::string &arg : commandStorage)
		commandArgs.push_back(arg);

	std::vector<std::optional<llvm::StringRef>> redirects = {
		std::nullopt,
		llvm::StringRef(stdoutPath),
		llvm::StringRef(stderrPath),
	};

	std::string executeError;
	bool executionFailed = false;
	int linkResult =
		llvm::sys::ExecuteAndWait(*linkerProgram, commandArgs, std::nullopt, redirects, 0, 0, &executeError, &executionFailed);

	auto readFile = [](llvm::StringRef path) {
		std::ifstream file(path.str(), std::ios::in | std::ios::binary);
		if (!file)
			return std::string{};
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	};

	std::string linkerOutput = readFile(stdoutPath) + readFile(stderrPath);
	llvm::sys::fs::remove(stdoutPath);
	llvm::sys::fs::remove(stderrPath);

	if (!executeError.empty()) {
		pushPlainError("failed to execute linker: " + executeError);
		return false;
	}

	if (executionFailed || linkResult != 0) {
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
		if (!linkerOutput.empty()) {
			if (linkerOutput.back() == '\n')
				linkerOutput.pop_back();
			diagnostic.message += "\nLinker output:\n" + linkerOutput;
		}

		context.diagnostics.push_back(std::move(diagnostic));
		return false;
	}

	// Clean up object file
	std::filesystem::remove(objectPath);

	return true;
}
