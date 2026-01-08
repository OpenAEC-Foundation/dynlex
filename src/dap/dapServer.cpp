#include "dap/dapServer.hpp"
#include "lexer/lexer.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace tbx {

DapServer::DapServer() {}
DapServer::~DapServer() = default;

void DapServer::run() {
  log("3BX Debug Adapter starting...");

  while (true) {
    try {
      std::string message = readMessage();
      if (message.empty()) {
        break;
      }

      nlohmann::json request = nlohmann::json::parse(message);
      std::string type = request["type"].get<std::string>();

      if (type == "request") {
        std::string command = request["command"].get<std::string>();
        int seq = request["seq"].get<int>();
        nlohmann::json args =
            request.value("arguments", nlohmann::json::object());

        nlohmann::json result = handleRequest(command, args, seq);

        if (command == "disconnect") {
          break;
        }
      }
    } catch (const std::exception &exception) {
      log("Error: " + std::string(exception.what()));
    }
  }

  log("3BX Debug Adapter shutting down.");
}

std::string DapServer::readMessage() {
  // Read headers until empty line
  int contentLength = -1;

  while (true) {
    std::string line;
    if (!std::getline(std::cin, line)) {
      return ""; // EOF
    }

    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      break; // End of headers
    }

    // Parse Content-Length header
    if (line.find("Content-Length:") == 0) {
      contentLength = std::stoi(line.substr(15));
    }
  }

  if (contentLength <= 0) {
    log("Invalid Content-Length");
    return "";
  }

  // Read content
  std::string content(contentLength, '\0');
  std::cin.read(&content[0], contentLength);

  if (std::cin.gcount() != contentLength) {
    log("Failed to read full message content");
    return "";
  }

  log("Received: " + content);
  return content;
}

void DapServer::writeMessage(const std::string &content) {
  log("Sending: " + content);
  std::cout << "Content-Length: " << content.size() << "\r\n\r\n" << content;
  std::cout.flush();
}

void DapServer::sendResponse(int requestSeq, bool success,
                             const std::string &command,
                             const nlohmann::json &body,
                             const std::string &message) {
  nlohmann::json response = nlohmann::json::object();
  response["seq"] = sequenceNumber++;
  response["type"] = "response";
  response["request_seq"] = requestSeq;
  response["success"] = success;
  response["command"] = command;

  if (!body.is_null()) {
    response["body"] = body;
  }

  if (!message.empty()) {
    response["message"] = message;
  }

  writeMessage(response.dump());
}

void DapServer::sendEvent(const std::string &event,
                          const nlohmann::json &body) {
  nlohmann::json eventMsg = nlohmann::json::object();
  eventMsg["seq"] = sequenceNumber++;
  eventMsg["type"] = "event";
  eventMsg["event"] = event;

  if (!body.is_null()) {
    eventMsg["body"] = body;
  }

  writeMessage(eventMsg.dump());
}

void DapServer::sendStoppedEvent(const std::string &reason,
                                 const std::string &description) {
  nlohmann::json body = nlohmann::json::object();
  body["reason"] = reason;
  body["threadId"] = 1;
  body["allThreadsStopped"] = true;

  if (!description.empty()) {
    body["description"] = description;
  }

  sendEvent("stopped", body);
}

void DapServer::sendTerminatedEvent() { sendEvent("terminated"); }

void DapServer::sendOutputEvent(const std::string &category,
                                const std::string &output) {
  nlohmann::json body = nlohmann::json::object();
  body["category"] = category;
  body["output"] = output;
  sendEvent("output", body);
}

void DapServer::log(const std::string &message) {
  if (debugMode) {
    std::cerr << "[3BX-DAP] " << message << std::endl;
  }
}

nlohmann::json DapServer::handleRequest(const std::string &command,
                                        const nlohmann::json &args, int seq) {
  log("Request: " + command);

  if (command == "initialize") {
    nlohmann::json result = handleInitialize(args);
    sendResponse(seq, true, command, result);

    // Send initialized event
    sendEvent("initialized");
    return result;
  }

  if (command == "launch") {
    nlohmann::json result = handleLaunch(args);
    sendResponse(seq, !result.is_null() || launched, command, result,
                 launched ? "" : "Launch failed");
    return result;
  }

  if (command == "setBreakpoints") {
    nlohmann::json result = handleSetBreakpoints(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "configurationDone") {
    nlohmann::json result = handleConfigurationDone(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "threads") {
    nlohmann::json result = handleThreads(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "stackTrace") {
    nlohmann::json result = handleStackTrace(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "scopes") {
    nlohmann::json result = handleScopes(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "variables") {
    nlohmann::json result = handleVariables(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "continue") {
    nlohmann::json result = handleContinue(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "next") {
    nlohmann::json result = handleNext(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "stepIn") {
    nlohmann::json result = handleStepIn(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "stepOut") {
    nlohmann::json result = handleStepOut(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "pause") {
    nlohmann::json result = handlePause(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "disconnect") {
    nlohmann::json result = handleDisconnect(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  if (command == "evaluate") {
    nlohmann::json result = handleEvaluate(args);
    sendResponse(seq, true, command, result);
    return result;
  }

  // Unknown command
  log("Unknown command: " + command);
  sendResponse(seq, false, command, nlohmann::json(),
               "Unknown command: " + command);
  return nlohmann::json();
}

nlohmann::json DapServer::handleInitialize(const nlohmann::json &args) {
  initialized = true;

  // Return capabilities
  nlohmann::json capabilities = nlohmann::json::object();
  capabilities["supportsConfigurationDoneRequest"] = true;
  capabilities["supportsFunctionBreakpoints"] = false;
  capabilities["supportsConditionalBreakpoints"] = false;
  capabilities["supportsHitConditionalBreakpoints"] = false;
  capabilities["supportsEvaluateForHovers"] = false;
  capabilities["supportsStepBack"] = false;
  capabilities["supportsSetVariable"] = false;
  capabilities["supportsRestartFrame"] = false;
  capabilities["supportsGotoTargetsRequest"] = false;
  capabilities["supportsStepInTargetsRequest"] = false;
  capabilities["supportsCompletionsRequest"] = false;
  capabilities["supportsModulesRequest"] = false;
  capabilities["supportsExceptionOptions"] = false;
  capabilities["supportsValueFormattingOptions"] = false;
  capabilities["supportsExceptionInfoRequest"] = false;
  capabilities["supportTerminateDebuggee"] = true;
  capabilities["supportsDelayedStackTraceLoading"] = false;
  capabilities["supportsLoadedSourcesRequest"] = false;
  capabilities["supportsLogPoints"] = false;
  capabilities["supportsTerminateThreadsRequest"] = false;
  capabilities["supportsSetExpression"] = false;
  capabilities["supportsTerminateRequest"] = true;

  return capabilities;
}

nlohmann::json DapServer::handleLaunch(const nlohmann::json &args) {
  if (args.contains("program")) {
    sourceFile = args["program"].get<std::string>();
  }

  if (sourceFile.empty()) {
    sendOutputEvent("stderr",
                    "Error: No program specified in launch configuration\n");
    return nlohmann::json();
  }

  // Read source file
  std::ifstream file(sourceFile);
  if (!file) {
    sendOutputEvent("stderr", "Error: Cannot open file: " + sourceFile + "\n");
    return nlohmann::json();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  sourceContent = buffer.str();

  sendOutputEvent("console", "Loading: " + sourceFile + "\n");

  // TODO: The old interpreter has been removed.
  // Debugging needs to be re-implemented with the new compiler pipeline.
  sendOutputEvent(
      "stderr",
      "Debug mode is not yet available with the new compiler pipeline.\n");
  sendOutputEvent("stderr", "Please use regular compilation (./build/3bx "
                            "<file>) to run your program.\n");

  return nlohmann::json();
}

nlohmann::json DapServer::handleSetBreakpoints(const nlohmann::json &args) {
  nlohmann::json source = args["source"];
  std::string path = source["path"].get<std::string>();

  // Clear existing breakpoints for this source
  breakpoints[path].clear();

  nlohmann::json result = nlohmann::json::object();
  nlohmann::json breakpointsArray = nlohmann::json::array();

  if (args.contains("breakpoints")) {
    for (const auto &bp : args["breakpoints"]) {
      int line = bp["line"].get<int>();

      Breakpoint newBp;
      newBp.id = nextBreakpointId++;
      newBp.source = path;
      newBp.line = line;
      newBp.verified = false; // Not verified since debugging is not implemented

      breakpoints[path].push_back(newBp);

      nlohmann::json bpResult = nlohmann::json::object();
      bpResult["id"] = newBp.id;
      bpResult["verified"] = false;
      bpResult["line"] = line;
      breakpointsArray.push_back(bpResult);
    }
  }

  result["breakpoints"] = breakpointsArray;
  return result;
}

nlohmann::json DapServer::handleConfigurationDone(const nlohmann::json &args) {
  // Debugging not implemented - send terminated event
  sendTerminatedEvent();
  return nlohmann::json::object();
}

nlohmann::json DapServer::handleThreads(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  nlohmann::json threads = nlohmann::json::array();

  nlohmann::json thread = nlohmann::json::object();
  thread["id"] = 1;
  thread["name"] = "main";
  threads.push_back(thread);

  result["threads"] = threads;
  return result;
}

nlohmann::json DapServer::handleStackTrace(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  nlohmann::json stackFrames = nlohmann::json::array();

  // No stack frames since debugging is not implemented
  result["stackFrames"] = stackFrames;
  result["totalFrames"] = 0;
  return result;
}

nlohmann::json DapServer::handleScopes(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  nlohmann::json scopes = nlohmann::json::array();
  result["scopes"] = scopes;
  return result;
}

nlohmann::json DapServer::handleVariables(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  nlohmann::json variables = nlohmann::json::array();
  result["variables"] = variables;
  return result;
}

nlohmann::json DapServer::handleContinue(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  result["allThreadsContinued"] = true;
  return result;
}

nlohmann::json DapServer::handleNext(const nlohmann::json &args) {
  return nlohmann::json::object();
}

nlohmann::json DapServer::handleStepIn(const nlohmann::json &args) {
  return nlohmann::json::object();
}

nlohmann::json DapServer::handleStepOut(const nlohmann::json &args) {
  return nlohmann::json::object();
}

nlohmann::json DapServer::handlePause(const nlohmann::json &args) {
  return nlohmann::json::object();
}

nlohmann::json DapServer::handleDisconnect(const nlohmann::json &args) {
  return nlohmann::json::object();
}

nlohmann::json DapServer::handleEvaluate(const nlohmann::json &args) {
  nlohmann::json result = nlohmann::json::object();
  result["result"] = "Debugging not available";
  result["type"] = "string";
  result["variablesReference"] = 0;
  return result;
}

void DapServer::startExecution() {
  // Not implemented
}

void DapServer::pauseExecution() {
  // Not implemented
}

void DapServer::continueExecution() {
  // Not implemented
}

void DapServer::stepExecution(StepType type) {
  // Not implemented
}

bool DapServer::shouldBreak(const SourceLocation &location) { return false; }

} // namespace tbx
