#pragma once
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

inline std::string serializeCachedModule(const llvm::Module &module) {
	std::string bitcode;
	llvm::raw_string_ostream output(bitcode);
	llvm::WriteBitcodeToFile(module, output);
	output.flush();
	return bitcode;
}

inline std::unique_ptr<llvm::Module> parseCachedModule(std::string_view bitcode, llvm::LLVMContext &context) {
	llvm::MemoryBufferRef buffer(llvm::StringRef(bitcode.data(), bitcode.size()), "cached LLVM module");
	auto parsed = llvm::parseBitcodeFile(buffer, context);
	if (!parsed)
		throw std::runtime_error("invalid cached LLVM bitcode: " + llvm::toString(parsed.takeError()));
	return std::move(*parsed);
}
