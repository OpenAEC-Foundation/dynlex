#include "parsedSyntaxProducts.h"
#include "bindingResolution.h"
#include "compiler.h"
#include "compilerUtils.h"
#include "expression.h"
#include "parseContext.h"
#include "patternDefinition.h"
#include "patternReference.h"
#include "section.h"
#include "sourceIdentity.h"
#include "sourcePreprocessing.h"
#include "llvm/ADT/ScopeExit.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

struct ParsedSyntaxProducts {
	std::array<std::string, 2> syntax;
	std::string bytes;
	bool complete = false;
	std::shared_ptr<const ParsedSyntaxProducts> lastComplete;
};

namespace {
// Native builds and tooling use the same unresolved expression/definition codec.
#include "incrementalParsedExpressionCache.inl"
} // namespace

ParsedSyntaxResult compileWithParsedSyntaxProducts(
	ParseContext &context, const std::shared_ptr<const ParsedSyntaxProducts> &previous, const std::function<bool()> &compile
) {
	std::unique_ptr<IncrementalParsedExpressionCache> cache;
	std::array<std::string, 2> syntax;
	bool compatible = false;
	auto &products = previous;
	auto current = [&]() -> IncrementalParsedExpressionCache & {
		if (!cache) {
			requireCompilerInvariant(
				context.hasCompleted(ParseContext::CompilationStage::ImportedFiles), "syntax cache used before source import"
			);
			syntax = {context.builtinSyntax.nonMessageIdentity, context.projectSyntax.nonMessageIdentity};
			compatible = products && products->syntax == syntax;
			std::string_view prior = compatible ? std::string_view(products->bytes) : std::string_view{};
			cache = std::make_unique<IncrementalParsedExpressionCache>(context, prior);
			if (compatible && products->lastComplete)
				cache->mergePrevious(products->lastComplete->bytes);
		}
		return *cache;
	};
	context.reuseParsedExpression = [&](Section *section, Range range, SectionType type, bool registerReferences) {
		return current().reuse(section, range, type, registerReferences);
	};
	context.recordParsedExpression = [&](Range range, SectionType type, bool registerReferences, const Expression *expression) {
		current().record(range, type, registerReferences, expression);
	};
	context.retainParsedTemporaryExpression = [&](Range range, SectionType type) {
		current().retainTemporary(range, type);
	};
	context.reuseParsedDefinition = [&](Range range, std::vector<DefinitionPatternElement> &elements) {
		return current().reuseDefinition(range, elements);
	};
	context.recordParsedDefinition = [&](Range range, const std::vector<DefinitionPatternElement> &elements) {
		current().recordDefinition(range, elements);
	};
	auto detach = llvm::scope_exit([&] {
		context.reuseParsedExpression = {};
		context.recordParsedExpression = {};
		context.retainParsedTemporaryExpression = {};
		context.reuseParsedDefinition = {};
		context.recordParsedDefinition = {};
	});
	ParsedSyntaxResult result;
	result.success = compile();
	if (!cache) {
		result.products = previous;
		if (previous)
			result.bytes = previous->bytes.size() + (previous->lastComplete ? previous->lastComplete->bytes.size() : 0);
		return result;
	}
	auto saved = std::make_shared<ParsedSyntaxProducts>();
	saved->syntax = std::move(syntax);
	saved->bytes = cache->snapshot(IncrementalParsedExpressionCache::SnapshotScope::Complete);
	saved->complete = result.success;
	if (!result.success && compatible)
		saved->lastComplete = previous->complete ? previous : previous->lastComplete;
	result.expressionsReused = cache->hits();
	result.expressionsParsed = cache->missCount();
	result.definitionsReused = cache->definitionHitCount();
	result.temporaryReused = cache->temporaryHitCount();
	result.bytes = saved->bytes.size() + (saved->lastComplete ? saved->lastComplete->bytes.size() : 0);
	result.products = std::move(saved);
	return result;
}
