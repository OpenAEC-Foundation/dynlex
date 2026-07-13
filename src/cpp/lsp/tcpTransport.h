#pragma once
#include "transport.h"
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace lsp {

#ifdef _WIN32
using SocketHandle = SOCKET;
inline constexpr SocketHandle invalidSocketHandle = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle invalidSocketHandle = -1;
#endif

// TCP transport - wraps an existing socket
class TcpTransport : public Transport {
  public:
	explicit TcpTransport(SocketHandle socketHandle);
	~TcpTransport() override;
	TcpTransport(const TcpTransport &) = delete;
	TcpTransport &operator=(const TcpTransport &) = delete;

	ssize_t read(char *buffer, size_t count) override;
	ssize_t write(const char *buffer, size_t count) override;
	bool isConnected() const override;
	void close() override;

  private:
	SocketHandle socketHandle;
};

// TCP server that accepts connections and creates TcpTransport instances
class TcpServer {
  public:
	explicit TcpServer(int port);
	~TcpServer();
	TcpServer(const TcpServer &) = delete;
	TcpServer &operator=(const TcpServer &) = delete;

	// Setup the server socket. Returns false on failure.
	bool setup();

	// Block until a client connects. Returns nullptr on failure.
	std::unique_ptr<TcpTransport> acceptConnection();

	// Shutdown the server
	void shutdown();

  private:
	int port;
	SocketHandle serverSocket = invalidSocketHandle;
#ifdef _WIN32
	bool winsockInitialized = false;
#endif
};

} // namespace lsp
