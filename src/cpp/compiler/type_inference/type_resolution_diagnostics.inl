static bool isWholeNumberLiteral(Expression *expr) {
	if (!expr || expr->kind != Expression::Kind::Literal || !std::holds_alternative<double>(expr->literalValue))
		return false;
	std::string_view literalText = expr->range.subString;
	if (literalText.find('.') != std::string_view::npos || literalText.find('e') != std::string_view::npos ||
		literalText.find('E') != std::string_view::npos)
		return false;
	double value = std::get<double>(expr->literalValue);
	return std::trunc(value) == value;
}

static std::string makeFloatLiteralReplacement(Expression *expr) {
	if (!isWholeNumberLiteral(expr))
		return {};
	return (std::string)expr->range.subString + ".0";
}

static std::string formatTypeList(const std::vector<DataType> &types, ParseContext &parseContext) {
	std::string out;
	for (size_t i = 0; i < types.size(); i++) {
		if (i > 0)
			out += ", ";
		out += typeToUserName(types[i], parseContext);
	}
	return out;
}

static DataType concretizeClassType(DataType type) {
	if (type.kind == DataType::Kind::Class && type.classDefinition && type.classInstIndex < 0 &&
		!type.classDefinition->instantiations.empty()) {
		type.classInstIndex = 0;
	}
	return type;
}

static DataType resolveBuiltInPropertyType(const DataType &ownerType, const std::string &fieldName) {
	if (fieldName == "data" && ownerType.isBytePointer())
		return ownerType;
	return {};
}

static bool isLogicalOperandType(const DataType &type) { return type.kind == DataType::Kind::Bool; }

static bool isBitwiseOperandType(const DataType &type) { return type.isInteger(); }

static std::string extractFieldName(Expression *expr) {
	if (!expr)
		return {};
	if (auto *str = std::get_if<std::string>(&expr->literalValue))
		return *str;
	if (expr->kind == Expression::Kind::Variable && expr->variable)
		return expr->variable->name;
	return {};
}

static std::string diagnosticExpressionText(Expression *expr) {
	if (!expr)
		return {};

	std::string text = (std::string)expr->range.subString;
	if (!expr->range.line)
		return text;

	std::string_view lineText = expr->range.line->patternText;
	if (lineText.empty() || lineText == expr->range.subString || !lineText.ends_with(expr->range.subString))
		return text;

	size_t missingPrefixLength = lineText.size() - expr->range.subString.size();
	if (missingPrefixLength == 0 || !std::isspace(static_cast<unsigned char>(lineText[missingPrefixLength - 1])))
		return text;

	return (std::string)lineText;
}

struct DiagnosticExpressionSnapshot {
	Range range;
	std::string text;
};

static DiagnosticExpressionSnapshot captureDiagnosticExpressionSnapshot(Expression *expr) {
	if (!expr)
		return {};
	return {expr->range, diagnosticExpressionText(expr)};
}

static Diagnostic buildTypeFailureDiagnostic(
	const ParseContext &parseContext, const DiagnosticExpressionSnapshot &snapshot, const std::string &detail,
	const std::vector<RelatedInfo> &relatedInfo = {}
) {
	std::string expressionText = snapshot.text;
	Diagnostic diagnostic;
	if (detail.empty()) {
		diagnostic = Diagnostic(
			parseContext, Diagnostic::Level::Error, "expression type inference failed", snapshot.range, "expression",
			expressionText
		);
	} else {
		diagnostic = Diagnostic(
			parseContext, Diagnostic::Level::Error, "expression type inference failed", "with detail", snapshot.range,
			"expression", expressionText, "detail", detail
		);
	}
	diagnostic.relatedInfo = relatedInfo;
	return diagnostic;
}

static Diagnostic buildFailureDetailDiagnostic(Range range, std::string detail, std::vector<RelatedInfo> relatedInfo = {}) {
	Diagnostic diagnostic;
	diagnostic.level = Diagnostic::Level::Error;
	diagnostic.message = std::move(detail);
	diagnostic.range = range;
	diagnostic.relatedInfo = std::move(relatedInfo);
	return diagnostic;
}

static std::string formatInferenceTraceType(const DataType &type, ParseContext &parseContext) {
	if (!type.isDeduced())
		return "undeduced";
	if (type.kind == DataType::Kind::Type) {
		DataType referencedType = type.toReferencedType();
		if (referencedType.kind == DataType::Kind::Unresolved)
			return "type";
		return "type(" + typeToUserName(referencedType, parseContext) + ")";
	}
	if (type.kind == DataType::Kind::Constraint)
		return "constraint";
	return typeToUserName(type, parseContext);
}

static std::string encodeDataTypeForCacheKey(const DataType &type) {
	std::string key;
	key.reserve(96);
	key += "k";
	key += std::to_string(static_cast<int>(type.kind));
	key += "|n";
	key += std::to_string(type.numericSize);
	key += "|p";
	key += std::to_string(type.pointerDepth);
	key += "|r";
	key += std::to_string(static_cast<int>(type.referencedKind));
	key += "|a";
	key += std::to_string(type.arraySize);
	key += "|m";
	key += std::to_string(type.matrixRowCount);
	key += "|ci";
	key += std::to_string(type.classInstIndex);
	key += "|cd";
	key += std::to_string(reinterpret_cast<uintptr_t>(type.classDefinition));
	key += "|te";
	key += std::to_string(reinterpret_cast<uintptr_t>(type.typeExpression));
	key += "|e";
	if (type.arrayElementType)
		key += "(" + encodeDataTypeForCacheKey(*type.arrayElementType) + ")";
	else
		key += "()";
	return key;
}

static std::string encodeTypeConstraintForCacheKey(const TypeConstraint &constraint) {
	std::string key = constraint.isResolved() ? "r1" : "r0";
	auto appendOptional = [&](std::string_view name, const auto &value) {
		key += "|" + std::string(name);
		key += value ? std::to_string(static_cast<long long>(*value)) : "-";
	};
	appendOptional("k", constraint.kind);
	appendOptional("n", constraint.numericSize);
	appendOptional("p", constraint.pointerDepth);
	appendOptional("a", constraint.arraySize);
	appendOptional("mr", constraint.matrixRows);
	appendOptional("mc", constraint.matrixColumns);
	appendOptional("ci", constraint.classInstantiationIndex);
	key += "|cd" + std::to_string(reinterpret_cast<uintptr_t>(constraint.classDefinition));
	key += constraint.constrainsClassDefinition ? "|dc1" : "|dc0";
	key += constraint.requiresCompileTimeValue ? "|ct1" : "|ct0";
	key += "|e";
	key += constraint.elementConstraint ? "(" + encodeTypeConstraintForCacheKey(*constraint.elementConstraint) + ")" : "()";
	return key;
}

static std::string encodeCompileTimeValueForCacheKey(const CompileTimeValue &value) {
	if (std::holds_alternative<std::monostate>(value))
		return "?";
	if (const auto *number = std::get_if<double>(&value))
		return "d" + std::to_string(std::bit_cast<uint64_t>(*number));
	if (const auto *text = std::get_if<std::string>(&value))
		return "s" + std::to_string(text->size()) + ":" + *text;
	if (const auto *boolean = std::get_if<bool>(&value))
		return *boolean ? "b1" : "b0";
	if (const auto *typeRef = std::get_if<TypeReferenceValue>(&value))
		return "t" + encodeDataTypeForCacheKey(typeRef->type) + "|" + encodeTypeConstraintForCacheKey(typeRef->constraint);
	if (const auto *constraint = std::get_if<TypeConstraint>(&value))
		return "c" + encodeTypeConstraintForCacheKey(*constraint);
	return "?";
}

static bool expressionParticipatesInInferenceTrace(Expression *expr) {
	if (!expr)
		return false;
	return expr->kind == Expression::Kind::PatternCall || expr->kind == Expression::Kind::IntrinsicCall;
}

static std::string describeInferenceTraceFrame(Expression *expr, ParseContext &parseContext) {
	if (!expr)
		return "while inferring <expression>";

	std::string expressionText = diagnosticExpressionText(expr);
	if (expressionText.empty())
		expressionText = "<expression>";

	std::vector<DataType> argumentTypes;
	argumentTypes.reserve(expr->arguments.size());
	for (Expression *argument : expr->arguments)
		argumentTypes.push_back(argument ? argument->type : DataType{});
	std::string resultType = formatInferenceTraceType(expr->type, parseContext);

	if (expr->kind == Expression::Kind::IntrinsicCall) {
		std::string frame = "while inferring intrinsic '" + expr->intrinsicName + "' in '" + expressionText + "'";
		if (!argumentTypes.empty())
			frame += " with arguments [" + formatTypeList(argumentTypes, parseContext) + "]";
		frame += " -> " + resultType;
		return frame;
	}

	std::string frame = "while inferring call '" + expressionText + "'";
	if (!argumentTypes.empty())
		frame += " with arguments [" + formatTypeList(argumentTypes, parseContext) + "]";
	frame += " -> " + resultType;
	return frame;
}

static bool isValidCastRuntimeType(const DataType &type) {
	if (!type.isDeduced() || type.kind == DataType::Kind::Void || type.isMetaType())
		return false;
	if (type.kind == DataType::Kind::Class && !type.isPointer())
		return false;
	return true;
}

// Must stay in sync with codegen's ensureType conversion support.
static bool isSupportedCastConversion(const DataType &fromType, const DataType &toType) {
	return isValidCastRuntimeType(fromType) && isValidCastRuntimeType(toType) &&
		   DataType::supportsRuntimeConversion(fromType, toType);
}

static bool tryResolveCastResultType(const DataType &fromType, const DataType &typeArgType, DataType &outType) {
	if (!isValidCastRuntimeType(fromType))
		return false;
	if (typeArgType.kind != DataType::Kind::Type)
		return false;
	DataType toType = concretizeClassType(typeArgType.toReferencedType());
	if (!isSupportedCastConversion(fromType, toType))
		return false;
	outType = toType;
	return true;
}
