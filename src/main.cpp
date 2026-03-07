#include "codegen/codegen.h"
#include "compiler/compiler.h"
#include "dap/dapServer.h"
#include "lsp/completion.h"
#include "lsp/dynlexServer.h"
#include "lsp/fileSystem.h"
#include "lsp/stdioTransport.h"
#include "parseContext.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

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
// if no arguments are given, the program will print its arguments to the console
// --lsp flag starts the language server on TCP port 5007 by default
// --stdio flag starts the language server on stdin/stdout (for MCP integration)
// --emit-llvm outputs .ll file instead of executable
int main(int argumentCount, char *argumentValues[]) {
	std::vector<std::string> args(argumentValues + 1, argumentValues + argumentCount);

	ParseContext context{};
	bool runLSP = false;
	bool runDAP = false;
	bool useStdio = false;
	bool waitDebugger = false;
	bool emitCompletions = false;
	bool enableLspTrace = false;
	int completionLine = 0;
	int completionColumn = 0;
	int lspPort = 5007;
	std::string inputFile;
	std::string lspTracePath;

	// Parse arguments
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &arg = args[i];
		if (arg == "--wait-debugger") {
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
		} else if (arg == "--port") {
			if (i + 1 >= args.size()) {
				std::cerr << "Missing value for --port" << std::endl;
				return 1;
			}
			try {
				lspPort = std::stoi(args[++i]);
			} catch (const std::exception &) {
				std::cerr << "Invalid --port value: " << args[i] << std::endl;
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
			} else if (i + 1 < args.size()) {
				value = args[++i];
			}

			if (!parseOneBasedLineColumn(value, completionLine, completionColumn)) {
				std::cerr << "Invalid --emit-completions value. Expected line:column with 1-based indices." << std::endl;
				return 1;
			}
			emitCompletions = true;
		} else if (arg == "--emit-llvm") {
			context.options.emitLLVM = true;
		} else if (arg == "--emit-spirv") {
			context.options.emitSPIRV = true;
		} else if (arg == "--shader-stage=vertex") {
			context.options.shaderStage = ParseContext::ShaderStage::Vertex;
		} else if (arg == "--shader-stage=fragment") {
			context.options.shaderStage = ParseContext::ShaderStage::Fragment;
		} else if (arg == "-g" || arg == "--debug") {
			context.options.emitDebugInfo = true;
		} else if (arg == "-O0") {
			context.options.optimizationLevel = 0;
		} else if (arg == "-O1") {
			context.options.optimizationLevel = 1;
		} else if (arg == "-O2") {
			context.options.optimizationLevel = 2;
		} else if (arg == "-O3") {
			context.options.optimizationLevel = 3;
		} else if (arg.starts_with("-o")) {
			if (arg.size() > 2) {
				context.options.outputPath = arg.substr(2);
			} else if (i + 1 < args.size()) {
				context.options.outputPath = args[++i];
			}
		} else if (!arg.starts_with("-")) {
			inputFile = arg;
		}
	}

	if (waitDebugger) {
		std::cerr << "Waiting for debugger to attach (PID: " << getpid() << ")..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(10));
		std::cerr << "Continuing..." << std::endl;
	}

	if (runDAP) {
		dap::DapServer server(std::make_unique<lsp::StdioTransport>(), argumentValues[0]);
		server.run();
		return 0;
	}

	if (runLSP || useStdio) {
		if (useStdio) {
			lsp::DynLexServer server(std::make_unique<lsp::StdioTransport>());
			if (enableLspTrace && !server.enableTrace(lspTracePath)) {
				std::cerr << "Failed to open LSP trace output: " << lspTracePath << std::endl;
				return 1;
			}
			server.run();
		} else {
			lsp::DynLexServer server(lspPort);
			if (enableLspTrace && !server.enableTrace(lspTracePath)) {
				std::cerr << "Failed to open LSP trace output: " << lspTracePath << std::endl;
				return 1;
			}
			server.run();
		}
		return 0;
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
			generateCode(context);
		}
		context.printDiagnostics();
		bool hasErrors = std::any_of(context.diagnostics.begin(), context.diagnostics.end(), [](const Diagnostic &d) {
			return d.level == Diagnostic::Level::Error;
		});
		if (hasErrors)
			return 1;
	} else {
		std::cerr << "Usage: dynlex <file.dl> [--emit-llvm] [--emit-spirv] [--emit-completions line:column] "
					 "[--shader-stage=vertex|fragment] "
					 "[-O0|-O1|-O2|-O3] "
					 "[-o output] [-g] [--lsp] [--port PORT] [--stdio] [--dap] [--lsp-trace[=PATH]]"
				  << std::endl;
	}

	return 0;
}
