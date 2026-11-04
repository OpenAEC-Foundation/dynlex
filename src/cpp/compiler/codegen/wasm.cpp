#include "wasm.h"
#include "lld/Common/Driver.h"
#include "targetOptions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

LLD_HAS_DRIVER(wasm)

std::unique_ptr<llvm::TargetMachine> createWASMTargetMachine(ParseContext &context, std::string &errorMessage) {
	LLVMInitializeWebAssemblyTarget();
	LLVMInitializeWebAssemblyTargetInfo();
	LLVMInitializeWebAssemblyTargetMC();
	LLVMInitializeWebAssemblyAsmPrinter();

	llvm::Triple targetTriple("wasm32-unknown-unknown");
	context.llvmModule->setTargetTriple(targetTriple);

	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, errorMessage);
	if (!target)
		return nullptr;

	return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
		targetTriple, "generic", "", llvmTargetOptions(context.options), llvm::Reloc::PIC_, std::nullopt,
		codeGenerationOptimizationLevel(context.options)
	));
}

bool emitWASMModule(ParseContext &context) {
	std::string outputPath = context.options.outputPath;
	if (outputPath.empty())
		outputPath = context.options.inputPath + ".wasm";

	requireCompilerInvariant(context.targetMachine != nullptr, "WASM emission requires an initialized target machine");
	int objectDescriptor;
	llvm::SmallString<128> objectPath;
	std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex-wasm", "o", objectDescriptor, objectPath);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm object open failed", Range(), "error", ec.message())
		);
		return false;
	}
	llvm::FileRemover removeObject(objectPath);
	llvm::raw_fd_ostream object(objectDescriptor, true);
	llvm::legacy::PassManager passManager;
	if (context.targetMachine->addPassesToEmitFile(passManager, object, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm target cannot emit object file", Range())
		);
		return false;
	}
	passManager.run(*context.llvmModule);
	object.close();
	if (object.has_error()) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "wasm object write failed", Range(), "error", object.error().message()
		));
		object.clear_error();
		return false;
	}

	// LLD resolves code and data relocations together. Zero is reserved for null;
	// the exported heap boundary includes aligned static data and the full stack.
	const char *arguments[] = {
		"wasm-ld",
		"--no-entry",
		"--no-stack-first",
		"--global-base=16",
		"-z",
		"stack-size=8388608",
		"--import-memory=env,__linear_memory",
		"--import-undefined",
		"--export=main",
		"--export=__heap_base",
		"--threads=1", // One compiler object, including in single-threaded Web Workers.
		"--fatal-warnings",
		objectPath.c_str(),
		"-o",
		outputPath.c_str()
	};
	std::string linkerOutput;
	llvm::raw_string_ostream linkerStream(linkerOutput);
	lld::Result result = lld::lldMain(arguments, linkerStream, linkerStream, {{lld::Wasm, &lld::wasm::link}});
	requireCompilerInvariant(result.canRunAgain, "WebAssembly linker could not clean up its internal state");
	if (result.retCode != 0) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm link failed", Range(), "error", linkerOutput)
		);
		return false;
	}
	return true;
}
