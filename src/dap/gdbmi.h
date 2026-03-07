#pragma once
#include "miRecord.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/types.h>
#include <unordered_map>

namespace dap {

using Json = nlohmann::json;

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

	// Send command and wait for its result record.
	// Must only be called when the reader thread is running (it delivers results).
	MiRecord sendAndWait(const std::string &command);

	// Called by the reader thread to deliver a result record for a pending sendAndWait.
	void deliverResult(const MiRecord &record);

	// Terminate GDB subprocess
	void terminate();

	bool isRunning() const { return pid > 0; }

  private:
	pid_t pid = -1;
	int toGdb = -1;	  // write end of stdin pipe
	int fromGdb = -1; // read end of stdout pipe
	int nextToken = 1;

	// Synchronization for sendAndWait: reader thread delivers results via deliverResult()
	std::mutex resultMutex;
	std::condition_variable resultCV;
	std::unordered_map<int, MiRecord> pendingResults;
	std::atomic<bool> shuttingDown{false};

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
