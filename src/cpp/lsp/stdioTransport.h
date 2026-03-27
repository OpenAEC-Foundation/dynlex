#pragma once
#include "transport.h"

namespace lsp {

// Stdio transport - reads from stdin, writes to stdout
class StdioTransport : public Transport {
  public:
	StdioTransport() = default;
	~StdioTransport() override = default;

	ssize_t read(char *buffer, size_t count) override;
	ssize_t write(const char *buffer, size_t count) override;
	bool isConnected() const override;
	void close() override;

  private:
	bool closed = false;
};

} // namespace lsp
