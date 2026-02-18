#include "gdbmi.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace dap {

GdbMI::~GdbMI() { terminate(); }

bool GdbMI::launch(const std::string &gdbPath) {
	int stdinPipe[2];
	int stdoutPipe[2];

	if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
		std::cerr << "[DAP] Failed to create pipes: " << strerror(errno) << std::endl;
		return false;
	}

	pid = fork();
	if (pid < 0) {
		std::cerr << "[DAP] Fork failed: " << strerror(errno) << std::endl;
		return false;
	}

	if (pid == 0) {
		// Child process — become GDB
		close(stdinPipe[1]);
		close(stdoutPipe[0]);
		dup2(stdinPipe[0], STDIN_FILENO);
		dup2(stdoutPipe[1], STDOUT_FILENO);
		dup2(stdoutPipe[1], STDERR_FILENO);
		close(stdinPipe[0]);
		close(stdoutPipe[1]);

		execlp(gdbPath.c_str(), gdbPath.c_str(), "--interpreter=mi", "--quiet", nullptr);
		// If exec fails
		std::cerr << "Failed to exec GDB: " << strerror(errno) << std::endl;
		_exit(1);
	}

	// Parent
	close(stdinPipe[0]);
	close(stdoutPipe[1]);
	toGdb = stdinPipe[1];
	fromGdb = stdoutPipe[0];

	// Read initial GDB output until we get the first prompt
	MiRecord record;
	while (readRecord(record)) {
		if (record.type == MiRecord::Prompt) {
			break;
		}
	}

	return true;
}

int GdbMI::send(const std::string &command) {
	int token = nextToken++;
	std::string line = std::to_string(token) + "-" + command + "\n";
	ssize_t written = ::write(toGdb, line.c_str(), line.size());
	if (written < 0) {
		std::cerr << "[DAP] Failed to write to GDB: " << strerror(errno) << std::endl;
	}
	return token;
}

std::string GdbMI::readLine() {
	std::string line;
	char c;
	while (true) {
		ssize_t n = ::read(fromGdb, &c, 1);
		if (n <= 0) {
			return "";
		}
		if (c == '\n') {
			return line;
		}
		line += c;
	}
}

bool GdbMI::readRecord(MiRecord &record) {
	std::string line = readLine();
	if (line.empty() && errno != 0) {
		return false;
	}
	// Skip empty lines
	while (line.empty()) {
		line = readLine();
		if (line.empty()) {
			return false;
		}
	}
	record = parseLine(line);
	return true;
}

MiRecord GdbMI::parseLine(const std::string &line) {
	MiRecord record;

	if (line == "(gdb)" || line == "(gdb) ") {
		record.type = MiRecord::Prompt;
		return record;
	}

	size_t pos = 0;

	// Stream records
	if (!line.empty()) {
		char first = line[0];
		if (first == '~' || first == '@' || first == '&') {
			if (first == '~')
				record.type = MiRecord::ConsoleStream;
			else if (first == '@')
				record.type = MiRecord::TargetStream;
			else
				record.type = MiRecord::LogStream;

			// Parse the C string
			if (line.size() > 1 && line[1] == '"') {
				pos = 1;
				record.streamText = parseMiString(line, pos);
			} else {
				record.streamText = line.substr(1);
			}
			return record;
		}
	}

	// Parse optional token
	while (pos < line.size() && isdigit(line[pos])) {
		if (record.token < 0)
			record.token = 0;
		record.token = record.token * 10 + (line[pos] - '0');
		pos++;
	}

	if (pos >= line.size()) {
		return record;
	}

	// Record type indicator
	char indicator = line[pos++];
	switch (indicator) {
	case '^':
		record.type = MiRecord::Result;
		break;
	case '*':
		record.type = MiRecord::ExecAsync;
		break;
	case '+':
		record.type = MiRecord::StatusAsync;
		break;
	case '=':
		record.type = MiRecord::NotifyAsync;
		break;
	default:
		return record;
	}

	// Record class (e.g., "done", "stopped", "error")
	size_t classStart = pos;
	while (pos < line.size() && line[pos] != ',') {
		pos++;
	}
	record.recordClass = line.substr(classStart, pos - classStart);

	// Parse results (key=value pairs)
	record.values = Json::object();
	while (pos < line.size() && line[pos] == ',') {
		pos++; // skip comma
		record.values.merge_patch(parseMiResult(line, pos));
	}

	return record;
}

std::string GdbMI::parseMiString(const std::string &s, size_t &pos) {
	assert(pos < s.size() && s[pos] == '"');
	pos++; // skip opening quote
	std::string result;
	while (pos < s.size() && s[pos] != '"') {
		if (s[pos] == '\\' && pos + 1 < s.size()) {
			pos++;
			switch (s[pos]) {
			case 'n':
				result += '\n';
				break;
			case 't':
				result += '\t';
				break;
			case '\\':
				result += '\\';
				break;
			case '"':
				result += '"';
				break;
			default:
				result += s[pos];
				break;
			}
		} else {
			result += s[pos];
		}
		pos++;
	}
	if (pos < s.size())
		pos++; // skip closing quote
	return result;
}

Json GdbMI::parseMiValue(const std::string &s, size_t &pos) {
	if (pos >= s.size())
		return Json();
	if (s[pos] == '"') {
		return parseMiString(s, pos);
	} else if (s[pos] == '{') {
		return parseMiTuple(s, pos);
	} else if (s[pos] == '[') {
		return parseMiList(s, pos);
	}
	return Json();
}

Json GdbMI::parseMiTuple(const std::string &s, size_t &pos) {
	assert(pos < s.size() && s[pos] == '{');
	pos++; // skip {
	Json obj = Json::object();
	while (pos < s.size() && s[pos] != '}') {
		if (s[pos] == ',')
			pos++;
		obj.merge_patch(parseMiResult(s, pos));
	}
	if (pos < s.size())
		pos++; // skip }
	return obj;
}

Json GdbMI::parseMiList(const std::string &s, size_t &pos) {
	assert(pos < s.size() && s[pos] == '[');
	pos++; // skip [
	Json arr = Json::array();

	while (pos < s.size() && s[pos] != ']') {
		if (s[pos] == ',')
			pos++;

		// Lists can contain values or results (key=value)
		// Check if it's a result by looking for name=
		size_t lookAhead = pos;
		while (lookAhead < s.size() && isalnum(s[lookAhead]))
			lookAhead++;
		if (lookAhead < s.size() && s[lookAhead] == '=') {
			// It's a result — parse as tuple-like
			Json obj = Json::object();
			obj.merge_patch(parseMiResult(s, pos));
			while (pos < s.size() && s[pos] == ',') {
				size_t la2 = pos + 1;
				while (la2 < s.size() && isalnum(s[la2]))
					la2++;
				if (la2 < s.size() && s[la2] == '=') {
					pos++; // skip comma
					obj.merge_patch(parseMiResult(s, pos));
				} else {
					break;
				}
			}
			arr.push_back(obj);
		} else {
			arr.push_back(parseMiValue(s, pos));
		}
	}
	if (pos < s.size())
		pos++; // skip ]
	return arr;
}

Json GdbMI::parseMiResult(const std::string &s, size_t &pos) {
	// Parse: variable=value
	size_t nameStart = pos;
	while (pos < s.size() && s[pos] != '=' && s[pos] != ',' && s[pos] != '}' && s[pos] != ']') {
		pos++;
	}
	std::string name = s.substr(nameStart, pos - nameStart);

	Json obj = Json::object();
	if (pos < s.size() && s[pos] == '=') {
		pos++; // skip =
		obj[name] = parseMiValue(s, pos);
	}
	return obj;
}

MiRecord GdbMI::sendAndWait(const std::string &command) {
	int token = send(command);

	std::unique_lock<std::mutex> lock(resultMutex);
	resultCV.wait(lock, [&] {
		return pendingResults.count(token) > 0 || shuttingDown.load();
	});

	if (shuttingDown.load()) {
		MiRecord error;
		error.type = MiRecord::Result;
		error.recordClass = "error";
		error.values = {{"msg", "GDB connection lost"}};
		return error;
	}

	MiRecord result = std::move(pendingResults[token]);
	pendingResults.erase(token);
	return result;
}

void GdbMI::deliverResult(const MiRecord &record) {
	std::lock_guard<std::mutex> lock(resultMutex);
	pendingResults[record.token] = record;
	resultCV.notify_all();
}

void GdbMI::terminate() {
	// Wake up any sendAndWait calls
	shuttingDown.store(true);
	resultCV.notify_all();

	if (pid <= 0)
		return;

	// Try graceful exit
	if (toGdb >= 0) {
		const char *exitCmd = "-gdb-exit\n";
		::write(toGdb, exitCmd, strlen(exitCmd));
	}

	// Wait briefly for GDB to exit
	int status;
	for (int i = 0; i < 10; i++) {
		pid_t result = waitpid(pid, &status, WNOHANG);
		if (result == pid) {
			pid = -1;
			break;
		}
		usleep(100000); // 100ms
	}

	// Force kill if still running
	if (pid > 0) {
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
		pid = -1;
	}

	if (toGdb >= 0) {
		close(toGdb);
		toGdb = -1;
	}
	if (fromGdb >= 0) {
		close(fromGdb);
		fromGdb = -1;
	}
}

} // namespace dap
