#pragma once
#include "bindingResolution.h"
#include "parseContext.h"
#include <algorithm>
#include <cstdint>
#include <sstream>

inline std::string expressionCaptureIdentity(Expression *pack, size_t index, const BindingFrameStack &bindings) {
	std::ostringstream identity;
	identity << pack << ":" << index;
	bindings.forEachFrame([&](const BindingFrame &frame) {
		std::vector<std::string> entries;
		for (const auto &[name, value] : frame.bindings) {
			std::ostringstream entry;
			entry << "n" << name.size() << ":" << name << ":" << value;
			entries.push_back(entry.str());
		}
		for (const auto &[definition, value] : frame.parameterBindings) {
			std::ostringstream entry;
			entry << "p" << definition << ":" << value;
			entries.push_back(entry.str());
		}
		for (const auto &[name, definition] : frame.declarations) {
			std::ostringstream entry;
			entry << "d" << name.size() << ":" << name << ":" << definition;
			entries.push_back(entry.str());
		}
		std::sort(entries.begin(), entries.end());
		identity << "[";
		for (const auto &entry : entries)
			identity << entry.size() << ":" << entry;
		identity << "]";
	});
	return identity.str();
}

inline ExpressionCapture
captureExpression(ParseContext &context, Expression *pack, size_t index, const BindingFrameStack &bindings) {
	context.retainCapturedExpressionBody(pack);
	return {expressionCaptureIdentity(pack, index, bindings), std::make_shared<ExpressionCaptureData>(pack, index, bindings)};
}
