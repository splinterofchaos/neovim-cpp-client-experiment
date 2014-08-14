
#include <algorithm>
#include <string>
#include <map>
#include <list>
#include <utility>
#include <tuple>

#include <unistd.h>  // fork()
#include <signal.h>  // kill()/SIGKILL
#include <pthread.h>

#include <msgpack.hpp>

#include <stdlib.h>
#include <curses.h>
#include <signal.h>

#include "Socket.h"
#include "NeoServer.h"

static void finish(int sig);

using Lines = std::vector<std::string>;
using Pos   = std::pair<int,int>;

// (y, x) semantics
int getx(Pos p) { return p.second; }
int gety(Pos p) { return p.first;  }

struct Object
{
  const char *prefix;
  uint64_t id;
};

struct Buffer;
struct Window;
struct Tab;

template<typename...T>
uint64_t request(NeoServer &serv, const Object &o,
                 const std::string mthd, const T &...t)
{
  return serv.request(o.prefix + ('_' + mthd), o.id, t...);
}

template<typename...T>
msgpack::object::implicit_type demand(NeoServer &serv, const Object &o,
                                      const std::string mthd, const T &...t)
{
  return serv.grab(request(serv, o, mthd, t...)).convert();
}

struct Tab : Object
{
  NeoServer& serv;

  Tab(NeoServer&);

  std::vector<Window> windows();

  Window window();
};

struct Window : Object
{
  NeoServer& serv;

  Window(NeoServer&);
  Window(NeoServer&, uint64_t);

  Buffer buffer();

  Pos cursor();
  void cursor(Pos);

  Pos position();
  void position(Pos);
};

struct Buffer : Object
{
  NeoServer& serv;

  Buffer(NeoServer&);
  Buffer(NeoServer&, uint64_t);

  size_t length();

  std::string name();
  void name(const std::string&);

  std::string operator[] (uint64_t);

  /// Gets a slice of the buffer.
  Lines slice(size_t start, size_t end=-1);

  /// Sets a slice of the buffer.
  void slice(size_t start, size_t end, const Lines&);
  void slice(size_t start, const Lines&);

  /// Sets a local variable.
  void var(const std::string&, msgpack::object);
  msgpack::object var(const std::string&);
};

Tab::Tab(NeoServer& s) : serv(s)
{
  prefix = "tabpage";
  serv.grab(serv.request("vim_get_current_tabpage"), id);
}

std::vector<Window> Tab::windows()
{
  std::vector<Window> ret;
  std::vector<uint64_t> ids = demand(serv, *this, "get_windows");
  std::transform(std::begin(ids), std::end(ids), std::back_inserter(ret),
                 [&](uint64_t id) {return Window(serv, id);} );
  return std::move(ret);
}

Window Tab::window()
{
  return Window(serv, demand(serv, *this, "get_window"));
}

Window::Window(NeoServer &s) : serv(s)
{
  prefix = "window";
  serv.grab(serv.request("vim_get_current_window"), id);
}

Window::Window(NeoServer &s, uint64_t id) : serv(s)
{
  prefix = "window";
  this->id = id;
}

Buffer Window::buffer()
{
  return Buffer(serv, (uint64_t) demand(serv, *this, "get_buffer"));
}

Pos Window::cursor()
{
  return demand(serv, *this, "get_cursor");
}

void Window::cursor(Pos p)
{
  request(serv, *this, "set_cursor", p);
}

Pos Window::position()
{
  return demand(serv, *this, "get_position");
}

void Window::position(Pos p)
{
  request(serv, *this, "set_position", p);
}

Buffer::Buffer(NeoServer& serv) : serv(serv)
{
  prefix = "buffer";
  serv.grab(serv.request("vim_get_current_buffer"), id);
}

Buffer::Buffer(NeoServer& serv, uint64_t id) : serv(serv)
{
  this->id = id;
  prefix = "buffer";
}

size_t Buffer::length()
{
  return demand(serv, *this, "get_length");
}

std::string Buffer::name()
{
  return demand(serv, *this, "get_name");
}

void Buffer::name(const std::string &newval)
{
  request(serv, *this, "set_name", newval);
}

std::string Buffer::operator[] (uint64_t line)
{
  return demand(serv, *this, "get_line", line);
}

Lines Buffer::slice(size_t start, size_t end)
{
  return demand(serv, *this, "get_slice", start, end, true, false);
}

void Buffer::slice(size_t start, size_t end, const Lines& lines)
{
  request(serv, *this, "set_slice", start, end, true, false, lines);
}

void Buffer::slice(size_t start, const Lines& lines)
{
  slice(start, -1, lines);
}

void Buffer::var(const std::string& name, msgpack::object o)
{
  request(serv, *this, "set_var", "b:" + name, o);
}

msgpack::object Buffer::var(const std::string& name)
{
  return serv.grab(request(serv, *this, "get_var", "b:" + name));
}

Pos operator+ (const Pos& a, const Pos& b)
{
  return {a.first + b.first, a.second + b.second};
}

Pos operator- (const Pos& a, const Pos& b)
{
  return {a.first - b.first, a.second - b.second};
}

/// A simple RAII wrapper for ncurses windows.
struct TermWindow
{
  WINDOW *win;
  Pos start, dims;

  std::vector<TermWindow *> children;

  TermWindow();  ///< A fullscreen window.
  TermWindow(Pos start, Pos dims);

  TermWindow(TermWindow& parent, Pos start, Pos dims);

  ~TermWindow();

  void move(Pos to={0,0});  ///< Move the cursor relative this window.

  template<typename...T>
  void print(Pos at, const T&...);

  void touch();     ///< touchwin
  void refresh();   ///< wrefresh
  void qrefresh();  ///< wnourefresh (pneumonic: quick-refresh)
};

TermWindow::TermWindow()
{
  start = {0, 0};
  getmaxyx(stdscr, dims.first, dims.second);
  win = newwin(0, 0, 0, 0);
}

TermWindow::TermWindow(Pos start, Pos dims) : start(start), dims(dims)
{
  win = newwin(gety(dims),  getx(dims), gety(start), getx(start));
}

TermWindow::TermWindow(TermWindow& parent, Pos start, Pos dims)
    : start(start), dims(dims)
{
  win = derwin(parent.win, gety(dims),  getx(dims), gety(start), getx(start));
  parent.children.push_back(this);
}

TermWindow::~TermWindow()
{
  delwin(win);
}

void TermWindow::move(Pos to)
{
  wmove(win, gety(to), getx(to));
}

template<typename...T>
void TermWindow::print(Pos at, const T&...t)
{
  this->move(at);
  wprintw(win, t...);
}

void TermWindow::touch()
{
  for (TermWindow* child : children)
    child->touch();
  touchwin(win);
}

void TermWindow::refresh()
{
  for (TermWindow* child : children)
    child->refresh();
  wrefresh(win);
}

void TermWindow::qrefresh()
{
  for (TermWindow* child : children)
    child->qrefresh();
  wnoutrefresh(win);
}

static std::string termkey_to_vimkey(int k);

static void handle_redraw_layout(const msgpack::object &,
                                 uint64_t window,
                                 std::vector<std::string>&);

int main(int argc, char *argv[])
{
  int num = 0;

  std::cout << "Connecting to server..." << std::endl;
  NeoServer serv;

  // Graceful exit for Ctrl-C.
  signal(SIGINT, finish);

  initscr();
  keypad(stdscr, TRUE);
  nonl();
  cbreak();
  echo();

  if (has_colors())
  {
    start_color();

    init_pair(1, COLOR_RED,     COLOR_BLACK);
    init_pair(2, COLOR_GREEN,   COLOR_BLACK);
    init_pair(3, COLOR_YELLOW,  COLOR_BLACK);
    init_pair(4, COLOR_BLUE,    COLOR_BLACK);
    init_pair(5, COLOR_CYAN,    COLOR_BLACK);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(7, COLOR_WHITE,   COLOR_BLACK);
  }

  Pos screenDims;
  getmaxyx(stdscr, screenDims.first, screenDims.second);

  int consoleY = gety(screenDims) - 10;
  TermWindow bufView({0,0}, screenDims - Pos{10,0});
  TermWindow console({consoleY, 0}, {10,0});

  //serv.request("vim_subscribe", std::string("redraw:layout"));
  //serv.request("vim_subscribe", std::string("redraw:cursor"));

  std::vector<std::string> slice;

  while (true)
  {
    // We could request this once, outside the loop, but it causes enough delay
    // to allow the fallowing functions to work.
    Data b = current(serv, "buffer");

    Pos p = serv.grab(current(serv, "window").get("cursor")).convert();
    size_t startingLine = p.first > 30 ? p.first - 30 - 2 : 0;

    // If we get here too quickly, this'll fetch the previous slice.
    serv.grab(b.get("slice", startingLine, startingLine + gety(bufView.dims),
                    true, false),
              slice);
    int y = 0;
    for (const auto& line : slice) {
      if (y >= bufView.dims.first)
        break;
      bufView.print({y++, 0}, "%s\n", line.c_str());
    }

    y = 0;
    for (const auto& note : serv.inquire()) 
    {
      if (std::get<0>(note) == "redraw:layout") 
      {
        handle_redraw_layout(std::get<1>(note), 
                             current(serv, "window").id,
                             slice);
      }
      else  // we don't know how to handle this event.
      {     // Let the user have a look.
        console.print (
            {y++,0},
            "%s : %s", 
            std::get<0>(note).c_str(),
            std::to_string(std::get<1>(note)).c_str()
        );
      }
    }
  

    move(31, p.second);
    num++;

    bufView.refresh();
    console.refresh();

    wclear(bufView.win);
    wclear(console.win);

    int c = getch();

    std::string feed = termkey_to_vimkey(c);
    if (feed != "")
      serv.request("vim_eval", "feedkeys(\"" + feed + "\")");

    // NOTE: uncomment to debug input.
    //  console.print({0,0},
    //                "Keycode %i = %c\n\rfeeding %s as '%s'\n",
    //                c,
    //                std::isgraph(c) ? (char)c : '?',
    //                keyname(c),
    //                feed.c_str());
  }

  finish(0);
}

static std::string termkey_to_vimkey(int k)
{
  if (k == 0)
    return "";

  // TODO: May need special handling for brackets ([]).
  if (std::isprint(k))
    return { (char) k };

  // Feed either '\<word>' or '\<mod-word>'.
  auto feed_word = [&](const char *word, const char *mod) {
      std::string feed = "\\<";
      if (mod) {
        feed += mod;
        feed += "-";
      }
      feed += word;
      feed += ">";
      return feed;
  };

  const char *mod = nullptr;
  switch (k)
  {
    case 033:   return feed_word("Esc", mod);
    case '\n':
    case '\r':  return feed_word("CR", mod);
    case ' ':   return feed_word("Space", mod);
    case KEY_BACKSPACE: return feed_word("BS", mod);

    case KEY_SR:     mod = "S";
    case KEY_UP:     return feed_word("Up", mod);

    case KEY_SF:     mod = "S";
    case KEY_DOWN:   return feed_word("Down", mod);

    case KEY_SLEFT:  mod = "S";
    case KEY_LEFT:   return feed_word("Left", mod);

    case KEY_SRIGHT: mod = "S";
    case KEY_RIGHT:  return feed_word("Right", mod);

    default: ;
  }

  const char *kname = keyname(k);
  if (kname[0] == '^') {
    std::string feed = "<C-";
    feed += tolower(kname[1]);
    feed += '>';
    return feed;
  }

  // TODO: I could not find any good resources on key handling.
  // As a last resort, compare by keyname().
  struct {
    const char *kname;
    const char *ans;
  } keys[] = {
    { "kUPS",   "<C-Up>"    },
    { "kDN5",   "<C-Down>"  },
    { "kRIT5",  "<C-Right>" },
    { "kLFT5",  "<C-Left>"  },
  };

  for (auto key : keys)
    if (strcmp(key.kname, kname) == 0)
      return key.ans;

  return "";
}

static void handle_redraw_layout(const msgpack::object    &o,
                                 uint64_t                 window,
                                 std::vector<std::string> &slice)
{
  std::map<std::string, msgpack::object> node = o.convert();
  if (node["type"] == "leaf") {
    if (node["window_id"] == window) {
    }
  }
}
      


static void finish(int sig)
{
  endwin();
  exit(0);
}

