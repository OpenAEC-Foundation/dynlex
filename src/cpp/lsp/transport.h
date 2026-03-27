#pragma once
#include <sys/types.h>

namespace lsp {

// Abstract transport interface for LSP communication
class Transport {
  public:
	virtual ~Transport() = default;

	// Read exactly `count` bytes into buffer. Returns bytes read, or <= 0 on error/EOF.
	virtual ssize_t read(char *buffer, size_t count) = 0;

	// Write exactly `count` bytes from buffer. Returns bytes written, or <= 0 on error.
	virtual ssize_t write(const char *buffer, size_t count) = 0;

	// Check if the transport is still connected/valid
	virtual bool isConnected() const = 0;

	// Close the transport
	virtual void close() = 0;
};

} // namespace lsp
