
#include "Socket.h"

#include <unistd.h>  // for close
#include <sys/un.h>  // unix sockaddr type.

UnixSocket::UnixSocket() 
{
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
}

UnixSocket::~UnixSocket()
{
  close(fd);
}

template<typename Addr>
bool UnixSocket::connect_addr(const Addr& addr)
{
  return connect(fd, (sockaddr*)&addr, sizeof(addr)) >= 0;
}

template<typename POD>
void zero(POD& pod)
{
  bzero((char *) &pod, sizeof(pod));
}


bool UnixSocket::connect_local(const char *path)
{
  sockaddr_un addr;
  zero(addr);
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);
  return connect_addr(addr);
}

int UnixSocket::send(const char *buf, size_t len)
{
  return ::send(fd, (void*)buf, len, 0);
}

int UnixSocket::send(const msgpack::sbuffer& b)
{
  return send(b.data(), b.size());
}

// nvim can give extreme amounts of data in one burst. Be prepared.
constexpr size_t MAX_SIZE = 2*1024*1024;

// TODO: This is really inefficient when writing to a msgpack::unpacker.
std::string UnixSocket::recv()
{
  char buf[MAX_SIZE];
  size_t len = ::recv(fd, buf, MAX_SIZE, 0);
  return std::string(buf, buf+len);
}

msgpack::unpacker UnixSocket::recv_msgpack()
{
  char buf[MAX_SIZE];
  size_t len = ::recv(fd, buf, MAX_SIZE, 0);
  msgpack::unpacker up(len);
  std::copy( buf, buf+len, up.buffer() );
  up.buffer_consumed(len);
  return std::move(up);
}

