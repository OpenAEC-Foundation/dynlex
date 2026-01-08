#pragma once

#include "dap/breakpoint.hpp"
#include "dap/debugState.hpp"
#include "dap/scope.hpp"
#include "dap/stackFrame.hpp"
#include "dap/stepType.hpp"
#include "dap/threadInfo.hpp"
#include "dap/variable.hpp"
#include "lexer/sourceLocation.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tbx {

// Debug Adapter Protocol server implementation
class DapServer {
public:
  DapServer();
  ~DapServer();

  // Run the DAP server (main loop reading from stdin, writing to stdout)
  void run();

  // Enable debug logging
  void setDebug(bool debug) { debugMode = debug; }

private:
  bool debugMode{};
  bool initialized{};
  bool launched{};
  int sequenceNumber{1};

  // TODO: The interpreter has been removed from the old pipeline
  // Debugging will be re-implemented with the new compiler pipeline

  // Breakpoint management
  int nextBreakpointId{1};
  std::unordered_map<std::string, std::vector<Breakpoint>> breakpoints;

  // Stack frame management
  int nextFrameId{1};
  std::vector<StackFrame> stackFrames;

  // Variable references
  int nextVariableRef{1};
  std::unordered_map<int, std::vector<Variable>> variableRefs;

  // Execution state
  std::atomic<DebugState> state{DebugState::Stopped};
  StepType stepType{StepType::None};
  int stepDepth{};
  std::mutex stateMutex;
  std::condition_variable stateChanged;

  // Source file content
  std::string sourceFile;
  std::string sourceContent;

  // Message handling
  nlohmann::json handleRequest(const std::string &command,
                               const nlohmann::json &args, int seq);

  // DAP request handlers
  nlohmann::json handleInitialize(const nlohmann::json &args);
  nlohmann::json handleLaunch(const nlohmann::json &args);
  nlohmann::json handleSetBreakpoints(const nlohmann::json &args);
  nlohmann::json handleConfigurationDone(const nlohmann::json &args);
  nlohmann::json handleThreads(const nlohmann::json &args);
  nlohmann::json handleStackTrace(const nlohmann::json &args);
  nlohmann::json handleScopes(const nlohmann::json &args);
  nlohmann::json handleVariables(const nlohmann::json &args);
  nlohmann::json handleContinue(const nlohmann::json &args);
  nlohmann::json handleNext(const nlohmann::json &args);
  nlohmann::json handleStepIn(const nlohmann::json &args);
  nlohmann::json handleStepOut(const nlohmann::json &args);
  nlohmann::json handlePause(const nlohmann::json &args);
  nlohmann::json handleDisconnect(const nlohmann::json &args);
  nlohmann::json handleEvaluate(const nlohmann::json &args);

  // Execution control
  void startExecution();
  void pauseExecution();
  void continueExecution();
  void stepExecution(StepType type);

  // Breakpoint checking
  bool shouldBreak(const SourceLocation &location);

  // Event sending
  void sendEvent(const std::string &event,
                 const nlohmann::json &body = nlohmann::json());
  void sendStoppedEvent(const std::string &reason,
                        const std::string &description = "");
  void sendTerminatedEvent();
  void sendOutputEvent(const std::string &category, const std::string &output);

  // IO helpers (reusing LSP JSON format)
  std::string readMessage();
  void writeMessage(const std::string &content);
  void sendResponse(int requestSeq, bool success, const std::string &command,
                    const nlohmann::json &body = nlohmann::json(),
                    const std::string &message = "");

  // Logging
  void log(const std::string &message);
};

} // namespace tbx
