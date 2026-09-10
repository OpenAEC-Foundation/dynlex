#include "executionTrace.h"
#include "compiler.h"
#include "pathUtils.h"
#include "sourceFile.h"
#include "textEncoding.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

void emitExecutionTrace(ParseContext &context, const Range &range, llvm::Value *condition) {
	if (!context.options.traceExecution || context.options.emitSPIRV)
		return;
	const SourceLocation start = range.sourceStart(), end = range.sourceEnd();
	requireCompilerInvariant(start.sourceFile && end.sourceFile, "execution trace has no authored source range");
	if (pathutil::toAbsoluteUri(start.sourceFile->uri) != pathutil::toAbsoluteUri(context.options.inputPath))
		return;
	auto &builder = static_cast<llvm::IRBuilder<> &>(*context.llvmBuilder);
	const auto format = "DYNLEX|TRACE|%d|%d|%d|%d|%d\n";
	auto *signature = llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, true);
	auto print = context.llvmModule->getOrInsertFunction("printf", signature);
	llvm::Value *outcome = condition ? builder.CreateZExt(condition, builder.getInt32Ty()) : builder.getInt32(-1);
	builder.CreateCall(
		print, {builder.CreateGlobalString(format), builder.getInt32(start.sourceFileLineIndex),
				builder.getInt32(lsp::utf16Column(start.sourceFile->getLine(start.sourceFileLineIndex), start.column)),
				builder.getInt32(end.sourceFileLineIndex),
				builder.getInt32(lsp::utf16Column(end.sourceFile->getLine(end.sourceFileLineIndex), end.column)), outcome}
	);
}
