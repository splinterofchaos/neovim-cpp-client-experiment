
#include "Socket.h"

#include <string>

#include <iostream>
#include <map>
#include <utility>

#include <msgpack.hpp>
//#include <uv.h> // TODO: Use libuv.

//#include "auto/neovim.h"

int msgpack_write_cb(void* data, const char* buf, unsigned int len);
void die_errno(const char* failedAt);
std::string socket_error_msg();

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

bool ok(const UnixSocket& s)
{
  return s.fd >= 0;
}

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

std::ostream& operator<< (std::ostream& os, const NeoFunc::Param& p)
{
  return os << p.type << ' ' << p.name;
}

std::ostream& operator<< (std::ostream& os, const NeoFunc& nf)
{
  os << nf.id << ":\t" << nf.name;

  if (nf.canFail)
    os << '!';

  os << '(';

  auto it = nf.args.begin();
  while (it != nf.args.end()) {
    os << *it;

    if (++it != nf.args.end())
      os << ", ";
  }

  os << ")\t=> " << nf.resultType;
  return os;
}

/// Manages the state of a connection to a running instance of nvim.
struct NeoServer 
{
  uint32_t id; ///< The id of the next message.

  UnixSocket sock;

  std::vector<std::string> classes;
  std::vector<NeoFunc>     functions;

  NeoServer();

  static const uint64_t REQUEST, RESPONSE, NOTIFY;

  template<typename T>
  void request(uint64_t method, const T& t);

  msgpack::object get_response();

  msgpack::unpacker up;
};

const uint64_t NeoServer::REQUEST  = 0,
               NeoServer::RESPONSE = 1,
               NeoServer::NOTIFY   = 2;

NeoServer::NeoServer()
{
  id = 0;

  if (!ok(sock))
    die_errno("Failed opening socket:\n");

  if (!sock.connect_local("/tmp/neovim"))
    die_errno("Failed connecting to server:\n");
}

template<typename T>
void NeoServer::request(uint64_t method, const T& t)
{
  msgpack::sbuffer sbuf;

  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_array(4) << REQUEST  // type
                   << id++     // msg id
                   << method   // method
                   << t;       // [args]

  sock.send(sbuf);
}

msgpack::object NeoServer::get_response()
{
  up = sock.recv_msgpack();

  msgpack::unpacked res;
  up.next(&res);

  msgpack::object replyObj = res.get();

  if (replyObj.type != msgpack::type::ARRAY) {
    std::cerr << "Response must be an array." << std::endl;
    exit(1);
  }

  msgpack::object_array replyArray = replyObj.via.array;

  if (replyArray.size != 4) {
    std::cerr << "Response array must have size of 4." << std::endl;
    exit(1);
  }

  uint64_t msgType = replyArray.ptr[0].via.u64;
  if (msgType != RESPONSE) {
    std::cerr << "Message type must be 1 (response)." << std::endl;
    exit(1);
  }

  uint64_t resId = replyArray.ptr[1].via.u64;
  if (resId != id-1) {
    std::cerr << "Wrong msg id: Expected " << id-1  ;
    std::cerr << ", got " << resId  << '.' << std::endl;
    exit(1);
  }
  
  msgpack::object error = replyArray.ptr[2];
  if (!error.is_nil()) {
    std::cerr << "Msgpack error: " << error << std::endl;
    //exit(1);
    // Probably better to just return this error since the user already expects
    // a msgpack::object.
    return error;
  }

  return replyArray.ptr[3];
}

namespace std {
  string to_string(msgpack::type::object_type type)
  {
    switch(type) {
     case msgpack::type::NIL               :  return "nil";
     case msgpack::type::BOOLEAN           :  return "bool";
     case msgpack::type::POSITIVE_INTEGER  :  return "+int";
     case msgpack::type::NEGATIVE_INTEGER  :  return "-int";
     case msgpack::type::DOUBLE            :  return "double";
     case msgpack::type::STR               :  return "string";
     case msgpack::type::BIN               :  return "binary";
     case msgpack::type::ARRAY             :  return "array";
     case msgpack::type::MAP               :  return "map";
     default: return "unknown type";
    }
  }
}

int main()
{
  NeoServer serv;

  std::cout << "Requesting API data..." << std::endl;

  serv.request(0, std::vector<int>{});

  msgpack::object resultObj = serv.get_response();

  std::cout << "Got data." << std::endl;

  if (resultObj.type != msgpack::type::ARRAY) {
    std::cerr << "result should be array" << std::endl;
    exit(1);
  }

  msgpack::object finalObj = resultObj.via.array.ptr[1];
  if (finalObj.type != msgpack::type::STR) {
    std::cerr << "Unexpected object type (final)." << std::endl;
    exit(1);
  }

  msgpack::object_str str = finalObj.via.str;

  std::cout << "Data:\n" << finalObj << std::endl;

  msgpack::unpacker upData(str.size + 1);
  memcpy(upData.buffer(), str.ptr, str.size);
  upData.buffer_consumed(str.size);

  msgpack::unpacked res;
  upData.next(&res);

  msgpack::object dataObj = res.get();
  if (dataObj.type != msgpack::type::MAP) {
    std::cerr << "Unexpected object type (data)." << std::endl;
    std::cerr << "Actually: " << std::to_string(dataObj.type) << std::endl;
    exit(1);
  }

  msgpack::object_map servicesMap = dataObj.via.map;

  for (size_t i=0; i < servicesMap.size; i++) {
    auto& kv = servicesMap.ptr[i];
    std::string key = kv.key.convert();

    if (key == "classes") {
      std::cout << "classes: ";

      kv.val.convert(&serv.classes);

      for (std::string& cl : serv.classes)
        std::cout << cl << ' ';
      std::cout << std::endl;

    } else if (key == "functions") {
      std::cout << "functions:\n";

      if (kv.val.type != msgpack::type::ARRAY)
        exit(5);

      msgpack::object_array fnArray = kv.val.via.array;

      if (fnArray.size == 0) {
        std::cerr << "No functions!" << std::endl;
        exit(1);
      }
      using Fn = std::map<std::string, msgpack::object>;
      std::vector<Fn> fns = kv.val.convert();

      for (auto& fn : fns) {
        NeoFunc nf;
        fn["name"]       .convert(&nf.name);
        fn["return_type"].convert(&nf.resultType);
        nf.canFail = fn["can_fail"].via.boolean;
        fn["id"]         .convert(&nf.id);
        fn["parameters"] .convert(&nf.args);

        std::cout << nf << std::endl;
        serv.functions.emplace_back(std::move(nf));
      }


      for (NeoFunc& nf : serv.functions)
        std::cout << nf << '\n';
    }
  }
}
