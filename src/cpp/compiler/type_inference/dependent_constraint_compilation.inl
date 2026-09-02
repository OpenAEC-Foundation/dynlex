#include "numericLiteral.h"

struct SymbolicSignatureValue {
	DataType type;
	TypeConstraint domain = TypeConstraint::any();
	CompileTimeValue constantValue;
	std::optional<size_t> sourceArgumentIndex;
	std::optional<ConstraintIntegerTerm> integerTerm;
	std::optional<TypeConstraintTemplate> constraint;

	bool valid() const {
		return type.kind != DataType::Kind::Unresolved || isCompileTimeKnown(constantValue) ||
			   sourceArgumentIndex.has_value() || integerTerm.has_value() || constraint.has_value();
	}
	bool compileTimeKnown() const {
		return isCompileTimeKnown(constantValue) || integerTerm.has_value() || constraint.has_value();
	}
};

using SymbolicSignatureBindings = std::unordered_map<VariableReference *, SymbolicSignatureValue>;

struct SymbolicConstraintCompiler {
	ParseContext &parseContext;
	PatternDefinition &sourceDefinition;
	size_t sourcePathIndex;
	size_t parameterIndex;
	std::vector<PatternParameterSignature> &sourceParameters;
	std::unordered_set<PatternDefinition *> activeDefinitions;
	std::string failure;

	SymbolicConstraintCompiler(
		ParseContext &parseContext, PatternDefinition &sourceDefinition, size_t sourcePathIndex, size_t parameterIndex,
		std::vector<PatternParameterSignature> &sourceParameters
	)
		: parseContext(parseContext), sourceDefinition(sourceDefinition), sourcePathIndex(sourcePathIndex),
		  parameterIndex(parameterIndex), sourceParameters(sourceParameters) {}

	SymbolicSignatureValue evaluate(Expression *expression, const SymbolicSignatureBindings &bindings) {
		if (!expression)
			return fail("dependent constraint contains an empty expression");
		switch (expression->kind) {
		case Expression::Kind::Literal:
			return evaluateLiteral(expression);
		case Expression::Kind::Variable:
			return evaluateVariable(expression, bindings);
		case Expression::Kind::IntrinsicCall:
			return evaluateIntrinsic(expression, bindings);
		case Expression::Kind::PatternCall:
			return evaluatePatternCall(expression, bindings);
		case Expression::Kind::TypedPlaceholder:
		case Expression::Kind::ArrayLiteral:
		case Expression::Kind::Pending:
			return fail("dependent constraint contains an unsupported expression");
		}
		return fail("dependent constraint contains an unknown expression");
	}

  private:
	SymbolicSignatureValue fail(std::string message) {
		if (failure.empty())
			failure = std::move(message);
		return {};
	}

	SymbolicSignatureValue evaluateLiteral(Expression *expression) {
		SymbolicSignatureValue result;
		std::optional<NumericLiteralValue> number;
		if (const auto *integer = std::get_if<std::int64_t>(&expression->literalValue))
			number = *integer;
		else if (const auto *minimumMagnitude = std::get_if<MinimumSignedIntegerMagnitude>(&expression->literalValue))
			number = *minimumMagnitude;
		else if (const auto *floatingPoint = std::get_if<double>(&expression->literalValue))
			number = *floatingPoint;
		if (number) {
			result.type = numericLiteralType(*number, parseContext.options.emitSPIRV);
			result.domain = TypeConstraint::fromValueType(result.type);
			result.constantValue = numericLiteralCompileTimeValue(*number);
			if (std::optional<std::int64_t> integer = getCompileTimeIntegerValue(result.constantValue))
				result.integerTerm = ConstraintIntegerTerm::constantValue(*integer);
			return result;
		}
		if (const auto *text = std::get_if<std::string>(&expression->literalValue)) {
			result.type = {DataType::Kind::Int, 1};
			result.type.pointerDepth = 1;
			result.domain = TypeConstraint::fromValueType(result.type);
			result.constantValue = *text;
			return result;
		}
		return fail("dependent constraint contains an unknown literal");
	}

	SymbolicSignatureValue evaluateVariable(Expression *expression, const SymbolicSignatureBindings &bindings) {
		if (!expression->variable)
			return fail("dependent constraint contains a variable without an identity");
		VariableReference *definition = normalizeBindingReference(expression->variable);
		auto binding = bindings.find(definition);
		if (binding != bindings.end())
			return binding->second;
		CompileTimeValue immediate = resolveImmediateCompileTimeValue(expression);
		if (isCompileTimeKnown(immediate)) {
			SymbolicSignatureValue result;
			result.type = resolveKnownExpressionType(expression, {});
			result.domain = result.type.isDeduced() ? TypeConstraint::fromValueType(result.type) : TypeConstraint::any();
			result.constantValue = std::move(immediate);
			if (std::optional<std::int64_t> integer = getCompileTimeIntegerValue(result.constantValue))
				result.integerTerm = ConstraintIntegerTerm::constantValue(*integer);
			return result;
		}
		return fail("dependent constraint reads unrelated state");
	}

	static std::optional<std::string> stringValue(const SymbolicSignatureValue &value) {
		if (const auto *text = std::get_if<std::string>(&value.constantValue))
			return *text;
		return std::nullopt;
	}

	static std::optional<std::int64_t> integerValue(const SymbolicSignatureValue &value) {
		if (!value.integerTerm || value.integerTerm->kind != ConstraintIntegerTerm::Kind::Constant)
			return std::nullopt;
		return value.integerTerm->constant;
	}

	SymbolicSignatureValue constraintValue(TypeConstraintTemplate constraint) {
		constraint.collectDependencies();
		SymbolicSignatureValue result;
		result.type = {DataType::Kind::Constraint};
		result.domain = TypeConstraint::fromValueType(result.type);
		result.constraint = std::move(constraint);
		return result;
	}

	SymbolicSignatureValue evaluateTypeIntrinsic(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.empty())
			return fail("type construction is missing its kind");
		std::optional<std::string> kind = stringValue(arguments[0]);
		if (!kind)
			return fail("type construction requires a constant kind");
		std::optional<int> byteSize;
		if (arguments.size() > 1) {
			std::optional<std::int64_t> bitCount = integerValue(arguments[1]);
			if (!bitCount || *bitCount <= 0 || *bitCount % 8 != 0 || *bitCount / 8 > std::numeric_limits<int>::max())
				return fail("type construction requires a positive byte-aligned width");
			byteSize = static_cast<int>(*bitCount / 8);
		}
		std::optional<DataType> typeReference = makeBuiltinTypeReference(*kind, parseContext.options.emitSPIRV, byteSize);
		if (!typeReference)
			return fail("type construction names an unsupported kind");
		TypeReferenceValue reference = TypeReferenceValue::builtin(*kind, parseContext.options.emitSPIRV, byteSize);
		return constraintValue(TypeConstraintTemplate::constant(std::move(reference.constraint)));
	}

	SymbolicSignatureValue evaluateArrayIntrinsic(const std::vector<SymbolicSignatureValue> &arguments) {
		TypeConstraintTemplate result;
		result.constantPart.kind = DataType::Kind::Array;
		result.constantPart.pointerDepth = 0;
		if (arguments.empty())
			return constraintValue(std::move(result));
		size_t consumedArguments = 0;
		if (arguments[0].integerTerm) {
			result.arraySize = arguments[0].integerTerm;
			consumedArguments = 1;
		}
		if (consumedArguments < arguments.size()) {
			if (!arguments[consumedArguments].constraint)
				return fail("array construction requires an element type or constraint");
			result.elementConstraint = std::make_shared<TypeConstraintTemplate>(*arguments[consumedArguments].constraint);
			consumedArguments++;
		}
		if (consumedArguments != arguments.size())
			return fail("array construction received too many symbolic arguments");
		return constraintValue(std::move(result));
	}

	SymbolicSignatureValue evaluateVectorIntrinsic(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.empty() || !arguments[0].integerTerm)
			return fail("vector construction requires a symbolic integer length");
		TypeConstraintTemplate result;
		result.constantPart.kind = DataType::Kind::Vector;
		result.constantPart.pointerDepth = 0;
		result.arraySize = arguments[0].integerTerm;
		if (arguments.size() > 1) {
			if (!arguments[1].constraint)
				return fail("vector construction requires an element type or constraint");
			result.elementConstraint = std::make_shared<TypeConstraintTemplate>(*arguments[1].constraint);
		}
		return constraintValue(std::move(result));
	}

	SymbolicSignatureValue evaluateMatrixIntrinsic(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.size() < 2 || !arguments[0].integerTerm || !arguments[1].integerTerm)
			return fail("matrix construction requires symbolic row and column counts");
		TypeConstraintTemplate result;
		result.constantPart.kind = DataType::Kind::Matrix;
		result.constantPart.pointerDepth = 0;
		result.matrixRows = arguments[0].integerTerm;
		result.matrixColumns = arguments[1].integerTerm;
		if (arguments.size() > 2) {
			if (!arguments[2].constraint)
				return fail("matrix construction requires an element type or constraint");
			result.elementConstraint = std::make_shared<TypeConstraintTemplate>(*arguments[2].constraint);
		}
		return constraintValue(std::move(result));
	}

	SymbolicSignatureValue evaluateTypeExtent(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.size() != 2 || !arguments[0].sourceArgumentIndex)
			return fail("type extent must inspect an earlier parameter directly");
		std::optional<std::int64_t> dimension = integerValue(arguments[1]);
		if (!dimension || *dimension < 0 || *dimension > std::numeric_limits<int>::max())
			return fail("type extent requires a constant non-negative dimension");
		const TypeConstraint &domain = arguments[0].domain;
		bool supportsDimension =
			domain.kind &&
			(((*domain.kind == DataType::Kind::Array || *domain.kind == DataType::Kind::Vector) && *dimension == 0) ||
			 (*domain.kind == DataType::Kind::Matrix && (*dimension == 0 || *dimension == 1)));
		if (!supportsDimension)
			return fail("type extent is not defined for every type accepted by the earlier parameter");
		SymbolicSignatureValue result;
		result.type = {DataType::Kind::Int, 4};
		result.domain = TypeConstraint::fromValueType(result.type);
		result.integerTerm = ConstraintIntegerTerm::typeExtent(*arguments[0].sourceArgumentIndex, static_cast<int>(*dimension));
		return result;
	}

	SymbolicSignatureValue evaluateTypeOf(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.size() != 1)
			return fail("type reflection requires one value");
		if (arguments[0].sourceArgumentIndex)
			return constraintValue(TypeConstraintTemplate::projectedType(*arguments[0].sourceArgumentIndex));
		if (!arguments[0].type.isDeduced())
			return fail("type reflection received an unresolved value");
		return constraintValue(TypeConstraintTemplate::constant(TypeConstraint::fromValueType(arguments[0].type)));
	}

	SymbolicSignatureValue evaluateElementType(const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.size() != 1)
			return fail("element type reflection requires one value");
		const TypeConstraint &domain = arguments[0].domain;
		bool aggregateDomain =
			domain.kind && (*domain.kind == DataType::Kind::Array || *domain.kind == DataType::Kind::Vector ||
							*domain.kind == DataType::Kind::Matrix);
		if (!aggregateDomain)
			return fail("element type is not defined for every type accepted by the earlier parameter");
		if (arguments[0].sourceArgumentIndex)
			return constraintValue(TypeConstraintTemplate::projectedType(*arguments[0].sourceArgumentIndex, 1));
		if (arguments[0].constraint && arguments[0].constraint->elementConstraint)
			return constraintValue(*arguments[0].constraint->elementConstraint);
		if (arguments[0].type.hasAggregateElementType()) {
			return constraintValue(
				TypeConstraintTemplate::constant(TypeConstraint::fromValueType(arguments[0].type.aggregateElementType()))
			);
		}
		return fail("element type reflection received an aggregate without an element domain");
	}

	SymbolicSignatureValue evaluateIntegerArithmetic(IntrinsicKind kind, const std::vector<SymbolicSignatureValue> &arguments) {
		if (arguments.size() != 2 || !arguments[0].integerTerm || !arguments[1].integerTerm)
			return fail("dependent integer operation requires integer terms");
		ConstraintIntegerTerm::Kind operation;
		switch (kind) {
		case IntrinsicKind::Add:
			operation = ConstraintIntegerTerm::Kind::Add;
			break;
		case IntrinsicKind::Subtract:
			operation = ConstraintIntegerTerm::Kind::Subtract;
			break;
		case IntrinsicKind::Multiply:
			operation = ConstraintIntegerTerm::Kind::Multiply;
			break;
		case IntrinsicKind::Divide:
			operation = ConstraintIntegerTerm::Kind::Divide;
			break;
		case IntrinsicKind::Modulo:
			operation = ConstraintIntegerTerm::Kind::Modulo;
			break;
		default:
			return fail("dependent constraint contains an unsupported integer operation");
		}
		SymbolicSignatureValue result;
		result.type = {DataType::Kind::Int, 8};
		result.domain = TypeConstraint::fromValueType(result.type);
		result.integerTerm = ConstraintIntegerTerm::binary(operation, *arguments[0].integerTerm, *arguments[1].integerTerm);
		return result;
	}

	SymbolicSignatureValue evaluateIntrinsic(Expression *expression, const SymbolicSignatureBindings &bindings) {
		const IntrinsicInfo *info = findIntrinsic(expression->intrinsicName);
		if (!info || info->purity == IntrinsicPurityKind::Impure || info->purity == IntrinsicPurityKind::Custom)
			return fail("dependent constraint calls an impure or unknown intrinsic");
		std::vector<SymbolicSignatureValue> arguments;
		for (size_t index = 1; index < expression->arguments.size(); index++) {
			SymbolicSignatureValue argument = evaluate(expression->arguments[index], bindings);
			if (!argument.valid())
				return {};
			arguments.push_back(std::move(argument));
		}
		IntrinsicKind kind = intrinsicKind(expression->intrinsicName);
		if (kind == IntrinsicKind::Type)
			return evaluateTypeIntrinsic(arguments);
		if (kind == IntrinsicKind::Array)
			return evaluateArrayIntrinsic(arguments);
		if (kind == IntrinsicKind::Vector)
			return evaluateVectorIntrinsic(arguments);
		if (kind == IntrinsicKind::Matrix)
			return evaluateMatrixIntrinsic(arguments);
		if (kind == IntrinsicKind::TypeExtent)
			return evaluateTypeExtent(arguments);
		if (kind == IntrinsicKind::TypeOf)
			return evaluateTypeOf(arguments);
		if (kind == IntrinsicKind::ElementType)
			return evaluateElementType(arguments);
		if (kind == IntrinsicKind::Number) {
			if (!arguments.empty())
				return fail("number constraint received arguments");
			TypeConstraint constraint = TypeConstraint::any();
			constraint.requiresNumeric = true;
			return constraintValue(TypeConstraintTemplate::constant(std::move(constraint)));
		}
		if (kind == IntrinsicKind::Fix) {
			if (arguments.size() != 1 || !arguments[0].constraint)
				return fail("fixed constraint requires a type or constraint");
			TypeConstraintTemplate result = *arguments[0].constraint;
			result.constantPart.requiresCompileTimeValue = true;
			return constraintValue(std::move(result));
		}
		if (kind == IntrinsicKind::Add || kind == IntrinsicKind::Subtract || kind == IntrinsicKind::Multiply ||
			kind == IntrinsicKind::Divide || kind == IntrinsicKind::Modulo)
			return evaluateIntegerArithmetic(kind, arguments);
		return fail("dependent constraint uses an operation without a symbolic representation");
	}

	static Expression *singleReplacementExpression(PatternDefinition *definition) {
		if (!definition || !definition->section)
			return nullptr;
		Expression *result = nullptr;
		size_t count = 0;
		bool onlyReplacement = true;
		definition->section->forEachDefinitionBodySection([&](Section *bodySection) {
			if (bodySection->type != SectionType::Replacement) {
				onlyReplacement = false;
				return false;
			}
			for (CodeLine *line : bodySection->codeLines) {
				if (!line || !line->expression)
					continue;
				result = line->expression;
				count++;
			}
			return true;
		});
		return onlyReplacement && count == 1 ? result : nullptr;
	}

	bool candidateAccepts(
		PatternDefinition *candidate, size_t pathIndex, const std::vector<SymbolicSignatureValue> &arguments,
		std::vector<TypeConstraint> &domains
	) {
		if (pathIndex >= candidate->signaturePaths.size())
			return false;
		const auto &parameters = candidate->signaturePaths[pathIndex].parameters;
		if (parameters.size() != arguments.size())
			return false;
		domains.clear();
		domains.reserve(parameters.size());
		for (size_t index = 0; index < parameters.size(); index++) {
			if (parameters[index].constraint.isDependent())
				return false;
			TypeConstraint parameterDomain = parameters[index].constraint.structuralEnvelope();
			if (parameters[index].requiresCompileTimeValue && !arguments[index].compileTimeKnown())
				return false;
			if (!parameterDomain.contains(arguments[index].domain))
				return false;
			domains.push_back(std::move(parameterDomain));
		}
		return true;
	}

	SymbolicSignatureValue evaluatePatternCall(Expression *expression, const SymbolicSignatureBindings &bindings) {
		if (!expression->patternMatch)
			return fail("dependent constraint contains an unresolved call");
		std::vector<SymbolicSignatureValue> arguments;
		for (Expression *argumentExpression : expression->arguments) {
			SymbolicSignatureValue argument = evaluate(argumentExpression, bindings);
			if (!argument.valid())
				return {};
			arguments.push_back(std::move(argument));
		}
		struct SymbolicOverload {
			PatternDefinition *definition;
			size_t pathIndex;
			std::vector<TypeConstraint> domains;
		};
		std::vector<SymbolicOverload> viableOverloads;
		for (PatternDefinition *candidate : expression->patternMatch->matchingDefinitions) {
			for (size_t pathIndex : matchingPatternPathIndices(expression->patternMatch->nodesPassed, candidate)) {
				std::vector<TypeConstraint> domains;
				if (!candidateAccepts(candidate, pathIndex, arguments, domains))
					continue;
				viableOverloads.push_back({candidate, pathIndex, std::move(domains)});
			}
		}
		if (viableOverloads.empty())
			return fail("dependent constraint call has no overload valid for the complete symbolic domain");

		std::vector<size_t> maximalOverloads;
		for (size_t candidateIndex = 0; candidateIndex < viableOverloads.size(); candidateIndex++) {
			bool dominated = false;
			for (size_t otherIndex = 0; otherIndex < viableOverloads.size(); otherIndex++) {
				if (candidateIndex == otherIndex)
					continue;
				if (compareConstraintSpecificity(
						viableOverloads[candidateIndex].domains, viableOverloads[otherIndex].domains
					) == ConstraintSpecificity::RightMoreSpecific) {
					dominated = true;
					break;
				}
			}
			if (!dominated)
				maximalOverloads.push_back(candidateIndex);
		}
		requireCompilerInvariant(!maximalOverloads.empty(), "symbolic overload domains have no maximal candidate");
		const SymbolicOverload &selectedOverload = viableOverloads[maximalOverloads.front()];
		for (size_t maximalIndex : maximalOverloads) {
			const SymbolicOverload &other = viableOverloads[maximalIndex];
			if (other.definition != selectedOverload.definition || other.pathIndex != selectedOverload.pathIndex)
				return fail("dependent constraint call is ambiguous for the symbolic domain");
		}
		PatternDefinition *selected = selectedOverload.definition;
		size_t selectedPath = selectedOverload.pathIndex;
		Expression *body = singleReplacementExpression(selected);
		if (!body)
			return fail("dependent constraint calls a function without one pure replacement expression");
		if (!activeDefinitions.insert(selected).second)
			return fail("dependent constraint contains recursive symbolic evaluation");
		SymbolicSignatureBindings bodyBindings = bindings;
		size_t argumentIndex = 0;
		forEachPatternParameterName(selected, selectedPath, [&](const std::string &name, PatternTreeNode *, size_t) {
			requireCompilerInvariant(argumentIndex < arguments.size(), "symbolic call has too few arguments");
			VariableReference *parameterDefinition = findPatternParameterDefinition(selected, name);
			requireCompilerInvariant(parameterDefinition, "symbolic call parameter has no definition identity");
			bodyBindings[parameterDefinition] = arguments[argumentIndex++];
		});
		requireCompilerInvariant(argumentIndex == arguments.size(), "symbolic call has too many arguments");
		SymbolicSignatureValue result = evaluate(body, bodyBindings);
		activeDefinitions.erase(selected);
		return result;
	}
};

static const DefinitionPatternElement *
signatureElement(PatternDefinition &definition, size_t pathIndex, size_t parameterIndex) {
	requireCompilerInvariant(pathIndex < definition.indexedPaths.size(), "signature path is out of range");
	size_t currentParameter = 0;
	const PatternElement *pathElement = nullptr;
	for (const PatternElement &element : definition.indexedPaths[pathIndex]) {
		if (element.type != PatternElement::Type::Variable)
			continue;
		if (currentParameter++ == parameterIndex) {
			pathElement = &element;
			break;
		}
	}
	requireCompilerInvariant(pathElement, "signature parameter is absent from its indexed path");
	return matchedPatternParameterElement(&definition, pathElement->text, pathElement->startPos);
}

static bool initializePatternPathSignatures(ParseContext &parseContext) {
	bool valid = true;
	std::function<void(Section *)> visit = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			definition->signaturePaths.clear();
			definition->signaturePaths.resize(definition->indexedPaths.size());
			for (size_t pathIndex = 0; pathIndex < definition->indexedPaths.size(); pathIndex++) {
				size_t parameterIndex = 0;
				forEachPatternParameterName(
					definition, pathIndex,
					[&](const std::string &name, PatternTreeNode *, size_t startPos) {
					const DefinitionPatternElement *element = signatureElement(*definition, pathIndex, parameterIndex++);
					if (!element) {
						valid = false;
						return;
					}
					PatternParameterSignature signature;
					signature.elementStartPos = startPos;
					Variable *parameterVariable = section->findVariable(name);
					bool variableHasConstraint = parameterVariable && parameterVariable->declaredTypeConstraint.isResolved();
					bool elementHasConstraint = element->resolvedTypeConstraint.isResolved();
					signature.constraint = TypeConstraintTemplate::constant(
						elementHasConstraint
							? element->resolvedTypeConstraint
							: (variableHasConstraint ? parameterVariable->declaredTypeConstraint : TypeConstraint::any())
					);
					signature.staticParameterType =
						elementHasConstraint ? element->resolvedParameterType
											 : (variableHasConstraint ? parameterVariable->declaredType : DataType{});
					signature.requiresCompileTimeValue = signature.constraint.constantPart.requiresCompileTimeValue;
					signature.acceptsUnresolvedType = !elementHasConstraint && !variableHasConstraint;
					signature.hasExplicitTypeConstraint = !element->typeConstraintName.empty() || variableHasConstraint;
					definition->signaturePaths[pathIndex].parameters.push_back(std::move(signature));
				}
				);
			}
		}
		for (Section *child : section->children)
			visit(child);
	};
	visit(parseContext.mainSection);
	return valid;
}

static bool compileDependentPatternSignatures(ParseContext &parseContext) {
	if (!initializePatternPathSignatures(parseContext))
		return false;
	std::function<bool(Section *)> visit = [&](Section *section) {
		for (PatternDefinition *definition : section->patternDefinitions) {
			for (size_t pathIndex = 0; pathIndex < definition->signaturePaths.size(); pathIndex++) {
				auto &parameters = definition->signaturePaths[pathIndex].parameters;
				for (size_t parameterIndex = 0; parameterIndex < parameters.size(); parameterIndex++) {
					const DefinitionPatternElement *element = signatureElement(*definition, pathIndex, parameterIndex);
					if (!element->hasDependentTypeConstraint)
						continue;
					Range constraintRange = patternElementTypeConstraintRange(*definition, *element);
					Expression *expression = createTypeConstraintExpression(parseContext, definition->section, constraintRange);
					if (!expression) {
						parseContext.diagnostics.push_back(
							unknownTypeConstraintDiagnostic(parseContext, constraintRange, element->typeConstraintName)
						);
						return false;
					}
					SymbolicSignatureBindings bindings;
					for (size_t earlierIndex = 0; earlierIndex < parameterIndex; earlierIndex++) {
						const DefinitionPatternElement *earlierElement = signatureElement(*definition, pathIndex, earlierIndex);
						VariableReference *parameterDefinition =
							findPatternParameterDefinition(definition, earlierElement->text);
						requireCompilerInvariant(
							parameterDefinition, "dependent signature parameter has no definition identity"
						);
						if (bindings.contains(parameterDefinition)) {
							parseContext.diagnostics.push_back(Diagnostic(
								parseContext, Diagnostic::Level::Error, "dependent type constraint parameter ambiguous",
								constraintRange, "parameter", earlierElement->text
							));
							destroyTypeConstraintExpression(expression);
							return false;
						}
						SymbolicSignatureValue value;
						value.type = parameters[earlierIndex].staticParameterType;
						value.domain = parameters[earlierIndex].constraint.structuralEnvelope();
						value.sourceArgumentIndex = earlierIndex;
						if (value.domain.kind && *value.domain.kind == DataType::Kind::Int) {
							value.integerTerm = ConstraintIntegerTerm::fixedArgument(earlierIndex);
						}
						bindings.emplace(parameterDefinition, std::move(value));
					}
					for (size_t laterIndex = parameterIndex; laterIndex < parameters.size(); laterIndex++) {
						const DefinitionPatternElement *laterElement = signatureElement(*definition, pathIndex, laterIndex);
						VariableReference *laterDefinition = findPatternParameterDefinition(definition, laterElement->text);
						if (!laterDefinition)
							continue;
						bool referenced = visitExpressionTree(expression, [&](Expression *current) {
							return current->kind == Expression::Kind::Variable && current->variable &&
								   normalizeBindingReference(current->variable) == laterDefinition;
						});
						if (!referenced)
							continue;
						parseContext.diagnostics.push_back(Diagnostic(
							parseContext, Diagnostic::Level::Error, "type constraint references later parameter",
							constraintRange, "type_constraint", element->typeConstraintName, "parameter", laterElement->text
						));
						destroyTypeConstraintExpression(expression);
						return false;
					}
					SymbolicConstraintCompiler compiler{parseContext, *definition, pathIndex, parameterIndex, parameters};
					SymbolicSignatureValue result = compiler.evaluate(expression, bindings);
					destroyTypeConstraintExpression(expression);
					if (!result.valid() || !result.constraint) {
						parseContext.diagnostics.push_back(Diagnostic(
							parseContext, Diagnostic::Level::Error, "dependent type constraint not representable",
							constraintRange, "type_constraint", element->typeConstraintName, "reason",
							compiler.failure.empty() ? "it does not produce a type or constraint" : compiler.failure
						));
						return false;
					}
					result.constraint->collectDependencies();
					for (const ConstraintDependency &dependency : result.constraint->dependencies) {
						if (dependency.sourceArgumentIndex >= parameterIndex)
							crashCompilerBug("dependent constraint template references a non-earlier argument");
						if (dependency.access == ConstraintAccess::CompileTimeValue) {
							parameters[dependency.sourceArgumentIndex].requiresCompileTimeValue = true;
							parameters[dependency.sourceArgumentIndex].constraint.constantPart.requiresCompileTimeValue = true;
						}
					}
					parameters[parameterIndex].constraint = std::move(*result.constraint);
					parameters[parameterIndex].staticParameterType = {};
				}
			}
		}
		for (Section *child : section->children) {
			if (!visit(child))
				return false;
		}
		return true;
	};
	return visit(parseContext.mainSection);
}
