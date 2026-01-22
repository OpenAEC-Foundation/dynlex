#include "codegen/codegen.h"
#include "compiler/compiler.h"
#include "lsp/tbxServer.h"
#include "parseContext.h"
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

// possible invocation: 3bx main.3bx
// will compile 3bx to an executable named main
// to execute that executable: ./main
// the compiler will always receive one source file, since that file imports all other files
// if no arguments are given, the program will print its arguments to the console
// --lsp flag starts the language server on port 5007
// --emit-llvm outputs .ll file instead of executable
int main(int argumentCount, char *argumentValues[]) {
	std::vector<std::string> args(argumentValues + 1, argumentValues + argumentCount);

	ParseContext context{};
	bool runLSP = false;
	bool waitDebugger = false;
	std::string inputFile;

	// Parse arguments
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &arg = args[i];
		if (arg == "--wait-debugger") {
			waitDebugger = true;
		} else if (arg == "--lsp") {
			runLSP = true;
		} else if (arg == "--emit-llvm") {
			context.options.emitLLVM = true;
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

	if (runLSP) {
		lsp::TbxServer server;
		server.run();
		return 0;
	}

	if (!inputFile.empty()) {
		context.options.inputPath = inputFile;
		if (compile(inputFile, context)) {
			generateCode(context);
		}
		context.reportDiagnostics();
	} else {
		std::cerr << "Usage: 3bx <file.3bx> [--emit-llvm] [-o output]" << std::endl;
	}

	return 0;
}