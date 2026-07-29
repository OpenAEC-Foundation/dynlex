case Expression::Kind::PatternCall:
if (!inferPatternCall(expr, context, flexBindingFrameStack, preserveCurrentGrouping))
	return;
break;

case Expression::Kind::Pending:
context.setExpressionValue(expr, {});
break;
}
CompileTimeValue inferredValue = context.lookupExpressionValue(expr);
if (!isCompileTimeKnown(inferredValue) && expr->type.kind == DataType::Kind::Type && !expr->inferredFlexExpansion)
	context.setExpressionValue(expr, TypeReferenceValue::exact(expr->type));
}

static bool
inferExpressionWithCurrentGrouping(Expression *&expr, InferenceContext &context, const BindingFrameStack &bindingFrameStack) {
	inferOrderedExpression(expr, context, bindingFrameStack, true);
	return context.typesValid;
}
