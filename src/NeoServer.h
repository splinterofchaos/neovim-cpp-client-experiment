
#include <iostream>
#include <string>
#include <vector>

#include <msgpack.hpp>

#include "Socket.h"

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
  uint32_t id; ///< The id of the next message.

  UnixSocket sock;
  std::string address;

  std::vector<std::string> classes;
  std::vector<NeoFunc>     functions;

  NeoServer();

  enum {
    REQUEST  = 0,
    RESPONSE = 1,
    NOTIFY   = 2
  };

  // TODO: std::future?
  template<typename T = std::vector<int>>
  void request(uint64_t method, const T& t = T{});

  template<typename T = std::vector<int>>
  void request(const std::string& method, const T& t = T{});

  msgpack::object receive();

  msgpack::unpacker up;
};

template<typename T>
void NeoServer::request(uint64_t method, const T& t)
{
  msgpack::sbuffer sbuf;

  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_array(4) << (uint64_t)REQUEST  // type
                   << id++               // msg id
                   << method             // method
                   << t;                 // [args]

  sock.send(sbuf);
}

template<typename T>
void NeoServer::request(const std::string& method, const T& t)
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
  request(mid, t);
}

