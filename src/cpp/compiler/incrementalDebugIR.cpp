#include "incrementalDebugIR.h"
#include "codeLine.h"
#include "lsp/sourceFile.h"
#include "parseContext.h"
#include "sourceLineIdentity.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <filesystem>
#include <set>
#include <stack>

using Json = nlohmann::json;

IncrementalDebugIR::IncrementalDebugIR(const ParseContext &context, const std::map<const Section *, std::string> &paths) {
	for (const auto &[line, identity] : sourceLineIdentities(context, paths)) {
		std::string file = line->sourceFile->uri;
		if (file.starts_with("file://"))
			file.erase(0, 7);
		file = std::filesystem::path(file).lexically_normal().string();
		unsigned number = line->sourceFileLineIndex + 1;
		std::string key = identity.dump();
		locations.emplace(key, Json::array({file, number}));
		auto [found, inserted] = authored.try_emplace({file, number}, Json::array());
		(void)inserted;
		found->second.push_back(std::move(key));
	}
}

void IncrementalDebugIR::visitMetadata(const llvm::Function &function, const std::function<void(const llvm::MDNode *)> &visit) {
	std::set<const llvm::Metadata *> visited;
	std::stack<const llvm::Metadata *> pending;
	auto attachments = [&](const auto &value) {
		llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> metadata;
		value.getAllMetadata(metadata);
		for (const auto &[kind, node] : metadata) {
			(void)kind;
			pending.push(node);
		}
	};
	attachments(function);
	std::set<const llvm::Value *> values;
	std::stack<const llvm::Value *> operands;
	for (const auto &block : function)
		for (const auto &instruction : block) {
			attachments(instruction);
			for (const llvm::DbgRecord &record : instruction.getDbgRecordRange()) {
				pending.push(record.getDebugLoc().get());
				if (const auto *variable = llvm::dyn_cast<llvm::DbgVariableRecord>(&record))
					pending.push(variable->getVariable());
				else if (const auto *label = llvm::dyn_cast<llvm::DbgLabelRecord>(&record))
					pending.push(label->getLabel());
			}
			for (const llvm::Use &operand : instruction.operands())
				operands.push(operand.get());
		}
	while (!operands.empty()) {
		const auto *value = operands.top();
		operands.pop();
		if (!values.insert(value).second || llvm::isa<llvm::Function>(value))
			continue; // Callee inputs belong to the existing IR dependency closure.
		if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value))
			attachments(*global);
		if (const auto *user = llvm::dyn_cast<llvm::User>(value))
			for (const llvm::Use &operand : user->operands())
				operands.push(operand.get());
	}
	while (!pending.empty()) {
		const auto *metadata = pending.top();
		pending.pop();
		if (!metadata || !visited.insert(metadata).second || llvm::isa<llvm::DICompileUnit>(metadata))
			continue; // Compile-unit ownership is reattached during IR import.
		if (const auto *node = llvm::dyn_cast<llvm::MDNode>(metadata)) {
			visit(node);
			for (const llvm::MDOperand &operand : node->operands())
				pending.push(operand.get());
		}
	}
}

Json IncrementalDebugIR::capture(const llvm::Function &function) const {
	Json result = Json::object();
	auto location = [&](const llvm::DIFile *file, unsigned line) {
		if (!line)
			return;
		requireCompilerInvariant(file, "debug location has no file");
		std::string path =
			(std::filesystem::path(file->getDirectory().str()) / file->getFilename().str()).lexically_normal().string();
		auto found = authored.find({path, line});
		requireCompilerInvariant(found != authored.end(), "debug location has no authored source identity");
		for (const Json &identity : found->second) {
			const std::string &key = identity.get_ref<const std::string &>();
			result[key] = locations.at(key);
		}
	};
	visitMetadata(function, [&](const llvm::MDNode *metadata) {
		if (const auto *value = llvm::dyn_cast<llvm::DILocation>(metadata))
			location(value->getScope()->getFile(), value->getLine());
		else if (const auto *value = llvm::dyn_cast<llvm::DISubprogram>(metadata)) {
			location(value->getFile(), value->getLine());
			location(value->getFile(), value->getScopeLine());
		} else if (const auto *value = llvm::dyn_cast<llvm::DIType>(metadata))
			location(value->getFile(), value->getLine());
		else if (const auto *value = llvm::dyn_cast<llvm::DIVariable>(metadata))
			location(value->getFile(), value->getLine());
		else if (const auto *value = llvm::dyn_cast<llvm::DILexicalBlock>(metadata))
			location(value->getFile(), value->getLine());
		else if (const auto *value = llvm::dyn_cast<llvm::DILabel>(metadata))
			location(value->getFile(), value->getLine());
	});
	return result;
}

bool IncrementalDebugIR::unchanged(const Json &inputs) const {
	for (const auto &[identity, expected] : inputs.items()) {
		checked++;
		auto found = locations.find(identity);
		if (found == locations.end() || found->second != expected)
			return false;
		reused++;
	}
	return true;
}

Json captureDebugIRInputs(const ParseContext &context, const std::map<const Section *, std::string> &paths) {
	Json result = Json::object();
	if (!context.options.emitDebugInfo)
		return result;
	IncrementalDebugIR inputs(context, paths);
	for (const llvm::Function &function : *context.llvmModule)
		if (!function.isDeclaration())
			result[function.getName().str()] = inputs.capture(function);
	return result;
}
