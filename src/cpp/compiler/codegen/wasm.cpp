#include "wasm.h"
#include "../incrementalModuleBitcode.h"
#include "../incrementalModulePartition.h"
#include "../moduleSymbols.h"
#include "../stableIdentity.h"
#include "codegen.h"
#include "debugTypeProducts.h"
#include "lld/Common/Driver.h"
#include "targetOptions.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include <array>
#include <unordered_map>
#include <unordered_set>

LLD_HAS_DRIVER(wasm)

namespace {
#include "../incrementalNativeLayout.inl"
}

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

std::string wasmOutputPath(const ParseContext::Options &options) {
	return options.outputPath.empty() ? options.inputPath + ".wasm" : options.outputPath;
}

std::unique_ptr<llvm::Module> optimizeWASMModule(
	ParseContext &context, llvm::LLVMContext &llvmContext, const std::string &identifier, std::string_view generatedIR
) {
	// The serialized IR is the optimizer input on both clean and incremental builds.
	// Reading it also resets LLVM's in-memory value naming and use-list order.
	auto module = parseCachedModule(generatedIR, llvmContext);
	module->setModuleIdentifier(identifier);
	consumeDebugIRProducts(*module);
	if (context.options.optimizationLevel)
		optimizeLLVMModule(context, *module);
	return module;
}

std::vector<WASMObjectPartition> prepareWASMObjectPartitions(llvm::Module &module) {
	// Relocations between objects need linkable names. Hidden visibility keeps
	// promoted definitions private to the linked module.
	for (llvm::GlobalValue &value : module.global_values()) {
		if (value.hasLocalLinkage()) {
			value.setName("__dynlex_wasm_" + value.getName().str());
			value.setLinkage(llvm::GlobalValue::ExternalLinkage);
			value.setVisibility(llvm::GlobalValue::HiddenVisibility);
		}
		if (!value.hasName())
			value.setName("__dynlex_wasm_anonymous");
	}
	sortModuleSymbols(module);
	NativePartitionLayout layout(module);
	std::unordered_set<std::string_view> codeComponents;
	for (const llvm::Function &function : module)
		if (!function.isDeclaration())
			codeComponents.insert(layout.component(function));
	// Group functions into stable buckets to amortize object emission and
	// linking setup. Keep independent static data in its own object.
	constexpr size_t codeBucketCount = 16;
	std::array<WASMObjectPartition, codeBucketCount + 1> buckets;
	buckets.front().containsAssembly = !module.getModuleInlineAsm().empty();
	for (const llvm::GlobalValue &value : module.global_values()) {
		if (value.isDeclaration())
			continue;
		const std::string &component = layout.component(value);
		size_t bucket = 0;
		if (codeComponents.contains(component)) {
			char digit = stableIdentityDigest(component).front();
			bucket = 1 + (digit < 'a' ? digit - '0' : digit - 'a' + 10);
		}
		buckets.at(bucket).definitions.push_back(&value);
	}
	std::vector<WASMObjectPartition> partitions;
	for (WASMObjectPartition &bucket : buckets)
		if (bucket.containsAssembly || !bucket.definitions.empty())
			partitions.push_back(std::move(bucket));
	return partitions;
}

std::unique_ptr<llvm::Module> cloneWASMObjectPartition(
	const llvm::Module &module, const WASMObjectPartition &partition, ModulePartitionCloneWork &work
) {
	auto part = cloneModulePartition(module, partition.definitions, work);
	if (!partition.containsAssembly)
		part->setModuleInlineAsm("");
	llvm::ModuleAnalysisManager analyses;
	llvm::StripDeadPrototypesPass().run(*part, analyses);
	sortModuleSymbols(*part);
	requireCompilerInvariant(!llvm::verifyModule(*part), "WebAssembly object partition contains invalid LLVM IR");
	return part;
}

bool emitWASMObject(ParseContext &context, llvm::Module &module, const std::string &objectPath) {
	requireCompilerInvariant(context.targetMachine != nullptr, "WASM emission requires an initialized target machine");
	std::error_code ec;
	llvm::raw_fd_ostream object(objectPath, ec);
	if (ec) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm object open failed", Range(), "error", ec.message())
		);
		return false;
	}
	llvm::legacy::PassManager passManager;
	if (context.targetMachine->addPassesToEmitFile(passManager, object, nullptr, llvm::CodeGenFileType::ObjectFile)) {
		context.diagnostics.push_back(
			Diagnostic(context, Diagnostic::Level::Error, "wasm target cannot emit object file", Range())
		);
		return false;
	}
	passManager.run(module);
	object.close();
	if (object.has_error()) {
		context.diagnostics.push_back(Diagnostic(
			context, Diagnostic::Level::Error, "wasm object write failed", Range(), "error", object.error().message()
		));
		object.clear_error();
		return false;
	}
	return true;
}

bool linkWASMObjects(ParseContext &context, std::span<const std::string> objectPaths) {
	const std::string outputPath = wasmOutputPath(context.options);
	// LLD resolves code and data relocations together. Zero is reserved for null;
	// the exported heap boundary includes aligned static data and the full stack.
	std::vector<const char *> arguments = {
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
		"--export-table", // Host imports may retain and invoke ordinary typed callbacks.
		"--threads=1",	  // The compiler can run in a single-threaded Web Worker.
		"--fatal-warnings"
	};
	for (const std::string &path : objectPaths)
		arguments.push_back(path.c_str());
	arguments.insert(arguments.end(), {"-o", outputPath.c_str()});
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

bool emitWASMModule(ParseContext &context) {
	llvm::SmallString<128> directory;
	if (std::error_code error = llvm::sys::fs::createUniqueDirectory("dynlex-wasm", directory)) {
		context.addDiagnostic(
			Diagnostic(context, Diagnostic::Level::Error, "wasm object open failed", Range(), "error", error.message())
		);
		return false;
	}
	llvm::scope_exit removeObjects([&] {
		if (std::error_code error = llvm::sys::fs::remove_directories(directory))
			llvm::errs() << "Failed to clean WebAssembly build directory: " << error.message() << '\n';
	});
	llvm::LLVMContext llvmContext;
	auto module = optimizeWASMModule(
		context, llvmContext, context.llvmModule->getModuleIdentifier(), serializeCachedModule(*context.llvmModule)
	);
	const auto partitions = prepareWASMObjectPartitions(*module);
	std::vector<std::string> objects;
	objects.reserve(partitions.size());
	ModulePartitionCloneWork work;
	for (const WASMObjectPartition &partition : partitions) {
		llvm::SmallString<128> path = directory;
		llvm::sys::path::append(path, std::to_string(objects.size()) + ".o");
		objects.push_back(path.str().str());
		auto part = cloneWASMObjectPartition(*module, partition, work);
		if (!emitWASMObject(context, *part, objects.back()))
			return false;
	}
	return linkWASMObjects(context, objects);
}
