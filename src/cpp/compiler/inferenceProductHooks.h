#pragma once
#include "compileTimeInfo.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct Section;
struct Instantiation;
struct InstantiationKey;
struct Diagnostic;
struct VariableStorageScope;
struct VariableReference;
struct Expression;
struct InferenceReadOwner;
struct PatternDefinition;
namespace lsp {
struct SourceFile;
}

struct InferenceProductHooks {
	virtual ~InferenceProductHooks() = default;
	virtual std::optional<std::string> storageIdentity(Section *, const InstantiationKey &) const = 0;
	virtual void completed(const Instantiation &, std::vector<Diagnostic>) = 0;
	virtual void globalInput(const InferenceReadOwner &, VariableReference *) = 0;
	virtual void
	conversionLookup(const InferenceReadOwner &, const lsp::SourceFile &, bool, const std::vector<PatternDefinition *> &) = 0;
	virtual void discarded(
		Section *, const InstantiationKey &, const Instantiation &, bool,
		const std::function<CompileTimeValue(const Expression *)> &
	) = 0;
};
