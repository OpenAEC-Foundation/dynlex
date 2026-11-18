#pragma once
#include "moduleSymbols.h"
#include "parseContext.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <string_view>

inline std::string llvmOutputPath(const ParseContext::Options &options) {
	return options.outputPath.empty() ? options.inputPath + ".ll" : options.outputPath;
}

inline bool llvmOutputIsStdout(const ParseContext::Options &options) { return options.outputPath == "-"; }

inline std::string renderLLVMOutput(llvm::Module &module) {
	sortModuleSymbols(module);
	std::string text;
	llvm::raw_string_ostream stream(text);
	module.print(stream, nullptr);
	return text;
}

inline bool writeLLVMOutput(ParseContext &context, std::string_view text) {
	std::error_code error;
	llvm::raw_fd_ostream output(llvmOutputPath(context.options), error);
	const char *message = "failed to open output file";
	if (!error) {
		output.write(text.data(), text.size());
		if (llvmOutputIsStdout(context.options))
			output.flush();
		else
			output.close();
		error = output.error();
		message = "failed to write output file";
	}
	if (error) {
		output.clear_error();
		context.addDiagnostic(Diagnostic(context, Diagnostic::Level::Error, message, Range(), "error", error.message()));
		return false;
	}
	return true;
}
