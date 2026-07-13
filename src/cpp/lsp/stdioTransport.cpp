#include "stdioTransport.h"
#include <cerrno>
#include <cstdio>
#include <string>
#include <system_error>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace lsp {

namespace {

#ifdef _WIN32
void setBinaryMode(FILE *stream, const char *name) {
	int descriptor = _fileno(stream);
	if (descriptor == -1)
		throw std::system_error(errno, std::generic_category(), std::string("Failed to access ") + name);
	if (_setmode(descriptor, _O_BINARY) == -1)
		throw std::system_error(errno, std::generic_category(), std::string("Failed to set ") + name + " to binary mode");
}
#endif

} // namespace

StdioTransport::StdioTransport() {
#ifdef _WIN32
	setBinaryMode(stdin, "stdin");
	setBinaryMode(stdout, "stdout");
#endif
}

ssize_t StdioTransport::read(char *buffer, size_t count) {
	if (closed)
		return -1;
	return ::read(STDIN_FILENO, buffer, count);
}

ssize_t StdioTransport::write(const char *buffer, size_t count) {
	if (closed)
		return -1;
	return ::write(STDOUT_FILENO, buffer, count);
}

bool StdioTransport::isConnected() const { return !closed; }

void StdioTransport::close() { closed = true; }

} // namespace lsp
