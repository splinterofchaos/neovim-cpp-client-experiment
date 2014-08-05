
#include "Socket.h"
#include "NeoServer.h"

#include <string>

#include <iostream>
#include <map>
#include <list>
#include <utility>
#include <tuple>

#include <unistd.h>  // fork()
#include <signal.h>  // kill()/SIGKILL
#include <pthread.h>

#include <msgpack.hpp>
//#include <uv.h> // TODO: Use libuv.

//#include "auto/neovim.h"

int msgpack_write_cb(void* data, const char* buf, unsigned int len);

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

void cout_reply(const NeoServer::Reply& reply)
{
  std::cout << '[' << std::get<0>(reply) << "] ";
  std::cout << std::get<1>(reply) << std::endl;
}

int main()
{
  NeoServer serv;
  server = &serv;

  std::cout << "API:" << std::endl;
  for (NeoFunc& nf : server->functions)
    std::cout << nf << '\n';

  Buffer currentBuf(*server);


  while (true)
  {
    if (currentBuf.pid.available)
      std::cout << "current buffer id: " 
                << currentBuf.pid.get() << std::endl;

    if (server->notifications.size()) { 
      std::cout << "Notifications:\n";
      do {
        NeoServer::Note note = server->notifications.front();
        server->notifications.pop_front();

        std::cout << std::get<0>(note) << ": ";
        std::cout << std::get<1>(note) << '\n';
      } while (server->notifications.size());
    }

    std::string line;
    std::cout << " : ";
    if (!std::getline(std::cin, line))
      break;

    if (line.size() == 0)
      continue;

    if (line == "quit")
      break;

    if (line == "?pending") {
      for (const auto& rep : server->pending())
        cout_reply(rep);
      continue;
    }

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
      else if (*it == "true" || *it == "True" || *it == "TRUE")
        args.emplace_back(true);
      else if (*it == "false" || *it == "False" || *it == "FALSE")
        args.emplace_back(false);
      else 
        args.emplace_back(*it);
    }

    uint64_t id;

    if (std::isdigit(ws[0][0]))
      id = server->request(std::stoi(ws[0]), args);
    else
      id = server->request(ws[0], args);

    std::cout << server->grab(id) << std::endl;
  }
}
