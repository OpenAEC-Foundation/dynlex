#include "stdioTransport.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
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

TransferSize StdioTransport::read(char *buffer, std::size_t count) {
	if (closed)
		return -1;
#ifdef _WIN32
	const unsigned int transferCount =
		static_cast<unsigned int>(std::min(count, static_cast<std::size_t>(std::numeric_limits<int>::max())));
	return _read(_fileno(stdin), buffer, transferCount);
#else
	return ::read(STDIN_FILENO, buffer, count);
#endif
}

TransferSize StdioTransport::write(const char *buffer, std::size_t count) {
	if (closed)
		return -1;
#ifdef _WIN32
	const unsigned int transferCount =
		static_cast<unsigned int>(std::min(count, static_cast<std::size_t>(std::numeric_limits<int>::max())));
	return _write(_fileno(stdout), buffer, transferCount);
#else
	return ::write(STDOUT_FILENO, buffer, count);
#endif
}

bool StdioTransport::isConnected() const { return !closed; }

void StdioTransport::close() { closed = true; }

} // namespace lsp
