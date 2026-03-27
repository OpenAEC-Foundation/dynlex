#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace dap {

using Json = nlohmann::json;

// DAP Source object
struct Source {
	std::string name;
	std::string path;
};

inline void to_json(Json &j, const Source &s) {
	j = Json::object();
	if (!s.name.empty())
		j["name"] = s.name;
	if (!s.path.empty())
		j["path"] = s.path;
}

// DAP Breakpoint object
struct Breakpoint {
	int id = 0;
	bool verified = false;
	int line = 0;
	Source source;
};

inline void to_json(Json &j, const Breakpoint &b) {
	j = {{"id", b.id}, {"verified", b.verified}, {"line", b.line}};
	if (!b.source.path.empty())
		j["source"] = b.source;
}

// DAP StackFrame object
struct StackFrame {
	int id = 0;
	std::string name;
	Source source;
	int line = 0;
	int column = 0;
};

inline void to_json(Json &j, const StackFrame &f) {
	j = {{"id", f.id}, {"name", f.name}, {"line", f.line}, {"column", f.column}};
	if (!f.source.path.empty())
		j["source"] = f.source;
}

// DAP Scope object
struct Scope {
	std::string name;
	int variablesReference = 0;
	bool expensive = false;
};

inline void to_json(Json &j, const Scope &s) {
	j = {{"name", s.name}, {"variablesReference", s.variablesReference}, {"expensive", s.expensive}};
}

// DAP Variable object
struct Variable {
	std::string name;
	std::string value;
	std::string type;
	int variablesReference = 0;
};

inline void to_json(Json &j, const Variable &v) {
	j = {{"name", v.name}, {"value", v.value}, {"variablesReference", v.variablesReference}};
	if (!v.type.empty())
		j["type"] = v.type;
}

// DAP Thread object
struct Thread {
	int id = 0;
	std::string name;
};

inline void to_json(Json &j, const Thread &t) { j = {{"id", t.id}, {"name", t.name}}; }

// DAP Capabilities (returned from initialize)
struct Capabilities {
	bool supportsConfigurationDoneRequest = true;
	bool supportsFunctionBreakpoints = false;
	bool supportsConditionalBreakpoints = false;
	bool supportsEvaluateForHovers = false;
	bool supportsStepBack = false;
	bool supportsSetVariable = false;
	bool supportTerminateDebuggee = true;
};

inline void to_json(Json &j, const Capabilities &c) {
	j = {
		{"supportsConfigurationDoneRequest", c.supportsConfigurationDoneRequest},
		{"supportsFunctionBreakpoints", c.supportsFunctionBreakpoints},
		{"supportsConditionalBreakpoints", c.supportsConditionalBreakpoints},
		{"supportsEvaluateForHovers", c.supportsEvaluateForHovers},
		{"supportsStepBack", c.supportsStepBack},
		{"supportsSetVariable", c.supportsSetVariable},
		{"supportTerminateDebuggee", c.supportTerminateDebuggee},
		{"exceptionBreakpointFilters", Json::array()},
	};
}

} // namespace dap
