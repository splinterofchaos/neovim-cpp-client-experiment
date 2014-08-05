
#include <iostream>
#include <string>
#include <vector>

#include <msgpack.hpp>

#include "Socket.h"

struct NeoFunc;
struct NeoServer;
struct Buffer;

struct NeoFunc
{
  struct Param
  {
    std::string type, name;

    MSGPACK_DEFINE(type, name)
  };

  std::string name;
  std::vector<Param> args;
  std::string resultType;
  int canFail;

  unsigned int id;

  // TODO: This doesn't work.
  MSGPACK_DEFINE(args, canFail, resultType, name, id)
};

std::ostream& operator<< (std::ostream&, const NeoFunc::Param&);
std::ostream& operator<< (std::ostream& os, const NeoFunc& nf);

/// Manages the state of a connection to a running instance of nvim.
struct NeoServer
{
  /// The data of a NOTIFY message.
  using Note = std::pair<std::string, msgpack::object>;

  /// The type returned by request().
  using Reply = std::pair<uint64_t, msgpack::object>;

  uint32_t id; ///< The id of the next message.

  UnixSocket sock;
  std::string address;

  std::vector<std::string> classes;
  std::vector<NeoFunc>     functions;

  std::list<Reply> replies;
  std::list<Note>  notifications;

  NeoServer();
  ~NeoServer();

  enum {
    REQUEST  = 0,
    RESPONSE = 1,
    NOTIFY   = 2
  };

  // TODO: std::future?
  /// Requests the value of method(t) from the server by id.
  /// @return The id to expect a response with.
  template<typename T = std::vector<int>>
  uint64_t request(uint64_t, const T& t = T{});

  /// Requests the value of method(t) from the server by name.
  /// @return The id to expect a response with.
  template<typename T = std::vector<int>>
  uint64_t request(const std::string&, const T& t = T{});

  /// Pull a specific reply from `replies`.
  msgpack::object grab(uint64_t);

  msgpack::unpacker up;

private:
  /// Ran in a separate thread, reads continuously from the server and updates
  /// the replies list.
  static void *listen(void *);
  pthread_t worker;  ///< used by `listen`
};

/// Global instance of the neovim server.
extern NeoServer *server;

/// A virtual neovim buffer.
struct Buffer
{
  uint64_t id;

  /// Obtains the current buffer.
  Buffer();
};

template<typename T>
uint64_t NeoServer::request(uint64_t method, const T& t)
{
  msgpack::sbuffer sbuf;

  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_array(4) << (uint64_t)REQUEST  // type
                   << id                 // msg id
                   << method             // method
                   << t;                 // [args]

  sock.send(sbuf);

  return id++;
}

template<typename T>
uint64_t NeoServer::request(const std::string& method, const T& t)
{
  uint64_t mid = 0;

  for (const NeoFunc& nf : functions) {
    if (nf.name == method) {
      mid = nf.id;
      break;
    }
  }

  if (mid == 0)
    throw std::runtime_error("function '" + method + "' not found");

  return request(mid, t);
}

