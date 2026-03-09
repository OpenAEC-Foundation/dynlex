#pragma once
#include "../lsp/transport.h"
#include "dapProtocol.h"
#include "debuggerAdapter.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dap {

class DapServer {
  public:
	explicit DapServer(std::unique_ptr<lsp::Transport> transport, std::string executablePath = "");
	~DapServer();

	// Run the server (blocks until disconnect)
	void run();

  private:
	std::unique_ptr<lsp::Transport> transport;
	std::unique_ptr<DebuggerAdapter> debugger;
	std::atomic<bool> running{false};
	int seq = 1;
	std::mutex writeMutex;
	std::thread gdbReaderThread;
	std::string executablePath;

	// Breakpoint tracking: source path -> list of GDB breakpoint numbers
	std::unordered_map<std::string, std::vector<int>> breakpointsByFile;

	// The compiled output binary path
	std::string compiledBinary;

	// Launch options
	bool stopOnEntry = false;

	// Variable reference IDs: 1=locals, 2=arguments
	static constexpr int localsRef = 1;
	static constexpr int argsRef = 2;

	// Message I/O (Content-Length framing, same as LSP)
	std::string readMessage();
	void sendJson(const Json &msg);
	void sendResponse(int requestSeq, const std::string &command, const Json &body);
	void sendErrorResponse(int requestSeq, const std::string &command, const std::string &message);
	void sendEvent(const std::string &event, const Json &body = Json::object());

	// Message dispatch
	void handleMessage(const Json &msg);

	// DAP request handlers
	void handleInitialize(int seq, const Json &args);
	void handleLaunch(int seq, const Json &args);
	void handleSetBreakpoints(int seq, const Json &args);
	void handleSetExceptionBreakpoints(int seq, const Json &args);
	void handleConfigurationDone(int seq, const Json &args);
	void handleThreads(int seq, const Json &args);
	void handleStackTrace(int seq, const Json &args);
	void handleScopes(int seq, const Json &args);
	void handleVariables(int seq, const Json &args);
	void handleContinue(int seq, const Json &args);
	void handleNext(int seq, const Json &args);
	void handleStepIn(int seq, const Json &args);
	void handleStepOut(int seq, const Json &args);
	void handlePause(int seq, const Json &args);
	void handleDisconnect(int seq, const Json &args);

	// GDB reader thread
	void gdbReaderLoop();
	void handleGdbRecord(const MiRecord &record);

	// Helpers
	bool compileDlFile(const std::string &dlFile, const std::string &outputPath, std::string &errorOutput);
	std::string findSelfPath() const;
	std::string demangleFunctionName(const std::string &mangled);

	void log(const std::string &msg);
};

} // namespace dap
