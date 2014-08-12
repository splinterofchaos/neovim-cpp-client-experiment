
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

struct ScopedLock
{
  pthread_mutex_t* m;
  ScopedLock(pthread_mutex_t&);
  ~ScopedLock();
};

/// Manages the state of a connection to a running instance of nvim.
///
/// The constructor makes a connection to vim and spawns a thread to listen for
/// responses. It fills `classes` and `functions` by downloading the API data
/// from the running vim instance.
///
/// request(id,args) sends data to vim and returns the message id, which can be
/// sent to grab() to obtain the response. Since a message may be missed or
/// come out of order, it may be desirable to run it in another thread.
///
/// @remark Assumes neovim server is at /tmp/neovim, or checks
///         $NEOVIM_LISTEN_ADDRESS.
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

  std::list<Note>  notifications;

  NeoServer();
  ~NeoServer();

  enum {
    REQUEST  = 0,
    RESPONSE = 1,
    NOTIFY   = 2
  };

  /// Returns a copy of all pending messages.
  std::vector<Reply> pending();

  /// Gets the id of a function for use with request().
  /// @returns non-zero on success
  /// @returns zero when the function is not found
  uint64_t method_id(const std::string&);

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

private:
  /// Ran in a separate thread, reads continuously from the server and updates
  /// the replies list.
  static void *listen(void *);
  pthread_t worker;             ///< runs `listen()`
  std::list<Reply> replies;     ///< Replies waiting to get grab()ed.
  pthread_mutex_t repliesLock;  ///< New reply from vim available.
  pthread_cond_t newReply;      ///< New reply from vim available.


  msgpack::unpacker up;  ///< Storage for msgpack objects. Used by `listen()`.
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
  uint64_t mid = method_id(method);

  if (mid == 0)
    throw std::runtime_error("function '" + method + "' not found");

  return request(mid, t);
}

