#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/types.h>

namespace dap {

using Json = nlohmann::json;

// Parsed GDB MI output record
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
	Json values;			 // parsed key=value pairs as JSON
	std::string streamText;	 // for stream records
};

// Manages a GDB subprocess communicating via MI protocol
class GdbMI {
  public:
	GdbMI() = default;
	~GdbMI();

	// Launch GDB subprocess with MI interpreter
	bool launch(const std::string &gdbPath = "gdb");

	// Send an MI command, returns token for matching response
	int send(const std::string &command);

	// Read one MI output record (blocks until available)
	bool readRecord(MiRecord &record);

	// Send command and wait for its result record, passing async records to callback
	MiRecord sendAndWait(const std::string &command, std::function<void(const MiRecord &)> asyncHandler = nullptr);

	// Terminate GDB subprocess
	void terminate();

	bool isRunning() const { return pid > 0; }

	// Get the read fd for polling
	int getReadFd() const { return fromGdb; }

  private:
	pid_t pid = -1;
	int toGdb = -1;	  // write end of stdin pipe
	int fromGdb = -1; // read end of stdout pipe
	int nextToken = 1;

	// Read a single line from GDB (blocks)
	std::string readLine();

	// Parse an MI output line into a record
	MiRecord parseLine(const std::string &line);

	// Parse MI value syntax
	static Json parseMiValue(const std::string &s, size_t &pos);
	static Json parseMiTuple(const std::string &s, size_t &pos);
	static Json parseMiList(const std::string &s, size_t &pos);
	static std::string parseMiString(const std::string &s, size_t &pos);
	static Json parseMiResult(const std::string &s, size_t &pos);
};

} // namespace dap
