#include "native.h"
#include "targetOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct LibraryNameMapping {
	llvm::StringLiteral portableName;
	llvm::StringLiteral linkerName;
};

struct ProgramExecutionResult {
	int exitCode = 0;
	bool executionFailed = false;
	std::string executeError;
	std::string output;
};

struct NativeTargetConfiguration {
	std::string cpu;
	std::string tuneCPU;
	std::string features;
};

bool isKnownTargetFeature(const llvm::MCSubtargetInfo &subtargetInfo, llvm::StringRef feature) {
	llvm::StringRef featureName = llvm::SubtargetFeatures::StripFlag(feature);
	return std::any_of(
		subtargetInfo.getAllProcessorFeatures().begin(), subtargetInfo.getAllProcessorFeatures().end(),
		[featureName](const llvm::SubtargetFeatureKV &candidate) {
		return featureName == candidate.Key;
	}
	);
}

bool resolveNativeTargetConfiguration(
	ParseContext &context, const llvm::Target &target, const llvm::Triple &targetTriple,
	NativeTargetConfiguration &configuration, std::string &errorMessage
) {
	std::unique_ptr<llvm::MCSubtargetInfo> subtargetInfo(target.createMCSubtargetInfo(targetTriple, "", ""));
	if (!subtargetInfo) {
		errorMessage = "target does not provide processor information for '" + targetTriple.str() + "'";
		return false;
	}

	std::string hostCPU;
	auto resolveCPU = [&](const std::string &requestedCPU, std::string &resolvedCPU) {
		if (requestedCPU != "native") {
			resolvedCPU = requestedCPU;
			return true;
		}
		if (hostCPU.empty())
			hostCPU = llvm::sys::getHostCPUName().str();
		if (hostCPU.empty()) {
			errorMessage = "host CPU detection returned no processor name";
			return false;
		}
		resolvedCPU = hostCPU;
		return true;
	};

	if (!resolveCPU(context.options.targetCPU, configuration.cpu) ||
		!resolveCPU(context.options.targetTuneCPU, configuration.tuneCPU)) {
		return false;
	}
	if (!subtargetInfo->isCPUStringValid(configuration.cpu)) {
		errorMessage = "unknown target CPU '" + configuration.cpu + "' for '" + targetTriple.str() + "'";
		return false;
	}
	if (!configuration.tuneCPU.empty() && !subtargetInfo->isCPUStringValid(configuration.tuneCPU)) {
		errorMessage = "unknown tuning CPU '" + configuration.tuneCPU + "' for '" + targetTriple.str() + "'";
		return false;
	}

	llvm::SubtargetFeatures features;
	if (context.options.targetCPU == "native") {
		std::vector<std::pair<std::string, bool>> hostFeatures;
		for (const auto &[feature, enabled] : llvm::sys::getHostCPUFeatures())
			hostFeatures.emplace_back(feature.str(), enabled);
		std::sort(hostFeatures.begin(), hostFeatures.end());
		for (const auto &[feature, enabled] : hostFeatures)
			features.AddFeature(feature, enabled);
	}
	features.addFeaturesVector(llvm::SubtargetFeatures(context.options.targetFeatures).getFeatures());
	for (const std::string &feature : features.getFeatures()) {
		if (isKnownTargetFeature(*subtargetInfo, feature))
			continue;
		errorMessage = "unknown target feature '" + feature + "' for '" + targetTriple.str() + "'";
		return false;
	}
	configuration.features = features.getString();
	return true;
}

std::optional<ProgramExecutionResult>
executeProgramAndCapture(llvm::StringRef program, llvm::ArrayRef<llvm::StringRef> arguments, std::string &setupError) {
	llvm::SmallString<128> stdoutPath;
	llvm::SmallString<128> stderrPath;
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_program_stdout", "log", stdoutPath)) {
		setupError = "failed to create temporary program stdout file: " + ec.message();
		return std::nullopt;
	}
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_program_stderr", "log", stderrPath)) {
		llvm::sys::fs::remove(stdoutPath);
		setupError = "failed to create temporary program stderr file: " + ec.message();
		return std::nullopt;
	}

	std::vector<std::optional<llvm::StringRef>> redirects = {
		std::nullopt,
		llvm::StringRef(stdoutPath),
		llvm::StringRef(stderrPath),
	};
	ProgramExecutionResult result;
	result.exitCode = llvm::sys::ExecuteAndWait(
		program, arguments, std::nullopt, redirects, 0, 0, &result.executeError, &result.executionFailed
	);

	auto readFile = [&](llvm::StringRef path) -> std::optional<std::string> {
		std::ifstream file(path.str(), std::ios::in | std::ios::binary);
		if (!file)
			return std::nullopt;
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	};
	std::optional<std::string> stdoutContents = readFile(stdoutPath);
	std::optional<std::string> stderrContents = readFile(stderrPath);
	llvm::sys::fs::remove(stdoutPath);
	llvm::sys::fs::remove(stderrPath);
	if (!stdoutContents || !stderrContents) {
		setupError = "failed to read captured program output";
		return std::nullopt;
	}
	result.output = std::move(*stdoutContents) + std::move(*stderrContents);
	return result;
}

std::vector<std::string> nativeLibraryArguments(const llvm::Triple &targetTriple, llvm::StringRef library) {
	if (library == "dynlex_runtime") {
		static const std::string runtimeLibraryPath = [] {
			std::string executable = llvm::sys::fs::getMainExecutable(
				nullptr, reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(&nativeLibraryArguments))
			);
			std::filesystem::path installPrefix = std::filesystem::path(executable).parent_path().parent_path();
			std::filesystem::path installedPath =
				installPrefix / DYNLEX_RUNTIME_LIBRARY_INSTALL_DIR / DYNLEX_RUNTIME_LIBRARY_FILENAME;
			if (std::filesystem::exists(installedPath))
				return installedPath.string();
			std::filesystem::path buildPath(DYNLEX_RUNTIME_LIBRARY_BUILD_PATH);
			if (std::filesystem::exists(buildPath))
				return buildPath.string();
			return installedPath.string();
		}();
		std::vector<std::string> arguments = {runtimeLibraryPath};
		if (!targetTriple.isOSWindows())
			arguments.push_back("-pthread");
		return arguments;
	}
	if (targetTriple.isOSDarwin() && library == "GL")
		return {"-framework", "OpenGL"};

	if (targetTriple.isOSWindows()) {
		static constexpr std::array windowsLibraryNames = {
			LibraryNameMapping{"GL", "opengl32"},
			LibraryNameMapping{"glfw", "glfw3dll"},
		};
		for (const LibraryNameMapping &mapping : windowsLibraryNames) {
			if (library == mapping.portableName) {
				library = mapping.linkerName;
				break;
			}
		}
	}

	return {"-l" + library.str()};
}

bool linkerReportsMissingLibrary(llvm::StringRef output, const std::vector<std::string> &arguments) {
	if (arguments.size() == 2 && arguments[0] == "-framework") {
		const std::string &framework = arguments[1];
		return output.contains("framework '" + framework + "' not found") ||
			   output.contains("framework not found " + framework);
	}

	for (const std::string &argument : arguments) {
		const llvm::StringRef argumentRef(argument);
		if (!argumentRef.starts_with("-l"))
			continue;
		const llvm::StringRef libraryName = argumentRef.drop_front(2);
		if (output.contains("cannot find " + argument) || output.contains("unable to find library " + argument) ||
			output.contains("library '" + libraryName.str() + "' not found"))
			return true;
	}
	return false;
}

} // namespace

std::unique_ptr<llvm::TargetMachine> createNativeTargetMachine(ParseContext &context, std::string &errorMessage) {
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();

	llvm::Triple targetTriple(llvm::sys::getDefaultTargetTriple());
	context.llvmModule->setTargetTriple(targetTriple);
	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, errorMessage);
	if (!target)
		return nullptr;

	NativeTargetConfiguration configuration;
	if (!resolveNativeTargetConfiguration(context, *target, targetTriple, configuration, errorMessage))
		return nullptr;
	context.resolvedTargetTuneCPU = configuration.tuneCPU;
	std::unique_ptr<llvm::TargetMachine> targetMachine(target->createTargetMachine(
		targetTriple, configuration.cpu, configuration.features, llvmTargetOptions(context.options), llvm::Reloc::PIC_,
		std::nullopt, codeGenerationOptimizationLevel(context.options)
	));
	if (!targetMachine)
		errorMessage = "failed to create target machine for '" + targetTriple.str() + "'";
	return targetMachine;
}

bool emitNativeExecutable(ParseContext &context) {
	auto pushPlainError = [&](std::string message) {
		Diagnostic diagnostic;
		diagnostic.level = Diagnostic::Level::Error;
		diagnostic.range = Range();
		diagnostic.message = std::move(message);
		context.diagnostics.push_back(std::move(diagnostic));
	};

	requireCompilerInvariant(context.targetMachine != nullptr, "native emission requires an initialized target machine");
	llvm::TargetMachine &targetMachine = *context.targetMachine;
	const llvm::Triple parsedTargetTriple(context.llvmModule->getTargetTriple());

#ifdef _WIN32
	const llvm::StringRef linkerName = "cc.exe";
#else
	const llvm::StringRef linkerName = "cc";
#endif
	auto linkerProgram = llvm::sys::Process::FindInEnvPath("PATH", linkerName);
	if (!linkerProgram) {
		pushPlainError("failed to find linker '" + linkerName.str() + "' on PATH");
		return false;
	}

	const bool requiresNonMergeableDwarfStrings = context.options.emitDebugInfo && parsedTargetTriple.isOSBinFormatELF();
	std::optional<std::string> objectCopyProgram;
	if (requiresNonMergeableDwarfStrings) {
		objectCopyProgram = llvm::sys::Process::FindInEnvPath("PATH", "objcopy");
		if (!objectCopyProgram) {
			pushPlainError("failed to find required ELF object utility 'objcopy' on PATH");
			return false;
		}
	}

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
		if (targetMachine.addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
			context.diagnostics.push_back(
				Diagnostic(context, Diagnostic::Level::Error, "target machine cannot emit object file", Range())
			);
			return false;
		}

		passManager.run(*context.llvmModule);
	}

	if (objectCopyProgram) {
		// GNU BFD suffix-merges SHF_MERGE debug strings without preserving the
		// string-boundary offsets required by DWARF 5. Keep the strings intact.
		std::vector<std::string> objectCopyStorage = {
			*objectCopyProgram,
			"--set-section-flags",
			".debug_str=readonly,debug",
			"--set-section-flags",
			".debug_line_str=readonly,debug",
			objectPath,
		};
		std::vector<llvm::StringRef> objectCopyArguments;
		objectCopyArguments.reserve(objectCopyStorage.size());
		for (const std::string &argument : objectCopyStorage)
			objectCopyArguments.push_back(argument);

		std::string setupError;
		std::optional<ProgramExecutionResult> result =
			executeProgramAndCapture(*objectCopyProgram, objectCopyArguments, setupError);
		if (!result) {
			pushPlainError(setupError);
			return false;
		}
		if (!result->executeError.empty()) {
			pushPlainError("failed to execute ELF object utility: " + result->executeError);
			return false;
		}
		if (result->executionFailed || result->exitCode != 0) {
			std::string message = "preparing ELF debug information failed with exit code " + std::to_string(result->exitCode);
			if (!result->output.empty())
				message += "\nObject utility output:\n" + result->output;
			pushPlainError(std::move(message));
			return false;
		}
	}

	std::vector<std::string> commandStorage;
	commandStorage.reserve(5 + context.requiredLibraries.size() * 2);
	commandStorage.push_back(*linkerProgram);
	commandStorage.push_back(objectPath);
	commandStorage.push_back("-o");
	commandStorage.push_back(outputPath);

	if (context.options.emitDebugInfo)
		commandStorage.push_back("-g");

	for (const std::string &lib : context.requiredLibraries) {
		std::vector<std::string> arguments = nativeLibraryArguments(parsedTargetTriple, lib);
		commandStorage.insert(
			commandStorage.end(), std::make_move_iterator(arguments.begin()), std::make_move_iterator(arguments.end())
		);
	}

	std::vector<llvm::StringRef> commandArgs;
	commandArgs.reserve(commandStorage.size());
	for (const std::string &arg : commandStorage)
		commandArgs.push_back(arg);

	std::string setupError;
	std::optional<ProgramExecutionResult> linkExecution = executeProgramAndCapture(*linkerProgram, commandArgs, setupError);
	if (!linkExecution) {
		pushPlainError(setupError);
		return false;
	}
	if (!linkExecution->executeError.empty()) {
		pushPlainError("failed to execute linker: " + linkExecution->executeError);
		return false;
	}

	if (linkExecution->executionFailed || linkExecution->exitCode != 0) {
		Diagnostic diagnostic(
			context, Diagnostic::Level::Error, "linking failed", Range(), "exit_code", std::to_string(linkExecution->exitCode)
		);
		const SyntaxConfig &syntax = syntaxConfigForRange(context, Range());

		// Check which libraries are actually missing
		std::vector<std::string> missingLibs;
		for (const std::string &lib : context.requiredLibraries) {
			if (linkerReportsMissingLibrary(linkExecution->output, nativeLibraryArguments(parsedTargetTriple, lib))) {
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
		if (!linkExecution->output.empty()) {
			if (linkExecution->output.back() == '\n')
				linkExecution->output.pop_back();
			diagnostic.message += "\nLinker output:\n" + linkExecution->output;
		}

		context.diagnostics.push_back(std::move(diagnostic));
		return false;
	}

	// Clean up object file
	std::filesystem::remove(objectPath);

	return true;
}
