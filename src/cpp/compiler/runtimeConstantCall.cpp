#include "runtimeConstantCall.h"
#include "compileTimeValue.h"
#include "intrinsicInfo.h"
#include "parseContext.h"
#include "patternDefinition.h"
#include <set>

namespace {
class RuntimeConstants {
	std::set<const Expression *> active;

	bool constant(Expression *expression, const BindingFrameStack &bindings) {
		if (!isCompileTimeKnown(resolveStoredCompileTimeValue(expression, bindings)))
			return false;
		auto resolved = resolveThroughBindingLayers(expression, bindings, selectedFlexBindingExpansion);
		expression = resolved.expression;
		if (!active.insert(expression).second)
			return false;
		struct Release {
			std::set<const Expression *> &active;
			const Expression *expression;
			~Release() { active.erase(expression); }
		} release{active, expression};
		if (expression->type.isMetaType() || expression->kind == Expression::Kind::Literal)
			return true;
		if (expression->kind == Expression::Kind::PatternCall)
			return callInputs(expression, resolved.bindingFrameStack);
		if (expression->kind != Expression::Kind::IntrinsicCall)
			return false; // Variables and placeholders can change between runtime evaluations.
		IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
		// A section-field selector reads its caller's storage, not just its name.
		// Captured arguments have already been followed by binding resolution.
		bool pure = kind == IntrinsicKind::Construct ? !typeHasManagedLifecycle(expression->type)
													 : intrinsicPurityKind(kind) == IntrinsicPurityKind::Pure;
		if (!pure || kind == IntrinsicKind::SectionField || expression->arguments.size() <= 1)
			return false;
		for (size_t index = 1; index < expression->arguments.size(); index++)
			if (!intrinsicArgumentIsTypeOnly(expression->intrinsicName, static_cast<int>(index)) &&
				!constant(expression->arguments[index], resolved.bindingFrameStack))
				return false;
		return true;
	}

  public:
	bool callInputs(Expression *expression, const BindingFrameStack &bindings) {
		const Instantiation *instance = expression->selectedInstantiation;
		if (!instance || instance->purity != InstantiationPurity::Pure || !expression->selectedPatternDefinition)
			return false;
		std::vector<std::pair<std::string, Expression *>> arguments;
		collectPatternCallBindingPairs(expression, expression->selectedPatternDefinition, arguments);
		requireCompilerInvariant(arguments.size() == instance->argumentTypes.size(), "constant call lost its typed arguments");
		for (size_t index = 0; index < arguments.size(); index++)
			if (!parameterRequiresCompileTimeInstantiationValue(
					instance->requiredCompileTimeParameters, arguments[index].first, instance->argumentTypes[index]
				) &&
				!constant(arguments[index].second, bindings))
				return false;
		return true;
	}
};
} // namespace

CompileTimeValue runtimeConstantCallValue(Expression *expression, const BindingFrameStack &bindings) {
	CompileTimeValue result = resolveStoredCompileTimeValue(expression, bindings);
	const Instantiation *instance = expression->selectedInstantiation;
	// Type-valued calls consume the argument types, not their runtime values.
	if (instance && instance->purity == InstantiationPurity::Pure && instance->returnType.isMetaType())
		return result;
	return isCompileTimeKnown(result) && RuntimeConstants{}.callInputs(expression, bindings) ? result : CompileTimeValue{};
}
