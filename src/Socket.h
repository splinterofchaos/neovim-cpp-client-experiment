
#pragma once

#include <string>
#include <msgpack.hpp>

struct UnixSocket
{
  int fd;

  UnixSocket();
  ~UnixSocket();

  /// Connects to an arbitrary sockaddr type.
  template<typename Addr>
  bool connect_addr(const Addr& addr);

  /// Connects to `path` using a unix address.
  bool connect_local(const char *path);

  int send(const char *buf, size_t len);
  int send(const msgpack::sbuffer&);

  std::string recv();
  int recv(msgpack::unpacker&);

#if MSGPACK_VERSION_MINOR >= 6
  // msgpack 0.6 makes unpacker move-enabled.
  msgpack::unpacker recv_msgpack();
#endif

  operator bool();
};

/// Converts errno into a human-readable message.
std::string socket_error_msg();

void die_errno(const char* failedAt);
