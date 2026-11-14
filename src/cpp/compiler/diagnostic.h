#pragma once
#include "range.h"
#include "sourceFile.h"
#include "stringFunctions.h"
#include "syntaxConfig.h"
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct ParseContext;
struct VariableStorageScope;

struct DiagnosticPublication {
	bool published = false;
	std::string identity;
};

struct DiagnosticEmission {
	std::string discriminator;
	std::shared_ptr<DiagnosticPublication> publication;
	std::shared_ptr<const VariableStorageScope> owner;
	std::vector<std::pair<std::shared_ptr<const VariableStorageScope>, int>> retentionScopes;
	int traceBase = -1;
};

struct ConfiguredDiagnosticMessage {
	std::string key;
	std::string variant;
	std::vector<std::pair<std::string, std::string>> placeholders;
	bool internal = false;
	std::string placeholderName;
	std::vector<ConfiguredDiagnosticMessage> nestedPlaceholders;
};

ConfiguredDiagnosticMessage configuredDiagnosticMessage(
	const ParseContext &context, Range sourceRange, std::string_view key, std::string_view variant,
	std::vector<std::pair<std::string, std::string>> placeholders
);
std::string renderConfiguredDiagnosticMessage(const ParseContext &context, const ConfiguredDiagnosticMessage &message);

struct RelatedInfo {
	std::string message;
	Range range;
	std::shared_ptr<const ConfiguredDiagnosticMessage> configuredMessage = {};
	std::shared_ptr<const VariableStorageScope> inferenceOwner = {};
	int inferenceDepth = -1;
	int inferenceOwnerBase = -1;
};

RelatedInfo configuredRelatedInfo(
	const ParseContext &context, Range syntaxRange, std::string_view key, std::string_view variant, Range relatedRange,
	std::vector<std::pair<std::string, std::string>> placeholders = {}
);

struct DiagnosticFix {
	std::string title;
	Range range;
	std::string replacement;
};

struct Diagnostic {
	enum class Level { Info, Warning, Error };
	Level level = Level::Error;
	std::string message;
	std::shared_ptr<const ConfiguredDiagnosticMessage> configuredMessage;
	Range range;
	std::vector<RelatedInfo> relatedInfo;
	std::vector<DiagnosticFix> quickFixes;
	std::shared_ptr<DiagnosticEmission> emission;
	DiagnosticEmission &editEmission() {
		if (!emission)
			emission = std::make_shared<DiagnosticEmission>();
		else if (emission.use_count() > 1)
			emission = std::make_shared<DiagnosticEmission>(*emission);
		return *emission;
	}
	Diagnostic() = default;
	template <typename... Args>
	Diagnostic(const ParseContext &context, Level level, Range range, Args &&...args)
		: Diagnostic(context, level, "message", range, std::forward<Args>(args)...) {}

	template <typename... Args>
	Diagnostic(const ParseContext &context, Level level, std::string_view key, Range range, Args &&...args)
		: Diagnostic(context, level, key, "message", range, std::forward<Args>(args)...) {}

	template <typename... Args>
	Diagnostic(
		const ParseContext &context, Level level, std::string_view key, std::string_view variant, Range range, Args &&...args
	)
		: level(level), range(range) {
		static_assert(sizeof...(Args) % 2 == 0, "Diagnostic placeholders must be provided as name/value pairs");
		std::vector<std::pair<std::string, std::string>> placeholders;
		placeholders.reserve(sizeof...(Args) / 2);
		appendPlaceholders(placeholders, std::forward<Args>(args)...);
		configuredMessage = std::make_shared<const ConfiguredDiagnosticMessage>(
			configuredDiagnosticMessage(context, range, key, variant, std::move(placeholders))
		);
		message = renderConfiguredDiagnosticMessage(context, *configuredMessage);
	}

	static Diagnostic configParseError(std::string_view message, Range range);
	std::string toString() const;
	Diagnostic relativeToInference(int base) const;
	bool sameEmission(const Diagnostic &other) const;
	void setConfiguredMessage(
		const ParseContext &context, Range syntaxRange, std::string_view key, std::string_view variant,
		std::vector<std::pair<std::string, std::string>> placeholders = {}
	);
	void setConfiguredPlaceholder(const ParseContext &context, std::string name, ConfiguredDiagnosticMessage replacement);

  private:
	template <typename T> static std::string_view requireStringView(T &&value) {
		static_assert(
			std::is_convertible_v<T, std::string_view>, "Diagnostic placeholders only accept string-like name/value arguments"
		);
		return std::string_view(std::forward<T>(value));
	}

	static void appendPlaceholders(std::vector<std::pair<std::string, std::string>> &) {}

	template <typename Name, typename Value, typename... Rest>
	static void appendPlaceholders(
		std::vector<std::pair<std::string, std::string>> &placeholders, Name &&name, Value &&value, Rest &&...rest
	) {
		placeholders.emplace_back(
			std::string(requireStringView(std::forward<Name>(name))), std::string(requireStringView(std::forward<Value>(value)))
		);
		if constexpr (sizeof...(Rest) > 0)
			appendPlaceholders(placeholders, std::forward<Rest>(rest)...);
	}
};

Diagnostic unknownTypeConstraintDiagnostic(const ParseContext &context, Range range, std::string_view constraint);

template <> inline bool stringToEnum<Diagnostic::Level>(std::string_view levelName, Diagnostic::Level &result) {
	if (levelName == "Info") {
		result = Diagnostic::Level::Info;
		return true;
	}
	if (levelName == "Warning") {
		result = Diagnostic::Level::Warning;
		return true;
	}
	if (levelName == "Error") {
		result = Diagnostic::Level::Error;
		return true;
	}
	return false;
}
template <> inline bool enumToString<Diagnostic::Level>(Diagnostic::Level level, std::string &result) {
	switch (level) {
	case Diagnostic::Level::Info:
		result = "Info";
		return true;
	case Diagnostic::Level::Warning:
		result = "Warning";
		return true;
	case Diagnostic::Level::Error:
		result = "Error";
		return true;
	}
	return false;
}
