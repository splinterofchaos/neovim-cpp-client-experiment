
#include "Socket.h"

#include <string>

#include <iostream>
#include <map>
#include <utility>
#include <tuple>

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

  // TODO: std::future?
  template<typename T = std::vector<int>>
  msgpack::object request(uint64_t method, const T& t = T{});

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

  // Request the API data.
  std::vector<msgpack::object> resultObj = request(0).convert();

  // resultObj[0] holds the response ID (I think), but we don't need it as
  // things are currently written.

#if MSGPACK_VERSION_MINOR >= 6
  msgpack::object_str raw = resultObj[1].via.str;
#else
  msgpack::object_raw raw = resultObj[1].via.raw;
#endif

  msgpack::unpacked up;
  msgpack::unpack(&up, raw.ptr, raw.size);

  using Services = std::map<std::string, msgpack::object>;
  Services servicesMap = up.get().convert();

  servicesMap["classes"].convert(&classes);

  using Fn = std::map<std::string, msgpack::object>;
  std::vector<Fn> fns = servicesMap["functions"].convert();

  for (auto& fn : fns) {
    NeoFunc nf;
    fn["name"]       .convert(&nf.name);
    fn["return_type"].convert(&nf.resultType);
    nf.canFail = fn["can_fail"].via.boolean;
    fn["id"]         .convert(&nf.id);
    fn["parameters"] .convert(&nf.args);

    functions.emplace_back(std::move(nf));
  }
}

template<typename T>
msgpack::object NeoServer::request(uint64_t method, const T& t)
{
  msgpack::sbuffer sbuf;

  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_array(4) << REQUEST  // type
                   << id++     // msg id
                   << method   // method
                   << t;       // [args]

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

namespace std {
  string to_string(msgpack::type::object_type type)
  {
    switch(type) {
     case msgpack::type::NIL               :  return "nil";
     case msgpack::type::BOOLEAN           :  return "bool";
     case msgpack::type::POSITIVE_INTEGER  :  return "+int";
     case msgpack::type::NEGATIVE_INTEGER  :  return "-int";
     case msgpack::type::DOUBLE            :  return "double";
#if MSGPACK_VERSION_MINOR >= 6
     // msgpack 0.6 differentiates between raw data (BIN) and strings.
     case msgpack::type::STR               :  return "string";
     case msgpack::type::BIN               :  return "binary";
#else
     case msgpack::type::RAW               :  return "raw";
#endif
     case msgpack::type::ARRAY             :  return "array";
     case msgpack::type::MAP               :  return "map";
     default: return "unknown type";
    }
  }
}

enum class WordsError { 
  UNESCAPED_QUOTE, 
  ENDS_WITH_ESCAPE,
  OK
};

// TODO: std::string_view?
template<typename StringIt, typename Inserter>
WordsError words(StringIt it, StringIt end, Inserter inserter)
{
  // To represent the parse state, store a predicate that defines how to
  // delimit a "word", which may include whitespace if in quotes.
  using Mode = bool(*)(char);
  Mode mode = nullptr;

  Mode non_white    = [](char c) {  return  !std::isspace(c);  };
  Mode is_white     = [](char c) {  return !!std::isspace(c);  };
  Mode single_quote = [](char c) {  return c == '\'';          };
  Mode double_quote = [](char c) {  return c == '"';           };

  // We also need to know if the parsed char should be escaped.
  bool esc = false;

  for (; it != end; it++) {
    // Skip whitespace.
    it = std::find_if(it, end, non_white);
    if (it == end)
      break;

    if (single_quote(*it)) {
      mode = single_quote;
      it++;
    } else if (double_quote(*it)) {
      mode = double_quote;
      it++;
    } else {
      mode = is_white;
    }

    std::string s;
    for (; it != end && (esc || !mode(*it)); it++) {
      if (esc || *it != '\\') {
        s.push_back(*it);
        esc = false;
      } else if (*it == '\\') {
        esc = true;
      }
    }

    // Check for errors, but add the new word first so the caller can see where
    // it errored.
    *(inserter++) = std::move(s);

    if (esc)
      return WordsError::ENDS_WITH_ESCAPE;

    if (mode != is_white && it == end)
      return WordsError::UNESCAPED_QUOTE;
  }

  return WordsError::OK;
}

int main()
{
  NeoServer serv;

  std::cout << "API:" << std::endl;
  for (NeoFunc& nf : serv.functions)
    std::cout << nf << '\n';

  while (true)
  {
    std::string line;
    std::cout << " : ";
    if (!std::getline(std::cin, line))
      break;

    if (line.size() == 0)
      continue;

    if (line == "quit")
      break;

    std::vector<std::string> ws;
    auto wordsError = words(std::begin(line), 
                            std::end(line),
                            std::back_inserter(ws));

    switch (wordsError) {
      case WordsError::UNESCAPED_QUOTE: 
        std::cerr << "Quotation not escaped: \"" << ws.back() << std::endl;
        continue;
      case WordsError::ENDS_WITH_ESCAPE:
        std::cerr << "Ends with escape: " << ws.back() << '\\' << std::endl;
        continue;
      default: break;  // Ok!
    }

    std::vector<msgpack::object> args;

    for (auto it=std::begin(ws)+1; it != std::end(ws); it++) {
      if (std::isdigit((*it)[0]))
        args.emplace_back(std::stoi(*it));
      else 
        args.emplace_back(*it);
    }

    msgpack::object reply = serv.request(std::stoi(line), args);
    std::cout << reply << std::endl;
  }
}
