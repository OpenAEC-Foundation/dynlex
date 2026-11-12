#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct ParseContext;
struct Section;
struct VariableReference;
enum class ValidationRead { Definition, Children, References, Binding, DiagnosticOrder };

std::vector<VariableReference *> captureReferences(Section *function, const std::function<void(Section *)> &observe = {});

struct ValidationWork {
	std::size_t evaluated = 0, reused = 0, diagnosticsRestored = 0, referencesVisited = 0, fingerprintsComputed = 0,
				referencesFingerprinted = 0;
};

struct ValidationHooks {
	std::function<bool(Section *, const std::function<bool()> &)> unit;
	std::function<void(Section *, ValidationRead, std::string_view)> read;
	ValidationWork *work{};
};

bool validate(ParseContext &context, const ValidationHooks &hooks);
bool compileBeforeTypeInference(const std::string &path, ParseContext &context, const std::function<bool()> &validation);
