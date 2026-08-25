#include "codegen/codegen.h"
#include "compiler/compiler.h"
#include "dap/dapServer.h"
#include "lsp/completion.h"
#include "lsp/dynlexServer.h"
#include "lsp/fileSystem.h"
#include "lsp/stdioTransport.h"
#include "parseContext.h"
#include "syntaxConfig.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifndef DYNLEX_VERSION
#define DYNLEX_VERSION "dev"
#endif

namespace {

constexpr std::string_view commandWrapperPath = "<command-wrapper>";
constexpr std::string_view commandSourcePath = "<command-line>";

void printUsage(std::ostream &output) {
	output << "Usage:\n"
		   << "  dynlex <file.dl> [compiler options]\n"
		   << "  dynlex <source...>\n"
		   << "  dynlex -- <source...>\n\n"
		   << "Compiler options:\n"
		   << "  --emit-llvm | --emit-wasm | --emit-spirv\n"
		   << "  --emit-completions line:column  --dump-purity\n"
		   << "  --shader-stage=vertex|fragment  -O0|-O1|-O2|-O3|-Os|-Oz|-Ofast\n"
		   << "  -ffast-math|-fno-fast-math  -ffp-contract=fast|off\n"
		   << "  -march=native|<cpu>  -mcpu=<cpu>  -mtune=<cpu>  -mattr=<features>\n"
		   << "  -fvectorize|-fno-vectorize  -fslp-vectorize|-fno-slp-vectorize\n"
		   << "  -funroll-loops|-fno-unroll-loops\n"
		   << "  -o <output>  -g|--debug\n"
		   << "  --lsp [--port PORT]  --stdio  --dap  --lsp-trace[=PATH]\n"
		   << "  --version  --help\n";
}

bool hasErrors(const ParseContext &context) {
	return std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [](const Diagnostic &diagnostic) {
		return diagnostic.level == Diagnostic::Level::Error;
	});
}

bool isFileArgument(const std::string &argument) {
	if (argument.ends_with(".dl"))
		return true;
	std::error_code error;
	return std::filesystem::exists(argument, error);
}

std::string joinSourceArguments(const std::vector<std::string> &arguments, size_t start) {
	std::string source;
	for (size_t index = start; index < arguments.size(); index++) {
		if (!source.empty())
			source += ' ';
		source += arguments[index];
	}
	return source;
}

int executeProgram(const std::string &programPath) {
	std::vector<llvm::StringRef> arguments = {programPath};
	std::string executionError;
	bool executionFailed = false;
	int exitCode = llvm::sys::ExecuteAndWait(programPath, arguments, std::nullopt, {}, 0, 0, &executionError, &executionFailed);
	if (executionFailed || exitCode < 0) {
		std::cerr << "Failed to execute compiled command";
		if (!executionError.empty())
			std::cerr << ": " << executionError;
		std::cerr << std::endl;
		return 1;
	}
	return exitCode;
}

int compileAndExecuteCommand(std::string source, ParseContext &context) {
	auto fileSystem = std::make_unique<lsp::MemoryFileSystem>(std::make_unique<lsp::LocalFileSystem>());
	fileSystem->setFile(std::string(commandSourcePath), std::move(source));
	context.fileSystem = std::move(fileSystem);
	if (!initializeSyntaxConfigs(context, std::string(commandSourcePath))) {
		context.printDiagnostics();
		return 1;
	}
	static_cast<lsp::MemoryFileSystem *>(context.fileSystem.get())
		->setFile(
			std::string(commandWrapperPath), context.projectSyntax.importKeyword + " lib/commands.dl\n" +
												 context.projectSyntax.importKeyword + " " + std::string(commandSourcePath) +
												 "\n"
		);

	llvm::SmallString<128> temporaryDirectory;
	if (std::error_code error = llvm::sys::fs::createUniqueDirectory("dynlex-command", temporaryDirectory)) {
		std::cerr << "Failed to create temporary command directory: " << error.message() << std::endl;
		return 1;
	}

	std::filesystem::path outputPath = std::filesystem::path(temporaryDirectory.str().str()) / "command";
#ifdef _WIN32
	outputPath += ".exe";
#endif
	context.options.inputPath = std::string(commandSourcePath);
	context.options.outputPath = outputPath.string();

	int result = 1;
	bool compileSucceeded = compile(std::string(commandWrapperPath), context);
	bool codegenSucceeded = compileSucceeded && generateCode(context);
	context.printDiagnostics();
	if (codegenSucceeded && !hasErrors(context))
		result = executeProgram(outputPath.string());

	std::error_code cleanupError;
	std::filesystem::remove_all(std::filesystem::path(temporaryDirectory.str().str()), cleanupError);
	if (cleanupError) {
		std::cerr << "Failed to remove temporary command directory: " << cleanupError.message() << std::endl;
		return 1;
	}
	return result;
}

} // namespace

static bool parseOneBasedLineColumn(std::string_view text, int &outLine, int &outColumn) {
	size_t colon = text.find(':');
	if (colon == std::string_view::npos)
		return false;

	std::string lineText(text.substr(0, colon));
	std::string columnText(text.substr(colon + 1));
	if (lineText.empty() || columnText.empty())
		return false;

	try {
		outLine = std::stoi(lineText);
		outColumn = std::stoi(columnText);
	} catch (const std::exception &) {
		return false;
	}

	return outLine > 0 && outColumn > 0;
}

// possible invocation: dynlex main.dl
// will compile DynLex to an executable named main
// to execute that executable: ./main
// the compiler will always receive one source file, since that file imports all other files
// non-file positional arguments are joined as source, compiled, and executed
// --lsp flag starts the language server on TCP port 5007 by default
// --stdio flag starts the language server on stdin/stdout (for MCP integration)
// --emit-llvm outputs .ll, --emit-wasm outputs a wasm artifact, otherwise native executable
// --version prints DynLex version and exits
int main(int argumentCount, char *argumentValues[]) {
	llvm::llvm_shutdown_obj llvmShutdown;
	std::vector<std::string> args(argumentValues + 1, argumentValues + argumentCount);

	ParseContext context{};
	bool runLSP = false;
	bool runDAP = false;
	bool useStdio = false;
	bool waitDebugger = false;
	bool emitCompletions = false;
	bool dumpPurity = false;
	bool enableLspTrace = false;
	bool showHelp = false;
	bool explicitShaderStage = false;
	int completionLine = 0;
	int completionColumn = 0;
	int lspPort = 5007;
	std::string inputFile;
	std::string lspTracePath;
	std::optional<size_t> commandSourceStart;

	// Parse arguments
	for (size_t argumentIndex = 0; argumentIndex < args.size(); ++argumentIndex) {
		const std::string &arg = args[argumentIndex];
		if (arg == "--") {
			if (!inputFile.empty()) {
				std::cerr << "Cannot combine file input with command-line source" << std::endl;
				return 1;
			}
			commandSourceStart = argumentIndex + 1;
			break;
		} else if (arg == "--help" || arg == "-h") {
			showHelp = true;
		} else if (arg == "--wait-debugger") {
			waitDebugger = true;
		} else if (arg == "--dap") {
			runDAP = true;
		} else if (arg == "--lsp") {
			runLSP = true;
		} else if (arg == "--lsp-trace") {
			enableLspTrace = true;
		} else if (arg.starts_with("--lsp-trace=")) {
			enableLspTrace = true;
			lspTracePath = arg.substr(std::string("--lsp-trace=").size());
		} else if (arg == "--version") {
			std::cout << DYNLEX_VERSION << std::endl;
			return 0;
		} else if (arg == "--port") {
			if (argumentIndex + 1 >= args.size()) {
				std::cerr << "Missing value for --port" << std::endl;
				return 1;
			}
			try {
				lspPort = std::stoi(args[++argumentIndex]);
			} catch (const std::exception &) {
				std::cerr << "Invalid --port value: " << args[argumentIndex] << std::endl;
				return 1;
			}
			if (lspPort <= 0 || lspPort > 65535) {
				std::cerr << "Port out of range: " << lspPort << std::endl;
				return 1;
			}
		} else if (arg == "--stdio") {
			useStdio = true;
		} else if (arg.starts_with("--emit-completions")) {
			std::string value;
			if (arg.size() > std::string("--emit-completions").size() && arg[std::string("--emit-completions").size()] == '=') {
				value = arg.substr(std::string("--emit-completions=").size());
			} else if (argumentIndex + 1 < args.size()) {
				value = args[++argumentIndex];
			}

			if (!parseOneBasedLineColumn(value, completionLine, completionColumn)) {
				std::cerr << "Invalid --emit-completions value. Expected line:column with 1-based indices." << std::endl;
				return 1;
			}
			emitCompletions = true;
		} else if (arg == "--dump-purity") {
			dumpPurity = true;
		} else if (arg == "--emit-llvm") {
			context.options.emitLLVM = true;
		} else if (arg == "--emit-wasm") {
			context.options.emitWASM = true;
		} else if (arg == "--emit-spirv") {
			context.options.emitSPIRV = true;
		} else if (arg == "--shader-stage=vertex") {
			explicitShaderStage = true;
			context.options.shaderStage = ParseContext::ShaderStage::Vertex;
		} else if (arg == "--shader-stage=fragment") {
			explicitShaderStage = true;
			context.options.shaderStage = ParseContext::ShaderStage::Fragment;
		} else if (arg == "-g" || arg == "--debug") {
			context.options.emitDebugInfo = true;
		} else if (arg == "-O0") {
			context.options.optimizationLevel = 0;
			context.options.optimizationSize = ParseContext::OptimizationSize::None;
		} else if (arg == "-O1") {
			context.options.optimizationLevel = 1;
			context.options.optimizationSize = ParseContext::OptimizationSize::None;
		} else if (arg == "-O2") {
			context.options.optimizationLevel = 2;
			context.options.optimizationSize = ParseContext::OptimizationSize::None;
		} else if (arg == "-O3") {
			context.options.optimizationLevel = 3;
			context.options.optimizationSize = ParseContext::OptimizationSize::None;
		} else if (arg == "-Os") {
			context.options.optimizationLevel = 2;
			context.options.optimizationSize = ParseContext::OptimizationSize::Size;
		} else if (arg == "-Oz") {
			context.options.optimizationLevel = 2;
			context.options.optimizationSize = ParseContext::OptimizationSize::Smallest;
		} else if (arg == "-Ofast") {
			context.options.optimizationLevel = 3;
			context.options.optimizationSize = ParseContext::OptimizationSize::None;
			context.options.fastMath = true;
			context.options.floatingPointContract = ParseContext::FloatingPointContract::Fast;
		} else if (arg == "-ffast-math") {
			context.options.fastMath = true;
			context.options.floatingPointContract = ParseContext::FloatingPointContract::Fast;
		} else if (arg == "-fno-fast-math") {
			context.options.fastMath = false;
			context.options.floatingPointContract =
				context.options.explicitFloatingPointContract.value_or(ParseContext::FloatingPointContract::Off);
		} else if (arg == "-ffp-contract=fast") {
			context.options.floatingPointContract = ParseContext::FloatingPointContract::Fast;
			context.options.explicitFloatingPointContract = ParseContext::FloatingPointContract::Fast;
		} else if (arg == "-ffp-contract=off") {
			context.options.floatingPointContract = ParseContext::FloatingPointContract::Off;
			context.options.explicitFloatingPointContract = ParseContext::FloatingPointContract::Off;
		} else if (arg.starts_with("-ffp-contract=")) {
			std::cerr << "Invalid -ffp-contract value; expected fast or off" << std::endl;
			return 1;
		} else if (arg == "-fvectorize") {
			context.options.loopVectorization = true;
		} else if (arg == "-fno-vectorize") {
			context.options.loopVectorization = false;
		} else if (arg == "-fslp-vectorize") {
			context.options.slpVectorization = true;
		} else if (arg == "-fno-slp-vectorize") {
			context.options.slpVectorization = false;
		} else if (arg == "-funroll-loops") {
			context.options.loopUnrolling = true;
		} else if (arg == "-fno-unroll-loops") {
			context.options.loopUnrolling = false;
		} else if (arg.starts_with("-march=")) {
			context.options.targetCPU = arg.substr(std::string("-march=").size());
			if (context.options.targetCPU.empty()) {
				std::cerr << "Missing value for -march" << std::endl;
				return 1;
			}
			context.options.hasExplicitTargetConfiguration = true;
		} else if (arg.starts_with("-mcpu=")) {
			context.options.targetCPU = arg.substr(std::string("-mcpu=").size());
			if (context.options.targetCPU.empty()) {
				std::cerr << "Missing value for -mcpu" << std::endl;
				return 1;
			}
			context.options.hasExplicitTargetConfiguration = true;
		} else if (arg.starts_with("-mtune=")) {
			context.options.targetTuneCPU = arg.substr(std::string("-mtune=").size());
			if (context.options.targetTuneCPU.empty()) {
				std::cerr << "Missing value for -mtune" << std::endl;
				return 1;
			}
			context.options.hasExplicitTargetConfiguration = true;
		} else if (arg.starts_with("-mattr=")) {
			std::string features = arg.substr(std::string("-mattr=").size());
			if (features.empty()) {
				std::cerr << "Missing value for -mattr" << std::endl;
				return 1;
			}
			for (size_t start = 0; start <= features.size();) {
				size_t end = features.find(',', start);
				std::string_view feature =
					std::string_view(features).substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (feature.size() < 2 || (feature.front() != '+' && feature.front() != '-')) {
					std::cerr << "Invalid -mattr value; expected comma-separated +feature or -feature entries" << std::endl;
					return 1;
				}
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			if (!context.options.targetFeatures.empty())
				context.options.targetFeatures += ',';
			context.options.targetFeatures += features;
			context.options.hasExplicitTargetConfiguration = true;
		} else if (arg.starts_with("-o")) {
			if (arg.size() > 2) {
				context.options.outputPath = arg.substr(2);
			} else {
				if (argumentIndex + 1 >= args.size()) {
					std::cerr << "Missing value for -o" << std::endl;
					return 1;
				}
				context.options.outputPath = args[++argumentIndex];
			}
		} else if (arg.starts_with("-")) {
			std::cerr << "Unknown option: " << arg << std::endl;
			return 1;
		} else if (!inputFile.empty()) {
			std::cerr << "Only one input file can be compiled" << std::endl;
			return 1;
		} else if (isFileArgument(arg)) {
			inputFile = arg;
		} else {
			commandSourceStart = argumentIndex;
			break;
		}
	}

	if (showHelp) {
		printUsage(std::cout);
		return 0;
	}

	int explicitOutputModes = 0;
	explicitOutputModes += context.options.emitLLVM ? 1 : 0;
	explicitOutputModes += context.options.emitWASM ? 1 : 0;
	explicitOutputModes += context.options.emitSPIRV ? 1 : 0;
	if (explicitOutputModes > 1) {
		std::cerr << "Choose at most one explicit output mode: --emit-llvm, --emit-wasm, or --emit-spirv" << std::endl;
		return 1;
	}
	if ((context.options.emitWASM || context.options.emitSPIRV) && context.options.hasExplicitTargetConfiguration) {
		std::cerr << "CPU target options require native or LLVM output" << std::endl;
		return 1;
	}
	if (commandSourceStart) {
		if (*commandSourceStart >= args.size()) {
			std::cerr << "No command-line source was provided after --" << std::endl;
			return 1;
		}
		if (runLSP || runDAP || useStdio || enableLspTrace) {
			std::cerr << "Cannot combine a language or debug server with command-line source" << std::endl;
			return 1;
		}
		if (explicitOutputModes != 0 || emitCompletions || dumpPurity || explicitShaderStage ||
			!context.options.outputPath.empty()) {
			std::cerr << "Command-line source supports native execution options only; use a .dl file when emitting output"
					  << std::endl;
			return 1;
		}
	}

	if (waitDebugger) {
		std::cerr << "Waiting for debugger to attach (PID: " << llvm::sys::Process::getProcessId() << ")..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(10));
		std::cerr << "Continuing..." << std::endl;
	}

	std::unique_ptr<lsp::StdioTransport> stdioTransport;
	if (runDAP || useStdio) {
		try {
			stdioTransport = std::make_unique<lsp::StdioTransport>();
		} catch (const std::system_error &error) {
			std::cerr << "Failed to configure stdio transport: " << error.what() << std::endl;
			return 1;
		}
	}

	if (runDAP) {
		dap::DapServer server(std::move(stdioTransport), argumentValues[0]);
		server.run();
		return 0;
	}

	if (runLSP || useStdio) {
		if (useStdio) {
			lsp::DynLexServer server(std::move(stdioTransport));
			if (enableLspTrace && !server.enableTrace(lspTracePath)) {
				std::cerr << "Failed to open LSP trace output: " << lspTracePath << std::endl;
				return 1;
			}
			return server.run() ? 0 : 1;
		} else {
			lsp::DynLexServer server(lspPort);
			if (enableLspTrace && !server.enableTrace(lspTracePath)) {
				std::cerr << "Failed to open LSP trace output: " << lspTracePath << std::endl;
				return 1;
			}
			return server.run() ? 0 : 1;
		}
	}

	if (commandSourceStart) {
		return compileAndExecuteCommand(joinSourceArguments(args, *commandSourceStart), context);
	}

	if (!inputFile.empty()) {
		context.fileSystem = std::make_unique<lsp::LocalFileSystem>();
		context.options.inputPath = inputFile;
		bool compileSucceeded = compile(inputFile, context);
		if (emitCompletions) {
			std::cout << lsp::renderCompletionDebugReport(
				context, std::filesystem::absolute(inputFile).string(), completionLine - 1, completionColumn - 1
			);
		} else if (compileSucceeded) {
			if (dumpPurity)
				std::cout << renderPurityReport(context);
			generateCode(context);
		}
		context.printDiagnostics();
		if (hasErrors(context))
			return 1;
	} else {
		printUsage(std::cerr);
	}

	return 0;
}
