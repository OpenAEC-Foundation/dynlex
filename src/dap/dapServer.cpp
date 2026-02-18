#include "dapServer.h"
#include "../lsp/stdioTransport.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace dap {

DapServer::DapServer(std::unique_ptr<lsp::Transport> transport) : transport(std::move(transport)) {}

DapServer::~DapServer() {
	running = false;
	gdb.terminate();
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
	gdb.terminate();
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
	if (!compileDlFile(program, compiledBinary)) {
		sendErrorResponse(reqSeq, "launch", "Compilation failed");
		return;
	}

	// Launch GDB
	if (!gdb.launch()) {
		sendErrorResponse(reqSeq, "launch", "Failed to launch GDB");
		return;
	}

	// Load the binary
	MiRecord result = gdb.sendAndWait("file-exec-and-symbols " + compiledBinary);
	if (result.recordClass != "done") {
		std::string msg = result.values.value("msg", "Failed to load binary");
		sendErrorResponse(reqSeq, "launch", msg);
		return;
	}

	// Start the GDB reader thread
	gdbReaderThread = std::thread(&DapServer::gdbReaderLoop, this);

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
			gdb.sendAndWait("break-delete " + std::to_string(bpNum));
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

			MiRecord result = gdb.sendAndWait("break-insert " + loc);

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

void DapServer::handleConfigurationDone(int reqSeq, const Json & /*args*/) {
	sendResponse(reqSeq, "configurationDone", Json::object());

	// Start the program
	gdb.send("exec-run");
}

void DapServer::handleThreads(int reqSeq, const Json & /*args*/) {
	MiRecord result = gdb.sendAndWait("thread-info", [this](const MiRecord &r) {
		handleGdbRecord(r);
	});

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
	MiRecord result = gdb.sendAndWait("stack-list-frames", [this](const MiRecord &r) {
		handleGdbRecord(r);
	});

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

	MiRecord result = gdb.sendAndWait("stack-list-variables --all-values", [this](const MiRecord &r) {
		handleGdbRecord(r);
	});

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
	gdb.send("exec-continue");
	sendResponse(reqSeq, "continue", {{"allThreadsContinued", true}});
}

void DapServer::handleNext(int reqSeq, const Json & /*args*/) {
	gdb.send("exec-next");
	sendResponse(reqSeq, "next", Json::object());
}

void DapServer::handleStepIn(int reqSeq, const Json & /*args*/) {
	gdb.send("exec-step");
	sendResponse(reqSeq, "stepIn", Json::object());
}

void DapServer::handleStepOut(int reqSeq, const Json & /*args*/) {
	gdb.send("exec-finish");
	sendResponse(reqSeq, "stepOut", Json::object());
}

void DapServer::handlePause(int reqSeq, const Json & /*args*/) {
	gdb.send("exec-interrupt");
	sendResponse(reqSeq, "pause", Json::object());
}

void DapServer::handleDisconnect(int reqSeq, const Json & /*args*/) {
	sendResponse(reqSeq, "disconnect", Json::object());
	gdb.terminate();
	running = false;

	// Clean up compiled binary
	if (!compiledBinary.empty()) {
		std::filesystem::remove(compiledBinary);
	}
}

// --- GDB reader thread ---

void DapServer::gdbReaderLoop() {
	while (running && gdb.isRunning()) {
		MiRecord record;
		if (!gdb.readRecord(record))
			break;
		if (record.type == MiRecord::Prompt)
			continue;
		handleGdbRecord(record);
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

bool DapServer::compileDlFile(const std::string &dlFile, const std::string &outputPath) {
	std::string selfPath = findSelfPath();
	if (selfPath.empty()) {
		log("Failed to find self executable path");
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log("Fork failed for compilation");
		return false;
	}

	if (pid == 0) {
		// Child: run dynlex compiler
		// Redirect stderr to a pipe so parent can capture errors
		execlp(selfPath.c_str(), selfPath.c_str(), dlFile.c_str(), "-g", "-O0", ("-o" + outputPath).c_str(), nullptr);
		_exit(1);
	}

	int status;
	waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return true;
	}

	log("Compilation failed with exit code " + std::to_string(WEXITSTATUS(status)));
	return false;
}

std::string DapServer::findSelfPath() {
	char buf[4096];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len > 0) {
		buf[len] = '\0';
		return std::string(buf);
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
