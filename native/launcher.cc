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
//                         ITEM <id>\t<label>\t<key>\t<flags c|d> /
//                         SUB <id>\t<label> … SUBEND /
//                         SEP / MENUEND              declare custom menu bar
//                                                    menus (applied on MENUEND;
//                                                    flags: c=checked,
//                                                    d=disabled; SUB nests)
//                         MENUUPD <id>\t<label>\t<checked>\t<enabled>
//                                                    patch a live item ('' =
//                                                    leave unchanged)
//                         GET <qid> <what>           read-back query; what =
//                                                    win | item:<id>
//                         TRAYBEGIN <title>\t<icon>\t<template01>\t<tooltip> /
//                         ITEM / SEP / TRAYEND       declare a menu bar status
//                                                    item (applied on TRAYEND)
//                         TRAYREMOVE                 remove the status item
//                         WINOP <op> [args]          window ops: hide, show,
//                                                    center, minimize,
//                                                    fullscreen [0|1], restore,
//                                                    ontop 0|1,
//                                                    resizable 0|1, pos <x> <y>,
//                                                    dock 0|1 (Dock icon),
//                                                    hideonclose 0|1
//                         CTXBEGIN / ITEM / SEP /
//                         CTXEND / CTXCLEAR          replace or restore the
//                                                    right-click menu
//                         HKREG <id>\t<combo> /
//                         HKUNREG <id>               global hotkeys
//                         PRINT                      native print panel
//                         QUIT                       close the window
//   launcher -> backend:  MENU <id>                  a custom menu item was
//                                                    clicked
//                         CTX <id>                   a context menu item was
//                                                    clicked
//                         HOTKEY <id>                a global hotkey fired
//                         SYS theme dark|light /
//                         SYS sleep / SYS wake       system events (theme also
//                                                    sent once at startup)
//                         TRAY <id>                  a tray menu item was
//                                                    clicked
//                         TRAYCLICK                  the tray icon itself was
//                                                    clicked (no menu set)
//                         DROP <json-paths>          files dragged onto the
//                                                    window (real paths)
//                         GOT <qid> <json>           read-back answer
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
#import <WebKit/WebKit.h>
#include <Carbon/Carbon.h> // RegisterEventHotKey (global hotkeys)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <map>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <vector>

extern char **environ;

static webview_t g_w = nullptr;
static int g_sock = -1;
static int g_listen_fd = -1;   // bundle mode: launcher listens, backend attaches
static std::string g_sock_dir; // bundle mode: private socket dir, cleaned at exit
static std::mutex g_write_mutex;
static std::string g_html_path; // empty when target is an http(s) URL
static std::string g_app_name = "tinyjs";
static std::string g_app_version = "0.0.0";
// Lines produced before the backend is connected (bundle mode: Apple Events
// and page calls can arrive before the spawned backend attaches). Flushed by
// sock_set_connected().
static std::vector<std::string> g_pending_out;

static void sock_write_raw(const std::string &msg) {
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

static void sock_write_line(const std::string &line) {
  std::lock_guard<std::mutex> lock(g_write_mutex);
  if (g_sock < 0) {
    if (g_pending_out.size() < 512)
      g_pending_out.push_back(line);
    return;
  }
  sock_write_raw(line + "\n");
}

static void sock_set_connected(int fd) {
  std::lock_guard<std::mutex> lock(g_write_mutex);
  g_sock = fd;
  for (const std::string &line : g_pending_out)
    sock_write_raw(line + "\n");
  g_pending_out.clear();
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

// Load a local HTML file. On macOS this must go through
// loadFileURL:allowingReadAccessToURL: rather than webview_set_html:
// loadHTMLString (what set_html uses) gives the page an opaque about:blank
// origin, which is not a secure context, and WebKit hides SecureContext-only
// APIs like navigator.gpu (WebGPU) there. A file:// document is a secure
// context, and read access to the containing directory lets the page load
// sibling assets.
static bool load_html_file(webview_t w, const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
#ifdef __APPLE__
  WKWebView *wv = (WKWebView *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (wv) {
    NSURL *url =
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    [wv loadFileURL:url
        allowingReadAccessToURL:[url URLByDeletingLastPathComponent]];
    return true;
  }
#endif
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string html = ss.str();
  webview_set_html(w, html.c_str());
  return true;
}

static void do_reload(webview_t w, void *) {
  if (g_html_path.empty())
    return;
#ifdef __APPLE__
  // reloadFromOrigin bypasses WebKit's caches, so edited subresources
  // (css/js/images) are re-read from disk, not just the main document.
  WKWebView *wv = (WKWebView *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (wv && wv.URL != nil) {
    [wv reloadFromOrigin];
    return;
  }
#endif
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
  bool checked = false;   // ITEM flags field: 'c'
  bool disabled = false;  // ITEM flags field: 'd'
  std::vector<MenuItemSpec> submenu; // SUB <id>\t<label> … SUBEND nesting
};
struct MenuSpec {
  std::string title;
  std::vector<MenuItemSpec> items;
};

#ifdef __APPLE__
static void send_open_urls(NSArray *urls); // defined below
@interface TinyMenuTarget : NSObject
- (void)itemClicked:(NSMenuItem *)sender;
- (void)trayItemClicked:(NSMenuItem *)sender;
- (void)ctxItemClicked:(NSMenuItem *)sender;
- (void)trayClicked:(id)sender;
- (void)showAbout:(id)sender;
- (void)doQuit:(id)sender;
@end

@implementation TinyMenuTarget
- (void)itemClicked:(NSMenuItem *)sender {
  NSString *mid = (NSString *)sender.representedObject;
  if (mid)
    sock_write_line(std::string("MENU ") + [mid UTF8String]);
}
- (void)trayItemClicked:(NSMenuItem *)sender {
  NSString *mid = (NSString *)sender.representedObject;
  if (mid)
    sock_write_line(std::string("TRAY ") + [mid UTF8String]);
}
- (void)ctxItemClicked:(NSMenuItem *)sender {
  NSString *mid = (NSString *)sender.representedObject;
  if (mid)
    sock_write_line(std::string("CTX ") + [mid UTF8String]);
}
- (void)handleGetURL:(NSAppleEventDescriptor *)event
           withReply:(NSAppleEventDescriptor *)reply {
  NSString *url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
  NSURL *u = url ? [NSURL URLWithString:url] : nil;
  if (u)
    send_open_urls(@[ u ]);
}
- (void)handleOpenDocs:(NSAppleEventDescriptor *)event
             withReply:(NSAppleEventDescriptor *)reply {
  NSAppleEventDescriptor *list = [event paramDescriptorForKeyword:keyDirectObject];
  NSMutableArray *urls = [NSMutableArray array];
  for (NSInteger i = 1; i <= [list numberOfItems]; i++) {
    NSURL *u = [[list descriptorAtIndex:i] fileURLValue];
    if (u)
      [urls addObject:u];
  }
  send_open_urls(urls);
}
- (void)trayClicked:(id)sender {
  sock_write_line("TRAYCLICK");
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

// Live NSMenuItems by id, per container, rebuilt on each apply — MENUUPD
// patches and `GET item:<id>` reads go through these.
static NSMutableDictionary *g_reg_menu = nil;
static NSMutableDictionary *g_reg_tray = nil;

// Recursively fill `menu` from specs. autoenablesItems=NO so `disabled`
// sticks (AppKit would otherwise re-enable anything with a live target).
static void build_menu_into(NSMenu *menu, const std::vector<MenuItemSpec> &items,
                            SEL action, NSMutableDictionary *registry) {
  menu.autoenablesItems = NO;
  for (const MenuItemSpec &it : items) {
    if (it.separator) {
      [menu addItem:[NSMenuItem separatorItem]];
      continue;
    }
    NSMenuItem *mi = [[[NSMenuItem alloc] initWithTitle:ns(it.label)
                                                 action:action
                                          keyEquivalent:ns(it.key)] autorelease];
    mi.target = g_menu_target;
    mi.representedObject = ns(it.id);
    mi.state = it.checked ? NSControlStateValueOn : NSControlStateValueOff;
    mi.enabled = it.disabled ? NO : YES;
    if (!it.submenu.empty()) {
      NSMenu *sub = [[[NSMenu alloc] initWithTitle:ns(it.label)] autorelease];
      build_menu_into(sub, it.submenu, action, registry);
      mi.submenu = sub;
    }
    if (registry && !it.id.empty())
      registry[ns(it.id)] = mi;
    [menu addItem:mi];
  }
}

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
    [g_reg_menu release];
    g_reg_menu = [[NSMutableDictionary alloc] init];
    if (menus) {
      for (const MenuSpec &m : *menus) {
        NSMenuItem *holder = [[NSMenuItem alloc] init];
        [bar addItem:holder];
        NSMenu *menu = [[NSMenu alloc] initWithTitle:ns(m.title)];
        build_menu_into(menu, m.items, @selector(itemClicked:), g_reg_menu);
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

// --- tray / status item (macOS) ----------------------------------------------
// TRAYBEGIN <title>\t<iconPath>\t<template01>\t<tooltip>, then ITEM/SEP lines,
// then TRAYEND. With menu items the icon opens the menu (clicks -> `TRAY <id>`);
// without, clicking the icon sends `TRAYCLICK`.

struct TraySpec {
  std::string title, icon, tooltip;
  bool template_icon = true;
  std::vector<MenuItemSpec> items;
  bool remove = false;
};

#ifdef __APPLE__
static NSStatusItem *g_status_item = nil;

static void apply_tray(webview_t, void *arg) {
  TraySpec *spec = static_cast<TraySpec *>(arg);
  @autoreleasepool {
    if (spec->remove) {
      if (g_status_item) {
        [[NSStatusBar systemStatusBar] removeStatusItem:g_status_item];
        [g_status_item release];
        g_status_item = nil;
      }
      delete spec;
      return;
    }
    if (!g_menu_target)
      g_menu_target = [[TinyMenuTarget alloc] init];
    if (!g_status_item) {
      g_status_item = [[[NSStatusBar systemStatusBar]
          statusItemWithLength:NSVariableStatusItemLength] retain];
    }
    NSStatusBarButton *btn = g_status_item.button;
    if (!spec->icon.empty()) {
      NSImage *img =
          [[[NSImage alloc] initWithContentsOfFile:ns(spec->icon)] autorelease];
      if (img) {
        [img setSize:NSMakeSize(18, 18)];
        [img setTemplate:spec->template_icon ? YES : NO];
        btn.image = img;
      }
    } else {
      btn.image = nil;
    }
    btn.title = ns(spec->title);
    btn.toolTip = spec->tooltip.empty() ? nil : ns(spec->tooltip);

    [g_reg_tray release];
    g_reg_tray = [[NSMutableDictionary alloc] init];
    if (!spec->items.empty()) {
      NSMenu *menu = [[[NSMenu alloc] init] autorelease];
      build_menu_into(menu, spec->items, @selector(trayItemClicked:), g_reg_tray);
      g_status_item.menu = menu;
    } else {
      g_status_item.menu = nil;
      btn.target = g_menu_target;
      btn.action = @selector(trayClicked:);
    }
  }
  delete spec;
}
#else
static void apply_tray(webview_t, void *arg) {
  delete static_cast<TraySpec *>(arg);
}
#endif

// --- window ops (macOS) --------------------------------------------------------
// WINOP hide | show | center | minimize | fullscreen | ontop 0|1 |
//       resizable 0|1 | pos <x> <y> | dock 0|1 | hideonclose 0|1

static bool g_hide_on_close = false;

#ifdef __APPLE__
// Installed on the webview library's window delegate class; consulted on every
// close click. When hide-on-close is on, the window just orders out (tray apps
// keep running); otherwise the normal close/quit path proceeds.
static BOOL tiny_windowShouldClose(id, SEL, id sender) {
  if (!g_hide_on_close)
    return YES;
  [(NSWindow *)sender orderOut:nil];
  return NO;
}

static void install_close_hook(webview_t w) {
  NSWindow *win = (NSWindow *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  id delegate = win ? [win delegate] : nil;
  if (!delegate)
    return;
  // Adds only if the delegate class doesn't implement it (webview's doesn't).
  class_addMethod(object_getClass(delegate), @selector(windowShouldClose:),
                  (IMP)tiny_windowShouldClose, "c@:@");
}
#endif

static void do_winop(webview_t w, void *arg) {
  std::string *op = static_cast<std::string *>(arg);
#ifdef __APPLE__
  @autoreleasepool {
    NSWindow *win = (NSWindow *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
    if (*op == "hide") {
      [win orderOut:nil];
    } else if (*op == "show") {
      [NSApp activateIgnoringOtherApps:YES];
      [win makeKeyAndOrderFront:nil];
    } else if (*op == "dock 0") {
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    } else if (*op == "dock 1") {
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
    } else if (*op == "hideonclose 1") {
      g_hide_on_close = true;
    } else if (*op == "hideonclose 0") {
      g_hide_on_close = false;
    } else if (*op == "center") {
      [win center];
    } else if (*op == "minimize") {
      [win miniaturize:nil];
    } else if (*op == "restore") {
      [win deminiaturize:nil];
    } else if (*op == "fullscreen") {
      [win toggleFullScreen:nil];
    } else if (*op == "fullscreen 1" || *op == "fullscreen 0") {
      bool want = op->back() == '1';
      bool is = (win.styleMask & NSWindowStyleMaskFullScreen) != 0;
      if (want != is)
        [win toggleFullScreen:nil];
    } else if (*op == "ontop 1") {
      win.level = NSFloatingWindowLevel;
    } else if (*op == "ontop 0") {
      win.level = NSNormalWindowLevel;
    } else if (*op == "resizable 1") {
      win.styleMask |= NSWindowStyleMaskResizable;
    } else if (*op == "resizable 0") {
      win.styleMask &= ~NSWindowStyleMaskResizable;
    } else if (op->rfind("pos ", 0) == 0) {
      // Top-left origin in screen points (y grows downward, CSS-style).
      int x = 0, y = 0;
      if (std::sscanf(op->c_str() + 4, "%d %d", &x, &y) == 2 && win) {
        CGFloat screenTop = NSMaxY([[NSScreen screens][0] frame]);
        [win setFrameTopLeftPoint:NSMakePoint(x, screenTop - y)];
      }
    }
  }
#endif
  delete op;
}

// --- file drag & drop (macOS) -------------------------------------------------
// HTML5 drop events expose File objects but never filesystem paths, and a
// page that doesn't call preventDefault() on dragover rejects file drags
// entirely. Swizzle WKWebView's NSDraggingDestination methods so file drags
// are always accepted and dropped file paths are reported over the socket
// (`DROP <json-array>`). The original implementations still run, so in-page
// HTML5 drag & drop keeps working for pages that use it.

#ifdef __APPLE__
typedef BOOL (*DragPerformIMP)(id, SEL, id);
typedef NSUInteger (*DragUpdateIMP)(id, SEL, id);
static DragPerformIMP g_orig_performDragOperation = nullptr;
static DragUpdateIMP g_orig_draggingEntered = nullptr;
static DragUpdateIMP g_orig_draggingUpdated = nullptr;

static NSArray *drag_file_urls(id info) {
  NSPasteboard *pb = [(id<NSDraggingInfo>)info draggingPasteboard];
  return [pb readObjectsForClasses:@[ [NSURL class] ]
                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
}

static NSUInteger tiny_draggingEntered(id self, SEL cmd, id info) {
  NSUInteger op = g_orig_draggingEntered ? g_orig_draggingEntered(self, cmd, info)
                                         : NSDragOperationNone;
  if (op == NSDragOperationNone && drag_file_urls(info).count > 0)
    op = NSDragOperationCopy;
  return op;
}

static NSUInteger tiny_draggingUpdated(id self, SEL cmd, id info) {
  NSUInteger op = g_orig_draggingUpdated ? g_orig_draggingUpdated(self, cmd, info)
                                         : NSDragOperationNone;
  if (op == NSDragOperationNone && drag_file_urls(info).count > 0)
    op = NSDragOperationCopy;
  return op;
}

static BOOL tiny_performDragOperation(id self, SEL cmd, id info) {
  @autoreleasepool {
    NSArray *urls = drag_file_urls(info);
    if (urls.count > 0) {
      std::string json = "[";
      for (NSUInteger i = 0; i < urls.count; i++) {
        if (i) json += ",";
        json += json_escape([[(NSURL *)urls[i] path] UTF8String]);
      }
      json += "]";
      sock_write_line("DROP " + json);
    }
    BOOL handled = g_orig_performDragOperation
                       ? g_orig_performDragOperation(self, cmd, info)
                       : NO;
    // The page rejected the HTML5 drop but we delivered the paths: report
    // success so the drag doesn't animate back to Finder.
    return handled || urls.count > 0;
  }
}

// Replace-or-add: patches the direct implementation when the class has one,
// otherwise installs an override that falls back to the inherited IMP.
static IMP swizzle(Class cls, SEL sel, IMP imp, const char *types) {
  Method m = class_getInstanceMethod(cls, sel);
  IMP orig = m ? method_getImplementation(m) : nullptr;
  if (!class_addMethod(cls, sel, imp, types))
    method_setImplementation(m, imp);
  return orig;
}

static void install_drop_hook() {
  Class cls = [WKWebView class];
  g_orig_draggingEntered = (DragUpdateIMP)swizzle(
      cls, @selector(draggingEntered:), (IMP)tiny_draggingEntered, "L@:@");
  g_orig_draggingUpdated = (DragUpdateIMP)swizzle(
      cls, @selector(draggingUpdated:), (IMP)tiny_draggingUpdated, "L@:@");
  g_orig_performDragOperation = (DragPerformIMP)swizzle(
      cls, @selector(performDragOperation:), (IMP)tiny_performDragOperation,
      "c@:@");
}
#endif

// --- global hotkeys (macOS) ----------------------------------------------------
// HKREG <id>\t<combo> registers a system-wide hotkey (combo like
// "cmd+shift+k"); presses arrive as `HOTKEY <id>`. HKUNREG <id> removes it.

struct HotkeyReq {
  std::string id, combo; // combo empty = unregister
};

#ifdef __APPLE__
static std::map<std::string, EventHotKeyRef> g_hotkeys;
static std::map<UInt32, std::string> g_hotkey_ids;
static UInt32 g_hotkey_seq = 1;

static int keycode_for(const std::string &k) {
  static const std::map<std::string, int> m = {
    {"a",0},{"s",1},{"d",2},{"f",3},{"h",4},{"g",5},{"z",6},{"x",7},{"c",8},
    {"v",9},{"b",11},{"q",12},{"w",13},{"e",14},{"r",15},{"y",16},{"t",17},
    {"1",18},{"2",19},{"3",20},{"4",21},{"6",22},{"5",23},{"9",25},{"7",26},
    {"8",28},{"0",29},{"o",31},{"u",32},{"i",34},{"p",35},{"l",37},{"j",38},
    {"k",40},{"n",45},{"m",46},{"space",49},{"tab",48},{"return",36},
    {"enter",36},{"escape",53},{"esc",53},{"left",123},{"right",124},
    {"down",125},{"up",126},{"f1",122},{"f2",120},{"f3",99},{"f4",118},
    {"f5",96},{"f6",97},{"f7",98},{"f8",100},{"f9",101},{"f10",109},
    {"f11",103},{"f12",111},{"minus",27},{"equal",24},{"comma",43},
    {"period",47},{"slash",44},{"semicolon",41},{"quote",39},
    {"bracketleft",33},{"bracketright",30},{"backslash",42},{"grave",50},
    {"delete",51},
  };
  auto it = m.find(k);
  return it == m.end() ? -1 : it->second;
}

static OSStatus hotkey_handler(EventHandlerCallRef, EventRef event, void *) {
  EventHotKeyID hkid;
  GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, NULL,
                    sizeof(hkid), NULL, &hkid);
  auto it = g_hotkey_ids.find(hkid.id);
  if (it != g_hotkey_ids.end())
    sock_write_line("HOTKEY " + it->second);
  return noErr;
}

static void ensure_hotkey_handler() {
  static bool installed = false;
  if (installed)
    return;
  installed = true;
  EventTypeSpec spec = {kEventClassKeyboard, kEventHotKeyPressed};
  InstallEventHandler(GetApplicationEventTarget(), hotkey_handler, 1, &spec,
                      NULL, NULL);
}

static void do_hotkey(webview_t, void *arg) {
  HotkeyReq *req = static_cast<HotkeyReq *>(arg);
  // Re-registering or unregistering an existing id removes the old binding.
  auto existing = g_hotkeys.find(req->id);
  if (existing != g_hotkeys.end()) {
    UnregisterEventHotKey(existing->second);
    g_hotkeys.erase(existing);
  }
  if (!req->combo.empty()) {
    UInt32 mods = 0;
    std::string key;
    std::stringstream ss(req->combo);
    std::string part;
    while (std::getline(ss, part, '+')) {
      for (auto &c : part) c = (char)tolower(c);
      if (part == "cmd" || part == "command" || part == "meta") mods |= cmdKey;
      else if (part == "ctrl" || part == "control") mods |= controlKey;
      else if (part == "alt" || part == "opt" || part == "option") mods |= optionKey;
      else if (part == "shift") mods |= shiftKey;
      else key = part;
    }
    int code = keycode_for(key);
    if (code >= 0) {
      ensure_hotkey_handler();
      EventHotKeyID hkid = {'tnyj', g_hotkey_seq++};
      EventHotKeyRef ref = NULL;
      if (RegisterEventHotKey((UInt32)code, mods, hkid,
                              GetApplicationEventTarget(), 0, &ref) == noErr) {
        g_hotkeys[req->id] = ref;
        g_hotkey_ids[hkid.id] = req->id;
      }
    }
  }
  delete req;
}
#else
static void do_hotkey(webview_t, void *arg) { delete static_cast<HotkeyReq *>(arg); }
#endif

// --- system events (macOS) ------------------------------------------------------
// Pushed as `SYS theme dark|light` (also once at startup), `SYS sleep`,
// `SYS wake`.

#ifdef __APPLE__
static void send_theme() {
  NSAppearance *ap = [NSApp effectiveAppearance];
  NSString *best = [ap bestMatchFromAppearancesWithNames:@[
    NSAppearanceNameAqua, NSAppearanceNameDarkAqua
  ]];
  bool dark = [best isEqualToString:NSAppearanceNameDarkAqua];
  sock_write_line(std::string("SYS theme ") + (dark ? "dark" : "light"));
}

static void install_system_observers() {
  [[NSDistributedNotificationCenter defaultCenter]
      addObserverForName:@"AppleInterfaceThemeChangedNotification"
                  object:nil
                   queue:[NSOperationQueue mainQueue]
              usingBlock:^(NSNotification *) {
                // NSApp's effectiveAppearance updates a beat after the
                // notification; read it on the next runloop turn.
                dispatch_async(dispatch_get_main_queue(), ^{ send_theme(); });
              }];
  NSNotificationCenter *wsnc = [[NSWorkspace sharedWorkspace] notificationCenter];
  [wsnc addObserverForName:NSWorkspaceWillSleepNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) { sock_write_line("SYS sleep"); }];
  [wsnc addObserverForName:NSWorkspaceDidWakeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *) { sock_write_line("SYS wake"); }];
  send_theme(); // initial state
}
#endif

// --- custom context menu (macOS) -------------------------------------------------
// CTXBEGIN, ITEM/SEP lines, CTXEND replaces the webview's right-click menu
// with the declared items (clicks -> `CTX <id>`); CTXCLEAR restores WebKit's
// default menu.

static std::vector<MenuItemSpec> g_ctx_items;
static bool g_ctx_active = false;

struct CtxReq {
  std::vector<MenuItemSpec> items;
  bool active;
};

#ifdef __APPLE__
typedef void (*WillOpenMenuIMP)(id, SEL, NSMenu *, NSEvent *);
static WillOpenMenuIMP g_orig_willOpenMenu = nullptr;

static void tiny_willOpenMenu(id self, SEL cmd, NSMenu *menu, NSEvent *ev) {
  if (g_orig_willOpenMenu)
    g_orig_willOpenMenu(self, cmd, menu, ev);
  if (!g_ctx_active)
    return;
  [menu removeAllItems];
  build_menu_into(menu, g_ctx_items, @selector(ctxItemClicked:), nil);
}

// Context menus are built lazily at right-click, so state updates and reads
// go against the stored spec rather than live NSMenuItems.
static MenuItemSpec *find_ctx_spec(std::vector<MenuItemSpec> &items,
                                   const std::string &id) {
  for (MenuItemSpec &it : items) {
    if (it.id == id)
      return &it;
    if (MenuItemSpec *hit = find_ctx_spec(it.submenu, id))
      return hit;
  }
  return nullptr;
}

static void apply_ctx(webview_t, void *arg) {
  CtxReq *req = static_cast<CtxReq *>(arg);
  if (!g_menu_target)
    g_menu_target = [[TinyMenuTarget alloc] init];
  g_ctx_items = req->items;
  g_ctx_active = req->active;
  delete req;
}

static void install_ctx_hook() {
  // willOpenMenu:withEvent: is swizzled with the same replace-or-add helper
  // used for drag & drop.
  g_orig_willOpenMenu = (WillOpenMenuIMP)swizzle(
      [WKWebView class], @selector(willOpenMenu:withEvent:),
      (IMP)tiny_willOpenMenu, "v@:@@");
}
#else
static void apply_ctx(webview_t, void *arg) { delete static_cast<CtxReq *>(arg); }
#endif

// --- stateful menus: surgical updates + read-backs -------------------------------
// MENUUPD <id>\t<label>\t<checked>\t<enabled> patches a live item (empty field
// = leave unchanged; checked/enabled are ''|0|1). GET <qid> <what> answers
// with GOT <qid> <json>; what = "win" (window state) or "item:<id>".

struct MenuUpdReq {
  std::string id, label, checked, enabled;
};

struct GetReq {
  std::string qid, what;
};

#ifdef __APPLE__
static void do_menu_update(webview_t, void *arg) {
  MenuUpdReq *req = static_cast<MenuUpdReq *>(arg);
  @autoreleasepool {
    NSString *key = ns(req->id);
    NSMenuItem *mi = g_reg_menu[key] ?: g_reg_tray[key];
    if (mi) {
      if (!req->label.empty())
        mi.title = ns(req->label);
      if (!req->checked.empty())
        mi.state = req->checked == "1" ? NSControlStateValueOn
                                       : NSControlStateValueOff;
      if (!req->enabled.empty())
        mi.enabled = req->enabled == "1";
    }
    // Context menus rebuild from spec at right-click; patch the spec too.
    if (MenuItemSpec *spec = find_ctx_spec(g_ctx_items, req->id)) {
      if (!req->label.empty())
        spec->label = req->label;
      if (!req->checked.empty())
        spec->checked = req->checked == "1";
      if (!req->enabled.empty())
        spec->disabled = req->enabled == "0";
    }
  }
  delete req;
}

static void do_get(webview_t w, void *arg) {
  GetReq *req = static_cast<GetReq *>(arg);
  std::string json = "null";
  @autoreleasepool {
    if (req->what == "win") {
      NSWindow *win = (NSWindow *)webview_get_native_handle(
          w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
      if (win) {
        NSRect f = win.frame;
        // width/height are frame size — the same units setSize uses, so
        // set → get round-trips.
        NSScreen *scr = win.screen ?: [NSScreen mainScreen];
        CGFloat top = NSMaxY([[NSScreen screens][0] frame]);
        bool fs = (win.styleMask & NSWindowStyleMaskFullScreen) != 0;
        char buf[512];
        std::snprintf(
            buf, sizeof(buf),
            "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
            "\"fullscreen\":%s,\"minimized\":%s,\"visible\":%s,\"focused\":%s,"
            "\"alwaysOnTop\":%s,\"resizable\":%s,"
            "\"screen\":{\"width\":%d,\"height\":%d,\"scale\":%.2f}}",
            (int)f.origin.x, (int)(top - NSMaxY(f)), (int)f.size.width,
            (int)f.size.height, fs ? "true" : "false",
            win.miniaturized ? "true" : "false", win.visible ? "true" : "false",
            win.keyWindow ? "true" : "false",
            win.level != NSNormalWindowLevel ? "true" : "false",
            (win.styleMask & NSWindowStyleMaskResizable) ? "true" : "false",
            (int)scr.frame.size.width, (int)scr.frame.size.height,
            (double)scr.backingScaleFactor);
        json = buf;
      }
    } else if (req->what.rfind("item:", 0) == 0) {
      std::string id = req->what.substr(5);
      NSString *key = ns(id);
      NSMenuItem *mi = g_reg_menu[key] ?: g_reg_tray[key];
      if (mi) {
        json = std::string("{\"exists\":true,\"label\":") +
               json_escape([mi.title UTF8String]) +
               ",\"checked\":" +
               (mi.state == NSControlStateValueOn ? "true" : "false") +
               ",\"enabled\":" + (mi.enabled ? "true" : "false") + "}";
      } else if (MenuItemSpec *spec = find_ctx_spec(g_ctx_items, id)) {
        json = std::string("{\"exists\":true,\"label\":") +
               json_escape(spec->label) +
               ",\"checked\":" + (spec->checked ? "true" : "false") +
               ",\"enabled\":" + (spec->disabled ? "false" : "true") + "}";
      } else {
        json = "{\"exists\":false}";
      }
    }
  }
  sock_write_line("GOT " + req->qid + " " + json);
  delete req;
}
#else
static void do_menu_update(webview_t, void *arg) { delete static_cast<MenuUpdReq *>(arg); }
static void do_get(webview_t, void *arg) {
  GetReq *req = static_cast<GetReq *>(arg);
  sock_write_line("GOT " + req->qid + " null");
  delete req;
}
#endif

// --- print (macOS) ---------------------------------------------------------------

static void do_print(webview_t w, void *) {
#ifdef __APPLE__
  @autoreleasepool {
    WKWebView *wv = (WKWebView *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
    NSWindow *win = (NSWindow *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
    if (!wv || !win)
      return;
    NSPrintInfo *pi = [NSPrintInfo sharedPrintInfo];
    pi.horizontallyCentered = YES;
    pi.verticallyCentered = NO;
    NSPrintOperation *op = [wv printOperationWithPrintInfo:pi];
    op.showsPrintPanel = YES;
    op.showsProgressPanel = YES;
    // The print view needs a nonzero frame or WebKit renders blank pages.
    op.view.frame = wv.bounds;
    [op runOperationModalForWindow:win
                          delegate:nil
                    didRunSelector:NULL
                       contextInfo:NULL];
  }
#endif
}

// --- WebGPU (macOS) ----------------------------------------------------------
// WKWebView gates WebGPU behind a WebKit feature flag (still "experimental"
// as of macOS 15; no public API to enable it). Flip it through the private
// WKPreferences feature list before any content loads. Preference changes
// propagate to the web process, so doing this right after webview_create and
// before the first navigate/set_html is sufficient. Harmless no-op on OS
// versions where the flag or the private API doesn't exist.

#ifdef __APPLE__
static void enable_webgpu(webview_t w) {
  WKWebView *wv = (WKWebView *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (!wv)
    return;
  WKPreferences *prefs = wv.configuration.preferences;
  // Newer WebKit exposes +_features / -_setEnabled:forFeature:; older builds
  // use the _experimentalFeatures spelling.
  struct { const char *list, *set; } apis[] = {
      {"_features", "_setEnabled:forFeature:"},
      {"_experimentalFeatures", "_setEnabled:forExperimentalFeature:"},
  };
  for (auto &api : apis) {
    SEL list_sel = sel_registerName(api.list);
    SEL set_sel = sel_registerName(api.set);
    if (![(id)[WKPreferences class] respondsToSelector:list_sel] ||
        ![prefs respondsToSelector:set_sel])
      continue;
    NSArray *features =
        ((NSArray * (*)(id, SEL))objc_msgSend)((id)[WKPreferences class], list_sel);
    for (id feature in features) {
      NSString *key =
          ((NSString * (*)(id, SEL))objc_msgSend)(feature, sel_registerName("key"));
      if ([key isEqualToString:@"WebGPUEnabled"]) {
        ((void (*)(id, SEL, BOOL, id))objc_msgSend)(prefs, set_sel, YES, feature);
        return;
      }
    }
  }
}
#endif

// -----------------------------------------------------------------------------

// Page called window.__invoke(...): forward to the backend.
static void on_invoke(const char *id, const char *req, void *) {
  sock_write_line(std::string("CALL ") + id + " " + req);
}

static void sock_read_loop() {
  // Bundle mode: wait for the spawned backend to attach before reading.
  if (g_listen_fd >= 0) {
    int fd = accept(g_listen_fd, nullptr, nullptr);
    if (fd < 0) {
      webview_dispatch(g_w, do_terminate, nullptr);
      return;
    }
    sock_set_connected(fd);
  }
  std::string buf;
  char chunk[4096];
  std::vector<MenuSpec> pending_menus;
  bool in_menu_block = false;
  TraySpec pending_tray;
  bool in_tray_block = false;
  bool in_ctx_block = false;
  // Items build through a stack so SUB…SUBEND nests; root is build_stack[0].
  std::vector<std::vector<MenuItemSpec>> build_stack(1);
  std::vector<MenuItemSpec> sub_parents;
  auto collapse_subs = [&]() {
    while (build_stack.size() > 1) {
      MenuItemSpec parent = sub_parents.back();
      sub_parents.pop_back();
      parent.submenu = build_stack.back();
      build_stack.pop_back();
      build_stack.back().push_back(parent);
    }
  };
  auto take_root = [&]() {
    collapse_subs();
    std::vector<MenuItemSpec> root = build_stack[0];
    build_stack.assign(1, {});
    return root;
  };
  auto flush_root = [&]() {
    if (!pending_menus.empty())
      pending_menus.back().items = take_root();
    else
      build_stack.assign(1, {});
  };
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
        collapse_subs();
        build_stack.assign(1, {});
        in_menu_block = true;
      } else if (in_menu_block && line.rfind("MENU ", 0) == 0) {
        flush_root(); // previous menu's items (if any)
        pending_menus.push_back(MenuSpec{line.substr(5), {}});
        build_stack.assign(1, {});
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("ITEM ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(5));
        MenuItemSpec it;
        it.id = p.size() > 0 ? p[0] : "";
        it.label = p.size() > 1 ? p[1] : it.id;
        it.key = p.size() > 2 ? p[2] : "";
        if (p.size() > 3) { // flags: c=checked, d=disabled
          it.checked = p[3].find('c') != std::string::npos;
          it.disabled = p[3].find('d') != std::string::npos;
        }
        build_stack.back().push_back(it);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) && line == "SEP") {
        MenuItemSpec sep;
        sep.separator = true;
        build_stack.back().push_back(sep);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("SUB ", 0) == 0) {
        // SUB <id>\t<label>: following ITEMs nest until SUBEND.
        std::vector<std::string> p = split_tabs(line.substr(4));
        MenuItemSpec parent;
        parent.id = p.size() > 0 ? p[0] : "";
        parent.label = p.size() > 1 ? p[1] : parent.id;
        sub_parents.push_back(parent);
        build_stack.push_back({});
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line == "SUBEND") {
        if (build_stack.size() > 1) {
          MenuItemSpec parent = sub_parents.back();
          sub_parents.pop_back();
          parent.submenu = build_stack.back();
          build_stack.pop_back();
          build_stack.back().push_back(parent);
        }
      } else if (line == "MENUEND") {
        flush_root();
        in_menu_block = false;
        webview_dispatch(g_w, apply_menus,
                         new std::vector<MenuSpec>(pending_menus));
      } else if (line.rfind("TRAYBEGIN", 0) == 0) {
        pending_tray = TraySpec{};
        std::vector<std::string> p =
            line.size() > 10 ? split_tabs(line.substr(10)) : std::vector<std::string>{};
        pending_tray.title = p.size() > 0 ? p[0] : "";
        pending_tray.icon = p.size() > 1 ? p[1] : "";
        pending_tray.template_icon = !(p.size() > 2 && p[2] == "0");
        pending_tray.tooltip = p.size() > 3 ? p[3] : "";
        collapse_subs();
        build_stack.assign(1, {});
        in_tray_block = true;
      } else if (line == "TRAYEND") {
        in_tray_block = false;
        pending_tray.items = take_root();
        webview_dispatch(g_w, apply_tray, new TraySpec(pending_tray));
      } else if (line == "TRAYREMOVE") {
        TraySpec *rm = new TraySpec{};
        rm->remove = true;
        webview_dispatch(g_w, apply_tray, rm);
      } else if (line.rfind("WINOP ", 0) == 0) {
        webview_dispatch(g_w, do_winop, new std::string(line.substr(6)));
      } else if (line == "CTXBEGIN") {
        collapse_subs();
        build_stack.assign(1, {});
        in_ctx_block = true;
      } else if (line == "CTXEND") {
        in_ctx_block = false;
        webview_dispatch(g_w, apply_ctx, new CtxReq{take_root(), true});
      } else if (line == "CTXCLEAR") {
        webview_dispatch(g_w, apply_ctx, new CtxReq{{}, false});
      } else if (line.rfind("HKREG ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(6));
        if (p.size() >= 2)
          webview_dispatch(g_w, do_hotkey, new HotkeyReq{p[0], p[1]});
      } else if (line.rfind("HKUNREG ", 0) == 0) {
        webview_dispatch(g_w, do_hotkey, new HotkeyReq{line.substr(8), ""});
      } else if (line.rfind("MENUUPD ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(8));
        MenuUpdReq *req = new MenuUpdReq;
        req->id = p.size() > 0 ? p[0] : "";
        req->label = p.size() > 1 ? p[1] : "";
        req->checked = p.size() > 2 ? p[2] : "";
        req->enabled = p.size() > 3 ? p[3] : "";
        webview_dispatch(g_w, do_menu_update, req);
      } else if (line.rfind("GET ", 0) == 0) {
        size_t sp1 = line.find(' ', 4);
        if (sp1 == std::string::npos)
          continue;
        webview_dispatch(g_w, do_get,
                         new GetReq{line.substr(4, sp1 - 4), line.substr(sp1 + 1)});
      } else if (line == "PRINT") {
        webview_dispatch(g_w, do_print, nullptr);
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

// --- bundle mode ---------------------------------------------------------------
// As the .app's CFBundleExecutable the launcher starts with no arguments: it
// derives everything from the bundle, LISTENS on a private socket, and spawns
// the backend (Contents/MacOS/tjs run Resources/app/entry.js) pointed at it
// via TINYJS_SOCKET. Being the LaunchServices-registered GUI process is what
// makes deep links, file opens, and single-instancing work: a second `open`
// activates this process instead of launching another copy.

#ifdef __APPLE__
// Deep links + file opens: NSApplication routes kAEGetURL / kAEOpenDocuments
// to the app delegate's application:openURLs: / application:openFiles: —
// checked dynamically at event time, so adding the methods to the webview
// library's delegate class works no matter when finishLaunching ran. Events
// arriving before the backend attaches (cold starts) are buffered by
// sock_write_line, so launch URLs/files are never lost.
// Deep links go out as OPENURL <url>; file opens (which macOS may deliver
// through the same openURLs: route as file:// URLs) as OPENFILES <json-paths>.
static void send_open_urls(NSArray *urls) {
  std::string files = "[";
  bool any_file = false;
  for (NSURL *u in urls) {
    if ([u isFileURL]) {
      if (any_file)
        files += ",";
      any_file = true;
      files += json_escape([[u path] UTF8String]);
    } else {
      sock_write_line(std::string("OPENURL ") + [[u absoluteString] UTF8String]);
    }
  }
  files += "]";
  if (any_file)
    sock_write_line("OPENFILES " + files);
}

static void tiny_openURLs(id, SEL, id /*app*/, NSArray *urls) {
  send_open_urls(urls);
}

static void tiny_openFiles(id, SEL, id app, NSArray *files) {
  std::string json = "[";
  for (NSUInteger i = 0; i < files.count; i++) {
    if (i)
      json += ",";
    json += json_escape([(NSString *)files[i] UTF8String]);
  }
  json += "]";
  sock_write_line("OPENFILES " + json);
  [(NSApplication *)app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

// Launch Apple Events can be dispatched while webview_create pumps the event
// loop — before the app delegate exists. Register plain NSAppleEventManager
// handlers FIRST (no delegate needed) so cold-start URLs/files are caught;
// NSApplication may re-route later events through the delegate methods
// installed by install_open_handlers, which produce identical wire lines.
// Swizzled -[NSApplication setDelegate:]: graft our open handlers onto any
// delegate the moment it is installed, so NSApplication's capability cache
// (checked when launch Apple Events dispatch, possibly inside webview_create)
// sees them from the start.
typedef void (*SetDelegateIMP)(id, SEL, id);
static SetDelegateIMP g_orig_setDelegate = nullptr;

static void tiny_setDelegate(id self, SEL cmd, id delegate) {
  if (delegate) {
    class_addMethod(object_getClass(delegate), @selector(application:openURLs:),
                    (IMP)tiny_openURLs, "v@:@@");
    class_addMethod(object_getClass(delegate), @selector(application:openFiles:),
                    (IMP)tiny_openFiles, "v@:@@");
  }
  if (g_orig_setDelegate)
    g_orig_setDelegate(self, cmd, delegate);
}

static void install_early_open_handlers() {
  if (!g_menu_target)
    g_menu_target = [[TinyMenuTarget alloc] init];
  g_orig_setDelegate = (SetDelegateIMP)swizzle(
      [NSApplication class], @selector(setDelegate:), (IMP)tiny_setDelegate,
      "v@:@");
  NSAppleEventManager *aem = [NSAppleEventManager sharedAppleEventManager];
  [aem setEventHandler:g_menu_target
           andSelector:@selector(handleGetURL:withReply:)
         forEventClass:kInternetEventClass
            andEventID:kAEGetURL];
  [aem setEventHandler:g_menu_target
           andSelector:@selector(handleOpenDocs:withReply:)
         forEventClass:kCoreEventClass
            andEventID:kAEOpenDocuments];
}

static void install_open_handlers() {
  id delegate = [NSApp delegate];
  if (!delegate)
    return;
  class_addMethod(object_getClass(delegate), @selector(application:openURLs:),
                  (IMP)tiny_openURLs, "v@:@@");
  class_addMethod(object_getClass(delegate), @selector(application:openFiles:),
                  (IMP)tiny_openFiles, "v@:@@");
  // NSApplication caches delegate capabilities at setDelegate: time; re-set
  // it so AppKit notices the methods we just added.
  [NSApp setDelegate:nil];
  [NSApp setDelegate:delegate];
}

static bool bundle_mode_setup(std::string &target, std::string &title,
                              std::string &size_s, std::string &version) {
  NSBundle *mb = [NSBundle mainBundle];
  NSString *bp = [mb bundlePath];
  NSString *frontend = [bp
      stringByAppendingPathComponent:@"Contents/Resources/app/frontend/index.html"];
  if (![bp hasSuffix:@".app"] ||
      ![[NSFileManager defaultManager] fileExistsAtPath:frontend])
    return false;

  target = [frontend UTF8String];
  NSString *name = [mb objectForInfoDictionaryKey:@"CFBundleName"];
  title = name ? [name UTF8String] : "tinyjs";
  NSString *ver = [mb objectForInfoDictionaryKey:@"CFBundleVersion"];
  version = ver ? [ver UTF8String] : "0.0.0";
  NSString *sz = [mb objectForInfoDictionaryKey:@"TinyjsWindowSize"];
  size_s = sz ? [sz UTF8String] : "960x640";

  // Private 0700 socket dir under the user temp dir (short enough for
  // sun_path's ~104-byte cap).
  std::string tmpl = std::string([NSTemporaryDirectory() UTF8String]) + "tinyjs-XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!mkdtemp(buf.data()))
    return false;
  g_sock_dir = buf.data();
  std::string sock_path = g_sock_dir + "/app.sock";

  g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
  if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
      listen(g_listen_fd, 1) != 0) {
    std::fprintf(stderr, "launcher: cannot listen on %s: %s\n",
                 sock_path.c_str(), std::strerror(errno));
    return false;
  }

  // Spawn the backend; it attaches to our socket (bridge attach mode).
  setenv("TINYJS_SOCKET", sock_path.c_str(), 1);
  std::string exe = [[mb executablePath] UTF8String];
  std::string exe_dir = exe.substr(0, exe.find_last_of('/'));
  std::string tjs = exe_dir + "/tjs";
  std::string entry =
      std::string([bp UTF8String]) + "/Contents/Resources/app/entry.js";
  const char *sargv[] = {tjs.c_str(), "run", entry.c_str(), nullptr};
  pid_t pid;
  if (posix_spawn(&pid, tjs.c_str(), nullptr, nullptr,
                  const_cast<char *const *>(sargv), environ) != 0) {
    std::fprintf(stderr, "launcher: cannot spawn backend: %s\n",
                 std::strerror(errno));
    return false;
  }
  return true;
}
#endif

int main(int argc, char *argv[]) {
  std::string target, sock_path, title = "tinyjs", size_s = "960x640";
  bool bundle_mode = false;

#ifdef __APPLE__
  if (argc < 3)
    bundle_mode = bundle_mode_setup(target, title, size_s, g_app_version);
#endif

  if (!bundle_mode) {
    if (argc < 3) {
      std::fprintf(stderr,
                   "usage: %s <html-file-or-url> <socket-path> [title] [WxH]\n",
                   argv[0]);
      return 1;
    }
    target = argv[1];
    sock_path = argv[2];
    if (argc > 3)
      title = argv[3];
    if (argc > 4)
      size_s = argv[4];
    if (argc > 5)
      g_app_version = argv[5];
  }
  g_app_name = title;
  int width = 960, height = 640;
  std::sscanf(size_s.c_str(), "%dx%d", &width, &height);

  if (!bundle_mode) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
      std::fprintf(stderr, "launcher: cannot connect to %s: %s\n",
                   sock_path.c_str(), std::strerror(errno));
      return 1;
    }
    g_sock = fd;
  }

#ifdef __APPLE__
  // Must precede webview_create: launch Apple Events (cold-start deep links /
  // file opens) can be dispatched during its internal event pumping.
  install_early_open_handlers();
#endif

  g_w = webview_create(1 /* debug: enables devtools */, nullptr);
  if (!g_w) {
    std::fprintf(stderr, "launcher: failed to create webview\n");
    return 1;
  }

#ifdef __APPLE__
  enable_webgpu(g_w);
  install_close_hook(g_w);
  install_drop_hook();
  install_ctx_hook();
  install_system_observers();
  install_open_handlers();
#endif

  webview_set_title(g_w, title.c_str());
  webview_set_size(g_w, width, height, WEBVIEW_HINT_NONE);
  webview_bind(g_w, "__invoke", on_invoke, nullptr);

  // Default menu bar (About/Quit + Edit); custom menus replace it via MENUEND.
  apply_menus(g_w, new std::vector<MenuSpec>());

  if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
    webview_navigate(g_w, target.c_str());
  } else {
    // file:// document (secure context; see load_html_file).
    g_html_path = target;
    if (!load_html_file(g_w, target)) {
      std::fprintf(stderr, "launcher: cannot read %s\n", target.c_str());
      return 1;
    }
  }

  std::thread(sock_read_loop).detach();

  webview_run(g_w);
  webview_destroy(g_w);
  if (!g_sock_dir.empty()) {
    unlink((g_sock_dir + "/app.sock").c_str());
    rmdir(g_sock_dir.c_str());
  }
  // The socket thread may still be blocked in read; exit hard.
  _exit(0);
}
