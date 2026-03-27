#include "stdioTransport.h"
#include <unistd.h>

namespace lsp {

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
