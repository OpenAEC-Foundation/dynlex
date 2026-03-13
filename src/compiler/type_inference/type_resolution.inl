#pragma once

#include "const_evaluation.inl"

static Function *resolveCompileTimeSelectBranch(
	Function *selectExpr, ParseContext &parseContext, const std::unordered_map<std::string, Function *> &bindings
) {
	CompileTimeValue conditionValue = evaluateCompileTimeValue(selectExpr->arguments[1], parseContext, bindings);
	std::optional<bool> condition = compileTimeTruthiness(conditionValue);
	if (!condition.has_value())
		return nullptr;
	return selectExpr->arguments[*condition ? 2 : 3];
}

static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	DataType &outTypeRef
);

static DataType resolveTypeThroughBindings(Function *expr, const std::unordered_map<std::string, Function *> &bindings) {
	struct TypeResolutionStateKey {
		const Function *expr;
		size_t bindingsHash;

		bool operator==(const TypeResolutionStateKey &other) const {
			return expr == other.expr && bindingsHash == other.bindingsHash;
		}
	};
	struct TypeResolutionStateKeyHasher {
		size_t operator()(const TypeResolutionStateKey &key) const {
			size_t h = std::hash<const Function *>{}(key.expr);
			h ^= key.bindingsHash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			return h;
		}
	};
	auto hashBindings = [](const std::unordered_map<std::string, Function *> &map) {
		std::vector<std::pair<std::string, uintptr_t>> entries;
		entries.reserve(map.size());
		for (const auto &[name, boundExpr] : map)
			entries.push_back({name, reinterpret_cast<uintptr_t>(boundExpr)});
		std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
			return a.first < b.first;
		});
		size_t h = 1469598103934665603ULL;
		for (const auto &[name, ptr] : entries) {
			size_t nameHash = std::hash<std::string>{}(name);
			h ^= nameHash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			h ^= ptr + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
		}
		return h;
	};
	static thread_local std::unordered_set<TypeResolutionStateKey, TypeResolutionStateKeyHasher> activeTypeResolutionStates;

	auto isMacroPatternCall = [](Function *call) {
		if (!call || call->kind != Function::Kind::PatternCall)
			return false;
		if (call->selectedPatternDefinition && call->selectedPatternDefinition->section)
			return call->selectedPatternDefinition->section->isMacro;
		if (!call->patternMatch || !call->patternMatch->matchedEndNode)
			return false;
		auto &defs = call->patternMatch->matchedEndNode->matchingDefinitions;
		return defs.size() == 1 && defs.front() && defs.front()->section && defs.front()->section->isMacro;
	};
	Function *boundResolved = resolveThroughBindings(expr, bindings);
	if (boundResolved && boundResolved != expr && boundResolved->type.isDeduced() && !isMacroPatternCall(boundResolved)) {
		bool isSafeBoundLeaf = boundResolved->kind == Function::Kind::Pending ||
							   boundResolved->kind == Function::Kind::Literal ||
							   boundResolved->kind == Function::Kind::Variable;
		if (isSafeBoundLeaf)
			return concretizeClassType(boundResolved->type);
	}
	if (expr && expr->kind == Function::Kind::PatternCall && expr->type.isDeduced() && bindings.empty() &&
		!isMacroPatternCall(expr)) {
		return concretizeClassType(expr->type);
	}
	std::unordered_map<std::string, Function *> effectiveBindings;
	Function *resolved = resolveThroughBindingsDeep(expr, bindings, effectiveBindings);
	if (!resolved)
		return {};
	bool dependsOnBindings = resolved != expr || !effectiveBindings.empty();
	TypeResolutionStateKey stateKey{resolved, dependsOnBindings ? hashBindings(effectiveBindings) : 0};
	if (activeTypeResolutionStates.contains(stateKey)) {
		if (resolved->type.isDeduced() && !isMacroPatternCall(resolved))
			return concretizeClassType(resolved->type);
		return {};
	}

	struct ActiveTypeResolutionGuard {
		TypeResolutionStateKey key;

		explicit ActiveTypeResolutionGuard(TypeResolutionStateKey key) : key(key) {
			activeTypeResolutionStates.insert(this->key);
		}
		~ActiveTypeResolutionGuard() { activeTypeResolutionStates.erase(key); }
	} activeGuard(stateKey);
	if (resolved->type.isDeduced() && !isMacroPatternCall(resolved) && !dependsOnBindings) {
		static bool traceTypeCache = std::getenv("DYNLEX_TRACE_TYPE_CACHE") != nullptr;
		if (traceTypeCache) {
			std::cerr << "[type-cache:resolved] expr='" << std::string(expr->range.subString) << "' resolved='"
					  << std::string(resolved->range.subString) << "' type=" << resolved->type.toString()
					  << " kind=" << static_cast<int>(resolved->type.kind)
					  << " refKind=" << static_cast<int>(resolved->type.referencedKind) << " num=" << resolved->type.numericSize
					  << " ptr=" << resolved->type.pointerDepth << " classDef=" << resolved->type.classDefinition
					  << " dependsOnBindings=" << dependsOnBindings << "\n";
		}
		return concretizeClassType(resolved->type);
	}
	if (resolved->kind == Function::Kind::Literal) {
		if (std::holds_alternative<double>(resolved->literalValue)) {
			double value = std::get<double>(resolved->literalValue);
			std::string_view literalText = resolved->range.subString;
			bool explicitlyFloat = literalText.find('.') != std::string_view::npos ||
								   literalText.find('e') != std::string_view::npos ||
								   literalText.find('E') != std::string_view::npos;
			if (!explicitlyFloat && std::trunc(value) == value)
				return {DataType::Kind::Int, 4};
			return {DataType::Kind::Float, 8};
		}
		if (std::holds_alternative<std::string>(resolved->literalValue)) {
			DataType strType{DataType::Kind::Int, 1};
			strType.pointerDepth = 1;
			return strType;
		}
	}
	if (resolved->kind == Function::Kind::ArrayLiteral) {
		if (resolved->arguments.empty())
			return {};
		DataType elementType = resolveTypeThroughBindings(resolved->arguments[0], effectiveBindings);
		if (!elementType.isDeduced())
			return {};
		for (size_t i = 1; i < resolved->arguments.size(); i++) {
			DataType nextType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
			DataType merged;
			if (!mergeArrayElementType(elementType, nextType, merged))
				return {};
			elementType = merged;
		}
		DataType arrayType{DataType::Kind::Array};
		arrayType.arraySize = static_cast<int>(resolved->arguments.size());
		arrayType.arrayElementType = std::make_shared<DataType>(elementType);
		return arrayType;
	}
	if (resolved->kind == Function::Kind::Variable && resolved->variable) {
		VariableReference *varRef = resolved->variable;
		VariableReference *definition = varRef->definition ? varRef->definition : varRef;
		Section *sec = definition->range.line ? definition->range.line->section : nullptr;
		Variable *var = sec ? sec->findVariable(definition->name) : nullptr;
		if (!var && resolved->range.line)
			var =
				resolved->range.line->section ? resolved->range.line->section->findVariable(resolved->variable->name) : nullptr;
		if (var && var->type.isDeduced())
			return concretizeClassType(var->type);
	}
	if (resolved->kind == Function::Kind::PatternCall && resolved->patternMatch && resolved->patternMatch->matchedEndNode) {
		auto &defs = resolved->patternMatch->matchedEndNode->matchingDefinitions;
		if (!defs.empty()) {
			std::vector<DataType> argTypesForOverload;
			bool allArgTypesDeduced = true;
			for (Function *arg : resolved->arguments)
				argTypesForOverload.push_back(resolveTypeThroughBindings(arg, effectiveBindings));
			for (const DataType &argType : argTypesForOverload) {
				if (!argType.isDeduced()) {
					allArgTypesDeduced = false;
					break;
				}
			}
			PatternDefinition *def =
				selectOverload(defs, resolved->arguments, resolved->patternMatch->nodesPassed, argTypesForOverload);
			if (def && allArgTypesDeduced && !dependsOnBindings)
				resolved->selectedPatternDefinition = def;
			if (def && def->section && def->section->type == SectionType::Class && !def->section->isMacro &&
				activeTypeResolutionParseContext) {
				std::unordered_map<std::string, Function *> callBindings;
				appendPatternCallBindings(resolved, def, callBindings);
				for (auto &[name, boundExpr] : callBindings) {
					Function *resolvedExpr = resolveThroughBindings(boundExpr, effectiveBindings);
					if (resolvedExpr)
						boundExpr = resolvedExpr;
				}
				std::unordered_map<std::string, Function *> classBindings = effectiveBindings;
				for (const auto &[name, boundExpr] : callBindings)
					classBindings[name] = boundExpr;
				return instantiateBoundClassType(
					*activeTypeResolutionParseContext, static_cast<ClassSection *>(def->section)->classDefinition, classBindings
				);
			}
		}
	}
	if (resolved->kind == Function::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (kind == IntrinsicKind::Property) {
			DataType instType = concretizeClassType(resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings));
			if (instType.isPointer() && instType.kind == DataType::Kind::Class)
				instType = concretizeClassType(instType.dereferenced());
			static bool tracePropertyType = std::getenv("DYNLEX_TRACE_PROPERTY_TYPE") != nullptr;
			if (tracePropertyType) {
				std::cerr << "[property-type] owner='" << std::string(resolved->arguments[1]->range.subString)
						  << "' ownerType=" << instType.toString() << " kind=" << static_cast<int>(instType.kind)
						  << " ptr=" << instType.pointerDepth << " classDef=" << instType.classDefinition
						  << " classInst=" << instType.classInstIndex << " bindings={";
				bool firstBinding = true;
				for (const auto &[name, bound] : effectiveBindings) {
					if (!firstBinding)
						std::cerr << ", ";
					firstBinding = false;
					std::cerr << name << "='" << (bound ? std::string(bound->range.subString) : "<null>") << "'";
				}
				std::cerr << "}\n";
			}
			Function *propExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
			std::string fieldName = extractFieldName(propExpr);
			DataType builtInPropertyType = resolveBuiltInPropertyType(instType, fieldName);
			if (builtInPropertyType.isDeduced())
				return builtInPropertyType;
			if (instType.kind == DataType::Kind::Class && instType.classDefinition) {
				for (size_t i = 0; i < instType.classDefinition->fields.size(); i++) {
					if (instType.classDefinition->fields[i].name != fieldName)
						continue;
					if (instType.classInstIndex >= 0 &&
						static_cast<size_t>(instType.classInstIndex) < instType.classDefinition->instantiations.size() &&
						i < instType.classDefinition->instantiations[instType.classInstIndex].fieldTypes.size()) {
						return instType.classDefinition->instantiations[instType.classInstIndex].fieldTypes[i];
					}
					DataType declaredFieldType = instType.classDefinition->fields[i].declaredType;
					if (declaredFieldType.isDeduced())
						return concretizeClassType(declaredFieldType);
				}
			}
		} else if (kind == IntrinsicKind::Dereference) {
			DataType ptrType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (ptrType.isDeduced() && ptrType.isPointer())
				return concretizeClassType(ptrType.dereferenced());
		} else if (kind == IntrinsicKind::AddressOf) {
			DataType valueType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (valueType.isDeduced())
				return valueType.pointed();
		} else if (kind == IntrinsicKind::Call) {
			if (resolved->arguments.size() <= 3)
				return {};
			DataType retTypeRef = resolveTypeThroughBindings(resolved->arguments[3], effectiveBindings);
			if (retTypeRef.kind != DataType::Kind::Type || retTypeRef.referencedKind == DataType::Kind::Type ||
				retTypeRef.referencedKind == DataType::Kind::Unresolved)
				return {};
			for (size_t i = 4; i < resolved->arguments.size(); i++) {
				DataType argType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
				if (argType.kind == DataType::Kind::Type)
					return {};
			}
			return concretizeClassType(retTypeRef.toReferencedType());
		} else if (kind == IntrinsicKind::Cast) {
			DataType typeArgType = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
			if (typeArgType.kind == DataType::Kind::Type)
				return concretizeClassType(typeArgType.toReferencedType());
		} else if (kind == IntrinsicKind::Type) {
			std::string kindStr;
			Function *kindExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *str = std::get_if<std::string>(&kindExpr->literalValue))
				kindStr = *str;
			if (!kindStr.empty()) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				if (kindStr == "int") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 4;
				} else if (kindStr == "float") {
					typeRef.referencedKind = DataType::Kind::Float;
					typeRef.numericSize = 8;
				} else if (kindStr == "bool") {
					typeRef.referencedKind = DataType::Kind::Bool;
				} else if (kindStr == "void") {
					typeRef.referencedKind = DataType::Kind::Void;
				} else if (kindStr == "string") {
					typeRef.referencedKind = DataType::Kind::Int;
					typeRef.numericSize = 1;
					typeRef.pointerDepth = 1;
				} else if (kindStr == "type") {
					typeRef.referencedKind = DataType::Kind::Type;
				}
				if (resolved->arguments.size() > 2) {
					Function *bitsExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
					if (auto *bits = std::get_if<double>(&bitsExpr->literalValue))
						typeRef.numericSize = (int)*bits / 8;
				}
				return typeRef;
			}
		} else if (kind == IntrinsicKind::TypeOf) {
			DataType valueType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (valueType.isDeduced()) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = valueType.kind;
				typeRef.numericSize = valueType.numericSize;
				typeRef.pointerDepth = valueType.pointerDepth;
				typeRef.classDefinition = valueType.classDefinition;
				typeRef.classInstIndex = valueType.classInstIndex;
				typeRef.arraySize = valueType.arraySize;
				typeRef.arrayElementType =
					valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
				return typeRef;
			}
		} else if (kind == IntrinsicKind::SizeOf) {
			DataType typeArgType;
			if (activeTypeResolutionParseContext &&
				resolveCompileTimeTypeReference(
					*activeTypeResolutionParseContext, resolved->arguments[1], effectiveBindings, typeArgType
				) &&
				typeArgType.kind == DataType::Kind::Type && typeArgType.referencedKind != DataType::Kind::Type &&
				typeArgType.referencedKind != DataType::Kind::Unresolved)
				return {DataType::Kind::Int, 8};
		} else if (kind == IntrinsicKind::BuildInfo) {
			Function *keyExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *key = std::get_if<std::string>(&keyExpr->literalValue)) {
				if (*key == "word size" || *key == "optimization level") {
					return {DataType::Kind::Int, 4};
				}
				return {DataType::Kind::Int, 1, 1};
			}
		} else if (kind == IntrinsicKind::Select && activeTypeResolutionParseContext) {
			Function *selectedBranch =
				resolveCompileTimeSelectBranch(resolved, *activeTypeResolutionParseContext, effectiveBindings);
			if (selectedBranch)
				return resolveTypeThroughBindings(selectedBranch, effectiveBindings);
		} else if (kind == IntrinsicKind::Array) {
			Function *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			if (auto *size = std::get_if<double>(&sizeExpr->literalValue)) {
				DataType typeRef;
				typeRef.kind = DataType::Kind::Type;
				typeRef.referencedKind = DataType::Kind::Array;
				typeRef.arraySize = static_cast<int>(*size);
				if (resolved->arguments.size() > 2) {
					DataType elemTypeRef = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
					if (elemTypeRef.kind == DataType::Kind::Type)
						typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef.toReferencedType());
				}
				return typeRef;
			}
		} else if (kind == IntrinsicKind::Vector) {
			Function *sizeExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			auto *size = std::get_if<double>(&sizeExpr->literalValue);
			if (!size)
				return {};
			int vectorSize = static_cast<int>(*size);
			if (*size != static_cast<double>(vectorSize) || vectorSize < 1)
				return {};
			DataType typeRef;
			typeRef.kind = DataType::Kind::Type;
			typeRef.referencedKind = DataType::Kind::Vector;
			typeRef.arraySize = vectorSize;
			typeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 2) {
				DataType elemTypeRef;
				if (resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings).kind == DataType::Kind::Type)
					elemTypeRef = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings).toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::Matrix) {
			Function *rowsExpr = resolveThroughBindings(resolved->arguments[1], effectiveBindings);
			Function *columnsExpr = resolveThroughBindings(resolved->arguments[2], effectiveBindings);
			auto *rowsValue = std::get_if<double>(&rowsExpr->literalValue);
			auto *columnsValue = std::get_if<double>(&columnsExpr->literalValue);
			if (!rowsValue || !columnsValue)
				return {};
			int rows = static_cast<int>(*rowsValue);
			int columns = static_cast<int>(*columnsValue);
			if (*rowsValue != static_cast<double>(rows) || *columnsValue != static_cast<double>(columns))
				return {};
			if (rows < 1 || columns < 1)
				return {};
			DataType typeRef;
			typeRef.kind = DataType::Kind::Type;
			typeRef.referencedKind = DataType::Kind::Matrix;
			typeRef.matrixRowCount = rows;
			typeRef.arraySize = columns;
			typeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 3) {
				DataType elemTypeRef;
				if (resolveTypeThroughBindings(resolved->arguments[3], effectiveBindings).kind == DataType::Kind::Type)
					elemTypeRef = resolveTypeThroughBindings(resolved->arguments[3], effectiveBindings).toReferencedType();
				if (elemTypeRef.isDeduced())
					typeRef.arrayElementType = std::make_shared<DataType>(elemTypeRef);
			}
			return typeRef;
		} else if (kind == IntrinsicKind::AddPointerDepth) {
			DataType typeArgType;
			static bool traceTypeRef = std::getenv("DYNLEX_TRACE_TYPE_REF") != nullptr;
			if (activeTypeResolutionParseContext &&
				resolveCompileTimeTypeReference(
					*activeTypeResolutionParseContext, resolved->arguments[1], effectiveBindings, typeArgType
				) &&
				typeArgType.kind == DataType::Kind::Type && typeArgType.referencedKind != DataType::Kind::Type &&
				typeArgType.referencedKind != DataType::Kind::Unresolved) {
				if (traceTypeRef) {
					std::cerr << "[type-ref:add-pointer] arg='" << std::string(resolved->arguments[1]->range.subString)
							  << "' resolvedKind=" << static_cast<int>(typeArgType.referencedKind)
							  << " num=" << typeArgType.numericSize << " ptr=" << typeArgType.pointerDepth
							  << " classDef=" << typeArgType.classDefinition << " bindings={";
					bool firstBinding = true;
					for (const auto &[name, bound] : effectiveBindings) {
						if (!firstBinding)
							std::cerr << ", ";
						firstBinding = false;
						std::cerr << name << "='" << (bound ? std::string(bound->range.subString) : "<null>") << "'";
					}
					std::cerr << "}\n";
				}
				typeArgType.pointerDepth++;
				return typeArgType;
			}
		} else if (kind == IntrinsicKind::Construct) {
			DataType typeRefType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (typeRefType.kind == DataType::Kind::Type && typeRefType.referencedKind == DataType::Kind::Array) {
				DataType arrayType = typeRefType.toReferencedType();
				if (arrayType.arraySize == static_cast<int>(resolved->arguments.size()) - 2) {
					DataType elementType =
						arrayType.arrayElementType ? *arrayType.arrayElementType : DataType{DataType::Kind::Unresolved};
					bool allDeduced = true;
					for (size_t i = 2; i < resolved->arguments.size(); i++) {
						DataType argType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
						if (!argType.isDeduced())
							allDeduced = false;
						if (!arrayType.arrayElementType) {
							if (!elementType.isDeduced())
								elementType = argType;
							else if (elementType != argType)
								allDeduced = false;
						}
					}
					if (allDeduced && elementType.isDeduced()) {
						arrayType.arrayElementType = std::make_shared<DataType>(elementType);
						return arrayType;
					}
				}
			} else if (typeRefType.kind == DataType::Kind::Type && typeRefType.classDefinition) {
				std::vector<DataType> argumentTypes;
				argumentTypes.reserve(resolved->arguments.size() - 2);
				bool allArgumentsDeduced = true;
				for (size_t i = 2; i < resolved->arguments.size(); i++) {
					DataType argumentType = resolveTypeThroughBindings(resolved->arguments[i], effectiveBindings);
					if (!argumentType.isDeduced()) {
						allArgumentsDeduced = false;
						break;
					}
					argumentTypes.push_back(argumentType);
				}

				DataType instantiatedTypeRef;
				if (allArgumentsDeduced &&
					instantiateClassFromArgumentTypes(
						typeRefType.classDefinition, argumentTypes, instantiatedTypeRef, typeRefType.classInstIndex
					)) {
					return instantiatedTypeRef.toReferencedType();
				}

				DataType targetType = concretizeClassType(typeRefType.toReferencedType());
				if (resolved->arguments.size() == targetType.classDefinition->fields.size() + 2 &&
					targetType.classInstIndex >= 0) {
					const auto &fieldTypes = targetType.classDefinition->instantiations[targetType.classInstIndex].fieldTypes;
					bool allCompatible = argumentTypes.size() == fieldTypes.size();
					for (size_t i = 0; allCompatible && i < fieldTypes.size(); i++) {
						if (argumentTypes[i] != fieldTypes[i])
							allCompatible = false;
					}
					if (allCompatible)
						return targetType;
				}
			} else if (typeRefType.kind == DataType::Kind::Type && resolved->arguments.size() == 3) {
				DataType targetType = typeRefType.toReferencedType();
				DataType valueType = resolveTypeThroughBindings(resolved->arguments[2], effectiveBindings);
				if (valueType.isDeduced())
					return targetType;
			}
		}
	}
	std::unordered_map<std::string, Function *> innerBindings;
	Function *bodyExpr = expandMacroPatternCall(resolved, innerBindings);
	if (bodyExpr) {
		std::unordered_map<std::string, Function *> mergedBindings = bindings;
		for (auto &[name, argExpr] : innerBindings) {
			// Preserve direct argument expressions to avoid placeholder self-capture
			// in nested macro expansions.
			mergedBindings[name] = resolveThroughBindings(argExpr, bindings);
		}
		return resolveTypeThroughBindings(bodyExpr, mergedBindings);
	}
	return concretizeClassType(resolved->type);
}

static std::string typeToUserName(const DataType &type, ParseContext &parseContext) {
	if (type.pointerDepth == 0) {
		if (type.kind == DataType::Kind::Int && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit integer";
		if (type.kind == DataType::Kind::Float && type.numericSize > 0)
			return "a " + std::to_string(type.numericSize * 8) + " bit float";
		if (type.kind == DataType::Kind::Bool)
			return "a boolean";
		if (type.kind == DataType::Kind::Void)
			return "nothing";
	}
	auto it = parseContext.typeAliasNames.find(type);
	if (it != parseContext.typeAliasNames.end())
		return it->second;
	return type.toString();
}

static bool resolveCompileTimeTypeReference(
	ParseContext &parseContext, Function *expr, const std::unordered_map<std::string, Function *> &bindings,
	DataType &outTypeRef
) {
	static bool traceTypeRefFail = std::getenv("DYNLEX_TRACE_TYPE_REF_FAIL") != nullptr;
	std::unordered_map<std::string, Function *> effectiveBindings;
	Function *resolved = resolveThroughBindingsDeep(expr, bindings, effectiveBindings);
	if (!resolved)
		return false;
	bool dependsOnBindings = resolved != expr || !effectiveBindings.empty();

	if (!dependsOnBindings && resolved->type.kind == DataType::Kind::Type &&
		resolved->type.referencedKind != DataType::Kind::Type && resolved->type.referencedKind != DataType::Kind::Unresolved) {
		static bool traceTypeRef = std::getenv("DYNLEX_TRACE_TYPE_REF") != nullptr;
		if (traceTypeRef) {
			std::cerr << "[type-ref:cached] expr='" << std::string(resolved->range.subString)
					  << "' kind=" << static_cast<int>(resolved->type.referencedKind) << " num=" << resolved->type.numericSize
					  << " ptr=" << resolved->type.pointerDepth << " classDef=" << resolved->type.classDefinition << "\n";
		}
		outTypeRef = resolved->type;
		return true;
	}

	if (resolved->kind == Function::Kind::Variable && resolved->variable) {
		DataType shorthandType = DataType::fromString(resolved->variable->name);
		if (shorthandType.isDeduced()) {
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = shorthandType.kind;
			outTypeRef.numericSize = shorthandType.numericSize;
			outTypeRef.pointerDepth = shorthandType.pointerDepth;
			return true;
		}
		return false;
	}

	if (resolved->kind == Function::Kind::IntrinsicCall) {
		IntrinsicKind kind = intrinsicKind(resolved->intrinsicName);
		if (kind == IntrinsicKind::Type) {
			DataType resolvedType = resolveTypeThroughBindings(resolved, effectiveBindings);
			if (resolvedType.kind == DataType::Kind::Type) {
				outTypeRef = resolvedType;
				return true;
			}
		}
		if (kind == IntrinsicKind::AddPointerDepth) {
			DataType innerTypeRef;
			if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[1], effectiveBindings, innerTypeRef) ||
				innerTypeRef.kind != DataType::Kind::Type)
				return false;
			innerTypeRef.pointerDepth++;
			outTypeRef = innerTypeRef;
			return true;
		}
		if (kind == IntrinsicKind::TypeOf) {
			DataType valueType = resolveTypeThroughBindings(resolved->arguments[1], effectiveBindings);
			if (!valueType.isDeduced())
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = valueType.kind;
			outTypeRef.numericSize = valueType.numericSize;
			outTypeRef.pointerDepth = valueType.pointerDepth;
			outTypeRef.classDefinition = valueType.classDefinition;
			outTypeRef.classInstIndex = valueType.classInstIndex;
			outTypeRef.arraySize = valueType.arraySize;
			outTypeRef.matrixRowCount = valueType.matrixRowCount;
			outTypeRef.arrayElementType =
				valueType.arrayElementType ? std::make_shared<DataType>(*valueType.arrayElementType) : nullptr;
			return true;
		}
		if (kind == IntrinsicKind::Array) {
			int arraySize = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, arraySize))
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Array;
			outTypeRef.arraySize = arraySize;
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[2], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Vector) {
			int vectorSize = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, vectorSize) ||
				vectorSize < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Vector;
			outTypeRef.arraySize = vectorSize;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 2) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[2], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Matrix) {
			int rows = 0;
			int columns = 0;
			if (!evaluateCompileTimeInteger(parseContext, resolved->arguments[1], effectiveBindings, rows) ||
				!evaluateCompileTimeInteger(parseContext, resolved->arguments[2], effectiveBindings, columns) || rows < 1 ||
				columns < 1)
				return false;
			outTypeRef.kind = DataType::Kind::Type;
			outTypeRef.referencedKind = DataType::Kind::Matrix;
			outTypeRef.matrixRowCount = rows;
			outTypeRef.arraySize = columns;
			outTypeRef.arrayElementType = std::make_shared<DataType>(DataType::Kind::Float, 4);
			if (resolved->arguments.size() > 3) {
				DataType elementTypeRef;
				if (!resolveCompileTimeTypeReference(parseContext, resolved->arguments[3], effectiveBindings, elementTypeRef) ||
					elementTypeRef.kind != DataType::Kind::Type)
					return false;
				outTypeRef.arrayElementType = std::make_shared<DataType>(elementTypeRef.toReferencedType());
			}
			return true;
		}
		if (kind == IntrinsicKind::Select) {
			Function *selectedBranch = resolveCompileTimeSelectBranch(resolved, parseContext, effectiveBindings);
			if (traceTypeRefFail && !selectedBranch) {
				std::cerr << "[type-ref-fail] select condition unknown expr='" << std::string(resolved->range.subString)
						  << "'\n";
			}
			if (selectedBranch)
				return resolveCompileTimeTypeReference(parseContext, selectedBranch, effectiveBindings, outTypeRef);
		}
	}

	if (resolved->kind == Function::Kind::PatternCall) {
		auto &defs = resolved->patternMatch->matchedEndNode->matchingDefinitions;
		if (defs.empty())
			return false;
		std::vector<DataType> argTypesForOverload;
		bool allArgTypesDeduced = true;
		for (Function *arg : resolved->arguments)
			argTypesForOverload.push_back(resolveTypeThroughBindings(arg, effectiveBindings));
		for (const DataType &argType : argTypesForOverload) {
			if (!argType.isDeduced()) {
				allArgTypesDeduced = false;
				break;
			}
		}
		PatternDefinition *def =
			selectOverload(defs, resolved->arguments, resolved->patternMatch->nodesPassed, argTypesForOverload);
		if (def && allArgTypesDeduced)
			resolved->selectedPatternDefinition = def;
		if (!def || !def->section)
			return false;
		if (traceTypeRefFail) {
			std::cerr << "[type-ref-fail] pattern-call expr='" << std::string(resolved->range.subString) << "' def='"
					  << std::string(def->range.subString) << "' isMacro=" << def->section->isMacro << "\n";
		}

		std::unordered_map<std::string, Function *> callBindings;
		appendPatternCallBindings(resolved, def, callBindings);
		for (auto &[name, boundExpr] : callBindings) {
			Function *resolvedExpr = resolveThroughBindings(boundExpr, effectiveBindings);
			if (resolvedExpr)
				boundExpr = resolvedExpr;
		}
		if (!def->section->isMacro && def->section->type == SectionType::Class) {
			if (traceTypeRefFail) {
				std::cerr << "[type-ref-fail] class call expr='" << std::string(resolved->range.subString)
						  << "' callBindings={";
				bool first = true;
				for (const auto &[name, boundExpr] : callBindings) {
					if (!first)
						std::cerr << ", ";
					first = false;
					std::cerr << name << "='" << (boundExpr ? std::string(boundExpr->range.subString) : "<null>") << "'";
					if (boundExpr) {
						std::cerr << "(k=" << static_cast<int>(boundExpr->kind);
						if (const auto *number = std::get_if<double>(&boundExpr->literalValue))
							std::cerr << ",lit=" << *number;
						std::cerr << ")";
					}
				}
				std::cerr << "}\n";
			}
			std::unordered_map<std::string, Function *> classBindings = effectiveBindings;
			for (const auto &[name, boundExpr] : callBindings)
				classBindings[name] = boundExpr;
			outTypeRef = instantiateBoundClassType(
				parseContext, static_cast<ClassSection *>(def->section)->classDefinition, classBindings
			);
			return outTypeRef.kind == DataType::Kind::Type;
		}

		std::unordered_map<std::string, Function *> innerBindings;
		Function *bodyExpr = expandMacroPatternCall(resolved, innerBindings);
		if (!bodyExpr)
			return false;
		if (traceTypeRefFail) {
			std::cerr << "[type-ref-fail] expanded body='" << std::string(bodyExpr->range.subString) << "' bindings={";
			bool first = true;
			for (const auto &[name, argExpr] : innerBindings) {
				if (!first)
					std::cerr << ", ";
				first = false;
				std::cerr << name << "='" << (argExpr ? std::string(argExpr->range.subString) : "<null>") << "'";
			}
			std::cerr << "}\n";
		}
		for (const auto &[name, argExpr] : innerBindings) {
			Function *resolvedArg = resolveThroughBindings(argExpr, callBindings);
			callBindings[name] = resolvedArg ? resolvedArg : argExpr;
		}
		bool resolvedType = resolveCompileTimeTypeReference(parseContext, bodyExpr, callBindings, outTypeRef);
		if (traceTypeRefFail) {
			std::cerr << "[type-ref-fail] body result=" << resolvedType;
			if (resolvedType)
				std::cerr << " type=" << outTypeRef.toString();
			std::cerr << "\n";
		}
		return resolvedType;
	}
	if (traceTypeRefFail) {
		std::cerr << "[type-ref-fail] unresolved expr='" << std::string(resolved->range.subString)
				  << "' kind=" << static_cast<int>(resolved->kind) << " bindings={";
		bool first = true;
		for (const auto &[name, boundExpr] : effectiveBindings) {
			if (!first)
				std::cerr << ", ";
			first = false;
			std::cerr << name << "='" << (boundExpr ? std::string(boundExpr->range.subString) : "<null>") << "'";
		}
		std::cerr << "}\n";
	}
	return false;
}

static DataType instantiateBoundClassType(
	ParseContext &parseContext, ClassDefinition *classDef, const std::unordered_map<std::string, Function *> &bindings
) {
	static bool traceClassInstantiate = std::getenv("DYNLEX_TRACE_CLASS_INSTANTIATE") != nullptr;
	if (!classDef)
		return {};

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(classDef->fields.size());
	for (FieldDefinition &field : classDef->fields) {
		DataType fieldType = field.declaredType;
		if (fieldType.kind == DataType::Kind::Any)
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		if (fieldType.kind == DataType::Kind::Unresolved && fieldType.typeFunction) {
			DataType resolvedTypeRef;
			if (!resolveCompileTimeTypeReference(parseContext, fieldType.typeFunction, bindings, resolvedTypeRef) ||
				resolvedTypeRef.kind != DataType::Kind::Type) {
				if (traceClassInstantiate) {
					std::cerr << "[class-instantiate] failed field type for class="
							  << (classDef->patternNames.empty() ? std::string("<unnamed>") : classDef->patternNames.front())
							  << " field='" << field.name << "' expr='" << std::string(fieldType.typeFunction->range.subString)
							  << "' bindings={";
					bool first = true;
					for (const auto &[name, boundExpr] : bindings) {
						if (!first)
							std::cerr << ", ";
						first = false;
						std::cerr << name << "='" << (boundExpr ? std::string(boundExpr->range.subString) : "<null>") << "'";
						if (boundExpr) {
							std::cerr << "(k=" << static_cast<int>(boundExpr->kind);
							if (const auto *number = std::get_if<double>(&boundExpr->literalValue))
								std::cerr << ",lit=" << *number;
							std::cerr << ")";
						}
					}
					std::cerr << "}\n";
				}
				return {};
			}
			fieldType = concretizeClassType(resolvedTypeRef.toReferencedType());
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced())
			return {DataType::Kind::Type, 0, 0, classDef, -1, nullptr, DataType::Kind::Class};
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	return {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
}

static bool instantiateClassFromArgumentTypes(
	ClassDefinition *classDef, const std::vector<DataType> &argumentTypes, DataType &outTypeRef, int baseClassInstIndex
) {
	if (!classDef || classDef->fields.size() != argumentTypes.size())
		return false;

	std::vector<DataType> fieldTypes;
	fieldTypes.reserve(argumentTypes.size());
	for (size_t i = 0; i < argumentTypes.size(); i++) {
		DataType fieldType = classDef->fields[i].declaredType;
		if (baseClassInstIndex >= 0 && static_cast<size_t>(baseClassInstIndex) < classDef->instantiations.size() &&
			i < classDef->instantiations[baseClassInstIndex].fieldTypes.size()) {
			fieldType = classDef->instantiations[baseClassInstIndex].fieldTypes[i];
		}
		if (fieldType.kind == DataType::Kind::Any) {
			if (!argumentTypes[i].isDeduced())
				return false;
			fieldType = argumentTypes[i];
		} else if (fieldType.kind == DataType::Kind::Array && fieldType.arraySize >= 0 && !fieldType.arrayElementType) {
			if (!argumentTypes[i].isDeduced() || argumentTypes[i].kind != DataType::Kind::Array ||
				argumentTypes[i].arraySize != fieldType.arraySize)
				return false;
			fieldType = argumentTypes[i];
		} else if (fieldType.kind == DataType::Kind::Class && fieldType.classInstIndex < 0) {
			fieldType = concretizeClassType(fieldType);
		}

		if (!fieldType.isDeduced())
			return false;
		fieldTypes.push_back(fieldType);
	}

	int instIndex = classDef->getOrCreateInstantiation(fieldTypes);
	outTypeRef = {DataType::Kind::Type, 0, 0, classDef, instIndex, nullptr, DataType::Kind::Class};
	return true;
}

static bool isWholeNumberLiteral(Function *expr) {
	if (!expr || expr->kind != Function::Kind::Literal || !std::holds_alternative<double>(expr->literalValue))
		return false;
	std::string_view literalText = expr->range.subString;
	if (literalText.find('.') != std::string_view::npos || literalText.find('e') != std::string_view::npos ||
		literalText.find('E') != std::string_view::npos)
		return false;
	double value = std::get<double>(expr->literalValue);
	return std::trunc(value) == value;
}

static std::string makeFloatLiteralReplacement(Function *expr) {
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

static bool isLogicalOperandType(const DataType &type) { return type.kind == DataType::Kind::Bool || type.isNumeric(); }

static std::string extractFieldName(Function *expr) {
	if (!expr)
		return {};
	if (auto *str = std::get_if<std::string>(&expr->literalValue))
		return *str;
	if (expr->kind == Function::Kind::Variable && expr->variable)
		return expr->variable->name;
	return {};
}

static std::string diagnosticFunctionText(Function *expr) {
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

static std::string buildTypeFailureDiagnostic(Function *expr, const std::string &detail) {
	std::string message =
		"Function '" + diagnosticFunctionText(expr) + "' parses successfully without types, but not with types";
	if (!detail.empty())
		message += ": " + detail;
	return message;
}

// Must stay in sync with codegen's ensureType conversion support.
static bool isSupportedCastConversion(const DataType &fromType, const DataType &toType) {
	if (fromType == toType)
		return true;
	if (fromType.isPointer() && toType.isPointer())
		return true;
	if (fromType.isPointer() && toType.kind == DataType::Kind::Int)
		return true;
	if (fromType.kind == DataType::Kind::Int && toType.isPointer())
		return true;
	if (fromType.isNumeric() && toType.isNumeric())
		return true;
	if (fromType.kind == DataType::Kind::Bool && toType.isNumeric())
		return true;
	return false;
}

static bool mergeArrayElementType(const DataType &current, const DataType &next, DataType &merged) {
	if (!current.isDeduced() || !next.isDeduced())
		return false;
	if (current == next) {
		merged = current;
		return true;
	}
	if (current.isNumeric() && next.isNumeric())
		return DataType::promoteArithmetic(current, next, merged);
	return false;
}

// Wraps ParseContext with type validity tracking and trial mode for operand reordering.
// During reordering trials, diagnostics are suppressed and failures only affect the current trial.
struct InferenceContext {
	struct TrialJournal {
		struct VariableUndo {
			Variable *variable;
			DataType type;
			Range typeOriginRange;
			std::string typeOriginFloatLiteralReplacement;
		};

		struct SectionInstantiationUndo {
			Section *section;
			std::vector<DataType> argTypes;
			bool existed;
			Instantiation value;
		};

		std::vector<VariableUndo> variableTypeUndo;
		std::unordered_set<Variable *> seenVariables;
		std::vector<std::pair<ClassDefinition *, size_t>> classInstantiationSizes;
		std::unordered_set<ClassDefinition *> seenClassDefinitions;
		std::vector<Section *> touchedSections;
		std::unordered_set<Section *> seenSections;
		std::vector<SectionInstantiationUndo> sectionInstantiationUndo;
		std::unordered_set<std::string> seenSectionInstantiations;

		void recordVariableWrite(Variable *var) {
			if (!var || seenVariables.contains(var))
				return;
			seenVariables.insert(var);
			variableTypeUndo.push_back({var, var->type, var->typeOriginRange, var->typeOriginFloatLiteralReplacement});
		}

		void recordClassInstantiationAppend(ClassDefinition *classDef) {
			if (!classDef || seenClassDefinitions.contains(classDef))
				return;
			seenClassDefinitions.insert(classDef);
			classInstantiationSizes.push_back({classDef, classDef->instantiations.size()});
		}

		void recordTouchedSection(Section *section) {
			if (!section || seenSections.contains(section))
				return;
			seenSections.insert(section);
			touchedSections.push_back(section);
		}

		void recordSectionInstantiationWrite(Section *section, const std::vector<DataType> &argTypes) {
			if (!section)
				return;
			std::string key = std::to_string(reinterpret_cast<uintptr_t>(section)) + "|";
			for (const DataType &type : argTypes)
				key += type.toString() + ";";
			if (seenSectionInstantiations.contains(key))
				return;
			seenSectionInstantiations.insert(key);
			auto it = section->instantiations.find(argTypes);
			if (it == section->instantiations.end())
				sectionInstantiationUndo.push_back({section, argTypes, false, {}});
			else
				sectionInstantiationUndo.push_back({section, argTypes, true, it->second});
		}
	};

	ParseContext &parseContext;
	Instantiation *currentInstantiation{};
	std::unordered_map<VariableReference *, CompileTimeValue> currentKnownConstants;
	std::vector<std::unordered_set<VariableReference *>> loopMutationStack;
	bool typesValid = true;
	bool trial = false;
	bool suppressDiagnostics = false;
	bool suppressReinferPassDiagnostics = false;
	bool observedInProgressUndeducedInstantiation = false;
	std::string typeFailureDetail;
	TrialJournal *trialJournal{};

	InferenceContext(ParseContext &pc) : parseContext(pc) {}
	InferenceContext(ParseContext &pc, bool trial) : parseContext(pc), trial(trial) {}

	void addDiagnostic(Diagnostic diagnostic) {
		if (!trial && !suppressDiagnostics && !suppressReinferPassDiagnostics)
			parseContext.diagnostics.push_back(std::move(diagnostic));
	}

	void setTypeFailure(std::string detail) {
		typesValid = false;
		if (typeFailureDetail.empty())
			typeFailureDetail = std::move(detail);
	}

	VariableReference *normalizeReference(VariableReference *reference) const {
		if (!reference)
			return nullptr;
		return reference->definition ? reference->definition : reference;
	}

	CompileTimeValue lookupKnownConstant(VariableReference *reference) const {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return {};
		auto it = currentKnownConstants.find(key);
		return it != currentKnownConstants.end() ? it->second : CompileTimeValue{};
	}

	void setKnownConstant(VariableReference *reference, const CompileTimeValue &value) {
		VariableReference *key = normalizeReference(reference);
		if (!key)
			return;
		if (isCompileTimeKnown(value))
			currentKnownConstants[key] = value;
		else
			currentKnownConstants.erase(key);
	}

	void snapshotReferenceConstant(VariableReference *reference) {
		if (trial || !reference)
			return;
		CompileTimeValue value = lookupKnownConstant(reference);
		auto &target =
			currentInstantiation ? currentInstantiation->constantValuesByReference : parseContext.constantValuesByReference;
		if (isCompileTimeKnown(value))
			target[reference] = value;
		else
			target.erase(reference);
	}

	void pushLoopMutationScope() { loopMutationStack.emplace_back(); }

	std::unordered_set<VariableReference *> popLoopMutationScope() {
		if (loopMutationStack.empty())
			return {};
		std::unordered_set<VariableReference *> mutations = std::move(loopMutationStack.back());
		loopMutationStack.pop_back();
		if (!loopMutationStack.empty())
			loopMutationStack.back().insert(mutations.begin(), mutations.end());
		return mutations;
	}

	bool inLoopMutationScope() const { return !loopMutationStack.empty(); }

	void noteLoopMutation(VariableReference *reference) {
		if (loopMutationStack.empty())
			return;
		VariableReference *normalized = normalizeReference(reference);
		if (!normalized)
			return;
		loopMutationStack.back().insert(normalized);
	}
};

struct ScopedDiagnosticSuppression {
	InferenceContext &context;
	bool previous;

	explicit ScopedDiagnosticSuppression(InferenceContext &context) : context(context), previous(context.suppressDiagnostics) {
		context.suppressDiagnostics = true;
	}

	~ScopedDiagnosticSuppression() { context.suppressDiagnostics = previous; }
};
