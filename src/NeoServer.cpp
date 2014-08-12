
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

ScopedLock::ScopedLock(pthread_mutex_t& ref) : m(&ref)
{
  pthread_mutex_lock(m);
}

ScopedLock::~ScopedLock()
{
  pthread_mutex_unlock(m);
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

  // Start the thread to read from the server.
  repliesLock      = PTHREAD_MUTEX_INITIALIZER;
  newReply         = PTHREAD_COND_INITIALIZER;
  if (pthread_create(&worker, nullptr, listen, this) != 0)
    die_errno("spawning listener with pthread_create()");

  std::cout << "Requesting API data...\n";
  Reply res = grab(request(0)).convert();

  std::cout << '#' << std::get<0>(res) << '\n';

#if MSGPACK_VERSION_MINOR >= 6
  msgpack::object_str raw = std::get<1>(res).via.str;
#else
  msgpack::object_raw raw = std::get<1>(res).via.raw;
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

NeoServer::~NeoServer()
{
  pthread_cond_destroy(&newReply);
  pthread_cancel(worker);
}

std::vector<NeoServer::Reply> NeoServer::pending()
{
  return std::vector<NeoServer::Reply>(std::begin(replies), std::end(replies));
}

uint64_t NeoServer::method_id(const std::string& name)
{
  for (const NeoFunc& nf : functions) {
    if (nf.name == name)
      return nf.id;
  }

  return 0;
}

msgpack::object NeoServer::grab(uint64_t mid)
{
  msgpack::object o;

  ScopedLock l(repliesLock);
  while (true) {
    for (auto& rep : replies) {
      if (std::get<0>(rep) == mid) {
        msgpack::object o = std::get<1>(rep);
        replies.remove(rep);  // TODO: remove()/erase()
        return o;
      }
    }

    // It probably hasn't been added yet.
    pthread_cond_wait(&newReply, &repliesLock);
  }
}

void *NeoServer::listen(void *pthis)
{
  NeoServer& self = *reinterpret_cast<NeoServer*>(pthis);
  msgpack::unpacked un;
  while (self.sock.recv(self.up)) {
    while (self.up.next(&un)) {
      msgpack::object_array reply_ar = un.get().via.array;
      auto reply = [&](size_t i) { return reply_ar.ptr[i]; };
      size_t len = reply_ar.size;

      // The first field must be the message type; either RESPONSE or NOTIFY.
      if (reply(0) == RESPONSE && len == 4) {
        // A msgpack response is either: 
        //    (RESPONSE, id,   nil, ret)
        // or (RESPONSE, id, error, nil)
        uint64_t rid = reply(1).convert();
        msgpack::object val = reply( reply(2).is_nil() ? 3 : 2 );

        ScopedLock l(self.repliesLock);
        self.replies.emplace_back(rid, val);
        pthread_cond_signal(&self.newReply);  // Signal grab() to try again.
      } else if (reply(0) == NOTIFY && len == 3) {
        // A msgpack notification looks like: (NOTIFY, name, args)
        self.notifications.emplace_back(reply(1).as<std::string>(), 
                                        reply(2));
      } else {
        std::cerr << "Unknown message type (" << reply(0).via.u64 << ")\n";
      }
    }
  }

  std::cout << "Socket closed; vim probably exited.\n";

  return nullptr;
}
  
