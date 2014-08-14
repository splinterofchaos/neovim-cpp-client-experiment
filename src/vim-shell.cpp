
#include "Socket.h"
#include "NeoServer.h"

#include <algorithm>
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

/// Converts a string to a msgpack object for sending to vim.
msgpack::object read_object(const std::string& str);

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

  std::vector<uint64_t> waiting;  // Messages actively waiting on.

  while (true)
  {
    for (const auto& note : server->inquire()) {
      std::cout << std::get<0>(note) << ": ";
      std::cout << std::get<1>(note) << '\n';
    }

    std::vector<uint64_t> stillWaiting;
    for (uint64_t mid : waiting) {
      msgpack::object o;
      if (serv.grab_if_ready(mid, o)) {
        std::cout << '[' << mid << ']';
        std::cout <<  " => " << o << '\n';
      } else {
        stillWaiting.push_back(mid);
      }
    }
    waiting = stillWaiting;

    std::string line;
    std::cout << '(' << serv.id << ") : ";
    if (!std::getline(std::cin, line))
      break;

    if (line.size() == 0)
      line = "0";

    if (line == "quit")
      break;

    if (line == "?pending") {
      for (const auto& rep : server->pending())
        cout_reply(rep);
      continue;
    }

    if (line == "?waiting") {
      std::cout << "[ ";
      for (uint64_t rep : waiting)
        std::cout << rep << " ";
      std::cout << "]\n";
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

    std::transform(std::begin(ws)+1, std::end(ws), 
                   std::back_inserter(args), read_object);

    uint64_t id = std::isdigit(ws[0][0]) ? std::stoi(ws[0])
                                         : server->method_id(ws[0]);

    id = server->request_with(id, args);

    if (!id && line != "0")
      std::cerr << "Unrecognized method name: " << ws[0] << std::endl;
    else
      waiting.push_back(id);
  }
}

msgpack::object read_object(const std::string& str)
{
  msgpack::object o;
  if (std::isdigit(str[0]))
    o = std::stoi(str);
  else if (str == "true" || str == "True" || str == "TRUE")
    o = true;
  else if (str == "false" || str == "False" || str == "FALSE")
    o = false;
  else 
    o = str;
  return o;
}
