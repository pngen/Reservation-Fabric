#pragma once
// Reference deployment networking helpers (loopback framed TCP). Windows only.
#include <cstdint>
#include <string>
#include <vector>
#include <reservation_fabric/protocol.hpp>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
#endif

namespace reservation_fabric {
namespace protocol {

bool netInit();
void netCleanup();
SOCKET connectTo(const std::string& host, int port);
bool listenOn(int port, SOCKET& listener);
SOCKET acceptConn(SOCKET listener, bool& ok);
bool recvAll(SOCKET s, std::uint8_t* buf, std::size_t len);
bool sendAll(SOCKET s, const std::uint8_t* buf, std::size_t len);

// Returns 0 on a full frame, 1 on clean close/error, -1 on a malformed frame.
int recvFrame(SOCKET s, Frame& frame);
bool sendFrame(SOCKET s, MessageType type, const std::vector<std::uint8_t>& payload);

}  // namespace protocol
}  // namespace reservation_fabric