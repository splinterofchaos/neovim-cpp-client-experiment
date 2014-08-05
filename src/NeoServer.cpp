
#include <cstdlib>   // getenv()
#include <stdio.h>
#include <unistd.h>  // fork()
#include <fcntl.h>

#include "NeoServer.h"

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

bool try_connect(NeoServer& serv)
{
  // Guess the server's address.
  const char* laddr = getenv("NEOVIM_LISTEN_ADDRESS");
  if (laddr && serv.sock.connect_local(laddr)) {
    serv.address = laddr;
    return true;
  }
  
  if (serv.sock.connect_local("/tmp/novim")) {
    serv.address = "/tmp/neovim";
    return true;
  }

  return false;
}

NeoServer *server = nullptr;

NeoServer::NeoServer()
{
  id = 0;

  if (!sock)
    die_errno("Failed opening socket:\n");

  if (!try_connect(*this)) {
    std::cout << "No neovim instance detected. Attempting to create one." 
              << std::endl;

    // Before failing, let's assume neovim hasn't started yet.
    pid_t pid = fork();
    if (pid == -1)
      die_errno("fork()");

    if (pid == 0) {
      std::cout << "Starting nvim" << std::endl;
      execlp("xterm", "xterm", "-e", "nvim", (char *) NULL);
      die_errno("running nvim");
    }

    std::cout << "Waiting for client to start..." << std::endl;
    unsigned int t = 0;
    while (!try_connect(*this)) { 
      usleep(200);
      // Call it quits after ten seconds.
      if ((t += 200) > 10e9) {
        std::cerr << "can't connect to server" << std::endl;
        exit(1);
      }
    }
  }

  // Request the API data.
  request(0);
  Reply res = receive();
  id = std::get<0>(res);
  std::vector<msgpack::object> resultObj = std::get<1>(res).convert();

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

NeoServer::Reply NeoServer::receive()
{
  sock.recv(up);

  msgpack::unpacked res;
  up.next(&res);


  // This works on with the `poc/0.6` branch of msgpack-c:
  /* 
   * uint64_t msgType, resId; msgpack::object error, ret;
   * using Reply = std::tuple<uint64_t,uint64_t,msgpack::object,msgpack::object>;
   * std::tie(msgType,resId,error,ret) = res.get().convert();
   */

  std::vector<msgpack::object> reply = res.get().convert();

  Reply ret;

  // The first field must be the message type; either RESPONSE or NOTIFY.
  if (reply[0] == RESPONSE) {
    // A msgpack response looks like this: [RESPONSE, message id, error, ret]
    std::get<0>(ret) = reply[1].convert();

    // If the error is set, ret will be nil.
    if (!reply[2].is_nil())
      std::get<1>(ret) = reply[2];
    else
      std::get<1>(ret) = reply[3];
  } else if (reply[0] == NOTIFY) {
    // A msgpack notification looks like: [NOTIFY, name, args]
    auto mthd = methods.find(reply[1].convert());
    if (mthd != std::end(methods)) {
      (mthd->second)(reply[2]);
    } else {
      // FIXME:
      std::cout << "Notified: " << reply[1] << ' ' << reply[2];
    }

    ret = receive();
  }

  return ret;
}

