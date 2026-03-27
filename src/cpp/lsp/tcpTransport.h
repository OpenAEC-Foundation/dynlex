#pragma once
#include "transport.h"
#include <memory>

namespace lsp {

// TCP transport - wraps an existing socket
class TcpTransport : public Transport {
  public:
	explicit TcpTransport(int socket);
	~TcpTransport() override;

	ssize_t read(char *buffer, size_t count) override;
	ssize_t write(const char *buffer, size_t count) override;
	bool isConnected() const override;
	void close() override;

  private:
	int socket;
};

// TCP server that accepts connections and creates TcpTransport instances
class TcpServer {
  public:
	explicit TcpServer(int port);
	~TcpServer();

	// Setup the server socket. Returns false on failure.
	bool setup();

	// Block until a client connects. Returns nullptr on failure.
	std::unique_ptr<TcpTransport> acceptConnection();

	// Shutdown the server
	void shutdown();

  private:
	int port;
	int serverSocket = -1;
};

} // namespace lsp
