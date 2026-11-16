#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include <algorithm>
#include <vector>

inline void sortModuleSymbols(llvm::Module &module) {
	auto byName = [](const auto &left, const auto &right) {
		return left.getName() < right.getName();
	};
	std::vector<llvm::GlobalVariable *> globals;
	for (llvm::GlobalVariable &global : module.globals())
		globals.push_back(&global);
	std::sort(globals.begin(), globals.end(), [&](const auto *left, const auto *right) {
		return byName(*left, *right);
	});
	for (llvm::GlobalVariable *global : globals)
		module.removeGlobalVariable(global);
	for (llvm::GlobalVariable *global : globals)
		module.insertGlobalVariable(global);
	module.getFunctionList().sort(byName);
}
