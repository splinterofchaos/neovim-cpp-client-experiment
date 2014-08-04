
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
  msgpack::object request(uint64_t method, const T& t = T{});

  template<typename T = std::vector<int>>
  msgpack::object request(const std::string& method, const T& t = T{});

  msgpack::unpacker up;
};

template<typename T>
msgpack::object NeoServer::request(uint64_t method, const T& t)
{
  msgpack::sbuffer sbuf;

  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_array(4) << (uint64_t)REQUEST  // type
                   << id++               // msg id
                   << method             // method
                   << t;                 // [args]

  sock.send(sbuf);

  sock.recv(up);

  msgpack::unpacked res;
  up.next(&res);

  uint64_t msgType, resId; msgpack::object error, ret;

  // This works on with the `poc/0.6` branch of msgpack-c:
  /*
   * using Reply = std::tuple<uint64_t,uint64_t,msgpack::object,msgpack::object>;
   * std::tie(msgType,resId,error,ret) = res.get().convert();
   */

  std::vector<msgpack::object> reply = res.get().convert();
  msgType = reply[0].convert();
  resId   = reply[1].convert();
  error   = reply[2];
  ret     = reply[3];

  if (msgType != RESPONSE) {
    std::cerr << "Message type must be 1 (response)." << std::endl;
    exit(1);
  }

  if (resId != id-1) {
    std::cerr << "Wrong msg id: Expected " << id-1  ;
    std::cerr << ", got " << resId  << '.' << std::endl;
    exit(1);
  }

  if (!error.is_nil()) {
    std::cerr << "Msgpack error: " << error << std::endl;
    //exit(1);
    // Probably better to just return this error since the user already expects
    // a msgpack::object.
    return error;
  }

  return ret;
}

template<typename T>
msgpack::object NeoServer::request(const std::string& method, const T& t)
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

