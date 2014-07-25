
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
  msgpack::unpacker recv_msgpack();
};

/// Converts errno into a human-readable message.
std::string socket_error_msg();

