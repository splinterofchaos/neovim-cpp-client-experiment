
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

int UnixSocket::recv(msgpack::unpacker& up)
{
  up.reserve_buffer(MAX_SIZE);
  size_t len = ::recv(fd, up.buffer(), MAX_SIZE, 0);
  up.buffer_consumed(len);
  return len;
}

UnixSocket::operator bool()
{
  return fd >= 0;
}

std::string socket_error_msg()
{

  std::string err;
  switch(errno) {
   case ENOENT:
    err = "entry does not exist";
   break;

   case EAFNOSUPPORT: 
    err = "wrong family type";
   break;

   case EMFILE: 
    err = "No more available file descriptors for process.";
   break;

   case ENFILE: 
    err = "No more available file descriptors for system.";
   break;

   case EPROTONOSUPPORT: 
    err = "Protocol not supported by either "
          "address family or implementation.";
   break;

   case EACCES:
    err = "Process lacks permissions.";
   break;

   case ENOBUFS:
    err = "Insufficient resources.";
   break;

   case ENOMEM:
    err = "Insufficient memory.";
   break;

   case ENOSR:
    err = "Insufficient stream resources.";
   break;

   case EPERM:
    err = "Socket created without socket broadcast flag.";
   break;

   case EADDRINUSE:
    err = "Address in use.";
   break;

  case EADDRNOTAVAIL:
    err = "file descriptor not in use";
  break;

  case EAGAIN:
    err = "try again";
  break;

  case EALREADY:
    err = "not ready";
  break;

  case EBADF:
    err = "bad file descriptor";
  break;

  case ECONNREFUSED:
    err = "connection refused";
  break;

  case EFAULT:
    err = "address too large";
  break;

  case EINPROGRESS:
    err = "connection not ready";
  break;

  case EISCONN:
    err = "already connected";
  break;

  case ENETUNREACH:
    err = "network unreachable";
  break;

  case ENOTSOCK:
    err = "not a socket";
  break;

  case EPROTOTYPE:
    err = "unsupported protocol type";
  break;

  case ETIMEDOUT:
    err = "timeout";
  break;

  case ENOTCONN:
    err = "not connected";
  break;

  case 134: 
    err = "unknown error"; // TODO: What's this!?
  break;

  case EINTR:
    err = "interrupted";
  break;
  }

  if (err != "")
    err = "ERROR: " + err + ".";

  return err;
}

#include <iostream>
void die_errno(const char* failedAt)
{
  // Make sure this isn't a false alarm.
  if (errno == 0)
    return;

  std::cerr << "Error while " << failedAt << '\n';
  std::cerr << socket_error_msg() << std::endl;

  // Just in case socket_error_msg() returns NULL, exit with errno so the user
  // can look up the code.
  exit(errno);
}

