#include "dapServer.h"
#include "../lsp/stdioTransport.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

namespace dap {

DapServer::DapServer(std::unique_ptr<lsp::Transport> transport, std::string executablePath)
	: transport(std::move(transport)), debugger(createDebuggerAdapter()), executablePath(std::move(executablePath)) {}

DapServer::~DapServer() {
	running = false;
	if (debugger)
		debugger->terminate();
	if (gdbReaderThread.joinable()) {
		gdbReaderThread.join();
	}
}

void DapServer::run() {
	running = true;
	log("DAP server started");

	while (running && transport && transport->isConnected()) {
		std::string message = readMessage();
		if (message.empty())
			break;

		try {
			Json j = Json::parse(message);
			handleMessage(j);
		} catch (const Json::parse_error &e) {
			log("JSON parse error: " + std::string(e.what()));
		}
	}

	running = false;
	if (debugger)
		debugger->terminate();
	if (gdbReaderThread.joinable()) {
		gdbReaderThread.join();
	}
}

// --- Message I/O (Content-Length framing, same as LSP) ---

std::string DapServer::readMessage() {
	if (!transport || !transport->isConnected())
		return "";

	std::string headers;
	char c;
	int consecutiveNewlines = 0;

	while (running && transport->isConnected()) {
		ssize_t n = transport->read(&c, 1);
		if (n <= 0)
			return "";
		headers += c;
		if (c == '\n') {
			consecutiveNewlines++;
			if (consecutiveNewlines >= 2 || (headers.size() >= 4 && headers.substr(headers.size() - 4) == "\r\n\r\n")) {
				break;
			}
		} else if (c != '\r') {
			consecutiveNewlines = 0;
		}
	}

	size_t contentLength = 0;
	std::string key = "Content-Length:";
	size_t pos = headers.find(key);
	if (pos != std::string::npos) {
		pos += key.length();
		while (pos < headers.size() && headers[pos] == ' ')
			pos++;
		size_t endPos = headers.find_first_of("\r\n", pos);
		contentLength = std::stoull(headers.substr(pos, endPos - pos));
	}

	if (contentLength == 0)
		return "";

	std::string body(contentLength, '\0');
	size_t totalRead = 0;
	while (totalRead < contentLength && running && transport->isConnected()) {
		ssize_t n = transport->read(&body[totalRead], contentLength - totalRead);
		if (n <= 0)
			return "";
		totalRead += n;
	}

	return body;
}

void DapServer::sendJson(const Json &msg) {
	std::lock_guard<std::mutex> lock(writeMutex);
	if (!transport || !transport->isConnected())
		return;

	std::string body = msg.dump();
	std::string header = "Content-Length: " + std::to_string(body.length()) + "\r\n\r\n";
	std::string full = header + body;

	size_t totalSent = 0;
	while (totalSent < full.size() && transport->isConnected()) {
		ssize_t n = transport->write(full.c_str() + totalSent, full.size() - totalSent);
		if (n <= 0)
			break;
		totalSent += n;
	}
}

void DapServer::sendResponse(int requestSeq, const std::string &command, const Json &body) {
	Json response = {
		{"seq", seq++},	   {"type", "response"}, {"request_seq", requestSeq},
		{"success", true}, {"command", command}, {"body", body},
	};
	sendJson(response);
}

void DapServer::sendErrorResponse(int requestSeq, const std::string &command, const std::string &message) {
	Json response = {
		{"seq", seq++},		{"type", "response"}, {"request_seq", requestSeq},
		{"success", false}, {"command", command}, {"message", message},
	};
	sendJson(response);
}

void DapServer::sendEvent(const std::string &event, const Json &body) {
	Json ev = {
		{"seq", seq++},
		{"type", "event"},
		{"event", event},
		{"body", body},
	};
	sendJson(ev);
}

// --- Message dispatch ---

void DapServer::handleMessage(const Json &msg) {
	std::string type = msg.value("type", "");
	if (type != "request")
		return;

	std::string command = msg.value("command", "");
	int reqSeq = msg.value("seq", 0);
	Json args = msg.value("arguments", Json::object());

	log("Request: " + command);

	if (command == "initialize")
		handleInitialize(reqSeq, args);
	else if (command == "launch")
		handleLaunch(reqSeq, args);
	else if (command == "setBreakpoints")
		handleSetBreakpoints(reqSeq, args);
	else if (command == "setExceptionBreakpoints")
		handleSetExceptionBreakpoints(reqSeq, args);
	else if (command == "configurationDone")
		handleConfigurationDone(reqSeq, args);
	else if (command == "threads")
		handleThreads(reqSeq, args);
	else if (command == "stackTrace")
		handleStackTrace(reqSeq, args);
	else if (command == "scopes")
		handleScopes(reqSeq, args);
	else if (command == "variables")
		handleVariables(reqSeq, args);
	else if (command == "continue")
		handleContinue(reqSeq, args);
	else if (command == "next")
		handleNext(reqSeq, args);
	else if (command == "stepIn")
		handleStepIn(reqSeq, args);
	else if (command == "stepOut")
		handleStepOut(reqSeq, args);
	else if (command == "pause")
		handlePause(reqSeq, args);
	else if (command == "disconnect")
		handleDisconnect(reqSeq, args);
	else {
		sendErrorResponse(reqSeq, command, "Unsupported command: " + command);
	}
}

// --- DAP request handlers ---

void DapServer::handleInitialize(int reqSeq, const Json & /*args*/) {
	Capabilities caps;
	sendResponse(reqSeq, "initialize", caps);
	sendEvent("initialized");
}

void DapServer::handleLaunch(int reqSeq, const Json &args) {
	std::string program = args.value("program", "");
	if (program.empty()) {
		sendErrorResponse(reqSeq, "launch", "No 'program' specified in launch configuration");
		return;
	}

	// Resolve to absolute path
	program = std::filesystem::absolute(program).string();

	// Compile the .dl file
	compiledBinary = program + ".debug_bin";
	std::string compileErrors;
	if (!compileDlFile(program, compiledBinary, compileErrors)) {
		std::string msg = "Compilation failed";
		if (!compileErrors.empty()) {
			msg += ":\n" + compileErrors;
		}
		sendErrorResponse(reqSeq, "launch", msg);
		return;
	}

	// Launch GDB
	if (!debugger || !debugger->isSupported()) {
		std::string message = debugger ? debugger->unsupportedReason() : "Debugger adapter unavailable";
		sendErrorResponse(reqSeq, "launch", message);
		return;
	}

	std::string launchError;
	if (!debugger->launch("gdb", launchError)) {
		sendErrorResponse(reqSeq, "launch", launchError.empty() ? "Failed to launch GDB" : launchError);
		return;
	}

	// Start the GDB reader thread — must be running before sendAndWait calls
	// so it can deliver result records
	gdbReaderThread = std::thread(&DapServer::gdbReaderLoop, this);

	// Set working directory: use launch config's cwd, or default to the DAP server's cwd
	// (VS Code sets the DAP adapter's cwd to the workspace root)
	std::string cwd = args.value("cwd", "");
	if (cwd.empty()) {
		cwd = std::filesystem::current_path().string();
	}
	debugger->sendAndWait("environment-cd " + cwd);

	// Load the binary
	MiRecord result = debugger->sendAndWait("file-exec-and-symbols " + compiledBinary);
	if (result.recordClass != "done") {
		std::string msg = result.values.value("msg", "Failed to load binary");
		sendErrorResponse(reqSeq, "launch", msg);
		return;
	}

	// Save launch options
	stopOnEntry = args.value("stopOnEntry", false);

	sendResponse(reqSeq, "launch", Json::object());
}

void DapServer::handleSetBreakpoints(int reqSeq, const Json &args) {
	std::string sourcePath;
	if (args.contains("source") && args["source"].contains("path")) {
		sourcePath = args["source"]["path"].get<std::string>();
	}

	// Delete existing breakpoints for this file
	if (breakpointsByFile.count(sourcePath)) {
		for (int bpNum : breakpointsByFile[sourcePath]) {
			debugger->sendAndWait("break-delete " + std::to_string(bpNum));
		}
		breakpointsByFile.erase(sourcePath);
	}

	// Insert new breakpoints
	std::vector<Breakpoint> breakpoints;
	std::vector<int> gdbBpNums;

	if (args.contains("breakpoints")) {
		for (const auto &bp : args["breakpoints"]) {
			int line = bp.value("line", 0);
			std::string loc = sourcePath + ":" + std::to_string(line);

			MiRecord result = debugger->sendAndWait("break-insert " + loc);

			Breakpoint dapBp;
			dapBp.id = static_cast<int>(breakpoints.size() + 1);
			dapBp.source = {std::filesystem::path(sourcePath).filename().string(), sourcePath};

			if (result.recordClass == "done" && result.values.contains("bkpt")) {
				auto &bkpt = result.values["bkpt"];
				dapBp.verified = true;
				dapBp.line = std::stoi(bkpt.value("line", std::to_string(line)));
				int gdbNum = std::stoi(bkpt.value("number", "0"));
				gdbBpNums.push_back(gdbNum);
			} else {
				dapBp.verified = false;
				dapBp.line = line;
			}
			breakpoints.push_back(dapBp);
		}
	}

	breakpointsByFile[sourcePath] = gdbBpNums;
	sendResponse(reqSeq, "setBreakpoints", {{"breakpoints", breakpoints}});
}

void DapServer::handleSetExceptionBreakpoints(int reqSeq, const Json & /*args*/) {
	sendResponse(reqSeq, "setExceptionBreakpoints", {{"breakpoints", Json::array()}});
}

void DapServer::handleConfigurationDone(int reqSeq, const Json & /*args*/) {
	sendResponse(reqSeq, "configurationDone", Json::object());

	// Set a temporary breakpoint on main if stopOnEntry is requested
	if (stopOnEntry) {
		debugger->sendAndWait("break-insert -t main");
	}

	// Start the program
	debugger->send("exec-run");
}

void DapServer::handleThreads(int reqSeq, const Json & /*args*/) {
	MiRecord result = debugger->sendAndWait("thread-info");

	std::vector<Thread> threads;
	if (result.values.contains("threads")) {
		for (const auto &t : result.values["threads"]) {
			Thread thread;
			thread.id = std::stoi(t.value("id", "1"));
			thread.name = t.value("name", t.value("target-id", "Thread " + t.value("id", "1")));
			threads.push_back(thread);
		}
	}

	if (threads.empty()) {
		threads.push_back({1, "Main Thread"});
	}

	sendResponse(reqSeq, "threads", {{"threads", threads}});
}

void DapServer::handleStackTrace(int reqSeq, const Json & /*args*/) {
	MiRecord result = debugger->sendAndWait("stack-list-frames");

	std::vector<StackFrame> frames;
	if (result.values.contains("stack")) {
		for (const auto &f : result.values["stack"]) {
			// GDB MI wraps each frame in a "frame" key
			const auto &frame = f.contains("frame") ? f["frame"] : f;
			StackFrame sf;
			sf.id = std::stoi(frame.value("level", "0"));
			sf.name = demangleFunctionName(frame.value("func", "??"));
			sf.line = std::stoi(frame.value("line", "0"));
			sf.column = 0;

			if (frame.contains("fullname")) {
				std::string fullname = frame["fullname"].get<std::string>();
				sf.source = {std::filesystem::path(fullname).filename().string(), fullname};
			} else if (frame.contains("file")) {
				std::string file = frame["file"].get<std::string>();
				sf.source = {std::filesystem::path(file).filename().string(), file};
			}

			frames.push_back(sf);
		}
	}

	sendResponse(reqSeq, "stackTrace", {{"stackFrames", frames}, {"totalFrames", frames.size()}});
}

void DapServer::handleScopes(int reqSeq, const Json & /*args*/) {
	std::vector<Scope> scopes = {
		{"Locals", localsRef, false},
		{"Arguments", argsRef, false},
	};
	sendResponse(reqSeq, "scopes", {{"scopes", scopes}});
}

void DapServer::handleVariables(int reqSeq, const Json &args) {
	int ref = args.value("variablesReference", 0);

	MiRecord result = debugger->sendAndWait("stack-list-variables --all-values");

	std::vector<Variable> vars;
	if (result.values.contains("variables")) {
		for (const auto &v : result.values["variables"]) {
			std::string name = v.value("name", "");
			std::string value = v.value("value", "");
			std::string arg = v.value("arg", "0");

			// Filter by scope: ref=1 for locals (arg=0), ref=2 for arguments (arg=1)
			bool isArg = (arg == "1");
			if ((ref == localsRef && isArg) || (ref == argsRef && !isArg)) {
				continue;
			}

			Variable var;
			var.name = name;
			var.value = value;
			vars.push_back(var);
		}
	}

	sendResponse(reqSeq, "variables", {{"variables", vars}});
}

void DapServer::handleContinue(int reqSeq, const Json & /*args*/) {
	debugger->send("exec-continue");
	sendResponse(reqSeq, "continue", {{"allThreadsContinued", true}});
}

void DapServer::handleNext(int reqSeq, const Json & /*args*/) {
	debugger->send("exec-next");
	sendResponse(reqSeq, "next", Json::object());
}

void DapServer::handleStepIn(int reqSeq, const Json & /*args*/) {
	debugger->send("exec-step");
	sendResponse(reqSeq, "stepIn", Json::object());
}

void DapServer::handleStepOut(int reqSeq, const Json & /*args*/) {
	debugger->send("exec-finish");
	sendResponse(reqSeq, "stepOut", Json::object());
}

void DapServer::handlePause(int reqSeq, const Json & /*args*/) {
	debugger->send("exec-interrupt");
	sendResponse(reqSeq, "pause", Json::object());
}

void DapServer::handleDisconnect(int reqSeq, const Json & /*args*/) {
	sendResponse(reqSeq, "disconnect", Json::object());
	if (debugger)
		debugger->terminate();
	running = false;

	// Clean up compiled binary
	if (!compiledBinary.empty()) {
		std::filesystem::remove(compiledBinary);
	}
}

// --- GDB reader thread ---

void DapServer::gdbReaderLoop() {
	while (running && debugger && debugger->isRunning()) {
		MiRecord record;
		if (!debugger->readRecord(record))
			break;
		if (record.type == MiRecord::Prompt)
			continue;
		if (record.type == MiRecord::Result) {
			debugger->deliverResult(record);
		} else {
			handleGdbRecord(record);
		}
	}
}

void DapServer::handleGdbRecord(const MiRecord &record) {
	if (record.type == MiRecord::ExecAsync) {
		if (record.recordClass == "stopped") {
			std::string reason = record.values.value("reason", "");

			if (reason == "exited-normally" || reason == "exited" || reason == "exited-signalled") {
				sendEvent("terminated");
			} else {
				// breakpoint-hit, end-stepping-range, signal-received, etc.
				std::string dapReason = "pause";
				if (reason == "breakpoint-hit")
					dapReason = "breakpoint";
				else if (reason == "end-stepping-range")
					dapReason = "step";
				else if (reason == "function-finished")
					dapReason = "step";
				else if (reason == "signal-received")
					dapReason = "exception";

				int threadId = 1;
				if (record.values.contains("thread-id")) {
					threadId = std::stoi(record.values["thread-id"].get<std::string>());
				}

				sendEvent(
					"stopped",
					{
						{"reason", dapReason},
						{"threadId", threadId},
						{"allThreadsStopped", true},
					}
				);
			}
		} else if (record.recordClass == "running") {
			sendEvent("continued", {{"threadId", 1}, {"allThreadsContinued", true}});
		}
	} else if (record.type == MiRecord::TargetStream) {
		sendEvent("output", {{"category", "stdout"}, {"output", record.streamText}});
	} else if (record.type == MiRecord::ConsoleStream) {
		sendEvent("output", {{"category", "console"}, {"output", record.streamText}});
	}
}

// --- Helpers ---

bool DapServer::compileDlFile(const std::string &dlFile, const std::string &outputPath, std::string &errorOutput) {
	std::string selfPath = findSelfPath();
	if (selfPath.empty()) {
		log("Failed to find self executable path");
		errorOutput = "Failed to find self executable path";
		return false;
	}

	llvm::SmallString<128> stdoutPath;
	llvm::SmallString<128> stderrPath;
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_dap_stdout", "log", stdoutPath)) {
		errorOutput = "Failed to create temp stdout file: " + ec.message();
		return false;
	}
	if (std::error_code ec = llvm::sys::fs::createTemporaryFile("dynlex_dap_stderr", "log", stderrPath)) {
		llvm::sys::fs::remove(stdoutPath);
		errorOutput = "Failed to create temp stderr file: " + ec.message();
		return false;
	}

	const std::string outputArg = "-o" + outputPath;
	std::vector<llvm::StringRef> commandArgs = {selfPath, dlFile, "-g", "-O0", outputArg};
	std::vector<std::optional<llvm::StringRef>> redirects = {
		std::nullopt,
		llvm::StringRef(stdoutPath),
		llvm::StringRef(stderrPath),
	};

	std::string executeError;
	bool executionFailed = false;
	int exitCode = llvm::sys::ExecuteAndWait(
		selfPath, commandArgs, std::nullopt, redirects, 0, 0, &executeError, &executionFailed
	);

	auto readFile = [](const std::string &path) {
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file)
			return std::string{};
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	};

	const std::string capturedOutput = readFile(std::string(stdoutPath)) + readFile(std::string(stderrPath));
	llvm::sys::fs::remove(stdoutPath);
	llvm::sys::fs::remove(stderrPath);

	if (!executeError.empty()) {
		errorOutput = executeError;
		return false;
	}
	if (executionFailed || exitCode != 0) {
		errorOutput = capturedOutput;
		log("Compilation failed with exit code " + std::to_string(exitCode));
		return false;
	}
	return true;
}

std::string DapServer::findSelfPath() const {
	if (!executablePath.empty()) {
		if (std::filesystem::path(executablePath).is_absolute())
			return executablePath;
		auto resolved = llvm::sys::findProgramByName(executablePath);
		if (resolved)
			return *resolved;
		return std::filesystem::absolute(executablePath).string();
	}
	return "";
}

std::string DapServer::demangleFunctionName(const std::string &mangled) {
	if (mangled == "main" || mangled == "??" || mangled.empty()) {
		return mangled;
	}

	// Reverse getPatternFunctionName: strip type suffix (_i32_i32), replace _ with space
	std::string name = mangled;

	// Strip type suffixes: find last segment of _typeN patterns
	// Type suffixes look like _i8, _i16, _i32, _i64, _f32, _f64, _bool, _string, _void, _ptr
	while (true) {
		size_t lastUnderscore = name.rfind('_');
		if (lastUnderscore == std::string::npos || lastUnderscore == 0)
			break;

		std::string suffix = name.substr(lastUnderscore + 1);
		if (suffix == "i8" || suffix == "i16" || suffix == "i32" || suffix == "i64" || suffix == "f32" || suffix == "f64" ||
			suffix == "bool" || suffix == "string" || suffix == "void" || suffix == "ptr") {
			name = name.substr(0, lastUnderscore);
		} else {
			break;
		}
	}

	// Replace remaining underscores with spaces
	for (char &c : name) {
		if (c == '_')
			c = ' ';
	}

	return name;
}

void DapServer::log(const std::string &msg) { std::cerr << "[DAP] " << msg << std::endl; }

} // namespace dap
