#pragma once
#include "transport.h"

namespace lsp {

// Stdio transport - reads from stdin, writes to stdout
class StdioTransport : public Transport {
  public:
	StdioTransport();
	~StdioTransport() override = default;

	TransferSize read(char *buffer, std::size_t count) override;
	TransferSize write(const char *buffer, std::size_t count) override;
	bool isConnected() const override;
	void close() override;

  private:
	bool closed = false;
};

} // namespace lsp
