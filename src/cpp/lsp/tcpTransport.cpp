#include "tcpTransport.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <system_error>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lsp {

namespace {

std::string socketErrorMessage() {
#ifdef _WIN32
	return std::system_category().message(WSAGetLastError());
#else
	return std::strerror(errno);
#endif
}

} // namespace

// TcpTransport implementation

TcpTransport::TcpTransport(SocketHandle socketHandle) : socketHandle(socketHandle) {}

TcpTransport::~TcpTransport() { close(); }

ssize_t TcpTransport::read(char *buffer, size_t count) {
	if (socketHandle == invalidSocketHandle)
		return -1;
#ifdef _WIN32
	const int socketCount = static_cast<int>(std::min(count, static_cast<size_t>(std::numeric_limits<int>::max())));
	return recv(socketHandle, buffer, socketCount, 0);
#else
	return recv(socketHandle, buffer, count, 0);
#endif
}

ssize_t TcpTransport::write(const char *buffer, size_t count) {
	if (socketHandle == invalidSocketHandle)
		return -1;
#ifdef _WIN32
	const int socketCount = static_cast<int>(std::min(count, static_cast<size_t>(std::numeric_limits<int>::max())));
	return send(socketHandle, buffer, socketCount, 0);
#else
	return send(socketHandle, buffer, count, 0);
#endif
}

bool TcpTransport::isConnected() const { return socketHandle != invalidSocketHandle; }

void TcpTransport::close() {
	if (socketHandle != invalidSocketHandle) {
#ifdef _WIN32
		closesocket(socketHandle);
#else
		::close(socketHandle);
#endif
		socketHandle = invalidSocketHandle;
	}
}

// TcpServer implementation

TcpServer::TcpServer(int port) : port(port) {}

TcpServer::~TcpServer() { shutdown(); }

bool TcpServer::setup() {
#ifdef _WIN32
	WSADATA wsaData;
	const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (startupResult != 0) {
		std::cerr << "[LSP ERROR] Failed to initialize Winsock: " << std::system_category().message(startupResult) << std::endl;
		return false;
	}
	winsockInitialized = true;
#endif

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == invalidSocketHandle) {
		std::cerr << "[LSP ERROR] Failed to create socket: " << socketErrorMessage() << std::endl;
		return false;
	}

#ifdef _WIN32
	const BOOL exclusiveAddressUse = TRUE;
	if (setsockopt(
			serverSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusiveAddressUse),
			sizeof(exclusiveAddressUse)
		) == SOCKET_ERROR) {
#else
	const int reuseAddress = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0) {
#endif
		std::cerr << "[LSP ERROR] Failed to set socket options: " << socketErrorMessage() << std::endl;
		return false;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);

	if (bind(serverSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
		std::cerr << "[LSP ERROR] Failed to bind socket: " << socketErrorMessage() << std::endl;
		return false;
	}

	if (listen(serverSocket, 1) < 0) {
		std::cerr << "[LSP ERROR] Failed to listen on socket: " << socketErrorMessage() << std::endl;
		return false;
	}

	return true;
}

std::unique_ptr<TcpTransport> TcpServer::acceptConnection() {
	struct sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);

	SocketHandle clientSocket = accept(serverSocket, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
	if (clientSocket == invalidSocketHandle) {
		return nullptr;
	}

	return std::make_unique<TcpTransport>(clientSocket);
}

void TcpServer::shutdown() {
	if (serverSocket != invalidSocketHandle) {
#ifdef _WIN32
		closesocket(serverSocket);
#else
		::close(serverSocket);
#endif
		serverSocket = invalidSocketHandle;
	}
#ifdef _WIN32
	if (winsockInitialized) {
		WSACleanup();
		winsockInitialized = false;
	}
#endif
}

} // namespace lsp
