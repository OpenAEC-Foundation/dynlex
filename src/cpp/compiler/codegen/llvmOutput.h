#pragma once
#include "fileOutput.h"
#include "moduleSymbols.h"
#include "parseContext.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <string_view>

inline std::string llvmOutputPath(const ParseContext::Options &options) {
	return options.outputPath.empty() ? options.inputPath + ".ll" : options.outputPath;
}

inline std::string renderLLVMOutput(llvm::Module &module) {
	sortModuleSymbols(module);
	std::string text;
	llvm::raw_string_ostream stream(text);
	module.print(stream, nullptr);
	return text;
}

inline bool writeLLVMOutput(ParseContext &context, std::string_view text) {
	return writeFileOutput(
		context, llvmOutputPath(context.options), text, "failed to open output file", "failed to write output file"
	);
}
