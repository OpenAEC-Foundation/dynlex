#pragma once
#include <cstddef>

namespace lsp {

using TransferSize = std::ptrdiff_t;

// Abstract transport interface for LSP communication
class Transport {
  public:
	virtual ~Transport() = default;

	// Read exactly `count` bytes into buffer. Returns bytes read, or <= 0 on error/EOF.
	virtual TransferSize read(char *buffer, std::size_t count) = 0;

	// Write exactly `count` bytes from buffer. Returns bytes written, or <= 0 on error.
	virtual TransferSize write(const char *buffer, std::size_t count) = 0;

	// Check if the transport is still connected/valid
	virtual bool isConnected() const = 0;

	// Close the transport
	virtual void close() = 0;
};

} // namespace lsp
