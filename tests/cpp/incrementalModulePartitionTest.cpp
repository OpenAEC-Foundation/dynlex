#include "compiler/incrementalModulePartition.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdlib>
#include <string>

static void expect(bool condition, const char *message) {
	if (!condition) {
		llvm::errs() << message << '\n';
		std::exit(1);
	}
}

static std::unique_ptr<llvm::Module> parse(llvm::LLVMContext &context, const std::string &source) {
	llvm::SMDiagnostic error;
	auto module = llvm::parseAssemblyString(source, error, context);
	if (!module)
		error.print("partition test", llvm::errs());
	expect(module && !llvm::verifyModule(*module, &llvm::errs()), "invalid test module");
	return module;
}

int main() {
	llvm::LLVMContext context;
	const std::string source = R"(
        @value = global i32 9, align 16, !test !0
        @meta = global i32 5, !test !1
        @annotation = global i32 3
        define i32 @target(i32 %x) nounwind { ret i32 %x }
        define i32 @unrelated() { ret i32 0 }
        define i32 @entry(i32 %x) {
            %call = call i32 @target(i32 %x)
            %value = load i32, ptr @value, !test !0
            %result = add i32 %call, %value
            ret i32 %result
        }
        !0 = !{ptr @meta}
        !1 = !{ptr @value}
        !named = !{!2}
        !2 = !{ptr @annotation}
        !llvm.module.flags = !{!3}
        !3 = !{i32 1, !"wchar_size", i32 4}
    )";
	auto module = parse(context, source);
	std::array<const llvm::GlobalValue *, 1> selected{module->getFunction("entry")};
	ModulePartitionCloneWork work;
	auto copy = cloneModulePartition(*module, selected, work);
	expect(!llvm::verifyModule(*copy, &llvm::errs()), "partition contains foreign values or invalid metadata");
	expect(work.definitions == 1 && work.declarations == 4, "partition copied unrelated globals");
	expect(std::distance(copy->global_values().begin(), copy->global_values().end()) == 5, "wrong closure size");
	expect(copy->getFunction("target")->isDeclaration(), "copied a dependency body");
	expect(copy->getFunction("target")->hasFnAttribute(llvm::Attribute::NoUnwind), "lost declaration attributes");
	expect(copy->getGlobalVariable("value")->getAlign() == llvm::Align(16), "lost global alignment");
	expect(copy->getGlobalVariable("value")->isDeclaration(), "copied an unrelated initializer");
	expect(copy->getModuleFlag("wchar_size") == module->getModuleFlag("wchar_size"), "lost module flags");
	expect(module->getFunction("unrelated") && module->getFunction("entry")->getParent() == module.get(), "source changed");
	auto weak = parse(context, R"(
        @weak_value = extern_weak global i32
        declare extern_weak i32 @weak_function()
        define i32 @read_weak() { %r = call i32 @weak_function() %v = load i32, ptr @weak_value %a = add i32 %r, %v ret i32 %a }
    )");
	std::array<const llvm::GlobalValue *, 1> weakDefinitions{weak->getFunction("read_weak")};
	ModulePartitionCloneWork weakWork;
	auto weakCopy = cloneModulePartition(*weak, weakDefinitions, weakWork);
	expect(!llvm::verifyModule(*weakCopy, &llvm::errs()), "invalid weak declaration partition");
	expect(weakCopy->getFunction("weak_function")->hasExternalWeakLinkage(), "lost weak function linkage");
	expect(weakCopy->getGlobalVariable("weak_value")->hasExternalWeakLinkage(), "lost weak global linkage");

	auto aliases = parse(context, R"(
        @alias = alias i32 (), ptr @actual
        @indirect = ifunc i32 (), ptr @resolve
        define i32 @actual() { ret i32 7 }
        define ptr @resolve() { ret ptr @actual }
        define i32 @caller() { %r = call i32 @alias() ret i32 %r }
    )");
	std::array<const llvm::GlobalValue *, 4> aliasDefinitions{
		aliases->getNamedAlias("alias"), aliases->getNamedIFunc("indirect"), aliases->getFunction("actual"),
		aliases->getFunction("resolve")
	};
	ModulePartitionCloneWork aliasWork;
	auto aliasCopy = cloneModulePartition(*aliases, aliasDefinitions, aliasWork);
	expect(!llvm::verifyModule(*aliasCopy, &llvm::errs()), "invalid alias/ifunc partition");
	expect(aliasWork.definitions == 4 && aliasWork.declarations == 0, "alias partition copied unrelated globals");
	expect(aliasCopy->getNamedAlias("alias")->getAliasee() == aliasCopy->getFunction("actual"), "foreign aliasee");
	expect(aliasCopy->getNamedIFunc("indirect")->getResolver() == aliasCopy->getFunction("resolve"), "foreign resolver");
	std::array<const llvm::GlobalValue *, 1> caller{aliases->getFunction("caller")};
	ModulePartitionCloneWork callerWork;
	auto callerCopy = cloneModulePartition(*aliases, caller, callerWork);
	expect(!llvm::verifyModule(*callerCopy, &llvm::errs()), "invalid external alias declaration");
	expect(callerCopy->getFunction("alias")->isDeclaration() && !callerCopy->getNamedAlias("alias"), "alias was not declared");
	expect(callerWork.definitions == 1 && callerWork.declarations == 1, "external alias pulled its target body");

	auto blocks = parse(context, R"(
        $group = comdat any
        @table = constant [1 x ptr] [ptr blockaddress(@jump, %target)], comdat($group)
        define i32 @jump() comdat($group) { entry: br label %target target: ret i32 7 }
    )");
	std::array<const llvm::GlobalValue *, 2> blockDefinitions{blocks->getGlobalVariable("table"), blocks->getFunction("jump")};
	ModulePartitionCloneWork blockWork;
	auto blockCopy = cloneModulePartition(*blocks, blockDefinitions, blockWork);
	expect(!llvm::verifyModule(*blockCopy, &llvm::errs()), "invalid block-address partition");
	auto *table = blockCopy->getGlobalVariable("table");
	auto *address = llvm::cast<llvm::BlockAddress>(table->getInitializer()->getOperand(0));
	expect(address->getFunction() == blockCopy->getFunction("jump"), "foreign block address");
	expect(table->getComdat() == blockCopy->getFunction("jump")->getComdat(), "COMDAT identity changed");
	expect(table->getComdat() != blocks->getGlobalVariable("table")->getComdat(), "COMDAT belongs to source module");

	auto cycles = parse(context, R"(
        @left = constant ptr @right
        @right = constant ptr @left
    )");
	std::array<const llvm::GlobalValue *, 2> cycleDefinitions{
		cycles->getGlobalVariable("left"), cycles->getGlobalVariable("right")
	};
	ModulePartitionCloneWork cycleWork;
	auto cycleCopy = cloneModulePartition(*cycles, cycleDefinitions, cycleWork);
	expect(!llvm::verifyModule(*cycleCopy, &llvm::errs()), "invalid cyclic initializer partition");
	expect(
		cycleCopy->getGlobalVariable("left")->getInitializer() == cycleCopy->getGlobalVariable("right"), "foreign cycle edge"
	);
	expect(cycleCopy->getGlobalVariable("right")->getInitializer() == cycleCopy->getGlobalVariable("left"), "cycle was split");
	return 0;
}
