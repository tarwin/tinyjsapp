// tinyjs window launcher.
//
// A dumb window process: it renders HTML in a native webview and shuttles
// JSON-RPC messages between the page and the backend over a Unix domain
// socket. No TCP, no ports; the socket lives in a private temp directory.
// The backend (txiki.js) creates the socket, then spawns this as a child.
//
// Protocol (newline-delimited; payloads are JSON so never contain raw \n):
//   launcher -> backend:  CALL <id> <json-args-array>
//   backend -> launcher:  RET <id> <status> <json>   resolve/reject a call
//                         EVAL <js>                  run JS in the page
//                         TITLE <text>               set window title
//                         SIZE <w> <h>               resize window
//                         DLG <id> <op>              run a native dialog on the
//                                                    UI thread and answer the
//                                                    call directly via
//                                                    webview_return; op is one
//                                                    of open|openmulti|dir|save
//                         RELOAD                     re-read the HTML file and
//                                                    re-render (dev hot-reload)
//                         MENUBEGIN / MENU <title> /
//                         ITEM <id>\t<label>\t<key> /
//                         SEP / MENUEND              declare custom menu bar
//                                                    menus (applied on MENUEND)
//                         QUIT                       close the window
//   launcher -> backend:  MENU <id>                  a custom menu item was
//                                                    clicked
//
// A default app menu (About + Quit) is always present; About shows the
// standard panel with the app name, version, and a tinyjs credit.
//
// Built as Objective-C++ on macOS (needs AppKit for NSOpenPanel/NSSavePanel).
//
// Usage: launcher <html-file-or-url> <socket-path> [title] [WxH] [version]

#include "webview.h"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <vector>

static webview_t g_w = nullptr;
static int g_sock = -1;
static std::mutex g_write_mutex;
static std::string g_html_path; // empty when target is an http(s) URL
static std::string g_app_name = "tinyjs";
static std::string g_app_version = "0.0.0";

static void sock_write_line(const std::string &line) {
  std::lock_guard<std::mutex> lock(g_write_mutex);
  std::string msg = line + "\n";
  const char *p = msg.data();
  size_t left = msg.size();
  while (left > 0) {
    ssize_t n = write(g_sock, p, left);
    if (n <= 0)
      return;
    p += n;
    left -= (size_t)n;
  }
}

static std::string json_escape(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if ((unsigned char)c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  out += "\"";
  return out;
}

static void do_eval(webview_t w, void *arg) {
  std::string *js = static_cast<std::string *>(arg);
  webview_eval(w, js->c_str());
  delete js;
}

static void do_title(webview_t w, void *arg) {
  std::string *t = static_cast<std::string *>(arg);
  webview_set_title(w, t->c_str());
  delete t;
}

struct SizeReq {
  int width, height;
};

static void do_size(webview_t w, void *arg) {
  SizeReq *s = static_cast<SizeReq *>(arg);
  webview_set_size(w, s->width, s->height, WEBVIEW_HINT_NONE);
  delete s;
}

static void do_terminate(webview_t w, void *) { webview_terminate(w); }

static bool load_html_file(webview_t w, const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string html = ss.str();
  webview_set_html(w, html.c_str());
  return true;
}

static void do_reload(webview_t w, void *) {
  if (!g_html_path.empty())
    load_html_file(w, g_html_path);
}

// --- native dialogs (macOS) -------------------------------------------------
// Runs on the UI thread via webview_dispatch; answers the pending page call
// directly with webview_return, so the backend never sees a reply line.

static std::vector<std::string> split_tabs(const std::string &s) {
  std::vector<std::string> out;
  size_t start = 0, tab;
  while ((tab = s.find('\t', start)) != std::string::npos) {
    out.push_back(s.substr(start, tab - start));
    start = tab + 1;
  }
  out.push_back(s.substr(start));
  return out;
}

struct DlgReq {
  std::string id;
  std::string op;                 // open | openmulti | dir | save | alert | confirm | prompt
  std::vector<std::string> args;  // op-specific, tab-separated on the wire
};

#ifdef __APPLE__
static NSString *ns(const std::string &s) {
  return [NSString stringWithUTF8String:s.c_str()];
}
#endif

static void do_dialog(webview_t w, void *arg) {
  DlgReq *req = static_cast<DlgReq *>(arg);
  std::string json = "null";
  auto a = [&](size_t i) { return i < req->args.size() ? req->args[i] : std::string(); };
#ifdef __APPLE__
  @autoreleasepool {
    if (req->op == "save") {
      NSSavePanel *panel = [NSSavePanel savePanel];
      if ([panel runModal] == NSModalResponseOK && panel.URL != nil) {
        json = json_escape([panel.URL.path UTF8String]);
      }
    } else if (req->op == "alert" || req->op == "confirm") {
      // args: message, detail, okLabel, cancelLabel
      NSAlert *alert = [[NSAlert alloc] init];
      alert.messageText = ns(a(0).empty() ? g_app_name : a(0));
      if (!a(1).empty()) alert.informativeText = ns(a(1));
      [alert addButtonWithTitle:ns(a(2).empty() ? "OK" : a(2))];
      if (req->op == "confirm") {
        [alert addButtonWithTitle:ns(a(3).empty() ? "Cancel" : a(3))];
      }
      NSModalResponse r = [alert runModal];
      json = (req->op == "alert") ? "true"
                                  : (r == NSAlertFirstButtonReturn ? "true" : "false");
    } else if (req->op == "prompt") {
      // args: message, defaultValue, okLabel, cancelLabel
      NSAlert *alert = [[NSAlert alloc] init];
      alert.messageText = ns(a(0).empty() ? g_app_name : a(0));
      NSTextField *field =
          [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
      field.stringValue = ns(a(1));
      alert.accessoryView = field;
      alert.window.initialFirstResponder = field;
      [alert addButtonWithTitle:ns(a(2).empty() ? "OK" : a(2))];
      [alert addButtonWithTitle:ns(a(3).empty() ? "Cancel" : a(3))];
      if ([alert runModal] == NSAlertFirstButtonReturn) {
        json = json_escape([field.stringValue UTF8String]);
      }
    } else {
      NSOpenPanel *panel = [NSOpenPanel openPanel];
      panel.canChooseFiles = (req->op != "dir");
      panel.canChooseDirectories = (req->op == "dir");
      panel.allowsMultipleSelection = (req->op == "openmulti");
      if ([panel runModal] == NSModalResponseOK && panel.URLs.count > 0) {
        if (req->op == "openmulti") {
          json = "[";
          for (NSUInteger i = 0; i < panel.URLs.count; i++) {
            if (i)
              json += ",";
            json += json_escape([[panel.URLs[i] path] UTF8String]);
          }
          json += "]";
        } else {
          json = json_escape([[panel.URLs[0] path] UTF8String]);
        }
      }
    }
  }
#endif
  webview_return(w, req->id.c_str(), 0, json.c_str());
  delete req;
}

// --- menu bar (macOS) --------------------------------------------------------
// A default app menu (About + Quit) is always installed; MENUBEGIN…MENUEND
// declares additional custom menus. Item clicks are reported to the backend
// as `MENU <id>` lines.

struct MenuItemSpec {
  std::string id, label, key;
  bool separator = false;
};
struct MenuSpec {
  std::string title;
  std::vector<MenuItemSpec> items;
};

#ifdef __APPLE__
@interface TinyMenuTarget : NSObject
- (void)itemClicked:(NSMenuItem *)sender;
- (void)showAbout:(id)sender;
- (void)doQuit:(id)sender;
@end

@implementation TinyMenuTarget
- (void)itemClicked:(NSMenuItem *)sender {
  NSString *mid = (NSString *)sender.representedObject;
  if (mid)
    sock_write_line(std::string("MENU ") + [mid UTF8String]);
}
- (void)showAbout:(id)sender {
  [NSApp orderFrontStandardAboutPanelWithOptions:@{
    @"ApplicationName" : ns(g_app_name),
    @"ApplicationVersion" : ns("Version " + g_app_version),
    @"Version" : @"",
    @"Credits" : [[NSAttributedString alloc]
        initWithString:@"Made with tinyjs — https://tinyjs.app"],
  }];
}
- (void)doQuit:(id)sender {
  webview_terminate(g_w);
}
@end

static TinyMenuTarget *g_menu_target = nil;

static void apply_menus(webview_t, void *arg) {
  std::vector<MenuSpec> *menus = static_cast<std::vector<MenuSpec> *>(arg);
  @autoreleasepool {
    if (!g_menu_target)
      g_menu_target = [[TinyMenuTarget alloc] init];

    NSMenu *bar = [[NSMenu alloc] init];

    // Default app menu: About + Quit.
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [bar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    NSMenuItem *about =
        [[NSMenuItem alloc] initWithTitle:ns("About " + g_app_name)
                                   action:@selector(showAbout:)
                            keyEquivalent:@""];
    about.target = g_menu_target;
    [appMenu addItem:about];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *quit =
        [[NSMenuItem alloc] initWithTitle:ns("Quit " + g_app_name)
                                   action:@selector(doQuit:)
                            keyEquivalent:@"q"];
    quit.target = g_menu_target;
    [appMenu addItem:quit];
    appItem.submenu = appMenu;

    // Standard Edit menu so cmd-C/V/X/A work in the webview.
    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    [bar addItem:editItem];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [editMenu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All"
                        action:@selector(selectAll:)
                 keyEquivalent:@"a"];
    editItem.submenu = editMenu;

    // Custom menus from the backend.
    if (menus) {
      for (const MenuSpec &m : *menus) {
        NSMenuItem *holder = [[NSMenuItem alloc] init];
        [bar addItem:holder];
        NSMenu *menu = [[NSMenu alloc] initWithTitle:ns(m.title)];
        for (const MenuItemSpec &it : m.items) {
          if (it.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
          }
          NSMenuItem *mi =
              [[NSMenuItem alloc] initWithTitle:ns(it.label)
                                         action:@selector(itemClicked:)
                                  keyEquivalent:ns(it.key)];
          mi.target = g_menu_target;
          mi.representedObject = ns(it.id);
          [menu addItem:mi];
        }
        holder.submenu = menu;
      }
    }

    [NSApp setMainMenu:bar];
  }
  delete menus;
}
#else
static void apply_menus(webview_t, void *arg) {
  delete static_cast<std::vector<MenuSpec> *>(arg);
}
#endif

// -----------------------------------------------------------------------------

// Page called window.__invoke(...): forward to the backend.
static void on_invoke(const char *id, const char *req, void *) {
  sock_write_line(std::string("CALL ") + id + " " + req);
}

static void sock_read_loop() {
  std::string buf;
  char chunk[4096];
  std::vector<MenuSpec> pending_menus;
  bool in_menu_block = false;
  for (;;) {
    ssize_t n = read(g_sock, chunk, sizeof(chunk));
    if (n <= 0)
      break;
    buf.append(chunk, (size_t)n);
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (line.rfind("RET ", 0) == 0) {
        size_t sp1 = line.find(' ', 4);
        size_t sp2 = line.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos)
          continue;
        std::string id = line.substr(4, sp1 - 4);
        int status = std::atoi(line.c_str() + sp1 + 1);
        std::string json = line.substr(sp2 + 1);
        // webview_return is documented as safe to call from any thread.
        webview_return(g_w, id.c_str(), status, json.c_str());
      } else if (line.rfind("EVAL ", 0) == 0) {
        webview_dispatch(g_w, do_eval, new std::string(line.substr(5)));
      } else if (line.rfind("TITLE ", 0) == 0) {
        webview_dispatch(g_w, do_title, new std::string(line.substr(6)));
      } else if (line.rfind("SIZE ", 0) == 0) {
        SizeReq *s = new SizeReq{960, 640};
        std::sscanf(line.c_str() + 5, "%d %d", &s->width, &s->height);
        webview_dispatch(g_w, do_size, s);
      } else if (line.rfind("DLG ", 0) == 0) {
        // DLG <id> <op>[\t<arg>...]
        size_t sp1 = line.find(' ', 4);
        if (sp1 == std::string::npos)
          continue;
        std::vector<std::string> parts = split_tabs(line.substr(sp1 + 1));
        DlgReq *req = new DlgReq;
        req->id = line.substr(4, sp1 - 4);
        req->op = parts[0];
        req->args.assign(parts.begin() + 1, parts.end());
        webview_dispatch(g_w, do_dialog, req);
      } else if (line == "MENUBEGIN") {
        pending_menus.clear();
        in_menu_block = true;
      } else if (in_menu_block && line.rfind("MENU ", 0) == 0) {
        pending_menus.push_back(MenuSpec{line.substr(5), {}});
      } else if (in_menu_block && line.rfind("ITEM ", 0) == 0 && !pending_menus.empty()) {
        std::vector<std::string> p = split_tabs(line.substr(5));
        MenuItemSpec it;
        it.id = p.size() > 0 ? p[0] : "";
        it.label = p.size() > 1 ? p[1] : it.id;
        it.key = p.size() > 2 ? p[2] : "";
        pending_menus.back().items.push_back(it);
      } else if (in_menu_block && line == "SEP" && !pending_menus.empty()) {
        MenuItemSpec sep;
        sep.separator = true;
        pending_menus.back().items.push_back(sep);
      } else if (line == "MENUEND") {
        in_menu_block = false;
        webview_dispatch(g_w, apply_menus,
                         new std::vector<MenuSpec>(pending_menus));
      } else if (line == "RELOAD") {
        webview_dispatch(g_w, do_reload, nullptr);
      } else if (line == "QUIT") {
        webview_dispatch(g_w, do_terminate, nullptr);
      }
    }
  }
  // Backend exited or closed the socket: close the window.
  webview_dispatch(g_w, do_terminate, nullptr);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <html-file-or-url> <socket-path> [title] [WxH]\n", argv[0]);
    return 1;
  }
  const std::string target = argv[1];
  const std::string sock_path = argv[2];
  const std::string title = argc > 3 ? argv[3] : "tinyjs";
  int width = 960, height = 640;
  if (argc > 4)
    std::sscanf(argv[4], "%dx%d", &width, &height);
  g_app_name = title;
  if (argc > 5)
    g_app_version = argv[5];

  g_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
  if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    std::fprintf(stderr, "launcher: cannot connect to %s: %s\n", sock_path.c_str(),
                 std::strerror(errno));
    return 1;
  }

  g_w = webview_create(1 /* debug: enables devtools */, nullptr);
  if (!g_w) {
    std::fprintf(stderr, "launcher: failed to create webview\n");
    return 1;
  }

  webview_set_title(g_w, title.c_str());
  webview_set_size(g_w, width, height, WEBVIEW_HINT_NONE);
  webview_bind(g_w, "__invoke", on_invoke, nullptr);

  // Default menu bar (About/Quit + Edit); custom menus replace it via MENUEND.
  apply_menus(g_w, new std::vector<MenuSpec>());

  if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
    webview_navigate(g_w, target.c_str());
  } else {
    // Load file contents via set_html; avoids file:// restrictions in WKWebView.
    g_html_path = target;
    if (!load_html_file(g_w, target)) {
      std::fprintf(stderr, "launcher: cannot read %s\n", target.c_str());
      return 1;
    }
  }

  std::thread(sock_read_loop).detach();

  webview_run(g_w);
  webview_destroy(g_w);
  // The socket thread may still be blocked in read; exit hard.
  _exit(0);
}
