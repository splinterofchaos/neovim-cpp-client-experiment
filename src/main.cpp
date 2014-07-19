
// TODO: Which of these do I really need?
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>  // unix sockaddr type.

#include <iostream>
#include <map>
#include <utility>

#include <msgpack.h>
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

template<typename POD>
void zero(POD& pod)
{
  bzero((char *) &pod, sizeof(pod));
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
};

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
constexpr size_t MAX_SIZE = 1024*1024;

// TODO: This is really inefficient when writing to a msgpack::unpacker.
std::string UnixSocket::recv()
{
  char buf[MAX_SIZE];
  size_t len = ::recv(fd, buf, MAX_SIZE, 0);
  return std::string(buf, buf+len);
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
  std::string buf = sock.recv();

  msgpack::unpacker up(buf.size());
  memcpy(up.buffer(), buf.data(), buf.size());
  up.buffer_consumed(buf.size());

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
  if (finalObj.type != msgpack::type::RAW) {
    std::cerr << "Unexpected object type." << std::endl;
    exit(1);
  }

  msgpack::object_raw raw = finalObj.via.raw;

  msgpack::unpacker upData(raw.size + 1);
  memcpy(upData.buffer(), raw.ptr, raw.size);
  upData.buffer_consumed(raw.size);

  msgpack::unpacked res;
  upData.next(&res);

  msgpack::object dataObj = res.get();
  if (dataObj.type != msgpack::type::MAP) {
    std::cerr << "Unexpected object type." << std::endl;
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
