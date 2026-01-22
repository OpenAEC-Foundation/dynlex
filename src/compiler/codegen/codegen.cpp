#include "codegen.h"
#include "patternReference.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

static bool generateSectionCode(ParseContext &context, Section *section);
static llvm::Value *generatePatternCode(ParseContext &context, PatternReference *reference);
static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<llvm::Value *> &args);

bool generateCode(ParseContext &context) {
	// Initialize LLVM state
	context.llvmContext = new llvm::LLVMContext();
	context.llvmModule = new llvm::Module("3bx_module", *context.llvmContext);
	context.llvmBuilder = new llvm::IRBuilder<>(*context.llvmContext);

	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);

	// Create main function
	llvm::FunctionType *mainType = llvm::FunctionType::get(builder.getInt32Ty(), false);
	llvm::Function *mainFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", context.llvmModule);

	llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context.llvmContext, "entry", mainFunc);
	builder.SetInsertPoint(entry);

	// Generate code for the main section
	if (!generateSectionCode(context, context.mainSection)) {
		return false;
	}

	// Return 0 from main
	builder.CreateRet(builder.getInt32(0));

	// Verify the module
	std::string error;
	llvm::raw_string_ostream errorStream(error);
	if (llvm::verifyModule(*context.llvmModule, &errorStream)) {
		context.diagnostics.push_back(Diagnostic(Diagnostic::Level::Error, "LLVM verification failed: " + error, Range()));
		return false;
	}

	// Output based on options
	if (context.options.emitLLVM) {
		std::string outputPath = context.options.outputPath;
		if (outputPath.empty()) {
			outputPath = context.options.inputPath + ".ll";
		}
		std::error_code ec;
		llvm::raw_fd_ostream out(outputPath, ec);
		if (ec) {
			context.diagnostics.push_back(
				Diagnostic(Diagnostic::Level::Error, "Failed to open output file: " + ec.message(), Range())
			);
			return false;
		}
		context.llvmModule->print(out, nullptr);
	} else {
		// TODO: Compile to executable
		context.diagnostics.push_back(
			Diagnostic(Diagnostic::Level::Error, "Compiling to executable not yet implemented", Range())
		);
		return false;
	}

	return true;
}

static bool generateSectionCode(ParseContext &context, Section *section) {
	for (PatternReference *reference : section->patternReferences) {
		if (reference->match) {
			generatePatternCode(context, reference);
		}
	}
	return true;
}

static llvm::Value *generatePatternCode(ParseContext &context, PatternReference *reference) {
	// Get the matched pattern's section (effect or expression definition)
	Section *matchedSection = reference->match->matchedEndNode->matchingSection;

	// The body is in a child section (execute: for effects, get: for expressions)
	for (Section *child : matchedSection->children) {
		generateSectionCode(context, child);
	}

	// TODO: Handle variable substitution and return values
	return nullptr;
}

[[maybe_unused]] static llvm::Value *
generateIntrinsicCode(ParseContext &context, const std::string &name, const std::vector<llvm::Value *> &args) {
	(void)context;
	(void)name;
	(void)args;
	// TODO: Handle intrinsics (store, add, return, call)
	return nullptr;
}
