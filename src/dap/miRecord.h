#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace dap {

// Parsed debugger-machine-interface output record.
struct MiRecord {
	enum Type {
		Result,		   // ^done, ^running, ^error, etc.
		ExecAsync,	   // *stopped, *running
		StatusAsync,   // +download, etc.
		NotifyAsync,   // =thread-group-added, etc.
		ConsoleStream, // ~"text"
		TargetStream,  // @"text"
		LogStream,	   // &"text"
		Prompt		   // (gdb)
	};

	Type type = Prompt;
	int token = -1;			 // command token (-1 if none)
	std::string recordClass; // e.g. "done", "stopped", "error"
	nlohmann::json values;	 // parsed key=value pairs as JSON
	std::string streamText;	 // for stream records
};

} // namespace dap
