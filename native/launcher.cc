// tinyjs window launcher.
//
// A dumb window process: it renders HTML in a native webview and shuttles
// JSON-RPC messages between the page and the backend over a Unix domain
// socket. No TCP, no ports; the socket lives in a private temp directory.
// The backend (txiki.js) creates the socket, then spawns this as a child.
//
// Protocol (newline-delimited; payloads are JSON so never contain raw \n).
// Call/window routing: page-call ids are "<winid>:<seq>"; window-targeted
// commands use CMD@<winid> (no suffix = main); EVAL@* broadcasts. Windows:
//   backend -> launcher:  WINOPEN <id>\t<page>\t<title>\t<WxH> (create/focus)
//                         WINCLOSE <id>
//   launcher -> backend:  WINCLOSED <id>
//
//   launcher -> backend:  CALL <winid>:<seq> <json-args-array>
//   backend -> launcher:  RET <id> <status> <json>   resolve/reject a call
//                         EVAL <js>                  run JS in the page
//                                                    (js is esc()-escaped;
//                                                    wire_unescape restores it)
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
//                                                    win | item:<id> | mouse |
//                                                    clipboard[:count]
//                         TRAYBEGIN <title>\t<icon>\t<template01>\t<tooltip>\t<primary01> /
//                         ITEM / SEP / TRAYEND       declare a menu bar status
//                                                    item (applied on TRAYEND;
//                                                    icon: png path or
//                                                    sf:<sfsymbol-name>;
//                                                    primary=1: left click
//                                                    sends TRAYCLICK, the menu
//                                                    opens on right-click)
//                         TRAYREMOVE                 remove the status item
//                         WINOP <op> [args]          window ops: hide (main:
//                                                    NSApp hide — focus returns
//                                                    to the previous app),
//                                                    show [0|1] (0 = don't
//                                                    steal focus), center,
//                                                    minimize,
//                                                    fullscreen [0|1], restore,
//                                                    ontop 0|1,
//                                                    resizable 0|1, pos <x> <y>,
//                                                    dock 0|1 (Dock icon),
//                                                    hideonclose 0|1
//                         CTXBEGIN / ITEM / SEP /
//                         CTXEND / CTXCLEAR          replace or restore the
//                                                    right-click menu
//                         CTXSUPPRESS <0|1>          suppress WebKit's default
//                                                    right-click menu
//                         HKREG <id>\t<combo> /
//                         HKUNREG <id>               global hotkeys
//                         AUDIOTAP <qid> <scope>\t<excludeSelf>\t<interval> /
//                         AUDIOTAP STOP              read output PCM (reply GOT)
//                         NOTIFY <id>\t<title>\t<body>\t<subtitle>\t<snd01>
//                                       [\t<actions-json>]
//                                                    notification (bundle mode:
//                                                    Notification Center);
//                                                    actions-json = [{id,
//                                                    title, reply?,
//                                                    placeholder?, destructive?}]
//                                                    → buttons / a reply field
//                         NOWPLAYING <json> | clear  set the Now Playing info
//                                                    (title/artist/album/
//                                                    duration/elapsed/playing)
//                                                    + arm the media keys
//                         SAY <qid> <text>\t<voice>\t<rate>
//                                                    speak via AVSpeech; GOT
//                                                    {ok} when done
//                         SAYSTOP                    stop speaking
//                         VOICES <qid>               list installed voices;
//                                                    GOT {ok, voices}
//                         WINCTRL <qid> <pid>\t<x>\t<y>\t<w>\t<h>
//                                                    move/resize another app's
//                                                    frontmost window
//                                                    (Accessibility); GOT {ok,
//                                                    error}. Reads: GET
//                                                    selectedtext, otherwindows,
//                                                    traypos.
//                         RECORD start <qid> <display>\t<path>
//                         RECORD stop <qid>          record a display to an
//                                                    .mp4 (SCStream →
//                                                    AVAssetWriter; macOS 14 +
//                                                    the 'screen' permission);
//                                                    start answers GOT {ok,
//                                                    error} once capturing,
//                                                    stop answers GOT {ok,
//                                                    path, duration, error}
//                         CHROME <frame>\t<traffic>\t<transp>\t<vibrancy>
//                                                    window chrome ('' = keep;
//                                                    0|1 flags; vibrancy =
//                                                    material name | none)
//                         DRAGWIN                    start a native window drag
//                                                    (page drag regions)
//                         DRAGOUT[@win] <image>\t<path>… start dragging real
//                                                    files OUT of the window
//                                                    (from a page mousedown;
//                                                    image: optional drag-image
//                                                    png, '' = file icons)
//                         CLIPWRITE <text>\t<html>\t<image>\t<color>\t<path>…
//                                                    write the clipboard (fields
//                                                    wire-escaped: \n \t \r \\;
//                                                    empty = skip; image = png
//                                                    path or base64/data-url)
//                         CLIPWATCH <ms>             poll the clipboard change
//                                                    count every <ms> (0 = stop)
//                         KEYSTROKE <qid> <combo>    post a CGEvent keystroke
//                                                    (e.g. cmd+v); answers
//                                                    GOT <qid> {ok, trusted}
//                         PERMCHK <qid> <name> /
//                         PERMREQ <qid> <name>       check/request a TCC
//                                                    permission (accessibility,
//                                                    screen, notifications,
//                                                    microphone, camera,
//                                                    automation[:<bundle-id>]);
//                                                    answers GOT <qid> {status}
//                         SHELL <qid> <op>\t<target> open a URL/path with the
//                                                    default app (open), reveal
//                                                    in Finder (reveal), or move
//                                                    to Trash (trash); answers
//                                                    GOT <qid> {ok, error}
//                         LOGIN <qid> get|set 0|1    launch-at-login status /
//                                                    register (SMAppService —
//                                                    bundle mode + macOS 13;
//                                                    else "unsupported");
//                                                    answers GOT <qid>
//                                                    {status, ok, error}
//                         BADGE <text>               Dock badge ('' clears)
//                         BOUNCE <critical01>        bounce the Dock icon
//                         POWER <qid> on\t<display01>\t<reason> / off
//                                                    prevent/allow idle sleep
//                                                    (IOPMAssertion — dies
//                                                    with the process, unlike
//                                                    a spawned caffeinate);
//                                                    answers GOT <qid>
//                                                    {ok, active}
//                         SOUND <qid> <target>       beep ('' ), a system
//                                                    sound name ('Ping'), or
//                                                    an audio file path;
//                                                    answers GOT <qid> {ok}
//                         SHARE[@win] <x>\t<y>\t<text>\t<url>\t<path>…
//                                                    native share sheet
//                                                    anchored at page coords
//                         QUICKLOOK [<path>\t…]      Quick Look panel for the
//                                                    file(s); bare = close
//                         PICKCOLOR <qid>            system eyedropper; GOT
//                                                    {ok, color '#rrggbb' |
//                                                    null on cancel}
//                         OCR <qid> <image-path>     on-device Vision OCR;
//                                                    GOT {ok, text, blocks}
//                         THUMB <qid> <path>\t<size> thumbnail png for ANY
//                                                    file type; GOT {ok,
//                                                    path, width, height}
//                         SECRET <qid> get|set|del\t<service>\t<account>[\t<value>]
//                                                    Keychain generic
//                                                    password; GOT {ok,
//                                                    value|null}
//                         AUTH <qid> <reason>        Touch ID / password
//                                                    sheet; GOT {ok, error}
//                         OSA <qid> <source>         run AppleScript
//                                                    in-process (no
//                                                    osascript); GOT {ok,
//                                                    result, error}
//                         CAPTURE <qid> <displayId>  screenshot a display
//                                                    (0 = primary; needs the
//                                                    'screen' permission +
//                                                    macOS 14); answers GOT
//                                                    <qid> {ok, path (png),
//                                                    width, height, error}
//                         PRINT                      native print panel
//                         PDF <qid> <path>           render the page to a PDF;
//                                                    GOT {ok, path, error}
//                         HAPTIC <pattern>           trackpad haptic feedback
//                                                    (generic|alignment|level)
//                         DOCKICON <path>            set the Dock icon from a
//                                                    png ('' = reset)
//                         SPOTLIGHT <qid> <query>    find files by content
//                                                    (NSMetadataQuery); GOT
//                                                    {ok, paths}
//                                                    Reads: GET battery, wifi.
//                         AI available <qid> /
//                         AI generate <qid> <prompt>\t<instructions>
//                                                    on-device LLM
//                                                    (FoundationModels; only
//                                                    when built TINYJS_AI=1 on
//                                                    macOS 26); GOT {ok, text |
//                                                    status, error}
//                         QUIT                       close the window
//   launcher -> backend:  MENU <id>                  a custom menu item was
//                                                    clicked
//                         CTX <id>                   a context menu item was
//                                                    clicked
//                         HOTKEY <id>                a global hotkey fired
//                         AUDIOTAP <b64>\t<sr>\t<ch>\t<frames>\t<t>
//                                                    a tap PCM chunk
//                         SYS theme dark|light /
//                         SYS sleep / SYS wake       system events (theme also
//                                                    sent once at startup)
//                         TRAY <id>                  a tray menu item was
//                                                    clicked
//                         TRAYCLICK                  the tray icon itself was
//                                                    clicked (no menu set, or
//                                                    left click in primary-
//                                                    action mode)
//                         DROP <json-paths>          files dragged onto the
//                                                    window (real paths)
//                         GOT <qid> <json>           read-back answer
//                         CLIPCHANGE <count> <self01> the clipboard changed
//                                                    (self=1: our own CLIPWRITE)
//                         NOTIFYCLICK <id>           a notification banner was
//                                                    clicked
//                         NOTIFYACTION <id>\t<action>\t<reply>
//                                                    an action button / reply
//                                                    field on a notification
//                         MEDIAKEY <name>[\t<secs>]  a media key / Control
//                                                    Center transport fired
//                                                    (play/pause/toggle/next/
//                                                    previous/seek)
//
// A default app menu (About + Quit) is always present; About shows the
// standard panel with the app name, version, and a tinyjs credit.
//
// Built as Objective-C++ on macOS (needs AppKit for NSOpenPanel/NSSavePanel).
//
// Usage: launcher <html-file-or-url> <socket-path> [title] [WxH] [version]

#include "webview.h"

#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <LocalAuthentication/LocalAuthentication.h> // Touch ID (AUTH)
#import <MediaPlayer/MediaPlayer.h>  // Now Playing + media keys
#import <Quartz/Quartz.h>             // QLPreviewPanel (Quick Look)
#import <QuickLookThumbnailing/QuickLookThumbnailing.h> // THUMB
#import <Security/Security.h>         // Keychain (SECRET)
#import <Vision/Vision.h>             // on-device OCR
#import <ScreenCaptureKit/ScreenCaptureKit.h> // weak-linked; macOS 14+ used
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WebKit.h>
#import <CoreWLAN/CoreWLAN.h> // Wi-Fi info
#import <CoreAudio/CoreAudio.h>              // process taps (tiny.audioTap)
#import <CoreAudio/AudioHardwareTapping.h>   // AudioHardwareCreateProcessTap (14.2+)
#import <CoreAudio/CATapDescription.h>       // CATapDescription (14.2+)
#import <AudioToolbox/AudioToolbox.h>        // AudioDeviceCreateIOProcIDWithBlock
#include <Carbon/Carbon.h> // RegisterEventHotKey (global hotkeys)
#include <IOKit/pwr_mgt/IOPMLib.h> // IOPMAssertion (prevent sleep)
#include <IOKit/ps/IOPowerSources.h> // battery
#include <IOKit/ps/IOPSKeys.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include "tiny_client.h" // generated from runtime/tiny.js (gen-client.sh)
#endif

#include <map>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath> // lrintf (audioTap float->int16)
#include <ctime> // clock_gettime_nsec_np (audioTap chunk timestamps)
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
static bool g_bundle_mode = false; // launcher IS the .app executable (attach mode)
#ifdef __APPLE__
// Secondary windows (the main window lives in the webview library).
struct TinyWindow {
  NSWindow *win = nil;
  WKWebView *wv = nil;
  NSObject *handler = nil;   // TinyMsgHandler
  NSObject *wdelegate = nil; // TinyWinDelegate
  NSVisualEffectView *effect = nil;
};
static std::map<std::string, TinyWindow> g_windows;
#endif

// Window chrome state (set by CHROME, reported by GET win).
static int g_resizable_override = -1; // -1 default, 0 forced off, 1 forced on
static bool g_chrome_frameless = false;
static bool g_chrome_traffic = true;
static bool g_chrome_transparent = false;
static bool g_chrome_square = false;  // borderless: square corners, no titlebar
static std::string g_chrome_vibrancy; // empty = none
// Extra directory the page may read file:// assets from (readAccess option) —
// widens WebKit's default (the page's own folder) so <audio>/<img>/fetch can
// reach files elsewhere. Empty = default (page dir only).
static std::string g_read_access;
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
  std::string win = "main";
};

static void reapply_window_overrides(webview_t w); // defined with chrome ops
#ifdef __APPLE__
static NSWindow *window_for_id(webview_t w, const std::string &id); // below
#endif

static void do_size(webview_t w, void *arg) {
  SizeReq *s = static_cast<SizeReq *>(arg);
  if (s->win == "main") {
    webview_set_size(w, s->width, s->height, WEBVIEW_HINT_NONE);
    // The webview library's set_size rewrites the styleMask wholesale,
    // wiping frameless chrome and resizable overrides — restore them.
    reapply_window_overrides(w);
  } else {
#ifdef __APPLE__
    NSWindow *win = window_for_id(w, s->win);
    if (win)
      [win setContentSize:NSMakeSize(s->width, s->height)];
#endif
  }
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
    NSURL *access =
        g_read_access.empty()
            ? [url URLByDeletingLastPathComponent]
            : [NSURL fileURLWithPath:[NSString stringWithUTF8String:
                                          g_read_access.c_str()]
                         isDirectory:YES];
    [wv loadFileURL:url allowingReadAccessToURL:access];
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

// CLIPWRITE/DRAGOUT payload fields escape \n \t \r \\ so multi-line clipboard
// text survives the newline-delimited, tab-separated wire; this reverses
// runtime/bridge.js esc().
static std::string wire_unescape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Reverse of wire_unescape / the bridge's esc(): make text safe to carry in
// a tab-separated wire field (the bridge unescapes it on the way in).
static std::string wire_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

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

static void reply_to_call(webview_t w, const std::string &composite, int status,
                          const std::string &json); // unified RPC (below)

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
  reply_to_call(w, req->id, 0, json);
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

// Tray globals live up here because TinyMenuTarget's click handlers use them;
// they are managed in the tray section below. g_tray_menu is set only in
// primary-action mode, where a left click on the icon is an event and the
// menu is popped up on right-click.
static NSStatusItem *g_status_item = nil;
static NSMenu *g_tray_menu = nil;

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
  NSEvent *ev = [NSApp currentEvent];
  bool secondary = ev && (ev.type == NSEventTypeRightMouseUp ||
                          ([ev modifierFlags] & NSEventModifierFlagControl));
  if (secondary && g_tray_menu && g_status_item) {
    // Attach the menu just for this tracking session so plain left clicks
    // keep firing the action (a set menu swallows all button clicks).
    g_status_item.menu = g_tray_menu;
    [g_status_item.button performClick:nil];
    g_status_item.menu = nil;
    return;
  }
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
// TRAYBEGIN <title>\t<icon>\t<template01>\t<tooltip>\t<primary01>, then
// ITEM/SEP lines, then TRAYEND. icon is a png path or sf:<name> (SF Symbol).
// With menu items the icon opens the menu (clicks -> `TRAY <id>`); without,
// clicking the icon sends `TRAYCLICK`. primary=1 splits the two: left click
// sends `TRAYCLICK`, right/ctrl-click opens the menu (Caffeine-style toggles).

struct TraySpec {
  std::string title, icon, tooltip;
  bool template_icon = true;
  bool primary = false;
  std::vector<MenuItemSpec> items;
  bool remove = false;
};

#ifdef __APPLE__
static void apply_tray(webview_t, void *arg) {
  TraySpec *spec = static_cast<TraySpec *>(arg);
  @autoreleasepool {
    if (spec->remove) {
      if (g_status_item) {
        [[NSStatusBar systemStatusBar] removeStatusItem:g_status_item];
        [g_status_item release];
        g_status_item = nil;
      }
      [g_tray_menu release];
      g_tray_menu = nil;
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
    NSImage *img = nil;
    if (spec->icon.rfind("sf:", 0) == 0) {
      // SF Symbol by name — crisp and menu-bar-templating with no shipped
      // assets. Unknown names resolve to nil and fall through to the title.
      if (@available(macOS 11.0, *)) {
        img = [NSImage imageWithSystemSymbolName:ns(spec->icon.substr(3))
                        accessibilityDescription:nil];
        NSImageSymbolConfiguration *conf = [NSImageSymbolConfiguration
            configurationWithPointSize:15
                                weight:NSFontWeightRegular];
        NSImage *sized = img ? [img imageWithSymbolConfiguration:conf] : nil;
        if (sized)
          img = sized;
      }
    } else if (!spec->icon.empty()) {
      img = [[[NSImage alloc] initWithContentsOfFile:ns(spec->icon)] autorelease];
      if (img) {
        // Scale to the menu-bar height (18pt) while preserving aspect ratio, so
        // wide "pill"/wordmark icons aren't squished into a square. Derive the
        // aspect from the image's own size, which already honors DPI (pHYs on a
        // 2x PNG reports point size, not pixels). Guard against a degenerate
        // height so a malformed rep can't divide by zero.
        NSSize orig = img.size;
        CGFloat h = 18.0;
        CGFloat aspect = orig.height > 0 ? (orig.width / orig.height) : 1.0;
        [img setSize:NSMakeSize(h * aspect, h)];
      }
    }
    if (img)
      [img setTemplate:spec->template_icon ? YES : NO];
    btn.image = img;
    btn.title = ns(spec->title);
    btn.toolTip = spec->tooltip.empty() ? nil : ns(spec->tooltip);

    [g_reg_tray release];
    g_reg_tray = [[NSMutableDictionary alloc] init];
    [g_tray_menu release];
    g_tray_menu = nil;
    if (!spec->items.empty() && !spec->primary) {
      NSMenu *menu = [[[NSMenu alloc] init] autorelease];
      build_menu_into(menu, spec->items, @selector(trayItemClicked:), g_reg_tray);
      g_status_item.menu = menu;
    } else {
      if (!spec->items.empty()) {
        // Primary-action mode: hold the menu aside; trayClicked: pops it on
        // right-click and reports left clicks as TRAYCLICK.
        NSMenu *menu = [[NSMenu alloc] init];
        build_menu_into(menu, spec->items, @selector(trayItemClicked:), g_reg_tray);
        g_tray_menu = menu;
      }
      g_status_item.menu = nil;
      btn.target = g_menu_target;
      btn.action = @selector(trayClicked:);
      [btn sendActionOn:(g_tray_menu
                             ? (NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp)
                             : NSEventMaskLeftMouseUp)];
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

// Accessory activation ("activation": "accessory" — menu-bar agents): while
// set, attempts to make the app Regular are coerced back to Accessory and the
// startup order-front is swallowed, so neither a Dock icon nor a window ever
// flashes. Installed in install_accessory_mode() below; WINOP dock 1 lifts it.
static bool g_accessory = false;
static bool g_suppress_order_front = false;

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

struct WinopReq {
  std::string win, opstr;
};

static void do_winop(webview_t w, void *arg) {
  WinopReq *wr = static_cast<WinopReq *>(arg);
  std::string *op = &wr->opstr;
#ifdef __APPLE__
  @autoreleasepool {
    NSWindow *win = window_for_id(w, wr->win);
    bool is_main = wr->win == "main";
    if (!win) {
      delete wr;
      return;
    }
    if (*op == "hide") {
      // Main-window hide means "get out of the way": NSApp hide deactivates
      // the app, so macOS hands focus back to the previously active app on
      // its own (palette apps paste into it with no frontmost-pid dance).
      // Secondary windows just order out.
      if (is_main)
        [NSApp hide:nil];
      else
        [win orderOut:nil];
    } else if (*op == "show" || *op == "show 1") {
      [NSApp unhide:nil];
      [NSApp activateIgnoringOtherApps:YES];
      [win makeKeyAndOrderFront:nil];
    } else if (*op == "show 0") {
      // Show without stealing focus (overlay/HUD panels): the window appears
      // but the active app keeps keyboard focus; clicking it activates
      // normally. (True non-activating click-through needs an NSPanel with
      // NSWindowStyleMaskNonactivatingPanel — not what webview creates.)
      [NSApp unhideWithoutActivation];
      [win orderFrontRegardless];
    } else if (*op == "dock 0") {
      if (is_main)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    } else if (*op == "dock 1" && is_main) {
      g_accessory = false; // lift the accessory-mode coercion, if any
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
    } else if (*op == "zoom") {
      [win zoom:nil];
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
      if (is_main)
        g_resizable_override = 1;
      win.styleMask |= NSWindowStyleMaskResizable;
    } else if (*op == "resizable 0") {
      if (is_main)
        g_resizable_override = 0;
      win.styleMask &= ~NSWindowStyleMaskResizable;
    } else if (*op == "clickthrough 1") {
      // Mouse events pass straight through to whatever is behind the window
      // (draw-on-screen overlays, HUDs that must not intercept clicks).
      win.ignoresMouseEvents = YES;
    } else if (*op == "clickthrough 0") {
      win.ignoresMouseEvents = NO;
    } else if (op->rfind("level ", 0) == 0) {
      // Stack the window in a whole band of the screen. 'desktop' pins it
      // behind normal windows (wallpaper/pets); 'overlay' floats above
      // almost everything incl. most fullscreen apps; 'floating' = ontop;
      // 'normal' resets.
      std::string lv = op->substr(6);
      win.level = lv == "desktop"  ? kCGDesktopWindowLevel
                  : lv == "overlay" ? kCGScreenSaverWindowLevel
                  : lv == "floating" ? NSFloatingWindowLevel
                                     : NSNormalWindowLevel;
    } else if (*op == "allspaces 1") {
      // Follow the user onto every Space (and appear over fullscreen apps).
      win.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces |
                                NSWindowCollectionBehaviorFullScreenAuxiliary;
    } else if (*op == "allspaces 0") {
      win.collectionBehavior &= ~(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorFullScreenAuxiliary);
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
  delete wr;
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

// --- accessory activation (macOS) ----------------------------------------------
// Menu-bar agents come up with no Dock icon and no window, with no flash of
// either. Packaged apps get LSUIElement in the plist (the system starts them
// as an accessory already); this hook keeps the webview library from undoing
// it: its startup path forces NSApplicationActivationPolicyRegular +
// activation for non-bundled processes (dev mode) and orders the window front
// unconditionally, so both are swizzled and neutered while the flags are set.
// The order-front suppression only spans webview_create (cleared in main
// before the socket loop starts, so WINOP show works normally); the policy
// coercion stays until the backend calls setDockVisible(true).

typedef BOOL (*SetPolicyIMP)(id, SEL, NSInteger);
static SetPolicyIMP g_orig_setActivationPolicy = nullptr;
typedef void (*OrderFrontIMP)(id, SEL, id);
static OrderFrontIMP g_orig_makeKeyAndOrderFront = nullptr;

static BOOL tiny_setActivationPolicy(id self, SEL cmd, NSInteger policy) {
  if (g_accessory && policy == NSApplicationActivationPolicyRegular)
    policy = NSApplicationActivationPolicyAccessory;
  return g_orig_setActivationPolicy
             ? g_orig_setActivationPolicy(self, cmd, policy)
             : NO;
}

static void tiny_makeKeyAndOrderFront(id self, SEL cmd, id sender) {
  if (g_suppress_order_front)
    return;
  if (g_orig_makeKeyAndOrderFront)
    g_orig_makeKeyAndOrderFront(self, cmd, sender);
}

static void install_accessory_mode() {
  g_accessory = true;
  g_suppress_order_front = true;
  g_orig_setActivationPolicy = (SetPolicyIMP)swizzle(
      [NSApplication class], @selector(setActivationPolicy:),
      (IMP)tiny_setActivationPolicy, "c@:q");
  g_orig_makeKeyAndOrderFront = (OrderFrontIMP)swizzle(
      [NSWindow class], @selector(makeKeyAndOrderFront:),
      (IMP)tiny_makeKeyAndOrderFront, "v@:@");
  [[NSApplication sharedApplication]
      setActivationPolicy:NSApplicationActivationPolicyAccessory];
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

// --- clipboard (macOS) ------------------------------------------------------------
// Native NSPasteboard in this long-lived process: reads answer `GET <qid>
// clipboard[:count]`, writes arrive as CLIPWRITE (multiple file URLs flush
// reliably here — the short-lived-writer race osascript/pbcopy tricks hit
// doesn't exist), and CLIPWATCH polls changeCount in-process (~free) instead
// of apps spawning pbpaste/osascript.

struct ClipWriteReq {
  std::string text, html, image, color;
  std::vector<std::string> paths;
};

#ifdef __APPLE__
static NSTimer *g_clip_timer = nil;
static NSInteger g_clip_seen = -1;
static NSInteger g_clip_self = -1;    // changeCount produced by our own CLIPWRITE
static NSInteger g_clip_png_count = -1;
static std::string g_clip_png_path;   // materialized image, one per changeCount
static long g_clip_png_w = 0, g_clip_png_h = 0;
static NSInteger g_clip_src_count = -1;
static std::string g_clip_src_json;   // {"name":…,"bundleId":…} for that count

// Pasteboards don't record their writer, so attribute a fresh changeCount to
// the frontmost app the moment it's first noticed — exact from the watch
// timer, best-effort from a later read(). Our own CLIPWRITEs are attributed
// to this app: the palette scenario writes while some other app is frontmost.
static void clip_note_source(NSInteger count) {
  if (count == g_clip_src_count)
    return;
  NSRunningApplication *ra =
      count == g_clip_self
          ? [NSRunningApplication currentApplication]
          : [[NSWorkspace sharedWorkspace] frontmostApplication];
  std::string name = ra.localizedName ? [ra.localizedName UTF8String] : "";
  std::string bid = ra.bundleIdentifier ? [ra.bundleIdentifier UTF8String] : "";
  g_clip_src_json = "{\"name\":" + (name.empty() ? "null" : json_escape(name)) +
                    ",\"bundleId\":" + (bid.empty() ? "null" : json_escape(bid)) +
                    "}";
  g_clip_src_count = count;
}

// image field: absolute png path, data: URL, or raw base64.
static NSData *decode_image_field(const std::string &image) {
  if (image.empty())
    return nil;
  if (image[0] == '/' || image[0] == '~')
    return [NSData dataWithContentsOfFile:ns(image)];
  std::string b64 = image;
  size_t comma = image.find(',');
  if (image.rfind("data:", 0) == 0 && comma != std::string::npos)
    b64 = image.substr(comma + 1);
  return [[[NSData alloc] initWithBase64EncodedString:ns(b64)
                                              options:NSDataBase64DecodingIgnoreUnknownCharacters]
      autorelease];
}

static bool parse_hex_color(const std::string &hex, CGFloat out[4]) {
  std::string h = hex[0] == '#' ? hex.substr(1) : hex;
  if (h.size() != 6 && h.size() != 8)
    return false;
  unsigned v = 0;
  if (std::sscanf(h.c_str(), "%x", &v) != 1)
    return false;
  bool alpha = h.size() == 8;
  out[0] = ((v >> (alpha ? 24 : 16)) & 0xff) / 255.0;
  out[1] = ((v >> (alpha ? 16 : 8)) & 0xff) / 255.0;
  out[2] = ((v >> (alpha ? 8 : 0)) & 0xff) / 255.0;
  out[3] = alpha ? (v & 0xff) / 255.0 : 1.0;
  return true;
}

static void do_clip_write(webview_t, void *arg) {
  ClipWriteReq *req = static_cast<ClipWriteReq *>(arg);
  @autoreleasepool {
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    NSMutableArray *objs = [NSMutableArray array];
    for (const std::string &p : req->paths) {
      NSURL *u = [NSURL fileURLWithPath:ns(p)];
      if (u)
        [objs addObject:u];
    }
    NSData *png = decode_image_field(req->image);
    if (png) {
      NSPasteboardItem *pi = [[[NSPasteboardItem alloc] init] autorelease];
      [pi setData:png forType:NSPasteboardTypePNG];
      // TIFF alongside PNG: plenty of apps only look for TIFF.
      NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:png];
      NSData *tiff = rep ? [rep TIFFRepresentation] : nil;
      if (tiff)
        [pi setData:tiff forType:NSPasteboardTypeTIFF];
      [objs addObject:pi];
    }
    if (!req->text.empty() || !req->html.empty()) {
      NSPasteboardItem *pi = [[[NSPasteboardItem alloc] init] autorelease];
      if (!req->text.empty())
        [pi setString:ns(req->text) forType:NSPasteboardTypeString];
      if (!req->html.empty())
        [pi setString:ns(req->html) forType:NSPasteboardTypeHTML];
      [objs addObject:pi];
    }
    CGFloat rgba[4];
    if (!req->color.empty() && parse_hex_color(req->color, rgba)) {
      NSColor *c = [NSColor colorWithSRGBRed:rgba[0] green:rgba[1] blue:rgba[2]
                                       alpha:rgba[3]];
      if (c)
        [objs addObject:c];
    }
    if (objs.count)
      [pb writeObjects:objs];
    // Lets the watcher tag the resulting CLIPCHANGE as self-inflicted.
    g_clip_self = [pb changeCount];
    clip_note_source(g_clip_self);
  }
  delete req;
}

static void do_clip_watch(webview_t, void *arg) {
  int ms = *static_cast<int *>(arg);
  delete static_cast<int *>(arg);
  if (g_clip_timer) {
    [g_clip_timer invalidate];
    g_clip_timer = nil;
  }
  if (ms <= 0)
    return;
  if (ms < 100)
    ms = 100;
  g_clip_seen = [[NSPasteboard generalPasteboard] changeCount];
  g_clip_timer = [NSTimer
      scheduledTimerWithTimeInterval:ms / 1000.0
                             repeats:YES
                               block:^(NSTimer *) {
                                 NSInteger c =
                                     [[NSPasteboard generalPasteboard] changeCount];
                                 if (c == g_clip_seen)
                                   return;
                                 g_clip_seen = c;
                                 clip_note_source(c);
                                 sock_write_line(
                                     "CLIPCHANGE " + std::to_string((long)c) +
                                     (c == g_clip_self ? " 1" : " 0"));
                               }];
}

static std::string clipboard_json(bool count_only) {
  NSPasteboard *pb = [NSPasteboard generalPasteboard];
  NSInteger count = [pb changeCount];
  if (count_only)
    return "{\"changeCount\":" + std::to_string((long)count) + "}";

  // files
  NSArray *urls = [pb readObjectsForClasses:@[ [NSURL class] ]
                                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  std::string paths = "[";
  bool has_paths = false;
  for (NSURL *u in urls) {
    if (![u isFileURL])
      continue;
    if (has_paths)
      paths += ",";
    has_paths = true;
    paths += json_escape([[u path] UTF8String]);
  }
  paths += "]";

  NSString *text = [pb stringForType:NSPasteboardTypeString];
  NSString *html = [pb stringForType:NSPasteboardTypeHTML];

  // image → materialized as a png temp file, rewritten only when the
  // clipboard actually changed (changeCount-keyed).
  NSData *png = [pb dataForType:NSPasteboardTypePNG];
  if (!png) {
    NSData *tiff = [pb dataForType:NSPasteboardTypeTIFF];
    if (tiff) {
      NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:tiff];
      png = rep ? [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}]
                : nil;
    }
  }
  std::string image;
  if (png) {
    if (count != g_clip_png_count) {
      if (!g_clip_png_path.empty())
        unlink(g_clip_png_path.c_str());
      std::string p = std::string([NSTemporaryDirectory() UTF8String]) +
                      "tinyjs-clip-" + std::to_string(getpid()) + "-" +
                      std::to_string((long)count) + ".png";
      if ([png writeToFile:ns(p) atomically:YES]) {
        g_clip_png_path = p;
        g_clip_png_count = count;
        NSBitmapImageRep *ir = [NSBitmapImageRep imageRepWithData:png];
        g_clip_png_w = ir ? (long)ir.pixelsWide : 0;
        g_clip_png_h = ir ? (long)ir.pixelsHigh : 0;
      }
    }
    if (count == g_clip_png_count)
      image = g_clip_png_path;
  }

  std::string color;
  NSArray *colors = [pb readObjectsForClasses:@[ [NSColor class] ] options:@{}];
  if (colors.count) {
    NSColor *c = [(NSColor *)colors[0]
        colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (c) {
      char buf[16];
      if (c.alphaComponent < 1.0)
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
                      (int)(c.redComponent * 255 + 0.5),
                      (int)(c.greenComponent * 255 + 0.5),
                      (int)(c.blueComponent * 255 + 0.5),
                      (int)(c.alphaComponent * 255 + 0.5));
      else
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                      (int)(c.redComponent * 255 + 0.5),
                      (int)(c.greenComponent * 255 + 0.5),
                      (int)(c.blueComponent * 255 + 0.5));
      color = buf;
    }
  }

  // Password managers mark secrets with the nspasteboard.org types; apps
  // that persist clipboard history must skip both concealed and transient.
  NSArray *types = [pb types];
  bool concealed = [types containsObject:@"org.nspasteboard.ConcealedType"] ||
                   [types containsObject:@"org.nspasteboard.TransientType"];
  // Chromium browsers stamp the page a copy came from.
  NSString *surl = [pb stringForType:@"org.chromium.source-url"];
  clip_note_source(count); // best-effort if the watcher didn't see it first

  const char *kind = has_paths                       ? "files"
                     : !image.empty()                ? "image"
                     : !color.empty()                ? "color"
                     : (text.length || html.length)  ? "text"
                                                     : "empty";
  std::string json = std::string("{\"kind\":\"") + kind + "\"";
  json += ",\"changeCount\":" + std::to_string((long)count);
  json += ",\"text\":" + (text.length ? json_escape([text UTF8String]) : "null");
  json += ",\"html\":" + (html.length ? json_escape([html UTF8String]) : "null");
  json += ",\"paths\":" + paths;
  json += ",\"image\":" + (image.empty() ? "null" : json_escape(image));
  if (!image.empty() && g_clip_png_w > 0)
    json += ",\"imageSize\":{\"width\":" + std::to_string(g_clip_png_w) +
            ",\"height\":" + std::to_string(g_clip_png_h) + "}";
  else
    json += ",\"imageSize\":null";
  json += ",\"color\":" + (color.empty() ? "null" : json_escape(color));
  json += ",\"concealed\":" + std::string(concealed ? "true" : "false");
  json += ",\"sourceApp\":" +
          (count == g_clip_src_count ? g_clip_src_json : std::string("null"));
  json += ",\"sourceURL\":" + (surl.length ? json_escape([surl UTF8String]) : "null");
  json += "}";
  return json;
}
#else
static void do_clip_write(webview_t, void *arg) { delete static_cast<ClipWriteReq *>(arg); }
static void do_clip_watch(webview_t, void *arg) { delete static_cast<int *>(arg); }
#endif

// --- drag-out (macOS) -------------------------------------------------------------
// DRAGOUT[@win] starts a native NSDraggingSession carrying real file URLs, so
// a page mousedown can drag files into Finder/Slack/anywhere. Must arrive
// while the mouse button is still down (same latency budget as DRAGWIN).

struct DragOutReq {
  std::string win, image;
  std::vector<std::string> paths;
};

#ifdef __APPLE__
static WKWebView *webview_for_id(webview_t w, const std::string &id); // below
static bool get_accepts_first_mouse(WKWebView *wv);                   // below

@interface TinyDragSource : NSObject <NSDraggingSource>
@end
@implementation TinyDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return NSDragOperationCopy;
}
@end

static TinyDragSource *g_drag_source = nil;

static void do_dragout(webview_t w, void *arg) {
  DragOutReq *req = static_cast<DragOutReq *>(arg);
  @autoreleasepool {
    WKWebView *wv = webview_for_id(w, req->win);
    NSEvent *ev = [NSApp currentEvent];
    // beginDraggingSession needs a live mouse-down; by the time the page's
    // call crosses the bridge the latest event is usually LeftMouseDragged.
    bool mouse_ok = ev && (ev.type == NSEventTypeLeftMouseDown ||
                           ev.type == NSEventTypeLeftMouseDragged);
    if (wv && mouse_ok && !req->paths.empty()) {
      NSPoint at = [wv convertPoint:ev.locationInWindow fromView:nil];
      NSImage *custom = nil;
      if (!req->image.empty())
        custom = [[[NSImage alloc] initWithContentsOfFile:ns(req->image)] autorelease];
      NSMutableArray *items = [NSMutableArray array];
      CGFloat off = 0;
      for (const std::string &p : req->paths) {
        NSURL *u = [NSURL fileURLWithPath:ns(p)];
        if (!u)
          continue;
        NSDraggingItem *di =
            [[[NSDraggingItem alloc] initWithPasteboardWriter:u] autorelease];
        NSImage *img = custom ?: [[NSWorkspace sharedWorkspace] iconForFile:ns(p)];
        NSSize sz = custom ? custom.size : NSMakeSize(32, 32);
        if (sz.width > 160 || sz.height > 160) {
          CGFloat s = 160 / (sz.width > sz.height ? sz.width : sz.height);
          sz = NSMakeSize(sz.width * s, sz.height * s);
        }
        [di setDraggingFrame:NSMakeRect(at.x - sz.width / 2 + off,
                                        at.y - sz.height / 2 + off, sz.width,
                                        sz.height)
                    contents:img];
        custom = nil; // custom image decorates the top item only
        off += 4;     // cascade the rest so a multi-file drag reads as a stack
        [items addObject:di];
      }
      if (!g_drag_source)
        g_drag_source = [[TinyDragSource alloc] init];
      @try {
        [wv beginDraggingSessionWithItems:items event:ev source:g_drag_source];
      } @catch (NSException *) {
      }
    }
  }
  delete req;
}
#else
static void do_dragout(webview_t, void *arg) { delete static_cast<DragOutReq *>(arg); }
#endif

// --- keystroke + permissions (macOS) ------------------------------------------------
// KEYSTROKE posts a CGEvent from this process — one Accessibility grant that
// names the app, instead of osascript→System Events (Automation +
// Accessibility, spawn latency, prompts naming osascript/the terminal).
// PERMCHK/PERMREQ let apps build onboarding instead of failing at first use.

struct KeystrokeReq {
  std::string qid, combo;
};
struct PermReq {
  std::string qid, name;
  bool request;
};

#ifdef __APPLE__
static void do_keystroke(webview_t, void *arg) {
  KeystrokeReq *req = static_cast<KeystrokeReq *>(arg);
  CGEventFlags flags = 0;
  std::string key;
  std::stringstream ss(req->combo);
  std::string part;
  while (std::getline(ss, part, '+')) {
    for (auto &c : part) c = (char)tolower(c);
    if (part == "cmd" || part == "command" || part == "meta") flags |= kCGEventFlagMaskCommand;
    else if (part == "ctrl" || part == "control") flags |= kCGEventFlagMaskControl;
    else if (part == "alt" || part == "opt" || part == "option") flags |= kCGEventFlagMaskAlternate;
    else if (part == "shift") flags |= kCGEventFlagMaskShift;
    else key = part;
  }
  int code = keycode_for(key);
  bool trusted = AXIsProcessTrusted();
  if (code >= 0) {
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, true);
    CGEventRef up = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, false);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
  }
  sock_write_line("GOT " + req->qid + " {\"ok\":" +
                  (code >= 0 && trusted ? "true" : "false") +
                  ",\"trusted\":" + (trusted ? "true" : "false") + "}");
  delete req;
}

static void perm_reply(const std::string &qid, const char *status) {
  sock_write_line("GOT " + qid + " {\"status\":\"" + status + "\"}");
}

static void do_perm(webview_t, void *arg) {
  PermReq *req = static_cast<PermReq *>(arg);
  std::string name = req->name, qid = req->qid;
  bool ask = req->request;
  delete req;

  if (name == "accessibility") {
    bool trusted;
    if (ask) {
      NSDictionary *opts = @{(NSString *)kAXTrustedCheckOptionPrompt : @YES};
      trusted = AXIsProcessTrustedWithOptions((CFDictionaryRef)opts);
    } else {
      trusted = AXIsProcessTrusted();
    }
    perm_reply(qid, trusted ? "granted" : "denied");
  } else if (name == "screen" || name == "screen-recording") {
    bool ok = ask ? CGRequestScreenCaptureAccess() : CGPreflightScreenCaptureAccess();
    perm_reply(qid, ok ? "granted" : "denied");
  } else if (name == "notifications") {
    // UNUserNotificationCenter needs a real bundle (see the notify section).
    if (!g_bundle_mode) {
      perm_reply(qid, "unsupported");
      return;
    }
    UNUserNotificationCenter *nc = [UNUserNotificationCenter currentNotificationCenter];
    if (ask) {
      [nc requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                           UNAuthorizationOptionSound |
                                           UNAuthorizationOptionBadge)
                        completionHandler:^(BOOL granted, NSError *) {
                          perm_reply(qid, granted ? "granted" : "denied");
                        }];
    } else {
      [nc getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *s) {
        perm_reply(qid,
                   s.authorizationStatus == UNAuthorizationStatusNotDetermined
                       ? "undetermined"
                   : s.authorizationStatus == UNAuthorizationStatusDenied
                       ? "denied"
                       : "granted");
      }];
    }
  } else if (name == "microphone" || name == "camera") {
    // The TCC layer under getUserMedia. Bundled apps also need the usage
    // string in Info.plist and — under the hardened runtime — the device
    // entitlement, both injected by `tinyjs build` from cfg.permissions.
    AVMediaType type = name == "camera" ? AVMediaTypeVideo : AVMediaTypeAudio;
    AVAuthorizationStatus st =
        [AVCaptureDevice authorizationStatusForMediaType:type];
    if (ask && st == AVAuthorizationStatusNotDetermined) {
      [AVCaptureDevice requestAccessForMediaType:type
                               completionHandler:^(BOOL granted) {
                                 perm_reply(qid, granted ? "granted" : "denied");
                               }];
      return;
    }
    perm_reply(qid, st == AVAuthorizationStatusAuthorized ? "granted"
               : st == AVAuthorizationStatusNotDetermined ? "undetermined"
                                                          : "denied");
  } else if (name == "automation" || name.rfind("automation:", 0) == 0) {
    // Per-target: automation:<bundle-id>; bare = System Events. The consent
    // dialog (and a possible target launch) can block, so ask off-main.
    std::string bid = name == "automation" ? "com.apple.systemevents"
                                           : name.substr(11);
    dispatch_async(
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
          AEAddressDesc addr;
          OSStatus st = AECreateDesc(typeApplicationBundleID, bid.c_str(),
                                     bid.size(), &addr);
          if (st != noErr) {
            perm_reply(qid, "undetermined");
            return;
          }
          st = AEDeterminePermissionToAutomateTarget(&addr, typeWildCard,
                                                     typeWildCard, ask);
          AEDisposeDesc(&addr);
          perm_reply(qid, st == noErr                              ? "granted"
                          : st == errAEEventNotPermitted           ? "denied"
                          : st == errAEEventWouldRequireUserConsent
                              ? "undetermined"
                              : "undetermined");
        });
  } else {
    perm_reply(qid, "unsupported");
  }
}
#else
static void do_keystroke(webview_t, void *arg) {
  KeystrokeReq *req = static_cast<KeystrokeReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false,\"trusted\":false}");
  delete req;
}
static void do_perm(webview_t, void *arg) {
  PermReq *req = static_cast<PermReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"status\":\"unsupported\"}");
  delete req;
}
#endif

// --- media capture (macOS) ---------------------------------------------------
// getUserMedia asks the WKUIDelegate per-origin before macOS asks TCC. The
// page is the app's own code, so that origin prompt is pure noise (it names
// file:// or localhost) and would double up with the system dialog — grant it
// and let the one TCC prompt naming the app be the real consent. The vendored
// delegate class is registered at runtime, so the handler (macOS 12+ selector,
// never called on older systems) is added here instead of patching the header.
#ifdef __APPLE__
static void install_media_capture_hook() {
  Class cls = objc_lookUpClass("WebviewWKUIDelegate");
  if (!cls) return;
  SEL sel = sel_registerName("webView:requestMediaCapturePermissionForOrigin:"
                             "initiatedByFrame:type:decisionHandler:");
  if (class_getInstanceMethod(cls, sel)) return;
  class_addMethod(cls, sel,
                  (IMP)(+[](id, SEL, id, id, id, NSInteger,
                            void (^decision)(NSInteger)) {
                    decision(1 /* WKPermissionDecisionGrant */);
                  }),
                  "v@:@@@q@?");
}
#endif

// --- shell, launch-at-login, dock (macOS) ------------------------------------------
// SHELL wraps the NSWorkspace verbs apps otherwise spawn `open` for; trash
// uses NSFileManager so the file is recoverable (vs tjs.remove). LOGIN wraps
// SMAppService (macOS 13+, needs a real bundle identity — dev-mode's bare
// launcher answers "unsupported"). BADGE/BOUNCE are Dock-tile fire-and-forget.

struct ShellReq {
  std::string qid, op, target;
};
struct LoginReq {
  std::string qid;
  int set; // -1 = get, 0 = unregister, 1 = register
};

#ifdef __APPLE__
static void do_shell(webview_t, void *arg) {
  ShellReq *req = static_cast<ShellReq *>(arg);
  @autoreleasepool {
    bool ok = false;
    std::string err;
    NSString *t = ns(req->target);
    // Anything that parses with a scheme is a URL; everything else is a
    // file path (~ expanded). file:// URLs are folded back to paths so
    // reveal/trash accept both spellings.
    NSURL *url = [NSURL URLWithString:t];
    bool is_url = url && url.scheme.length > 0;
    NSString *path = [t stringByExpandingTildeInPath];
    if (is_url && url.fileURL) {
      path = url.path;
      is_url = req->op == "open"; // reveal/trash want the path form
    }
    NSFileManager *fm = [NSFileManager defaultManager];
    if (req->op == "open") {
      if (is_url) {
        ok = [[NSWorkspace sharedWorkspace] openURL:url];
        if (!ok)
          err = "no application registered for URL";
      } else if ([fm fileExistsAtPath:path]) {
        ok = [[NSWorkspace sharedWorkspace]
            openURL:[NSURL fileURLWithPath:path]];
        if (!ok)
          err = "open failed";
      } else {
        err = "no such file";
      }
    } else if (req->op == "reveal") {
      if ([fm fileExistsAtPath:path]) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[
          [NSURL fileURLWithPath:path]
        ]];
        ok = true;
      } else {
        err = "no such file";
      }
    } else if (req->op == "trash") {
      NSError *e = nil;
      ok = [fm trashItemAtURL:[NSURL fileURLWithPath:path]
              resultingItemURL:nil
                         error:&e];
      if (!ok)
        err = e ? [e.localizedDescription UTF8String] : "trash failed";
    } else {
      err = "unknown shell op";
    }
    sock_write_line("GOT " + req->qid + " {\"ok\":" + (ok ? "true" : "false") +
                    ",\"error\":" +
                    (err.empty() ? "null" : json_escape(err)) + "}");
  }
  delete req;
}

static void do_login(webview_t, void *arg) {
  LoginReq *req = static_cast<LoginReq *>(arg);
  std::string qid = req->qid;
  int set = req->set;
  delete req;
  @autoreleasepool {
    if (@available(macOS 13.0, *)) {
      if ([[NSBundle mainBundle] bundleIdentifier]) {
        SMAppService *svc = [SMAppService mainAppService];
        bool ok = true;
        std::string err;
        if (set == 1 && svc.status != SMAppServiceStatusEnabled) {
          NSError *e = nil;
          ok = [svc registerAndReturnError:&e];
          if (!ok && e)
            err = [e.localizedDescription UTF8String];
        } else if (set == 0 && (svc.status == SMAppServiceStatusEnabled ||
                                svc.status ==
                                    SMAppServiceStatusRequiresApproval)) {
          NSError *e = nil;
          ok = [svc unregisterAndReturnError:&e];
          if (!ok && e)
            err = [e.localizedDescription UTF8String];
        }
        const char *st =
            svc.status == SMAppServiceStatusEnabled ? "enabled"
            : svc.status == SMAppServiceStatusRequiresApproval
                ? "requires-approval"
                : "disabled"; // notRegistered / notFound
        sock_write_line("GOT " + qid + " {\"status\":\"" + st +
                        "\",\"ok\":" + (ok ? "true" : "false") + ",\"error\":" +
                        (err.empty() ? "null" : json_escape(err)) + "}");
        return;
      }
    }
    sock_write_line("GOT " + qid +
                    " {\"status\":\"unsupported\",\"ok\":false,"
                    "\"error\":null}");
  }
}

static void do_badge(webview_t, void *arg) {
  std::string *text = static_cast<std::string *>(arg);
  [NSApp dockTile].badgeLabel = text->empty() ? nil : ns(*text);
  delete text;
}

static void do_bounce(webview_t, void *arg) {
  int *critical = static_cast<int *>(arg);
  [NSApp requestUserAttention:*critical ? NSCriticalRequest
                                        : NSInformationalRequest];
  delete critical;
}
#else
static void do_shell(webview_t, void *arg) {
  ShellReq *req = static_cast<ShellReq *>(arg);
  sock_write_line("GOT " + req->qid +
                  " {\"ok\":false,\"error\":\"unsupported\"}");
  delete req;
}
static void do_login(webview_t, void *arg) {
  LoginReq *req = static_cast<LoginReq *>(arg);
  sock_write_line("GOT " + req->qid +
                  " {\"status\":\"unsupported\",\"ok\":false,\"error\":null}");
  delete req;
}
static void do_badge(webview_t, void *arg) { delete static_cast<std::string *>(arg); }
static void do_bounce(webview_t, void *arg) { delete static_cast<int *>(arg); }
#endif

// --- power, sound, share (macOS) ----------------------------------------------------
// POWER holds a single IOPMAssertion (replacing spawned `caffeinate` — the
// assertion dies with the launcher, so a crashed app never wedges sleep).
// SOUND plays a beep, a system sound by name, or an audio file. SHARE shows
// NSSharingServicePicker anchored at page coordinates.

struct PowerReq {
  std::string qid, reason;
  bool on, display;
};
struct SoundReq {
  std::string qid, target;
};
struct ShareReq {
  std::string win, text, url;
  std::vector<std::string> paths;
  int x, y;
};

#ifdef __APPLE__
static IOPMAssertionID g_power_assertion = kIOPMNullAssertionID;

static void do_power(webview_t, void *arg) {
  PowerReq *req = static_cast<PowerReq *>(arg);
  bool ok = true;
  if (g_power_assertion != kIOPMNullAssertionID) {
    IOPMAssertionRelease(g_power_assertion);
    g_power_assertion = kIOPMNullAssertionID;
  }
  if (req->on) {
    CFStringRef reason = CFStringCreateWithCString(
        NULL, req->reason.empty() ? "tinyjs app" : req->reason.c_str(),
        kCFStringEncodingUTF8);
    ok = IOPMAssertionCreateWithName(
             req->display ? kIOPMAssertionTypePreventUserIdleDisplaySleep
                          : kIOPMAssertionTypePreventUserIdleSystemSleep,
             kIOPMAssertionLevelOn, reason,
             &g_power_assertion) == kIOReturnSuccess;
    CFRelease(reason);
    if (!ok)
      g_power_assertion = kIOPMNullAssertionID;
  }
  sock_write_line("GOT " + req->qid + " {\"ok\":" + (ok ? "true" : "false") +
                  ",\"active\":" +
                  (g_power_assertion != kIOPMNullAssertionID ? "true"
                                                             : "false") +
                  "}");
  delete req;
}

// NSSound stops when released; hold the last one until the next play.
static NSSound *g_sound = nil;

static void do_sound(webview_t, void *arg) {
  SoundReq *req = static_cast<SoundReq *>(arg);
  @autoreleasepool {
    bool ok = true;
    if (req->target.empty()) {
      NSBeep();
    } else {
      NSSound *snd =
          req->target[0] == '/'
              ? [[NSSound alloc] initWithContentsOfFile:ns(req->target)
                                            byReference:YES]
              : [NSSound soundNamed:ns(req->target)];
      ok = snd != nil;
      if (snd) {
        [g_sound stop];
        g_sound = snd;
        [snd play];
      }
    }
    sock_write_line("GOT " + req->qid + " {\"ok\":" + (ok ? "true" : "false") +
                    "}");
  }
  delete req;
}

// The picker dismisses itself; keep it alive while it's up.
static NSSharingServicePicker *g_share_picker = nil;

static void do_share(webview_t w, void *arg) {
  ShareReq *req = static_cast<ShareReq *>(arg);
  @autoreleasepool {
    NSMutableArray *items = [NSMutableArray array];
    if (!req->text.empty())
      [items addObject:ns(req->text)];
    if (!req->url.empty()) {
      NSURL *u = [NSURL URLWithString:ns(req->url)];
      if (u)
        [items addObject:u];
    }
    for (auto &p : req->paths)
      [items addObject:[NSURL fileURLWithPath:ns(p)]];
    WKWebView *wv = webview_for_id(w, req->win);
    if (wv && items.count) {
      g_share_picker =
          [[NSSharingServicePicker alloc] initWithItems:items];
      // Page coords are top-left of the content area; WKWebView is flipped,
      // but convert defensively.
      CGFloat y = wv.isFlipped ? req->y : wv.bounds.size.height - req->y;
      [g_share_picker
          showRelativeToRect:NSMakeRect(req->x, y, 1, 1)
                      ofView:wv
               preferredEdge:NSMinYEdge];
    }
  }
  delete req;
}
#else
static void do_power(webview_t, void *arg) {
  PowerReq *req = static_cast<PowerReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false,\"active\":false}");
  delete req;
}
static void do_sound(webview_t, void *arg) {
  SoundReq *req = static_cast<SoundReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false}");
  delete req;
}
static void do_share(webview_t, void *arg) { delete static_cast<ShareReq *>(arg); }
#endif

// --- quick look + screen capture (macOS) --------------------------------------------
// QUICKLOOK drives the shared QLPreviewPanel (the Finder-spacebar preview,
// no qlmanage spawn). CAPTURE screenshots a display via ScreenCaptureKit
// (weak-linked; macOS 14+ and the 'screen' permission — errors cleanly
// otherwise) and materializes a png in the temp dir, named per request:
// the caller owns the file.

struct QLReq {
  std::vector<std::string> paths;
};
struct CaptureReq {
  std::string qid;
  long display; // CGDirectDisplayID; 0 = primary
};

#ifdef __APPLE__
@interface TinyQLSource : NSObject <QLPreviewPanelDataSource>
@property(strong) NSMutableArray<NSURL *> *items;
@end
@implementation TinyQLSource
- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel *)panel {
  return (NSInteger)self.items.count;
}
- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel *)panel
              previewItemAtIndex:(NSInteger)idx {
  return self.items[(NSUInteger)idx];
}
@end

static TinyQLSource *g_ql_source = nil;

static void do_quicklook(webview_t, void *arg) {
  QLReq *req = static_cast<QLReq *>(arg);
  @autoreleasepool {
    if (!g_ql_source) {
      g_ql_source = [[TinyQLSource alloc] init];
      g_ql_source.items = [NSMutableArray array];
    }
    [g_ql_source.items removeAllObjects];
    for (auto &p : req->paths)
      [g_ql_source.items addObject:[NSURL fileURLWithPath:ns(p)]];
    if (req->paths.empty()) {
      if ([QLPreviewPanel sharedPreviewPanelExists] &&
          [QLPreviewPanel sharedPreviewPanel].visible)
        [[QLPreviewPanel sharedPreviewPanel] orderOut:nil];
    } else {
      QLPreviewPanel *panel = [QLPreviewPanel sharedPreviewPanel];
      panel.dataSource = g_ql_source;
      [panel reloadData];
      [panel makeKeyAndOrderFront:nil];
    }
  }
  delete req;
}

static void capture_fail(const std::string &qid, const std::string &err) {
  sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":" +
                  json_escape(err) + "}");
}

static void do_capture(webview_t, void *arg) {
  CaptureReq *req = static_cast<CaptureReq *>(arg);
  std::string qid = req->qid;
  long want = req->display;
  delete req;
  if (@available(macOS 14.0, *)) {
    [SCShareableContent
        getShareableContentWithCompletionHandler:^(SCShareableContent *content,
                                                   NSError *error) {
          if (!content) {
            capture_fail(qid, error ? [error.localizedDescription UTF8String]
                                    : "no shareable content "
                                      "(screen-recording permission?)");
            return;
          }
          SCDisplay *disp = nil;
          for (SCDisplay *d in content.displays)
            if (!want || (long)d.displayID == want) {
              disp = d;
              break;
            }
          if (!disp) {
            capture_fail(qid, "no such display");
            return;
          }
          CGFloat scale = 1;
          for (NSScreen *s in [NSScreen screens])
            if ([s.deviceDescription[@"NSScreenNumber"] longValue] ==
                (long)disp.displayID) {
              scale = s.backingScaleFactor;
              break;
            }
          SCContentFilter *filter =
              [[SCContentFilter alloc] initWithDisplay:disp
                                      excludingWindows:@[]];
          SCStreamConfiguration *cfg = [[SCStreamConfiguration alloc] init];
          cfg.width = (size_t)(disp.width * scale);
          cfg.height = (size_t)(disp.height * scale);
          cfg.showsCursor = NO;
          [SCScreenshotManager
              captureImageWithFilter:filter
                        configuration:cfg
                    completionHandler:^(CGImageRef img, NSError *err2) {
                      if (!img) {
                        capture_fail(
                            qid, err2 ? [err2.localizedDescription UTF8String]
                                      : "capture failed");
                        return;
                      }
                      NSBitmapImageRep *rep =
                          [[NSBitmapImageRep alloc] initWithCGImage:img];
                      NSData *png = [rep
                          representationUsingType:NSBitmapImageFileTypePNG
                                       properties:@{}];
                      std::string p =
                          std::string([NSTemporaryDirectory() UTF8String]) +
                          "tinyjs-shot-" + std::to_string(getpid()) + "-" +
                          qid + ".png";
                      if (!png || ![png writeToFile:ns(p) atomically:YES]) {
                        capture_fail(qid, "png write failed");
                        return;
                      }
                      sock_write_line(
                          "GOT " + qid + " {\"ok\":true,\"path\":" +
                          json_escape(p) +
                          ",\"width\":" + std::to_string((long)rep.pixelsWide) +
                          ",\"height\":" +
                          std::to_string((long)rep.pixelsHigh) + "}");
                    }];
        }];
  } else {
    capture_fail(qid, "needs macOS 14");
  }
}
#else
static void do_quicklook(webview_t, void *arg) { delete static_cast<QLReq *>(arg); }
static void do_capture(webview_t, void *arg) {
  CaptureReq *req = static_cast<CaptureReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  delete req;
}
#endif

// --- Mac superpowers: eyedropper, OCR, thumbnails, Keychain, Touch ID, ---------------
// --- AppleScript (macOS) -------------------------------------------------------------
// PICKCOLOR: NSColorSampler — the system-wide eyedropper, notably WITHOUT
// needing the screen-recording permission. OCR: Vision, on-device. THUMB:
// QLThumbnailGenerator — a preview png for any file type Quick Look knows.
// SECRET: Keychain generic passwords (the keytar/safeStorage role). AUTH:
// LocalAuthentication (Touch ID, falls back to the account password). OSA:
// NSAppleScript in-process — Apple Events fire under the same 'automation'
// TCC the permissions api already covers, with no osascript spawn.

struct OcrReq {
  std::string qid, path;
};
struct ThumbReq {
  std::string qid, path;
  int size;
};
struct SecretReq {
  std::string qid, op, service, account, value;
};
struct AuthReq {
  std::string qid, reason;
};
struct OsaReq {
  std::string qid, source;
};

#ifdef __APPLE__
static NSColorSampler *g_sampler = nil; // alive while the loupe is up

static void do_pickcolor(webview_t, void *arg) {
  std::string *qidp = static_cast<std::string *>(arg);
  std::string qid = *qidp;
  delete qidp;
  if (@available(macOS 10.15, *)) {
    g_sampler = [[NSColorSampler alloc] init];
    [g_sampler showSamplerWithSelectionHandler:^(NSColor *c) {
      if (!c) {
        sock_write_line("GOT " + qid + " {\"ok\":true,\"color\":null}");
        return;
      }
      NSColor *s = [c colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] ?: c;
      char buf[48];
      std::snprintf(buf, sizeof(buf), "\"#%02x%02x%02x\"",
                    (int)lround(s.redComponent * 255),
                    (int)lround(s.greenComponent * 255),
                    (int)lround(s.blueComponent * 255));
      sock_write_line("GOT " + qid + " {\"ok\":true,\"color\":" + buf + "}");
    }];
  } else {
    sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  }
}

static void do_ocr(webview_t, void *arg) {
  OcrReq *req = static_cast<OcrReq *>(arg);
  std::string qid = req->qid, path = req->path;
  delete req;
  // Vision takes ~100ms+; keep it off the UI thread.
  dispatch_async(
      dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @autoreleasepool {
          VNRecognizeTextRequest *r = [[VNRecognizeTextRequest alloc] init];
          r.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
          r.usesLanguageCorrection = YES;
          VNImageRequestHandler *h = [[VNImageRequestHandler alloc]
              initWithURL:[NSURL fileURLWithPath:ns(path)]
                  options:@{}];
          NSError *e = nil;
          if (![h performRequests:@[ r ] error:&e]) {
            sock_write_line(
                "GOT " + qid + " {\"ok\":false,\"error\":" +
                json_escape(e ? [e.localizedDescription UTF8String]
                              : "ocr failed") +
                "}");
            return;
          }
          std::string text, blocks = "[";
          bool first = true;
          for (VNRecognizedTextObservation *o in r.results) {
            VNRecognizedText *t = [[o topCandidates:1] firstObject];
            if (!t)
              continue;
            if (!text.empty())
              text += "\n";
            text += [t.string UTF8String];
            // boundingBox is normalized with a bottom-left origin; flip to
            // the top-left convention everything else in tinyjs uses.
            CGRect b = o.boundingBox;
            char geo[128];
            std::snprintf(geo, sizeof(geo),
                          ",\"confidence\":%.3f,\"box\":{\"x\":%.4f,"
                          "\"y\":%.4f,\"width\":%.4f,\"height\":%.4f}}",
                          (double)t.confidence, b.origin.x,
                          1.0 - b.origin.y - b.size.height, b.size.width,
                          b.size.height);
            if (!first)
              blocks += ",";
            first = false;
            blocks += "{\"text\":" + json_escape([t.string UTF8String]) + geo;
          }
          blocks += "]";
          sock_write_line("GOT " + qid + " {\"ok\":true,\"text\":" +
                          json_escape(text) + ",\"blocks\":" + blocks + "}");
        }
      });
}

static void do_thumb(webview_t, void *arg) {
  ThumbReq *req = static_cast<ThumbReq *>(arg);
  std::string qid = req->qid, path = req->path;
  int size = req->size > 0 ? req->size : 256;
  delete req;
  if (@available(macOS 10.15, *)) {
    QLThumbnailGenerationRequest *r = [[QLThumbnailGenerationRequest alloc]
        initWithFileAtURL:[NSURL fileURLWithPath:ns(path)]
                     size:CGSizeMake(size, size)
                    scale:2.0
      representationTypes:
          QLThumbnailGenerationRequestRepresentationTypeThumbnail];
    [[QLThumbnailGenerator sharedGenerator]
        generateBestRepresentationForRequest:r
                           completionHandler:^(
                               QLThumbnailRepresentation *rep, NSError *e) {
                             if (!rep) {
                               sock_write_line(
                                   "GOT " + qid + " {\"ok\":false,\"error\":" +
                                   json_escape(
                                       e ? [e.localizedDescription UTF8String]
                                         : "no thumbnail") +
                                   "}");
                               return;
                             }
                             NSBitmapImageRep *bm = [[NSBitmapImageRep alloc]
                                 initWithCGImage:rep.CGImage];
                             NSData *png = [bm
                                 representationUsingType:
                                     NSBitmapImageFileTypePNG
                                              properties:@{}];
                             std::string p =
                                 std::string(
                                     [NSTemporaryDirectory() UTF8String]) +
                                 "tinyjs-thumb-" + std::to_string(getpid()) +
                                 "-" + qid + ".png";
                             if (!png ||
                                 ![png writeToFile:ns(p) atomically:YES]) {
                               sock_write_line("GOT " + qid +
                                               " {\"ok\":false,\"error\":"
                                               "\"png write failed\"}");
                               return;
                             }
                             sock_write_line(
                                 "GOT " + qid + " {\"ok\":true,\"path\":" +
                                 json_escape(p) + ",\"width\":" +
                                 std::to_string((long)bm.pixelsWide) +
                                 ",\"height\":" +
                                 std::to_string((long)bm.pixelsHigh) + "}");
                           }];
  } else {
    sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  }
}

static void do_secret(webview_t, void *arg) {
  SecretReq *req = static_cast<SecretReq *>(arg);
  @autoreleasepool {
    NSMutableDictionary *q = [@{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : ns(req->service),
      (__bridge id)kSecAttrAccount : ns(req->account),
    } mutableCopy];
    std::string out;
    if (req->op == "get") {
      q[(__bridge id)kSecReturnData] = @YES;
      q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
      CFTypeRef data = NULL;
      OSStatus st = SecItemCopyMatching((__bridge CFDictionaryRef)q, &data);
      if (st == errSecSuccess && data) {
        NSData *d = (__bridge_transfer NSData *)data;
        NSString *s = [[NSString alloc] initWithData:d
                                            encoding:NSUTF8StringEncoding];
        out = "{\"ok\":true,\"value\":" +
              (s ? json_escape([s UTF8String]) : std::string("null")) + "}";
      } else if (st == errSecItemNotFound) {
        out = "{\"ok\":true,\"value\":null}";
      } else {
        out = "{\"ok\":false,\"error\":\"keychain error " +
              std::to_string((long)st) + "\"}";
      }
    } else if (req->op == "set") {
      SecItemDelete((__bridge CFDictionaryRef)q); // replace semantics
      q[(__bridge id)kSecValueData] =
          [ns(req->value) dataUsingEncoding:NSUTF8StringEncoding];
      OSStatus st = SecItemAdd((__bridge CFDictionaryRef)q, NULL);
      out = st == errSecSuccess
                ? "{\"ok\":true}"
                : "{\"ok\":false,\"error\":\"keychain error " +
                      std::to_string((long)st) + "\"}";
    } else if (req->op == "del") {
      OSStatus st = SecItemDelete((__bridge CFDictionaryRef)q);
      out = (st == errSecSuccess || st == errSecItemNotFound)
                ? "{\"ok\":true}"
                : "{\"ok\":false,\"error\":\"keychain error " +
                      std::to_string((long)st) + "\"}";
    } else {
      out = "{\"ok\":false,\"error\":\"unknown secret op\"}";
    }
    sock_write_line("GOT " + req->qid + " " + out);
  }
  delete req;
}

static void do_auth(webview_t, void *arg) {
  AuthReq *req = static_cast<AuthReq *>(arg);
  std::string qid = req->qid, reason = req->reason;
  delete req;
  @autoreleasepool {
    LAContext *ctx = [[LAContext alloc] init];
    NSError *e = nil;
    // DeviceOwnerAuthentication = Touch ID when available, else the account
    // password sheet — both count as "the user proved it's them".
    if (![ctx canEvaluatePolicy:LAPolicyDeviceOwnerAuthentication error:&e]) {
      sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":" +
                      json_escape(e ? [e.localizedDescription UTF8String]
                                    : "authentication unavailable") +
                      "}");
      return;
    }
    [ctx evaluatePolicy:LAPolicyDeviceOwnerAuthentication
        localizedReason:ns(reason.empty() ? "authenticate" : reason)
                  reply:^(BOOL ok, NSError *err) {
                    sock_write_line(
                        "GOT " + qid + " {\"ok\":" + (ok ? "true" : "false") +
                        ",\"error\":" +
                        (err ? json_escape(
                                   [err.localizedDescription UTF8String])
                             : "null") +
                        "}");
                  }];
  }
}

static void do_osa(webview_t, void *arg) {
  OsaReq *req = static_cast<OsaReq *>(arg);
  @autoreleasepool {
    // NSAppleScript is main-thread-only; long-running scripts briefly block
    // the UI (typical Apple Events round-trips are milliseconds).
    NSAppleScript *scr =
        [[NSAppleScript alloc] initWithSource:ns(req->source)];
    NSDictionary *err = nil;
    NSAppleEventDescriptor *d = [scr executeAndReturnError:&err];
    if (!d) {
      NSString *msg = err[NSAppleScriptErrorMessage]
                          ?: err[NSAppleScriptErrorBriefMessage];
      sock_write_line("GOT " + req->qid + " {\"ok\":false,\"error\":" +
                      json_escape(msg ? [msg UTF8String] : "script error") +
                      "}");
    } else {
      NSString *s = [d stringValue];
      sock_write_line("GOT " + req->qid + " {\"ok\":true,\"result\":" +
                      (s ? json_escape([s UTF8String]) : "null") + "}");
    }
  }
  delete req;
}
#else
static void unsupported_reply(const std::string &qid) {
  sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":\"unsupported\"}");
}
static void do_pickcolor(webview_t, void *arg) {
  std::string *q = static_cast<std::string *>(arg);
  unsupported_reply(*q);
  delete q;
}
static void do_ocr(webview_t, void *arg) {
  OcrReq *r = static_cast<OcrReq *>(arg);
  unsupported_reply(r->qid);
  delete r;
}
static void do_thumb(webview_t, void *arg) {
  ThumbReq *r = static_cast<ThumbReq *>(arg);
  unsupported_reply(r->qid);
  delete r;
}
static void do_secret(webview_t, void *arg) {
  SecretReq *r = static_cast<SecretReq *>(arg);
  unsupported_reply(r->qid);
  delete r;
}
static void do_auth(webview_t, void *arg) {
  AuthReq *r = static_cast<AuthReq *>(arg);
  unsupported_reply(r->qid);
  delete r;
}
static void do_osa(webview_t, void *arg) {
  OsaReq *r = static_cast<OsaReq *>(arg);
  unsupported_reply(r->qid);
  delete r;
}
#endif

// --- screen recording to .mp4 (macOS) -----------------------------------------------
// SCStream feeds screen CMSampleBuffers into an AVAssetWriterInput. All
// recorder state lives on one serial queue (g_rec_queue) — the sample
// handler runs there, and start/stop hop onto it — so nothing races. Needs
// macOS 14 and the 'screen' permission; ScreenCaptureKit is weak-linked so
// older systems still launch. Video-only for now (no audio track).

struct RecordReq {
  std::string qid, path;
  long display;
  bool start;
};

#ifdef __APPLE__
static dispatch_queue_t g_rec_queue = nullptr;
static SCStream *g_rec_stream = nil;
static AVAssetWriter *g_rec_writer = nil;
static AVAssetWriterInput *g_rec_input = nil;
static bool g_rec_session = false;
static CMTime g_rec_first, g_rec_last;
static std::string g_rec_path;

@interface TinyRecOutput : NSObject <SCStreamOutput>
@end
@implementation TinyRecOutput
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sb
                   ofType:(SCStreamOutputType)type
    API_AVAILABLE(macos(14.0)) {
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sb))
    return;
  // SCStream emits idle/duplicate frames too; only append complete ones.
  NSArray *att = (__bridge NSArray *)CMSampleBufferGetSampleAttachmentsArray(
      sb, false);
  NSDictionary *info = att.firstObject;
  if (info) {
    NSNumber *st = info[SCStreamFrameInfoStatus];
    if (st && st.intValue != SCFrameStatusComplete)
      return;
  }
  if (!g_rec_writer || g_rec_writer.status != AVAssetWriterStatusWriting)
    return;
  CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
  if (!g_rec_session) {
    [g_rec_writer startSessionAtSourceTime:pts];
    g_rec_first = pts;
    g_rec_session = true;
  }
  if (g_rec_input.isReadyForMoreMediaData) {
    [g_rec_input appendSampleBuffer:sb];
    g_rec_last = pts;
  }
}
@end

static TinyRecOutput *g_rec_output = nil;

static void rec_reset() {
  g_rec_stream = nil;
  g_rec_writer = nil;
  g_rec_input = nil;
  g_rec_session = false;
  g_rec_path.clear();
}

API_AVAILABLE(macos(14.0))
static void rec_start(const std::string &qid, long want,
                      const std::string &path) {
  if (g_rec_stream) {
    capture_fail(qid, "already recording");
    return;
  }
  // Fail fast when the screen-recording permission is missing:
  // getShareableContent's completion handler is unreliable when TCC has
  // denied us (it can neither error nor fire), which would hang start().
  if (!CGPreflightScreenCaptureAccess()) {
    CGRequestScreenCaptureAccess(); // adds us to System Settings for next time
    capture_fail(qid, "screen recording permission required "
                      "(System Settings > Privacy > Screen Recording)");
    return;
  }
  [SCShareableContent
      getShareableContentWithCompletionHandler:^(SCShareableContent *content,
                                                 NSError *error) {
        dispatch_async(g_rec_queue, ^{
          if (!content) {
            capture_fail(qid,
                         error ? [error.localizedDescription UTF8String]
                               : "no shareable content (screen permission?)");
            return;
          }
          SCDisplay *disp = nil;
          for (SCDisplay *d in content.displays)
            if (!want || (long)d.displayID == want) {
              disp = d;
              break;
            }
          if (!disp) {
            capture_fail(qid, "no such display");
            return;
          }
          CGFloat scale = 1;
          for (NSScreen *s in [NSScreen screens])
            if ([s.deviceDescription[@"NSScreenNumber"] longValue] ==
                (long)disp.displayID)
              scale = s.backingScaleFactor;
          size_t w = (size_t)(disp.width * scale), h = (size_t)(disp.height * scale);

          NSError *werr = nil;
          [[NSFileManager defaultManager] removeItemAtPath:ns(path) error:nil];
          AVAssetWriter *writer = [[AVAssetWriter alloc]
              initWithURL:[NSURL fileURLWithPath:ns(path)]
                 fileType:AVFileTypeMPEG4
                    error:&werr];
          if (!writer) {
            capture_fail(qid, werr ? [werr.localizedDescription UTF8String]
                                   : "cannot create the mp4");
            return;
          }
          AVAssetWriterInput *input = [AVAssetWriterInput
              assetWriterInputWithMediaType:AVMediaTypeVideo
                             outputSettings:@{
                               AVVideoCodecKey : AVVideoCodecTypeH264,
                               AVVideoWidthKey : @(w),
                               AVVideoHeightKey : @(h),
                             }];
          input.expectsMediaDataInRealTime = YES;
          if (![writer canAddInput:input]) {
            capture_fail(qid, "cannot add the video track");
            return;
          }
          [writer addInput:input];
          if (![writer startWriting]) {
            capture_fail(qid, "asset writer refused to start");
            return;
          }

          SCContentFilter *filter =
              [[SCContentFilter alloc] initWithDisplay:disp
                                      excludingWindows:@[]];
          SCStreamConfiguration *cfg = [[SCStreamConfiguration alloc] init];
          cfg.width = w;
          cfg.height = h;
          cfg.showsCursor = YES;
          cfg.minimumFrameInterval = CMTimeMake(1, 60);
          cfg.pixelFormat = kCVPixelFormatType_32BGRA;
          if (!g_rec_output)
            g_rec_output = [[TinyRecOutput alloc] init];
          SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                                configuration:cfg
                                                     delegate:nil];
          NSError *oerr = nil;
          [stream addStreamOutput:g_rec_output
                             type:SCStreamOutputTypeScreen
               sampleHandlerQueue:g_rec_queue
                            error:&oerr];
          if (oerr) {
            capture_fail(qid, [oerr.localizedDescription UTF8String]);
            return;
          }
          g_rec_writer = writer;
          g_rec_input = input;
          g_rec_stream = stream;
          g_rec_session = false;
          g_rec_path = path;
          [stream startCaptureWithCompletionHandler:^(NSError *serr) {
            dispatch_async(g_rec_queue, ^{
              if (serr) {
                [writer cancelWriting];
                rec_reset();
                capture_fail(qid, [serr.localizedDescription UTF8String]);
              } else {
                sock_write_line("GOT " + qid + " {\"ok\":true,\"error\":null}");
              }
            });
          }];
        });
      }];
}

API_AVAILABLE(macos(14.0))
static void rec_stop(const std::string &qid) {
  if (!g_rec_stream) {
    capture_fail(qid, "not recording");
    return;
  }
  SCStream *stream = g_rec_stream;
  AVAssetWriter *writer = g_rec_writer;
  AVAssetWriterInput *input = g_rec_input;
  std::string path = g_rec_path;
  bool session = g_rec_session;
  CMTime first = g_rec_first, last = g_rec_last;
  g_rec_stream = nil; // block re-entrancy; keep the rest until finalize
  [stream stopCaptureWithCompletionHandler:^(NSError *) {
    dispatch_async(g_rec_queue, ^{
      [input markAsFinished];
      [writer finishWritingWithCompletionHandler:^{
        dispatch_async(g_rec_queue, ^{
          bool ok = writer.status == AVAssetWriterStatusCompleted && session;
          double dur =
              session ? CMTimeGetSeconds(CMTimeSubtract(last, first)) : 0;
          if (ok) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.3f", dur);
            sock_write_line("GOT " + qid + " {\"ok\":true,\"path\":" +
                            json_escape(path) + ",\"duration\":" + buf +
                            ",\"error\":null}");
          } else {
            capture_fail(qid, writer.error
                                  ? [writer.error.localizedDescription UTF8String]
                                  : "no frames captured");
          }
          rec_reset();
        });
      }];
    });
  }];
}

static void do_record(webview_t, void *arg) {
  RecordReq *req = static_cast<RecordReq *>(arg);
  std::string qid = req->qid, path = req->path;
  long display = req->display;
  bool start = req->start;
  delete req;
  if (@available(macOS 14.0, *)) {
    if (!g_rec_queue)
      g_rec_queue = dispatch_queue_create("app.tinyjs.recorder", DISPATCH_QUEUE_SERIAL);
    dispatch_async(g_rec_queue, ^{
      if (start)
        rec_start(qid, display, path);
      else
        rec_stop(qid);
    });
  } else {
    capture_fail(qid, "needs macOS 14");
  }
}
#else
static void do_record(webview_t, void *arg) {
  RecordReq *req = static_cast<RecordReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  delete req;
}
#endif

// --- accessibility: read selection + move other apps' windows (macOS) ---------------
// All under the Accessibility permission (permissions.check('accessibility')).
// GET selectedtext → the text selected in the frontmost app (PopClip-style
// popovers). GET otherwindows → every on-screen window of OTHER apps. WINCTRL
// move re-fetches an app's frontmost window and repositions/resizes it (a
// Rectangle/Magnet "snap the active window" primitive).

struct WinCtrlReq {
  std::string qid;
  long pid;
  int x, y, w, h;
};

#ifdef __APPLE__
// AXFocusedUIElement → AXSelectedText of the system-wide focused element.
static std::string ax_selected_text() {
  AXUIElementRef sys = AXUIElementCreateSystemWide();
  CFTypeRef focused = NULL;
  std::string out;
  bool have = false;
  if (AXUIElementCopyAttributeValue(sys, kAXFocusedUIElementAttribute,
                                    &focused) == kAXErrorSuccess &&
      focused) {
    CFTypeRef sel = NULL;
    if (AXUIElementCopyAttributeValue((AXUIElementRef)focused,
                                      kAXSelectedTextAttribute,
                                      &sel) == kAXErrorSuccess &&
        sel) {
      if (CFGetTypeID(sel) == CFStringGetTypeID()) {
        out = [(__bridge NSString *)sel UTF8String];
        have = true;
      }
      CFRelease(sel);
    }
    CFRelease(focused);
  }
  CFRelease(sys);
  return have ? json_escape(out) : std::string("null");
}

// AXPosition/AXSize come back as AXValue; unwrap to CG structs.
static bool ax_rect(AXUIElementRef win, CGPoint *pos, CGSize *size) {
  CFTypeRef p = NULL, s = NULL;
  bool ok = false;
  if (AXUIElementCopyAttributeValue(win, kAXPositionAttribute, &p) ==
          kAXErrorSuccess &&
      AXUIElementCopyAttributeValue(win, kAXSizeAttribute, &s) ==
          kAXErrorSuccess &&
      p && s) {
    ok = AXValueGetValue((AXValueRef)p, kAXValueTypeCGPoint, pos) &&
         AXValueGetValue((AXValueRef)s, kAXValueTypeCGSize, size);
  }
  if (p) CFRelease(p);
  if (s) CFRelease(s);
  return ok;
}

static std::string ax_other_windows() {
  if (!AXIsProcessTrusted())
    return "null"; // caller maps null → "needs Accessibility"
  std::string json = "[";
  bool first = true;
  pid_t self = getpid();
  for (NSRunningApplication *app in
       [[NSWorkspace sharedWorkspace] runningApplications]) {
    if (app.processIdentifier == self ||
        app.activationPolicy != NSApplicationActivationPolicyRegular)
      continue;
    AXUIElementRef axApp = AXUIElementCreateApplication(app.processIdentifier);
    CFTypeRef windows = NULL;
    if (AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute, &windows) ==
            kAXErrorSuccess &&
        windows) {
      NSArray *wins = (__bridge NSArray *)windows;
      int idx = 0;
      for (id w in wins) {
        AXUIElementRef win = (AXUIElementRef)w;
        CGPoint pos;
        CGSize size;
        if (ax_rect(win, &pos, &size)) {
          CFTypeRef t = NULL;
          std::string title;
          if (AXUIElementCopyAttributeValue(win, kAXTitleAttribute, &t) ==
                  kAXErrorSuccess &&
              t) {
            if (CFGetTypeID(t) == CFStringGetTypeID())
              title = [(__bridge NSString *)t UTF8String];
            CFRelease(t);
          }
          char geo[160];
          std::snprintf(geo, sizeof(geo),
                        ",\"pid\":%d,\"index\":%d,\"x\":%d,\"y\":%d,"
                        "\"width\":%d,\"height\":%d}",
                        (int)app.processIdentifier, idx, (int)pos.x,
                        (int)pos.y, (int)size.width, (int)size.height);
          if (!first)
            json += ",";
          first = false;
          json += "{\"app\":" +
                  json_escape(app.localizedName ? [app.localizedName UTF8String]
                                                : "") +
                  ",\"bundleId\":" +
                  (app.bundleIdentifier
                       ? json_escape([app.bundleIdentifier UTF8String])
                       : "null") +
                  ",\"title\":" + json_escape(title) + geo;
        }
        idx++;
      }
      CFRelease(windows);
    }
    CFRelease(axApp);
  }
  json += "]";
  return json;
}

static void do_winctrl(webview_t, void *arg) {
  WinCtrlReq *req = static_cast<WinCtrlReq *>(arg);
  std::string qid = req->qid;
  @autoreleasepool {
    if (!AXIsProcessTrusted()) {
      sock_write_line("GOT " + qid +
                      " {\"ok\":false,\"error\":\"needs Accessibility\"}");
      delete req;
      return;
    }
    AXUIElementRef axApp = AXUIElementCreateApplication((pid_t)req->pid);
    CFTypeRef win = NULL;
    // The app's main/frontmost window (the one a user would arrange).
    if (AXUIElementCopyAttributeValue(axApp, kAXMainWindowAttribute, &win) !=
            kAXErrorSuccess ||
        !win) {
      CFTypeRef wins = NULL;
      if (AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute, &wins) ==
              kAXErrorSuccess &&
          wins && CFArrayGetCount((CFArrayRef)wins) > 0) {
        win = CFRetain(CFArrayGetValueAtIndex((CFArrayRef)wins, 0));
      }
      if (wins) CFRelease(wins);
    }
    bool ok = false;
    if (win) {
      CGPoint pos = CGPointMake(req->x, req->y);
      CGSize size = CGSizeMake(req->w, req->h);
      AXValueRef pv = AXValueCreate(kAXValueTypeCGPoint, &pos);
      AXValueRef sv = AXValueCreate(kAXValueTypeCGSize, &size);
      AXError e1 = AXUIElementSetAttributeValue((AXUIElementRef)win,
                                                kAXPositionAttribute, pv);
      AXError e2 = AXUIElementSetAttributeValue((AXUIElementRef)win,
                                                kAXSizeAttribute, sv);
      ok = (e1 == kAXErrorSuccess && e2 == kAXErrorSuccess);
      CFRelease(pv);
      CFRelease(sv);
      CFRelease(win);
    }
    CFRelease(axApp);
    sock_write_line("GOT " + qid + " {\"ok\":" + (ok ? "true" : "false") +
                    ",\"error\":" +
                    (ok ? "null" : "\"no movable window\"") + "}");
  }
  delete req;
}
#else
static std::string ax_selected_text() { return "null"; }
static std::string ax_other_windows() { return "null"; }
static void do_winctrl(webview_t, void *arg) {
  WinCtrlReq *req = static_cast<WinCtrlReq *>(arg);
  sock_write_line("GOT " + req->qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  delete req;
}
#endif

// --- custom context menu (macOS) -------------------------------------------------
// CTXBEGIN, ITEM/SEP lines, CTXEND replaces the webview's right-click menu
// with the declared items (clicks -> `CTX <id>`); CTXCLEAR restores WebKit's
// default menu.

static std::vector<MenuItemSpec> g_ctx_items;
static bool g_ctx_active = false;
// contextMenu:false in the manifest suppresses WebKit's default right-click
// menu (Reload/Back/Inspect Element…) for app-like windows. A custom menu
// (g_ctx_active) always wins; this only affects the otherwise-default menu.
static bool g_ctx_suppress = false;

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
  if (g_ctx_active) {
    [menu removeAllItems];
    build_menu_into(menu, g_ctx_items, @selector(ctxItemClicked:), nil);
    return;
  }
  // Empty menu -> AppKit shows nothing, so removeAllItems suppresses it.
  if (g_ctx_suppress)
    [menu removeAllItems];
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

struct CtxSuppressReq {
  bool on;
};
static void apply_ctx_suppress(webview_t, void *arg) {
  CtxSuppressReq *req = static_cast<CtxSuppressReq *>(arg);
  g_ctx_suppress = req->on;
  delete req;
}

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

static std::string battery_json();
static std::string wifi_json();

static void do_get(webview_t w, void *arg) {
  GetReq *req = static_cast<GetReq *>(arg);
  std::string json = "null";
  @autoreleasepool {
    if (req->what == "windows") {
      json = "[\"main\"";
      for (auto &kv : g_windows)
        json += "," + json_escape(kv.first);
      json += "]";
    } else if (req->what == "win" || req->what.rfind("win:", 0) == 0) {
      std::string wid = req->what == "win" ? "main" : req->what.substr(4);
      NSWindow *win = window_for_id(w, wid);
      WKWebView *gwv = webview_for_id(w, wid);
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
            "\"clickThrough\":%s,\"level\":\"%s\",\"allSpaces\":%s,"
            "\"chrome\":{\"frame\":%s,\"trafficLights\":%s,"
            "\"transparent\":%s,\"vibrancy\":%s,\"squareCorners\":%s,"
            "\"acceptsFirstMouse\":%s},"
            "\"screen\":{\"width\":%d,\"height\":%d,\"scale\":%.2f}}",
            (int)f.origin.x, (int)(top - NSMaxY(f)), (int)f.size.width,
            (int)f.size.height, fs ? "true" : "false",
            win.miniaturized ? "true" : "false", win.visible ? "true" : "false",
            win.keyWindow ? "true" : "false",
            win.level != NSNormalWindowLevel ? "true" : "false",
            (win.styleMask & NSWindowStyleMaskResizable) ? "true" : "false",
            win.ignoresMouseEvents ? "true" : "false",
            win.level == kCGDesktopWindowLevel      ? "desktop"
            : win.level == kCGScreenSaverWindowLevel ? "overlay"
            : win.level == NSFloatingWindowLevel     ? "floating"
                                                     : "normal",
            (win.collectionBehavior &
             NSWindowCollectionBehaviorCanJoinAllSpaces)
                ? "true"
                : "false",
            // Chrome derived from the live window, so secondary windows
            // report their own state (not the main globals). A borderless
            // (square) window has neither titlebar nor traffic lights.
            (!(win.styleMask & NSWindowStyleMaskTitled) ||
             ((win.styleMask & NSWindowStyleMaskFullSizeContentView) &&
              win.titlebarAppearsTransparent))
                ? "false"
                : "true",
            (!(win.styleMask & NSWindowStyleMaskTitled) ||
             [win standardWindowButton:NSWindowCloseButton].hidden)
                ? "false"
                : "true",
            win.opaque ? "false" : "true",
            // vibrancy name is tracked for main only; secondary → null.
            (wid == "main" && !g_chrome_vibrancy.empty())
                ? json_escape(g_chrome_vibrancy).c_str()
                : "null",
            // Square = borderless = no Titled style bit.
            (win.styleMask & NSWindowStyleMaskTitled) ? "false" : "true",
            get_accepts_first_mouse(gwv) ? "true" : "false",
            (int)scr.frame.size.width, (int)scr.frame.size.height,
            (double)scr.backingScaleFactor);
        json = buf;
      }
    } else if (req->what == "battery") {
      json = battery_json();
    } else if (req->what == "wifi") {
      json = wifi_json();
    } else if (req->what == "selectedtext") {
      json = ax_selected_text();
    } else if (req->what == "otherwindows") {
      json = ax_other_windows();
    } else if (req->what == "clipboard" || req->what == "clipboard:count") {
      json = clipboard_json(req->what == "clipboard:count");
    } else if (req->what == "mouse" || req->what.rfind("mouse:", 0) == 0) {
      // Global cursor position in the same top-left coordinates WINOP pos
      // and getState use, so setPosition(mouse.x, mouse.y) just works;
      // `screen` is the display the cursor is on (frame in those coords).
      // `window` is relative to the queried window's CONTENT area (top-left,
      // same units as the page's clientX/clientY) — mouse:<winid> targets a
      // secondary window, bare = main.
      NSPoint p = [NSEvent mouseLocation]; // bottom-left origin
      CGFloat top = NSMaxY([[NSScreen screens][0] frame]);
      NSScreen *scr = nil;
      for (NSScreen *s in [NSScreen screens])
        if (NSMouseInRect(p, s.frame, NO)) {
          scr = s;
          break;
        }
      if (!scr)
        scr = [NSScreen mainScreen];
      NSRect sf = scr.frame;
      std::string wid = req->what == "mouse" ? "main" : req->what.substr(6);
      NSWindow *win = window_for_id(w, wid);
      std::string winjson = "null";
      if (win) {
        NSRect cf = [win contentRectForFrameRect:win.frame]; // screen coords
        char wbuf[128];
        std::snprintf(wbuf, sizeof(wbuf),
                      "{\"x\":%d,\"y\":%d,\"inside\":%s}",
                      (int)(p.x - cf.origin.x), (int)(NSMaxY(cf) - p.y),
                      NSMouseInRect(p, cf, NO) ? "true" : "false");
        winjson = wbuf;
      }
      char buf[384];
      std::snprintf(
          buf, sizeof(buf),
          "{\"x\":%d,\"y\":%d,\"window\":%s,\"screen\":{\"x\":%d,\"y\":%d,"
          "\"width\":%d,\"height\":%d,\"scale\":%.2f}}",
          (int)p.x, (int)(top - p.y), winjson.c_str(), (int)sf.origin.x,
          (int)(top - NSMaxY(sf)), (int)sf.size.width, (int)sf.size.height,
          (double)scr.backingScaleFactor);
      json = buf;
    } else if (req->what == "screens") {
      // Every display, in the same top-left global coordinates WINOP pos /
      // getState / mousePosition use, so setPosition against any screen's
      // frame just works. visible excludes the menu bar and Dock. primary
      // is the menu-bar screen (screens[0], the coordinate origin).
      CGFloat top = NSMaxY([[NSScreen screens][0] frame]);
      json = "[";
      bool first_scr = true;
      for (NSScreen *s in [NSScreen screens]) {
        NSRect f = s.frame, v = s.visibleFrame;
        NSNumber *num = s.deviceDescription[@"NSScreenNumber"];
        std::string name = "null";
        if (@available(macOS 10.15, *))
          name = json_escape([s.localizedName UTF8String]);
        char buf[512];
        std::snprintf(
            buf, sizeof(buf),
            "{\"id\":%ld,\"name\":%s,"
            "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
            "\"visible\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
            "\"scale\":%.2f,\"primary\":%s}",
            (long)num.integerValue, name.c_str(), (int)f.origin.x,
            (int)(top - NSMaxY(f)), (int)f.size.width, (int)f.size.height,
            (int)v.origin.x, (int)(top - NSMaxY(v)), (int)v.size.width,
            (int)v.size.height, (double)s.backingScaleFactor,
            s == [NSScreen screens][0] ? "true" : "false");
        if (!first_scr)
          json += ",";
        first_scr = false;
        json += buf;
      }
      json += "]";
    } else if (req->what == "idle") {
      // Seconds since the user's last input, session-wide (pause polling /
      // dim UI when the user walks away).
      double s = CGEventSourceSecondsSinceLastEventType(
          kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
      char buf[64];
      std::snprintf(buf, sizeof(buf), "{\"seconds\":%.3f}", s);
      json = buf;
    } else if (req->what == "frontmost") {
      // The active app right now (palettes: who focus returns to on hide()).
      NSRunningApplication *fa =
          [[NSWorkspace sharedWorkspace] frontmostApplication];
      if (fa) {
        json = std::string("{\"name\":") +
               (fa.localizedName ? json_escape([fa.localizedName UTF8String])
                                 : "null") +
               ",\"bundleId\":" +
               (fa.bundleIdentifier
                    ? json_escape([fa.bundleIdentifier UTF8String])
                    : "null") +
               ",\"pid\":" + std::to_string((long)fa.processIdentifier) + "}";
      }
    } else if (req->what == "debug:trafficpos") {
      NSWindow *win = (NSWindow *)webview_get_native_handle(
          w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
      NSButton *btn = win ? [win standardWindowButton:NSWindowCloseButton] : nil;
      if (btn && !btn.hidden) {
        NSRect r = [btn convertRect:btn.bounds toView:nil]; // window coords
        double fromTop = win.frame.size.height - NSMaxY(r);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "{\"fromTop\":%.1f,\"x\":%.1f}",
                      fromTop, r.origin.x);
        json = buf;
      } else {
        json = "{\"hidden\":true}";
      }
    } else if (req->what == "debug:activation") {
      NSApplicationActivationPolicy p = [NSApp activationPolicy];
      const char *pol = p == NSApplicationActivationPolicyRegular ? "regular"
                        : p == NSApplicationActivationPolicyAccessory
                            ? "accessory"
                            : "prohibited";
      json = std::string("{\"policy\":\"") + pol + "\"" +
             ",\"active\":" + ([NSApp isActive] ? "true" : "false") +
             ",\"hidden\":" + ([NSApp isHidden] ? "true" : "false") + "}";
    } else if (req->what == "traypos") {
      // The tray icon's on-screen rect in the same top-left coordinates as
      // setPosition — anchor a dropdown/panel window under the icon.
      if (g_status_item && g_status_item.button.window) {
        NSRect f = g_status_item.button.window.frame;
        CGFloat sTop = NSMaxY([[NSScreen screens][0] frame]);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
                      (int)f.origin.x, (int)(sTop - NSMaxY(f)),
                      (int)f.size.width, (int)f.size.height);
        json = buf;
      }
    } else if (req->what == "debug:tray") {
      // NSStatusItem windows are system-hosted (invisible to CGWindowList),
      // so tests read the item's wiring here instead.
      if (g_status_item) {
        NSStatusBarButton *btn = g_status_item.button;
        json = std::string("{\"exists\":true,\"menuAttached\":") +
               (g_status_item.menu ? "true" : "false") +
               ",\"menuHeld\":" + (g_tray_menu ? "true" : "false") +
               ",\"icon\":" + (btn.image ? "true" : "false") + ",\"title\":" +
               json_escape(btn.title ? [btn.title UTF8String] : "") + "}";
      } else {
        json = "{\"exists\":false}";
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

// --- native notifications (macOS, bundle mode only) ------------------------------
// NOTIFY <id>\t<title>\t<body>\t<subtitle>\t<sound01>. Requires a real bundle
// (UNUserNotificationCenter refuses bare processes), so this only runs when
// the launcher is the .app executable; dev builds use the bridge's osascript
// fallback. Authorization is requested lazily on the first notification and
// pending ones queue until the user answers. Banner clicks come back as
// `NOTIFYCLICK <id>` — including the click that launched the app.

struct NotifReq {
  std::string id, title, body, subtitle, actions_json;
  bool sound = false;
};

#ifdef __APPLE__
@interface TinyNotifDelegate : NSObject <UNUserNotificationCenterDelegate>
@end
@implementation TinyNotifDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler {
  std::string nid = [response.notification.request.identifier UTF8String];
  if ([response.actionIdentifier
          isEqualToString:UNNotificationDefaultActionIdentifier]) {
    sock_write_line("NOTIFYCLICK " + nid);
  } else if (![response.actionIdentifier
                 isEqualToString:UNNotificationDismissActionIdentifier]) {
    // A custom action button (or a reply field submit) was tapped.
    std::string reply;
    if ([response isKindOfClass:[UNTextInputNotificationResponse class]])
      reply = [[(UNTextInputNotificationResponse *)response userText] UTF8String];
    sock_write_line("NOTIFYACTION " + nid + "\t" +
                    std::string([response.actionIdentifier UTF8String]) + "\t" +
                    wire_escape(reply));
  }
  completionHandler();
}
// Show banners even while the app is frontmost (default is to suppress).
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:
             (void (^)(UNNotificationPresentationOptions))completionHandler {
  completionHandler(UNNotificationPresentationOptionBanner |
                    UNNotificationPresentationOptionList);
}
@end

static TinyNotifDelegate *g_notif_delegate = nil;
enum class NotifAuth { Unasked, Pending, Granted, Denied, Fallback };
static NotifAuth g_notif_auth = NotifAuth::Unasked;
static std::vector<NotifReq> g_notif_queue;

static void install_notif_delegate() {
  if (!g_bundle_mode)
    return;
  g_notif_delegate = [[TinyNotifDelegate alloc] init];
  [UNUserNotificationCenter currentNotificationCenter].delegate =
      g_notif_delegate;
}

// Ad-hoc-signed apps are refused by Notification Center outright (error,
// no prompt). Fall back to osascript so notify() still works in unsigned
// builds; a real signing identity upgrades to native banners.
static void deliver_osascript(const NotifReq &req) {
  auto q = [](const std::string &v) {
    std::string out = "\"";
    for (char ch : v) {
      if (ch == '\\' || ch == '"')
        out += '\\';
      out += ch;
    }
    return out + "\"";
  };
  std::string script = "display notification " + q(req.body) +
                       " with title " + q(req.title);
  if (!req.subtitle.empty())
    script += " subtitle " + q(req.subtitle);
  const char *sargv[] = {"/usr/bin/osascript", "-e", script.c_str(), nullptr};
  pid_t pid;
  posix_spawn(&pid, "/usr/bin/osascript", nullptr, nullptr,
              const_cast<char *const *>(sargv), environ);
}

// Action buttons / reply fields need a UNNotificationCategory registered
// before delivery. Each notification with actions gets its own category
// (id = "tinyjs-cat-" + notif id); categories accumulate because
// setNotificationCategories replaces the whole set.
static NSMutableDictionary<NSString *, UNNotificationCategory *> *g_notif_cats;

static NSString *register_notif_category(const std::string &nid,
                                         const std::string &actions_json) {
  @autoreleasepool {
    NSData *d = [ns(actions_json) dataUsingEncoding:NSUTF8StringEncoding];
    NSArray *arr = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
    if (![arr isKindOfClass:[NSArray class]] || arr.count == 0)
      return nil;
    NSMutableArray<UNNotificationAction *> *actions = [NSMutableArray array];
    for (NSDictionary *a in arr) {
      if (![a isKindOfClass:[NSDictionary class]])
        continue;
      NSString *aid = a[@"id"], *title = a[@"title"] ?: a[@"id"];
      if (!aid)
        continue;
      UNNotificationActionOptions opt =
          [a[@"destructive"] boolValue] ? UNNotificationActionOptionDestructive
                                        : UNNotificationActionOptionNone;
      if ([a[@"reply"] boolValue]) {
        [actions addObject:[UNTextInputNotificationAction
                               actionWithIdentifier:aid
                                                title:title
                                              options:opt
                                 textInputButtonTitle:(a[@"buttonTitle"] ?: title)
                                 textInputPlaceholder:(a[@"placeholder"] ?: @"")]];
      } else {
        [actions addObject:[UNNotificationAction actionWithIdentifier:aid
                                                                title:title
                                                              options:opt]];
      }
    }
    if (actions.count == 0)
      return nil;
    NSString *catId =
        [NSString stringWithFormat:@"tinyjs-cat-%s", nid.c_str()];
    UNNotificationCategory *cat =
        [UNNotificationCategory categoryWithIdentifier:catId
                                               actions:actions
                                     intentIdentifiers:@[]
                                               options:UNNotificationCategoryOptionNone];
    if (!g_notif_cats)
      g_notif_cats = [[NSMutableDictionary alloc] init];
    g_notif_cats[catId] = cat;
    [[UNUserNotificationCenter currentNotificationCenter]
        setNotificationCategories:[NSSet setWithArray:g_notif_cats.allValues]];
    return catId;
  }
}

static void deliver_notification(const NotifReq &req) {
  UNMutableNotificationContent *content =
      [[[UNMutableNotificationContent alloc] init] autorelease];
  content.title = ns(req.title);
  if (!req.body.empty())
    content.body = ns(req.body);
  if (!req.subtitle.empty())
    content.subtitle = ns(req.subtitle);
  if (req.sound)
    content.sound = [UNNotificationSound defaultSound];
  if (!req.actions_json.empty()) {
    NSString *catId = register_notif_category(
        req.id.empty() ? "anon" : req.id, req.actions_json);
    if (catId)
      content.categoryIdentifier = catId;
  }
  NSString *nid =
      req.id.empty() ? [[NSUUID UUID] UUIDString] : ns(req.id);
  UNNotificationRequest *r =
      [UNNotificationRequest requestWithIdentifier:nid
                                           content:content
                                           trigger:nil];
  [[UNUserNotificationCenter currentNotificationCenter]
      addNotificationRequest:r
       withCompletionHandler:^(NSError *err) {
         if (getenv("TINYJS_NOTIF_DEBUG"))
           std::fprintf(stderr, "DBG deliver err=%s\n",
                        err ? [[err description] UTF8String] : "none");
       }];
}

static void do_notify(webview_t, void *arg) {
  NotifReq *req = static_cast<NotifReq *>(arg);
  if (!g_bundle_mode) {
    delete req;
    return;
  }
  switch (g_notif_auth) {
  case NotifAuth::Granted:
    deliver_notification(*req);
    break;
  case NotifAuth::Fallback:
    deliver_osascript(*req);
    break;
  case NotifAuth::Pending:
    g_notif_queue.push_back(*req);
    break;
  case NotifAuth::Denied:
    break; // the user explicitly said no; respect it
  case NotifAuth::Unasked: {
    g_notif_auth = NotifAuth::Pending;
    g_notif_queue.push_back(*req);
    [[UNUserNotificationCenter currentNotificationCenter]
        requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                         UNAuthorizationOptionSound |
                                         UNAuthorizationOptionBadge)
                      completionHandler:^(BOOL granted, NSError *err) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                          // granted=NO WITH an error and no prompt means the
                          // system refused (ad-hoc signature) — fall back.
                          // granted=NO without an error is the user's choice.
                          g_notif_auth = granted ? NotifAuth::Granted
                                        : err    ? NotifAuth::Fallback
                                                 : NotifAuth::Denied;
                          for (const NotifReq &n : g_notif_queue) {
                            if (g_notif_auth == NotifAuth::Granted)
                              deliver_notification(n);
                            else if (g_notif_auth == NotifAuth::Fallback)
                              deliver_osascript(n);
                          }
                          g_notif_queue.clear();
                        });
                      }];
    break;
  }
  }
  delete req;
}
#else
static void install_notif_delegate() {}
static void do_notify(webview_t, void *arg) { delete static_cast<NotifReq *>(arg); }
#endif

// --- Now Playing + media keys (macOS) -----------------------------------------------
// NOWPLAYING <json> populates MPNowPlayingInfoCenter (Control Center / lock
// screen) and, on first use, wires MPRemoteCommandCenter so the hardware
// media keys, AirPods taps, and Control Center transport route to the app as
// `MEDIAKEY <name>` (play/pause/toggle/next/previous/seek\t<secs>). Any
// value nowPlayingInfo needs sits in the JSON; "clear" tears it all down.

struct NowPlayingReq {
  std::string json; // "" / "clear" = clear
};

#ifdef __APPLE__
static bool g_media_armed = false;

static void arm_media_commands() {
  if (g_media_armed)
    return;
  g_media_armed = true;
  MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
  auto bind = [](MPRemoteCommand *cmd, const char *name) {
    cmd.enabled = YES;
    [cmd addTargetWithHandler:^MPRemoteCommandHandlerStatus(
             MPRemoteCommandEvent *ev) {
      if ([ev isKindOfClass:[MPChangePlaybackPositionCommandEvent class]]) {
        double t = ((MPChangePlaybackPositionCommandEvent *)ev).positionTime;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "MEDIAKEY seek\t%.3f", t);
        sock_write_line(buf);
      } else {
        sock_write_line(std::string("MEDIAKEY ") + name);
      }
      return MPRemoteCommandHandlerStatusSuccess;
    }];
  };
  bind(cc.playCommand, "play");
  bind(cc.pauseCommand, "pause");
  bind(cc.togglePlayPauseCommand, "toggle");
  bind(cc.nextTrackCommand, "next");
  bind(cc.previousTrackCommand, "previous");
  bind(cc.changePlaybackPositionCommand, "seek");
}

static void do_nowplaying(webview_t, void *arg) {
  NowPlayingReq *req = static_cast<NowPlayingReq *>(arg);
  @autoreleasepool {
    MPNowPlayingInfoCenter *ic = [MPNowPlayingInfoCenter defaultCenter];
    if (req->json.empty() || req->json == "clear") {
      ic.nowPlayingInfo = nil;
      ic.playbackState = MPNowPlayingPlaybackStateStopped;
    } else {
      arm_media_commands();
      NSData *d = [ns(req->json) dataUsingEncoding:NSUTF8StringEncoding];
      NSDictionary *j =
          [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
      NSMutableDictionary *info = [NSMutableDictionary dictionary];
      if ([j[@"title"] isKindOfClass:[NSString class]])
        info[MPMediaItemPropertyTitle] = j[@"title"];
      if ([j[@"artist"] isKindOfClass:[NSString class]])
        info[MPMediaItemPropertyArtist] = j[@"artist"];
      if ([j[@"album"] isKindOfClass:[NSString class]])
        info[MPMediaItemPropertyAlbumTitle] = j[@"album"];
      if ([j[@"duration"] isKindOfClass:[NSNumber class]])
        info[MPMediaItemPropertyPlaybackDuration] = j[@"duration"];
      if ([j[@"elapsed"] isKindOfClass:[NSNumber class]])
        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = j[@"elapsed"];
      bool playing = [j[@"playing"] boolValue];
      info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
      ic.nowPlayingInfo = info;
      ic.playbackState = playing ? MPNowPlayingPlaybackStatePlaying
                                 : MPNowPlayingPlaybackStatePaused;
    }
  }
  delete req;
}
#else
static void do_nowplaying(webview_t, void *arg) {
  delete static_cast<NowPlayingReq *>(arg);
}
#endif

// --- speech synthesis (macOS) -------------------------------------------------------
// SAY <qid> <text>\t<voice>\t<rate> speaks with AVSpeechSynthesizer and
// answers GOT {ok} when the utterance FINISHES (so `await say()` waits for
// playback). VOICES lists installed voices; SAYSTOP interrupts.

struct SayReq {
  std::string qid, text, voice;
  double rate;
};
struct VoicesReq {
  std::string qid;
};

#ifdef __APPLE__
@interface TinySpeechDelegate : NSObject <AVSpeechSynthesizerDelegate>
@property(strong) NSMutableDictionary<NSValue *, NSString *> *qids; // utterance -> qid
@end
@implementation TinySpeechDelegate
- (void)finish:(AVSpeechUtterance *)u ok:(BOOL)ok {
  NSValue *k = [NSValue valueWithNonretainedObject:u];
  NSString *qid = self.qids[k];
  if (qid) {
    sock_write_line("GOT " + std::string([qid UTF8String]) +
                    " {\"ok\":" + (ok ? "true" : "false") + "}");
    [self.qids removeObjectForKey:k];
  }
}
- (void)speechSynthesizer:(AVSpeechSynthesizer *)s
    didFinishSpeechUtterance:(AVSpeechUtterance *)u {
  [self finish:u ok:YES];
}
- (void)speechSynthesizer:(AVSpeechSynthesizer *)s
    didCancelSpeechUtterance:(AVSpeechUtterance *)u {
  [self finish:u ok:NO];
}
@end

static AVSpeechSynthesizer *g_synth = nil;
static TinySpeechDelegate *g_synth_delegate = nil;

static void do_say(webview_t, void *arg) {
  SayReq *req = static_cast<SayReq *>(arg);
  @autoreleasepool {
    if (!g_synth) {
      g_synth = [[AVSpeechSynthesizer alloc] init];
      g_synth_delegate = [[TinySpeechDelegate alloc] init];
      g_synth_delegate.qids = [NSMutableDictionary dictionary];
      g_synth.delegate = g_synth_delegate;
    }
    AVSpeechUtterance *u =
        [AVSpeechUtterance speechUtteranceWithString:ns(req->text)];
    if (!req->voice.empty()) {
      AVSpeechSynthesisVoice *v =
          [AVSpeechSynthesisVoice voiceWithIdentifier:ns(req->voice)]
              ?: [AVSpeechSynthesisVoice voiceWithLanguage:ns(req->voice)];
      if (v)
        u.voice = v;
    }
    if (req->rate > 0)
      u.rate = (float)req->rate; // 0..1 (AVSpeechUtteranceDefaultSpeechRate ~0.5)
    g_synth_delegate.qids[[NSValue valueWithNonretainedObject:u]] =
        ns(req->qid);
    [g_synth speakUtterance:u];
  }
  delete req;
}

static void do_saystop(webview_t, void *) {
  // Resolve every pending utterance as interrupted here rather than trust
  // didCancelSpeechUtterance, which the framework skips when a stop lands
  // during synthesis latency (before playback starts).
  if (g_synth_delegate) {
    for (NSValue *k in g_synth_delegate.qids.allKeys) {
      NSString *qid = g_synth_delegate.qids[k];
      sock_write_line("GOT " + std::string([qid UTF8String]) +
                      " {\"ok\":false}");
    }
    [g_synth_delegate.qids removeAllObjects];
  }
  [g_synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

static void do_voices(webview_t, void *arg) {
  VoicesReq *req = static_cast<VoicesReq *>(arg);
  @autoreleasepool {
    std::string json = "[";
    bool first = true;
    for (AVSpeechSynthesisVoice *v in [AVSpeechSynthesisVoice speechVoices]) {
      const char *q = v.quality == AVSpeechSynthesisVoiceQualityPremium
                          ? "premium"
                          : v.quality == AVSpeechSynthesisVoiceQualityEnhanced
                                ? "enhanced"
                                : "default";
      if (!first)
        json += ",";
      first = false;
      json += "{\"id\":" + json_escape([v.identifier UTF8String]) +
              ",\"name\":" + json_escape([v.name UTF8String]) +
              ",\"lang\":" + json_escape([v.language UTF8String]) +
              ",\"quality\":\"" + q + "\"}";
    }
    json += "]";
    sock_write_line("GOT " + req->qid + " {\"ok\":true,\"voices\":" + json + "}");
  }
  delete req;
}
#else
static void do_say(webview_t, void *arg) {
  SayReq *r = static_cast<SayReq *>(arg);
  sock_write_line("GOT " + r->qid + " {\"ok\":false}");
  delete r;
}
static void do_saystop(webview_t, void *) {}
static void do_voices(webview_t, void *arg) {
  VoicesReq *r = static_cast<VoicesReq *>(arg);
  sock_write_line("GOT " + r->qid + " {\"ok\":true,\"voices\":[]}");
  delete r;
}
#endif

// --- window chrome: frameless / traffic lights / transparency / vibrancy ---------
// CHROME <frame>\t<traffic>\t<transparent>\t<vibrancy> ('' = leave unchanged;
// frame/traffic/transparent are 0|1; vibrancy is a material name or "none").
// "Frameless" keeps the window titled (fullSizeContentView + transparent,
// hidden titlebar) so focus, resize edges, shadows, rounded corners, and
// fullscreen all keep working — unlike true borderless. DRAGWIN starts a
// native window drag (pages mark drag regions with data-tiny-drag).

struct ChromeReq {
  std::string win = "main";
  std::string frame, traffic, transparent, vibrancy, square, first_mouse;
};

#ifdef __APPLE__
static NSVisualEffectView *g_effect_view = nil;

static NSVisualEffectMaterial material_for(const std::string &name) {
  static const std::map<std::string, NSVisualEffectMaterial> m = {
      {"titlebar", NSVisualEffectMaterialTitlebar},
      {"selection", NSVisualEffectMaterialSelection},
      {"menu", NSVisualEffectMaterialMenu},
      {"popover", NSVisualEffectMaterialPopover},
      {"sidebar", NSVisualEffectMaterialSidebar},
      {"header", NSVisualEffectMaterialHeaderView},
      {"sheet", NSVisualEffectMaterialSheet},
      {"window", NSVisualEffectMaterialWindowBackground},
      {"hud", NSVisualEffectMaterialHUDWindow},
      {"fullscreen", NSVisualEffectMaterialFullScreenUI},
      {"tooltip", NSVisualEffectMaterialToolTip},
      {"content", NSVisualEffectMaterialContentBackground},
      {"underwindow", NSVisualEffectMaterialUnderWindowBackground},
      {"underpage", NSVisualEffectMaterialUnderPageBackground},
  };
  auto it = m.find(name);
  return it == m.end() ? NSVisualEffectMaterialSidebar : it->second;
}

// The hidden titlebar's NSTitlebarContainerView still occupies (and drags
// from) the top strip; hide it entirely when frameless with no traffic
// lights so the page owns every pixel and every mouse event.
static void set_titlebar_hidden(NSWindow *win, bool hidden) {
  NSView *frameView = win.contentView.superview;
  for (NSView *v in frameView.subviews) {
    if ([v isKindOfClass:NSClassFromString(@"NSTitlebarContainerView")])
      v.hidden = hidden;
  }
}

// After styleMask/frame changes the private NSTitlebarContainerView can be
// left at a stale position (traffic lights floating outside the window).
// Re-anchor it to the top of the frame view explicitly.
static void relayout_titlebar(NSWindow *win) {
  NSView *frameView = win.contentView.superview;
  CGFloat H = frameView.bounds.size.height;
  CGFloat W = frameView.bounds.size.width;
  for (NSView *v in frameView.subviews) {
    if ([v isKindOfClass:NSClassFromString(@"NSTitlebarContainerView")]) {
      CGFloat h = v.frame.size.height > 0 ? v.frame.size.height : 28;
      v.frame = NSMakeRect(0, H - h, W, h);
      [v setNeedsLayout:YES];
    }
  }
}

// Toggling fullSizeContentView grows the content view, but the webview's
// autoresizing doesn't reliably follow a style-mask change — pin it (and the
// vibrancy layer) to the new bounds explicitly.
static void extend_content(WKWebView *wv, NSWindow *win,
                           NSVisualEffectView *effect) {
  if (wv)
    wv.frame = win.contentView.bounds;
  if (effect)
    effect.frame = win.contentView.bounds;
}

static void webview_draws_background(WKWebView *wv, bool draws) {
  // Private-but-stable WKWebView property, reached via KVC's _drawsBackground
  // fallback (same trick the webview library uses for WKPreferences).
  @try {
    [wv setValue:@(draws) forKey:@"drawsBackground"];
  } @catch (NSException *) {
  }
}

static NSWindow *window_for_id(webview_t w, const std::string &id);
static WKWebView *webview_for_id(webview_t w, const std::string &id);
static NSVisualEffectView **effect_slot_for(const std::string &id); // main/secondary

// Borderless windows (square corners) return NO for canBecomeKeyWindow, so
// they can't take keyboard focus. Force YES on the window's class — always
// the correct answer for our windows, so a per-class replace is safe.
static void ensure_window_can_key(Class cls) {
  if (!cls)
    return;
  IMP yes = (IMP)(+[](id, SEL) -> BOOL { return YES; });
  class_replaceMethod(cls, @selector(canBecomeKeyWindow), yes, "c@:");
  class_replaceMethod(cls, @selector(canBecomeMainWindow), yes, "c@:");
}

// First-mouse ("click-through focus"): WKWebView answers NO to
// acceptsFirstMouse: — Apple's guard against stray clicks into web content —
// so a click that only makes the window key is swallowed and the page never
// sees the mousedown (the classic "click once to focus, again to act", and
// why an unfocused window's DOM drag region needs an extra click). We opt in
// per window: a swizzle on WKWebView returns a per-instance flag (default NO,
// exactly Apple's behavior), set via an associated object so only windows
// that asked for it change.
static const void *kFirstMouseKey = &kFirstMouseKey;

static void set_accepts_first_mouse(WKWebView *wv, bool on) {
  if (wv)
    objc_setAssociatedObject(wv, kFirstMouseKey, @(on),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static bool get_accepts_first_mouse(WKWebView *wv) {
  return wv && [objc_getAssociatedObject(wv, kFirstMouseKey) boolValue];
}

static void install_first_mouse_hook() {
  // Instances the webview library creates are plain WKWebView, so patching the
  // class reaches them; the per-instance flag keeps every other webview at NO.
  swizzle([WKWebView class], @selector(acceptsFirstMouse:),
          (IMP)(+[](id self, SEL, NSEvent *) -> BOOL {
            return get_accepts_first_mouse((WKWebView *)self);
          }),
          "c@:@");
}

// --- media proxy scheme handler (tiny.proxyURL) --------------------------------
// A page can't run a cross-origin stream (internet radio) through Web Audio: a
// MediaElementSource on a cross-origin <audio> outputs silence by spec, and a
// cross-origin fetch without CORS is blocked outright. This handler serves a
// custom scheme that proxies the upstream through the native layer and injects
// Access-Control-Allow-Origin:*, so <audio crossorigin="anonymous"
// src="tiny-media://…"> (or a cors fetch) is CORS-approved and its samples
// reach the EQ/analyser graph. The launcher does the HTTP itself (NSURLSession
// — native buffering, redirects, byte-range/seek, HLS), so there's no backend
// hop and no base64. tiny.proxyURL(remote) builds the URL; the handler is
// registered on every webview via a swizzle of -[WKWebView init…] (below).
#define TINY_MEDIA_SCHEME @"tiny-media"

@interface TinyMediaScheme : NSObject <WKURLSchemeHandler, NSURLSessionDataDelegate>
@end

@implementation TinyMediaScheme {
  NSURLSession *_session;
  NSMutableSet *_live;         // id<WKURLSchemeTask> still running (page side)
  NSMapTable *_dataToScheme;   // NSURLSessionTask* -> id<WKURLSchemeTask>
}
- (instancetype)init {
  if ((self = [super init])) {
    _live = [[NSMutableSet alloc] init];
    _dataToScheme = [[NSMapTable strongToStrongObjectsMapTable] retain];
    NSURLSessionConfiguration *sc =
        [NSURLSessionConfiguration defaultSessionConfiguration];
    sc.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    // Deliver on the main queue so start/stop and the data callbacks are all
    // serialized there — the _live guard then needs no locks and we never
    // touch a task WebKit has already stopped (which would raise).
    _session = [[NSURLSession sessionWithConfiguration:sc
                                              delegate:self
                                         delegateQueue:[NSOperationQueue mainQueue]] retain];
  }
  return self;
}
// The live scheme task for an upstream data task, or nil if it was stopped.
- (id<WKURLSchemeTask>)liveFor:(NSURLSessionTask *)dt {
  id<WKURLSchemeTask> st = [_dataToScheme objectForKey:dt];
  return (st && [_live containsObject:st]) ? st : nil;
}
- (void)webView:(WKWebView *)wv startURLSchemeTask:(id<WKURLSchemeTask>)task {
  NSURLComponents *c = [NSURLComponents componentsWithURL:task.request.URL
                                  resolvingAgainstBaseURL:NO];
  NSString *upstream = nil;
  for (NSURLQueryItem *qi in c.queryItems)
    if ([qi.name isEqualToString:@"u"]) upstream = qi.value; // already decoded
  NSURL *uu = upstream ? [NSURL URLWithString:upstream] : nil;
  NSString *sch = uu.scheme.lowercaseString;
  if (!uu || !([sch isEqualToString:@"http"] || [sch isEqualToString:@"https"])) {
    [task didFailWithError:[NSError errorWithDomain:@"tinyjs" code:400
        userInfo:@{NSLocalizedDescriptionKey:
                   @"tiny.proxyURL: only http/https URLs can be proxied"}]];
    return;
  }
  [_live addObject:task];
  NSMutableURLRequest *up = [NSMutableURLRequest requestWithURL:uu];
  // Forward the bits that matter for media: range (seek) + any UA the page set.
  NSString *range = [task.request valueForHTTPHeaderField:@"Range"];
  if (range) [up setValue:range forHTTPHeaderField:@"Range"];
  NSString *ua = [task.request valueForHTTPHeaderField:@"User-Agent"];
  if (ua) [up setValue:ua forHTTPHeaderField:@"User-Agent"];
  NSURLSessionDataTask *dt = [_session dataTaskWithRequest:up];
  [_dataToScheme setObject:task forKey:dt];
  [dt resume];
}
- (void)webView:(WKWebView *)wv stopURLSchemeTask:(id<WKURLSchemeTask>)task {
  [_live removeObject:task];
  for (NSURLSessionTask *k in [[_dataToScheme keyEnumerator] allObjects]) {
    if ([_dataToScheme objectForKey:k] == task) {
      [k cancel];
      [_dataToScheme removeObjectForKey:k];
      break;
    }
  }
}
- (void)URLSession:(NSURLSession *)s
          dataTask:(NSURLSessionDataTask *)dt
didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))done {
  id<WKURLSchemeTask> st = [self liveFor:dt];
  if (!st) { done(NSURLSessionResponseCancel); return; }
  NSMutableDictionary *h = [NSMutableDictionary dictionary];
  NSInteger code = 200;
  // A live stream (icecast/shoutcast internet radio) sends its 200 with no
  // Content-Length and no byte-range support. WKWebView's custom-scheme media
  // loader then refuses the <audio> with error 4 (SRC_NOT_SUPPORTED) even
  // though the bytes are valid MP3/AAC and stream fine to fetch(). Advertise a
  // large fake Content-Length and hide range/chunked framing so the media
  // engine treats it as one long non-seekable resource and plays it
  // progressively — which lets MediaElementSource tap it for the EQ/analyser
  // graph. A finite file keeps its real Content-Length and stays seekable.
  //
  // Gated on an audio/video MIME so it only touches media: proxyURL also fronts
  // ordinary CORS fetches, and a fake length on a length-less JSON/text stream
  // would make fetch() error on completion (fewer bytes than promised).
  NSString *mime = response.MIMEType.lowercaseString;
  BOOL isMedia = [mime hasPrefix:@"audio/"] || [mime hasPrefix:@"video/"];
  BOOL endless = isMedia &&
                 (response.expectedContentLength == NSURLResponseUnknownLength);
  if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
    NSHTTPURLResponse *hr = (NSHTTPURLResponse *)response;
    code = hr.statusCode;
    // Pass upstream headers through (Content-Type, Content-Length, Accept-
    // Ranges, Content-Range for seek…) but drop any ACAO — we set our own.
    // For an endless stream also drop the length/range/chunked headers so they
    // don't contradict the synthetic Content-Length we add below.
    for (id k in hr.allHeaderFields) {
      NSString *lk = [[k description] lowercaseString];
      if ([lk hasPrefix:@"access-control-"]) continue;
      if (endless && ([lk isEqualToString:@"transfer-encoding"] ||
                      [lk isEqualToString:@"accept-ranges"] ||
                      [lk isEqualToString:@"content-length"] ||
                      [lk isEqualToString:@"content-range"])) continue;
      h[k] = hr.allHeaderFields[k];
    }
  }
  if (!h[@"Content-Type"] && response.MIMEType) h[@"Content-Type"] = response.MIMEType;
  // ~1 TiB — longer than any listening session; the element just streams until
  // we stop feeding it. Only for a successful 200 (redirects/errors untouched).
  if (endless && code == 200) h[@"Content-Length"] = @"1099511627776";
  h[@"Access-Control-Allow-Origin"] = @"*";
  h[@"Access-Control-Allow-Headers"] = @"*";
  h[@"Access-Control-Expose-Headers"] = @"*";
  NSHTTPURLResponse *out = [[[NSHTTPURLResponse alloc]
      initWithURL:st.request.URL statusCode:code HTTPVersion:@"HTTP/1.1"
     headerFields:h] autorelease];
  @try { [st didReceiveResponse:out]; }
  @catch (NSException *e) { done(NSURLSessionResponseCancel); return; }
  done(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)s
          dataTask:(NSURLSessionDataTask *)dt
    didReceiveData:(NSData *)data {
  id<WKURLSchemeTask> st = [self liveFor:dt];
  if (!st) return;
  @try { [st didReceiveData:data]; } @catch (NSException *e) {}
}
- (void)URLSession:(NSURLSession *)s
              task:(NSURLSessionTask *)dt
didCompleteWithError:(NSError *)err {
  id<WKURLSchemeTask> st = [_dataToScheme objectForKey:dt];
  [_dataToScheme removeObjectForKey:dt];
  if (!st || ![_live containsObject:st]) return;
  [_live removeObject:st];
  @try {
    if (err) [st didFailWithError:err];
    else [st didFinish];
  } @catch (NSException *e) {}
}
@end

static TinyMediaScheme *g_media_handler = nil;
typedef id (*WKInitIMP)(id, SEL, CGRect, id);
static WKInitIMP g_orig_wk_init = nullptr;

// Every WKWebView (the library's main one and our secondary windows) is built
// with -[initWithFrame:configuration:]; register the media scheme on the config
// here, before the original init consumes it (handlers can't be added after).
static id tiny_wk_init(id self, SEL _cmd, CGRect frame, id config) {
  @autoreleasepool {
    if (config && g_media_handler) {
      @try {
        if (![config urlSchemeHandlerForURLScheme:TINY_MEDIA_SCHEME])
          [config setURLSchemeHandler:g_media_handler
                         forURLScheme:TINY_MEDIA_SCHEME];
      } @catch (NSException *e) {}
    }
  }
  return g_orig_wk_init ? g_orig_wk_init(self, _cmd, frame, config) : self;
}

// Must run BEFORE webview_create (the main webview's init happens inside it).
static void install_media_scheme_hook() {
  g_media_handler = [[TinyMediaScheme alloc] init];
  g_orig_wk_init = (WKInitIMP)swizzle([WKWebView class],
      @selector(initWithFrame:configuration:), (IMP)tiny_wk_init,
      "@@:{CGRect={CGPoint=dd}{CGSize=dd}}@");
}

// ============================ tiny.audioTap ================================
// Read the app's (or the whole system's) *rendered* audio output as PCM, for
// VU meters / visualizers — including audio that never touches Web Audio
// (native HLS, CORS-tainted <audio>, other apps). Uses Core Audio process
// taps (macOS 14.2+): a CATapDescription -> AudioHardwareCreateProcessTap ->
// an aggregate device with that sub-tap -> an IOProc that reads float32,
// converts to interleaved Int16, and a main-queue timer that chunks + base64s
// it out as `AUDIOTAP <b64>\t<sr>\t<ch>\t<frames>\t<t>` frames to the backend
// (which push()es an 'audio-tap' page event). Read-only: it observes the mix,
// it can't process it (EQ still needs the signal in the graph — proxyURL).
//
// scope 'app'  -> tap every audio process object whose *responsible pid*
//   matches ours. WKWebView renders audio in a com.apple.WebKit.GPU XPC helper
//   (ppid 1, so not findable by walking children); the responsible-pid link is
//   the reliable way to select exactly our app's WebKit processes. In a bundle
//   the launcher is its own responsible root, so this is tight; in `dev` the
//   terminal is the root, so it can over-capture (dev-only).
// scope 'system' -> a global tap (optionally excluding our own processes).
//   Trips the "System Audio Recording" TCC prompt.
// TCC note: an unauthorized tap returns success but delivers SILENCE (zeroed
// buffers), not an error — so `denied` cannot be reported synchronously; it
// surfaces as chunks whose samples are all zero.
extern "C" pid_t responsibility_get_pid_responsible_for_pid(pid_t);

struct AudioTapReq { std::string qid; std::string scope; bool excludeSelf; int interval; };

static AudioObjectID g_tap_id = 0;
static AudioObjectID g_tap_agg = 0;
static AudioDeviceIOProcID g_tap_proc = nullptr;
static bool g_tap_active = false;
static bool g_tap_dev_listener = false;
static std::string g_tap_scope = "app";
static bool g_tap_exclude_self = false;
static int g_tap_interval = 80;
static double g_tap_sr = 48000;
static int g_tap_ch = 2;
static std::vector<int16_t> g_tap_pcm;      // interleaved Int16 accumulator
static std::mutex g_tap_pcm_lock;
static double g_tap_chunk_t0 = 0;           // monotonic ms of the chunk's first sample
static dispatch_source_t g_tap_timer = nullptr;

static double tiny_now_ms() {
  return (double)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1.0e6;
}

// Audio process objects belonging to our app (same responsible pid). Captures
// the whole app tree — launcher + WebKit GPU/WebContent/Networking helpers.
API_AVAILABLE(macos(14.2))
static NSArray<NSNumber *> *tiny_app_process_objects() {
  NSMutableArray *out = [NSMutableArray array];
  pid_t myResp = responsibility_get_pid_responsible_for_pid(getpid());
  AudioObjectPropertyAddress la = { kAudioHardwarePropertyProcessObjectList,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  UInt32 sz = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &la, 0, NULL, &sz) != noErr)
    return out;
  int n = sz / sizeof(AudioObjectID);
  std::vector<AudioObjectID> objs(n > 0 ? n : 0);
  if (n <= 0 ||
      AudioObjectGetPropertyData(kAudioObjectSystemObject, &la, 0, NULL, &sz, objs.data()) != noErr)
    return out;
  for (int i = 0; i < n; i++) {
    pid_t pid = 0; UInt32 s = sizeof(pid);
    AudioObjectPropertyAddress pa = { kAudioProcessPropertyPID,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(objs[i], &pa, 0, NULL, &s, &pid) != noErr || pid <= 0)
      continue;
    if (pid == getpid() ||
        (myResp > 0 && responsibility_get_pid_responsible_for_pid(pid) == myResp))
      [out addObject:@(objs[i])];
  }
  return out;
}

// Tear down the running tap graph (not the device-change listener). Safe to
// call repeatedly; leaves g_tap_active untouched (the caller owns that flag).
API_AVAILABLE(macos(14.2))
static void tiny_audiotap_teardown() {
  if (g_tap_timer) { dispatch_source_cancel(g_tap_timer); g_tap_timer = nullptr; }
  if (g_tap_agg && g_tap_proc) {
    AudioDeviceStop(g_tap_agg, g_tap_proc);
    AudioDeviceDestroyIOProcID(g_tap_agg, g_tap_proc);
  }
  g_tap_proc = nullptr;
  if (g_tap_agg) { AudioHardwareDestroyAggregateDevice(g_tap_agg); g_tap_agg = 0; }
  if (g_tap_id) { AudioHardwareDestroyProcessTap(g_tap_id); g_tap_id = 0; }
  std::lock_guard<std::mutex> lk(g_tap_pcm_lock);
  g_tap_pcm.clear();
  g_tap_chunk_t0 = 0;
}

API_AVAILABLE(macos(14.2))
static void tiny_audiotap_start_timer() {
  g_tap_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  uint64_t iv = (uint64_t)g_tap_interval * NSEC_PER_MSEC;
  dispatch_source_set_timer(g_tap_timer, dispatch_time(DISPATCH_TIME_NOW, iv), iv, iv / 10);
  dispatch_source_set_event_handler(g_tap_timer, ^{
    std::vector<int16_t> chunk; double t0 = 0;
    {
      std::lock_guard<std::mutex> lk(g_tap_pcm_lock);
      if (g_tap_pcm.empty()) return;
      chunk.swap(g_tap_pcm);
      t0 = g_tap_chunk_t0;
      g_tap_chunk_t0 = 0;
    }
    NSData *d = [NSData dataWithBytesNoCopy:chunk.data()
                                    length:chunk.size() * sizeof(int16_t)
                              freeWhenDone:NO];
    NSString *b64 = [d base64EncodedStringWithOptions:0];
    int ch = g_tap_ch > 0 ? g_tap_ch : 1;
    int frames = (int)(chunk.size() / ch);
    char meta[96];
    snprintf(meta, sizeof(meta), "\t%d\t%d\t%d\t%.1f", (int)g_tap_sr, g_tap_ch, frames, t0);
    sock_write_line(std::string("AUDIOTAP ") + [b64 UTF8String] + meta);
  });
  dispatch_resume(g_tap_timer);
}

// Build tap -> aggregate device -> IOProc and start it. Sets g_tap_sr/ch.
API_AVAILABLE(macos(14.2))
static OSStatus tiny_audiotap_build() {
  CATapDescription *desc;
  if (g_tap_scope == "system") {
    NSArray *excl = g_tap_exclude_self ? tiny_app_process_objects() : @[];
    desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excl];
  } else {
    NSArray *incl = tiny_app_process_objects();
    if (incl.count == 0) return kAudioHardwareBadObjectError;
    desc = [[CATapDescription alloc] initStereoMixdownOfProcesses:incl];
  }
  desc.name = @"tinyjs-audioTap";
  desc.privateTap = YES;
  desc.muteBehavior = CATapUnmuted; // still play through the speakers

  OSStatus st = AudioHardwareCreateProcessTap(desc, &g_tap_id);
  if (st != noErr) { g_tap_id = 0; return st; }

  CFStringRef uid = NULL; UInt32 s = sizeof(uid);
  AudioObjectPropertyAddress ua = { kAudioTapPropertyUID,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  AudioObjectGetPropertyData(g_tap_id, &ua, 0, NULL, &s, &uid);
  AudioStreamBasicDescription fmt = {}; s = sizeof(fmt);
  AudioObjectPropertyAddress fa = { kAudioTapPropertyFormat,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  AudioObjectGetPropertyData(g_tap_id, &fa, 0, NULL, &s, &fmt);
  g_tap_sr = fmt.mSampleRate > 0 ? fmt.mSampleRate : 48000;
  g_tap_ch = fmt.mChannelsPerFrame > 0 ? (int)fmt.mChannelsPerFrame : 2;

  NSString *aggUID = [[NSUUID UUID] UUIDString];
  NSDictionary *aggd = @{
    @"name": @"tinyjs-audioTap-agg",
    @"uid": aggUID,
    @"private": @1,
    @"tapautostart": @1,
    @"taps": @[ @{ @"uid": (uid ? (__bridge NSString *)uid : @""), @"drift": @0 } ],
  };
  st = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggd, &g_tap_agg);
  if (uid) CFRelease(uid);
  if (st != noErr) { g_tap_agg = 0; tiny_audiotap_teardown(); return st; }

  st = AudioDeviceCreateIOProcIDWithBlock(&g_tap_proc, g_tap_agg, NULL,
      ^(const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *inT,
        AudioBufferList *out, const AudioTimeStamp *outT) {
        if (!in) return;
        std::lock_guard<std::mutex> lk(g_tap_pcm_lock);
        if (g_tap_pcm.empty()) g_tap_chunk_t0 = tiny_now_ms();
        for (UInt32 b = 0; b < in->mNumberBuffers; b++) {
          const AudioBuffer &buf = in->mBuffers[b];
          const float *f = (const float *)buf.mData;
          if (!f) continue;
          UInt32 cnt = buf.mDataByteSize / sizeof(float);
          size_t base = g_tap_pcm.size();
          g_tap_pcm.resize(base + cnt);
          for (UInt32 i = 0; i < cnt; i++) {
            float v = f[i];
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
            g_tap_pcm[base + i] = (int16_t)lrintf(v * 32767.0f);
          }
        }
      });
  if (st != noErr) { g_tap_proc = nullptr; tiny_audiotap_teardown(); return st; }

  st = AudioDeviceStart(g_tap_agg, g_tap_proc);
  if (st != noErr) { tiny_audiotap_teardown(); return st; }
  return noErr;
}

// Default-output-device changed (e.g. headphones plugged): re-arm on the new
// device. A brief audio gap is acceptable.
API_AVAILABLE(macos(14.2))
static OSStatus tiny_audiotap_dev_changed(AudioObjectID, UInt32,
                                          const AudioObjectPropertyAddress *, void *) {
  if (!g_tap_active) return noErr;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_tap_active) return;
    tiny_audiotap_teardown();
    if (tiny_audiotap_build() == noErr) tiny_audiotap_start_timer();
  });
  return noErr;
}

static const AudioObjectPropertyAddress kDefaultOutAddr = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };

// Handles AUDIOTAP start (scope != "__stop__") and stop, on the main thread.
static void do_audiotap(webview_t, void *arg) {
  std::unique_ptr<AudioTapReq> req((AudioTapReq *)arg);
  if (@available(macOS 14.2, *)) {
    bool stop = (req->scope == "__stop__");
    if (stop) {
      if (g_tap_active) {
        g_tap_active = false;
        if (g_tap_dev_listener) {
          AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDefaultOutAddr,
                                            tiny_audiotap_dev_changed, NULL);
          g_tap_dev_listener = false;
        }
        tiny_audiotap_teardown();
      }
      return;
    }
    auto reply_ok = [&]() {
      sock_write_line("GOT " + req->qid + " {\"ok\":true,\"sampleRate\":" +
                      std::to_string((int)g_tap_sr) + ",\"channels\":" +
                      std::to_string(g_tap_ch) + "}");
    };
    int iv = req->interval < 20 ? 20 : (req->interval > 500 ? 500 : req->interval);
    if (g_tap_active) {
      if (g_tap_scope == req->scope && g_tap_exclude_self == req->excludeSelf &&
          g_tap_interval == iv) { reply_ok(); return; }   // idempotent
      g_tap_active = false;                                // restart with new opts
      if (g_tap_dev_listener) {
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDefaultOutAddr,
                                          tiny_audiotap_dev_changed, NULL);
        g_tap_dev_listener = false;
      }
      tiny_audiotap_teardown();
    }
    g_tap_scope = req->scope;
    g_tap_exclude_self = req->excludeSelf;
    g_tap_interval = iv;
    OSStatus st = tiny_audiotap_build();
    if (st != noErr) {
      tiny_audiotap_teardown();
      sock_write_line("GOT " + req->qid +
                      " {\"ok\":false,\"code\":\"failed\",\"status\":" +
                      std::to_string((int)st) + "}");
      return;
    }
    g_tap_active = true;
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &kDefaultOutAddr,
                                   tiny_audiotap_dev_changed, NULL);
    g_tap_dev_listener = true;
    tiny_audiotap_start_timer();
    reply_ok();
  } else {
    if (req->scope != "__stop__")
      sock_write_line("GOT " + req->qid + " {\"ok\":false,\"code\":\"unsupported\"}");
  }
}

// Switch a window between titled (rounded corners) and borderless (square).
// Borderless keeps Resizable (drag-resize edges) and the shadow; the page
// owns every pixel and moves the window via data-tiny-drag, exactly like
// frameless. This is the ONLY way to lose macOS's window corner radius.
static void apply_square(NSWindow *win, bool square) {
  NSRect keep = win.frame;
  if (square) {
    ensure_window_can_key(object_getClass(win));
    win.styleMask = NSWindowStyleMaskResizable; // no Titled bit → square
    win.hasShadow = YES;
    [win setFrame:keep display:YES];
    [win makeKeyAndOrderFront:nil]; // re-key after the styleMask swap
  } else {
    win.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    [win setFrame:keep display:YES];
  }
}

// The chrome-application core, factored out so do_winopen can apply chrome
// to a secondary window BEFORE it's ordered on screen (no titlebar flash).
static void apply_chrome_fields(NSWindow *win, WKWebView *wv,
                                NSVisualEffectView **effect, bool is_main,
                                ChromeReq *req) {
    bool req_frameless = req->frame == "0";
    bool req_traffic_off = req->traffic == "0";
    if (!req->frame.empty()) {
      bool frameless = req_frameless;
      if (is_main)
        g_chrome_frameless = frameless;
      // AppKit preserves the CONTENT rect across a styleMask change and
      // resizes the frame; pin the frame instead so the window keeps its
      // size and the page absorbs (or returns) the titlebar strip.
      NSRect keep = win.frame;
      if (frameless) {
        win.styleMask |= NSWindowStyleMaskFullSizeContentView;
        win.titlebarAppearsTransparent = YES;
        win.titleVisibility = NSWindowTitleHidden;
      } else {
        win.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
        win.titlebarAppearsTransparent = NO;
        win.titleVisibility = NSWindowTitleVisible;
      }
      [win setFrame:keep display:YES];
    }
    if (!req->traffic.empty()) {
      bool show = req->traffic == "1";
      if (is_main)
        g_chrome_traffic = show;
      [win standardWindowButton:NSWindowCloseButton].hidden = !show;
      [win standardWindowButton:NSWindowMiniaturizeButton].hidden = !show;
      [win standardWindowButton:NSWindowZoomButton].hidden = !show;
    }
    if (!req->transparent.empty()) {
      bool tr = req->transparent == "1";
      if (is_main)
        g_chrome_transparent = tr;
      win.opaque = !tr;
      win.backgroundColor = tr ? [NSColor clearColor]
                               : [NSColor windowBackgroundColor];
      win.hasShadow = !tr;
      webview_draws_background(wv, !tr);
    }
    if (!req->frame.empty() || !req->traffic.empty()) {
      // Main tracks state globally; secondary windows derive from this
      // request (set frame+trafficLights together for those).
      bool hide_bar = is_main ? (g_chrome_frameless && !g_chrome_traffic)
                              : (req_frameless && req_traffic_off);
      set_titlebar_hidden(win, hide_bar);
      relayout_titlebar(win);
      extend_content(wv, win, *effect);
    }
    if (!req->vibrancy.empty()) {
      if (*effect) {
        [*effect removeFromSuperview];
        [*effect release];
        *effect = nil;
      }
      if (req->vibrancy == "none") {
        if (is_main)
          g_chrome_vibrancy.clear();
        if (!is_main || !g_chrome_transparent)
          webview_draws_background(wv, true);
      } else {
        if (is_main)
          g_chrome_vibrancy = req->vibrancy;
        NSView *content = win.contentView;
        NSVisualEffectView *ev =
            [[NSVisualEffectView alloc] initWithFrame:content.bounds];
        ev.material = material_for(req->vibrancy);
        ev.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        ev.state = NSVisualEffectStateFollowsWindowActiveState;
        ev.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [content addSubview:ev positioned:NSWindowBelow relativeTo:nil];
        *effect = ev;
        // The page must not paint an opaque background over the effect.
        webview_draws_background(wv, false);
        win.opaque = NO;
        win.backgroundColor = [NSColor clearColor];
      }
    }
    // Square corners LAST — it rewrites the styleMask wholesale (borderless),
    // so it must win over the frame/traffic titlebar work above.
    if (!req->square.empty()) {
      bool sq = req->square == "1";
      if (is_main)
        g_chrome_square = sq;
      apply_square(win, sq);
    }
    // First-mouse is a per-view flag on the webview (survives styleMask/size
    // rewrites, so no reapply needed).
    if (!req->first_mouse.empty())
      set_accepts_first_mouse(wv, req->first_mouse == "1");
}

static void do_chrome(webview_t w, void *arg) {
  ChromeReq *req = static_cast<ChromeReq *>(arg);
  @autoreleasepool {
    NSWindow *win = window_for_id(w, req->win);
    WKWebView *wv = webview_for_id(w, req->win);
    NSVisualEffectView **effect = effect_slot_for(req->win);
    if (win && wv && effect)
      apply_chrome_fields(win, wv, effect, req->win == "main", req);
  }
  delete req;
}

static void reapply_window_overrides(webview_t w) {
  NSWindow *win = (NSWindow *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  if (!win)
    return;
  // set_size rewrote the styleMask wholesale; a square (borderless) main
  // window would snap back to titled/rounded — reassert it.
  if (g_chrome_square) {
    apply_square(win, true);
    return;
  }
  if (g_resizable_override == 0)
    win.styleMask &= ~NSWindowStyleMaskResizable;
  if (g_chrome_frameless) {
    NSRect keep = win.frame;
    win.styleMask |= NSWindowStyleMaskFullSizeContentView;
    win.titlebarAppearsTransparent = YES;
    win.titleVisibility = NSWindowTitleHidden;
    [win setFrame:keep display:YES];
    set_titlebar_hidden(win, g_chrome_frameless && !g_chrome_traffic);
    relayout_titlebar(win);
    WKWebView *mwv = (WKWebView *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
    extend_content(mwv, win, g_effect_view);
  }
}

static void do_dragwin(webview_t w, void *) {
  NSWindow *win = (NSWindow *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  NSEvent *ev = [NSApp currentEvent];
  if (win && ev)
    [win performWindowDragWithEvent:ev];
}
#else
static void do_chrome(webview_t, void *arg) { delete static_cast<ChromeReq *>(arg); }
static void do_dragwin(webview_t, void *) {}
static void reapply_window_overrides(webview_t) {}
#endif

// --- multi-window + unified page RPC ---------------------------------------------
// Every window (the main one included) gets the same injected bridge: the
// page's __invoke posts to the "tiny" script-message handler, which forwards
// `CALL <winid>:<seq> [payload]` to the backend. RET lines resolve the
// promise by evaluating __tinyResolve in the origin window — one code path
// for any number of windows, and the backend learns which window called.
// Secondary windows: WINOPEN <id>\t<pagePath>\t<title>\t<WxH>, WINCLOSE <id>,
// targeted ops via CMD@<id>, EVAL@* broadcasts, `WINCLOSED <id>` on close.

#ifdef __APPLE__
static NSWindow *window_for_id(webview_t w, const std::string &id) {
  if (id.empty() || id == "main")
    return (NSWindow *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  auto it = g_windows.find(id);
  return it == g_windows.end() ? nil : it->second.win;
}

static WKWebView *webview_for_id(webview_t w, const std::string &id) {
  if (id.empty() || id == "main")
    return (WKWebView *)webview_get_native_handle(
        w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  auto it = g_windows.find(id);
  return it == g_windows.end() ? nil : it->second.wv;
}

static NSVisualEffectView **effect_slot_for(const std::string &id) {
  if (id.empty() || id == "main")
    return &g_effect_view;
  auto it = g_windows.find(id);
  return it == g_windows.end() ? nullptr : &it->second.effect;
}

static NSString *tiny_shim_js(const std::string &winid) {
  std::string js =
      "(() => {"
      "if (window.__tinyShim) return; window.__tinyShim = true;"
      "window.__TINY_WIN = '" + winid + "';"
      "let seq = 0; const pending = {};"
      "window.__invoke = (payload) => new Promise((res, rej) => {"
      "  const s = ++seq; pending[s] = { res, rej };"
      "  window.webkit.messageHandlers.tiny.postMessage(String(s) + ':' + String(payload));"
      "});"
      "window.__tinyResolve = (s, ok, jsonText) => {"
      "  const p = pending[s]; if (!p) return; delete pending[s];"
      "  let v = null; try { v = JSON.parse(jsonText); } catch (e) {}"
      "  ok ? p.res(v) : p.rej(v);"
      "};"
      "})();";
  return ns(js);
}

@interface TinyMsgHandler : NSObject <WKScriptMessageHandler>
@property(nonatomic, copy) NSString *winId;
@end
@implementation TinyMsgHandler
- (void)userContentController:(WKUserContentController *)ucc
      didReceiveScriptMessage:(WKScriptMessage *)msg {
  if (![msg.body isKindOfClass:[NSString class]])
    return;
  std::string body = [(NSString *)msg.body UTF8String];
  size_t c = body.find(':');
  if (c == std::string::npos)
    return;
  std::string seq = body.substr(0, c);
  std::string payload = body.substr(c + 1);
  sock_write_line("CALL " + std::string([self.winId UTF8String]) + ":" + seq +
                  " [" + json_escape(payload) + "]");
}
@end

@interface TinyWinDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, copy) NSString *winId;
@end
@implementation TinyWinDelegate
- (void)windowWillClose:(NSNotification *)n {
  std::string id = [self.winId UTF8String];
  sock_write_line("WINCLOSED " + id);
  auto it = g_windows.find(id);
  if (it != g_windows.end()) {
    TinyWindow tw = it->second;
    g_windows.erase(it);
    // Deferred release: we're inside the window's own close notification.
    dispatch_async(dispatch_get_main_queue(), ^{
      [tw.wv release];
      [tw.handler release];
      [tw.wdelegate release];
      [tw.effect release];
      [tw.win release];
    });
  }
}
@end

static void attach_tiny_bridge(WKUserContentController *ucc,
                               const std::string &winid) {
  TinyMsgHandler *h = [[TinyMsgHandler alloc] init];
  h.winId = ns(winid);
  [ucc addScriptMessageHandler:h name:@"tiny"];
  WKUserScript *script = [[[WKUserScript alloc]
        initWithSource:tiny_shim_js(winid)
         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
      forMainFrameOnly:YES] autorelease];
  [ucc addUserScript:script];
  // The tiny.* client library rides along — every page in every window gets
  // window.tiny with no script tag (dev servers and file pages alike).
  WKUserScript *client = [[[WKUserScript alloc]
        initWithSource:[NSString stringWithUTF8String:TINY_CLIENT_JS]
         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
      forMainFrameOnly:YES] autorelease];
  [ucc addUserScript:client];
  // main window: handler ownership parked here (never removed)
  if (winid != "main")
    g_windows[winid].handler = h;
}

// Resolve a page call: RET <winid>:<seq> routes here (dialogs too).
static void reply_to_call(webview_t w, const std::string &composite, int status,
                          const std::string &json) {
  size_t c = composite.find(':');
  if (c == std::string::npos)
    return;
  std::string winid = composite.substr(0, c);
  std::string seq = composite.substr(c + 1);
  if (seq.empty() || seq.find_first_not_of("0123456789") != std::string::npos)
    return;
  WKWebView *wv = webview_for_id(w, winid);
  if (!wv)
    return;
  std::string js = "window.__tinyResolve(" + seq + "," +
                   (status == 0 ? "true" : "false") + "," + json_escape(json) +
                   ")";
  [wv evaluateJavaScript:ns(js) completionHandler:nil];
}

struct ReplyReq {
  std::string composite;
  int status;
  std::string json;
};

static void do_reply(webview_t w, void *arg) {
  ReplyReq *req = static_cast<ReplyReq *>(arg);
  reply_to_call(w, req->composite, req->status, req->json);
  delete req;
}

struct WinOpenReq {
  std::string id, page, title;
  int width = 600, height = 400;
  // Chrome + position applied BEFORE the window is shown (no titlebar flash).
  std::string frame, traffic, transparent, vibrancy, square, first_mouse; // '' = default
  bool hasPos = false;
  int x = 0, y = 0;
};

static void enable_webgpu_prefs(id preferences); // defined in WebGPU section
static void enable_file_access(WKWebViewConfiguration *cfg); // ditto

static void do_winopen(webview_t w, void *arg) {
  WinOpenReq *req = static_cast<WinOpenReq *>(arg);
  @autoreleasepool {
    if (req->id.empty() || req->id == "main" ||
        g_windows.count(req->id)) { // exists: just focus it
      NSWindow *ex = window_for_id(w, req->id);
      if (ex)
        [ex makeKeyAndOrderFront:nil];
      delete req;
      return;
    }
    WKWebViewConfiguration *cfg =
        [[[WKWebViewConfiguration alloc] init] autorelease];
    enable_webgpu_prefs(cfg.preferences);
    enable_file_access(cfg);
    TinyWindow &tw = g_windows[req->id]; // create slot first (attach parks handler)
    attach_tiny_bridge(cfg.userContentController, req->id);

    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, req->width, req->height)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    win.releasedWhenClosed = NO;
    win.title = ns(req->title.empty() ? req->id : req->title);
    [win center];

    WKWebView *wv =
        [[WKWebView alloc] initWithFrame:win.contentView.bounds
                           configuration:cfg];
    wv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [win.contentView addSubview:wv];

    if (req->page.rfind("http://", 0) == 0 ||
        req->page.rfind("https://", 0) == 0) {
      // Dev-server pages (devUrl mode); the bridge shim injects the same way.
      [wv loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:ns(req->page)]]];
    } else {
      NSURL *url = [NSURL fileURLWithPath:ns(req->page)];
      NSURL *access = g_read_access.empty()
                          ? [url URLByDeletingLastPathComponent]
                          : [NSURL fileURLWithPath:ns(g_read_access)
                                       isDirectory:YES];
      [wv loadFileURL:url allowingReadAccessToURL:access];
    }

    TinyWinDelegate *del = [[TinyWinDelegate alloc] init];
    del.winId = ns(req->id);
    win.delegate = del;

    tw.win = win;
    tw.wv = wv;
    tw.wdelegate = del;

    // Apply chrome + position BEFORE ordering the window on screen, so a
    // frameless/positioned secondary window never flashes a titlebar or
    // jumps from center. The slot exists now (tw is in g_windows).
    if (!req->frame.empty() || !req->traffic.empty() ||
        !req->transparent.empty() || !req->vibrancy.empty() ||
        !req->square.empty() || !req->first_mouse.empty()) {
      ChromeReq cr;
      cr.win = req->id;
      cr.frame = req->frame;
      cr.traffic = req->traffic;
      cr.transparent = req->transparent;
      cr.vibrancy = req->vibrancy;
      cr.square = req->square;
      cr.first_mouse = req->first_mouse;
      NSVisualEffectView **effect = effect_slot_for(req->id);
      if (effect)
        apply_chrome_fields(win, wv, effect, false, &cr);
    }
    if (req->hasPos) {
      CGFloat screenTop = NSMaxY([[NSScreen screens][0] frame]);
      [win setFrameTopLeftPoint:NSMakePoint(req->x, screenTop - req->y)];
    }

    [win makeKeyAndOrderFront:nil];
  }
  delete req;
}

static void do_winclose(webview_t w, void *arg) {
  std::string *id = static_cast<std::string *>(arg);
  auto it = g_windows.find(*id);
  if (it != g_windows.end())
    [it->second.win close]; // delegate sends WINCLOSED + cleans up
  delete id;
}

struct EvalReq {
  std::string win; // "", "main", "*", or an id
  std::string js;
};

static void do_title_win(webview_t w, void *arg) {
  EvalReq *req = static_cast<EvalReq *>(arg); // win + text
  @autoreleasepool {
    NSWindow *win = window_for_id(w, req->win);
    if (win && req->win != "main")
      win.title = ns(req->js);
    else if (win)
      webview_set_title(w, req->js.c_str());
  }
  delete req;
}

static void do_eval_win(webview_t w, void *arg) {
  EvalReq *req = static_cast<EvalReq *>(arg);
  @autoreleasepool {
    if (req->win == "*") {
      WKWebView *main = webview_for_id(w, "main");
      if (main)
        [main evaluateJavaScript:ns(req->js) completionHandler:nil];
      for (auto &kv : g_windows)
        [kv.second.wv evaluateJavaScript:ns(req->js) completionHandler:nil];
    } else {
      WKWebView *wv = webview_for_id(w, req->win);
      if (wv)
        [wv evaluateJavaScript:ns(req->js) completionHandler:nil];
    }
  }
  delete req;
}
#else
struct ReplyReq { std::string composite; int status; std::string json; };
struct WinOpenReq {
  std::string id, page, title;
  int width, height;
  std::string frame, traffic, transparent, vibrancy, square, first_mouse;
  bool hasPos;
  int x, y;
};
struct EvalReq { std::string win, js; };
static void reply_to_call(webview_t, const std::string &, int, const std::string &) {}
static void do_reply(webview_t, void *arg) { delete static_cast<ReplyReq *>(arg); }
static void do_winopen(webview_t, void *arg) { delete static_cast<WinOpenReq *>(arg); }
static void do_winclose(webview_t, void *arg) { delete static_cast<std::string *>(arg); }
static void do_eval_win(webview_t, void *arg) { delete static_cast<EvalReq *>(arg); }
static void do_title_win(webview_t, void *arg) { delete static_cast<EvalReq *>(arg); }
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

// --- deep-Mac citizen: PDF, haptics, dock icon, battery, wifi, spotlight ------------
// The small native niceties apps otherwise shell out (or give up) for.

struct PdfReq {
  std::string qid, path;
};
struct SpotlightReq {
  std::string qid, query;
};

#ifdef __APPLE__
static void do_pdf(webview_t w, void *arg) {
  PdfReq *req = static_cast<PdfReq *>(arg);
  std::string qid = req->qid, path = req->path;
  delete req;
  WKWebView *wv = (WKWebView *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (!wv) {
    sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":\"no webview\"}");
    return;
  }
  WKPDFConfiguration *cfg = [[WKPDFConfiguration alloc] init]; // whole page
  [wv createPDFWithConfiguration:cfg
               completionHandler:^(NSData *data, NSError *err) {
                 if (!data || ![data writeToFile:ns(path) atomically:YES]) {
                   sock_write_line(
                       "GOT " + qid + " {\"ok\":false,\"error\":" +
                       json_escape(err ? [err.localizedDescription UTF8String]
                                       : "pdf write failed") +
                       "}");
                   return;
                 }
                 sock_write_line("GOT " + qid + " {\"ok\":true,\"path\":" +
                                 json_escape(path) + ",\"error\":null}");
               }];
}

static void do_haptic(webview_t, void *arg) {
  std::string *pat = static_cast<std::string *>(arg);
  NSHapticFeedbackPattern p =
      *pat == "alignment" ? NSHapticFeedbackPatternAlignment
      : *pat == "level"   ? NSHapticFeedbackPatternLevelChange
                          : NSHapticFeedbackPatternGeneric;
  [[NSHapticFeedbackManager defaultPerformer]
      performFeedbackPattern:p
             performanceTime:NSHapticFeedbackPerformanceTimeDefault];
  delete pat;
}

static void do_dockicon(webview_t, void *arg) {
  std::string *path = static_cast<std::string *>(arg);
  @autoreleasepool {
    if (path->empty()) {
      [NSApp setApplicationIconImage:nil]; // reset to the bundle icon
    } else {
      NSImage *img = [[NSImage alloc] initWithContentsOfFile:ns(*path)];
      if (img)
        [NSApp setApplicationIconImage:img];
    }
  }
  delete path;
}

static std::string battery_json() {
  CFTypeRef info = IOPSCopyPowerSourcesInfo();
  CFArrayRef list = info ? IOPSCopyPowerSourcesList(info) : NULL;
  std::string out = "null";
  if (list && CFArrayGetCount(list) > 0) {
    CFDictionaryRef d =
        IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(list, 0));
    if (d) {
      NSDictionary *ps = (__bridge NSDictionary *)d;
      double cur = [ps[@kIOPSCurrentCapacityKey] doubleValue];
      double max = [ps[@kIOPSMaxCapacityKey] doubleValue];
      bool charging = [ps[@kIOPSIsChargingKey] boolValue];
      NSString *state = ps[@kIOPSPowerSourceStateKey];
      bool plugged = [state isEqualToString:@kIOPSACPowerValue];
      int toEmpty = [ps[@kIOPSTimeToEmptyKey] intValue];
      int toFull = [ps[@kIOPSTimeToFullChargeKey] intValue];
      int mins = charging ? toFull : toEmpty; // -1 = calculating
      char buf[192];
      std::snprintf(buf, sizeof(buf),
                    "{\"percent\":%d,\"charging\":%s,\"plugged\":%s,"
                    "\"minutesRemaining\":%s}",
                    max > 0 ? (int)lround(cur / max * 100) : 0,
                    charging ? "true" : "false", plugged ? "true" : "false",
                    mins < 0 ? "null" : std::to_string(mins).c_str());
      out = buf;
    }
  }
  if (list) CFRelease(list);
  if (info) CFRelease(info);
  return out;
}

static std::string wifi_json() {
  @autoreleasepool {
    CWInterface *itf = [[CWWiFiClient sharedWiFiClient] interface];
    if (!itf || !itf.powerOn)
      return "null";
    // ssid is nil without the Location permission on macOS 14+; the rest of
    // the fields still come through.
    NSString *ssid = itf.ssid;
    return std::string("{\"ssid\":") +
           (ssid ? json_escape([ssid UTF8String]) : "null") + ",\"bssid\":" +
           (itf.bssid ? json_escape([itf.bssid UTF8String]) : "null") +
           ",\"rssi\":" + std::to_string((long)itf.rssiValue) +
           ",\"noise\":" + std::to_string((long)itf.noiseMeasurement) +
           ",\"txRate\":" + std::to_string((long)itf.transmitRate) + "}";
  }
}

// NSMetadataQuery gathers asynchronously off the main runloop; observe the
// finish notification, snapshot up to 100 paths, tear the query down.
@class TinySpotlight;
static TinySpotlight *g_spotlight_hold = nil;
@interface TinySpotlight : NSObject
@property(strong) NSMetadataQuery *query;
@property(assign) id observer;
@property std::string qid;
@end
@implementation TinySpotlight
- (void)finished:(NSNotification *)note {
  [self.query stopQuery];
  std::string json = "[";
  bool first = true;
  NSUInteger n = MIN(self.query.resultCount, (NSUInteger)100);
  for (NSUInteger i = 0; i < n; i++) {
    NSMetadataItem *it = [self.query resultAtIndex:i];
    NSString *path = [it valueForAttribute:(NSString *)kMDItemPath];
    if (!path)
      continue;
    if (!first)
      json += ",";
    first = false;
    json += json_escape([path UTF8String]);
  }
  json += "]";
  sock_write_line("GOT " + self.qid + " {\"ok\":true,\"paths\":" + json + "}");
  [[NSNotificationCenter defaultCenter] removeObserver:self.observer];
  self.query = nil;
  g_spotlight_hold = nil; // release self
}
@end

static void do_spotlight(webview_t, void *arg) {
  SpotlightReq *req = static_cast<SpotlightReq *>(arg);
  @autoreleasepool {
    NSMetadataQuery *q = [[NSMetadataQuery alloc] init];
    // Match display name OR text content (the two things users mean by
    // "find files about X").
    q.predicate = [NSPredicate
        predicateWithFormat:@"(kMDItemDisplayName CONTAINS[cd] %@) OR "
                            @"(kMDItemTextContent CONTAINS[cd] %@)",
                            ns(req->query), ns(req->query)];
    q.searchScopes = @[ NSMetadataQueryUserHomeScope ];
    TinySpotlight *sl = [[TinySpotlight alloc] init];
    sl.query = q;
    sl.qid = req->qid;
    sl.observer = sl;
    g_spotlight_hold = sl; // keep alive until the notification fires
    [[NSNotificationCenter defaultCenter]
        addObserver:sl
           selector:@selector(finished:)
               name:NSMetadataQueryDidFinishGatheringNotification
             object:q];
    [q startQuery];
  }
  delete req;
}
#else
static void do_pdf(webview_t, void *arg) {
  PdfReq *r = static_cast<PdfReq *>(arg);
  sock_write_line("GOT " + r->qid + " {\"ok\":false,\"error\":\"unsupported\"}");
  delete r;
}
static void do_haptic(webview_t, void *arg) { delete static_cast<std::string *>(arg); }
static void do_dockicon(webview_t, void *arg) { delete static_cast<std::string *>(arg); }
static std::string battery_json() { return "null"; }
static std::string wifi_json() { return "null"; }
static void do_spotlight(webview_t, void *arg) {
  SpotlightReq *r = static_cast<SpotlightReq *>(arg);
  sock_write_line("GOT " + r->qid + " {\"ok\":true,\"paths\":[]}");
  delete r;
}
#endif

// --- on-device AI: FoundationModels via the Swift shim (macOS 26) --------------------
// Built only with TINYJS_AI=1 (needs the macOS 26 SDK + swiftc; see setup.sh).
// Generation blocks for seconds, so it runs on a dedicated background queue,
// never the UI thread. Without the flag every call answers "unsupported".

struct AiReq {
  std::string qid, op, prompt, instructions;
};

#if defined(__APPLE__) && defined(TINYJS_AI)
extern "C" int tiny_ai_available();
extern "C" char *tiny_ai_generate(const char *prompt, const char *instructions,
                                  char **errOut);

static dispatch_queue_t g_ai_queue = nullptr;

static void do_ai(webview_t, void *arg) {
  AiReq *req = static_cast<AiReq *>(arg);
  if (!g_ai_queue)
    g_ai_queue = dispatch_queue_create("app.tinyjs.ai", DISPATCH_QUEUE_SERIAL);
  std::string qid = req->qid, op = req->op, prompt = req->prompt,
              instr = req->instructions;
  delete req;
  dispatch_async(g_ai_queue, ^{
    if (op == "available") {
      int a = tiny_ai_available();
      const char *st = a == 1 ? "available" : a == 0 ? "unavailable" : "unsupported";
      sock_write_line("GOT " + qid + " {\"ok\":true,\"status\":\"" + st + "\"}");
      return;
    }
    char *err = nullptr;
    char *out = tiny_ai_generate(prompt.c_str(),
                                 instr.empty() ? nullptr : instr.c_str(), &err);
    if (out) {
      sock_write_line("GOT " + qid + " {\"ok\":true,\"text\":" +
                      json_escape(out) + "}");
      free(out);
    } else {
      sock_write_line("GOT " + qid + " {\"ok\":false,\"error\":" +
                      json_escape(err ? err : "generation failed") + "}");
      if (err) free(err);
    }
  });
}
#else
static void do_ai(webview_t, void *arg) {
  AiReq *req = static_cast<AiReq *>(arg);
  if (req->op == "available")
    sock_write_line("GOT " + req->qid +
                    " {\"ok\":true,\"status\":\"unsupported\"}");
  else
    sock_write_line("GOT " + req->qid +
                    " {\"ok\":false,\"error\":\"tiny.ai not built in "
                    "(needs macOS 26 + TINYJS_AI=1)\"}");
  delete req;
}
#endif

// --- WebGPU (macOS) ----------------------------------------------------------
// WKWebView gates WebGPU behind a WebKit feature flag (still "experimental"
// as of macOS 15; no public API to enable it). Flip it through the private
// WKPreferences feature list before any content loads. Preference changes
// propagate to the web process, so doing this right after webview_create and
// before the first navigate/set_html is sufficient. Harmless no-op on OS
// versions where the flag or the private API doesn't exist.

#ifdef __APPLE__
static void enable_webgpu_prefs(id preferences) {
  WKPreferences *prefs = (WKPreferences *)preferences;
  if (!prefs)
    return;
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
// file:// pages are opaque origins, so module scripts with `crossorigin`
// (every Vite build) fail CORS. These private-but-stable flags make
// file→file loads same-origin, exactly like Tauri's wry does.
static void enable_file_access(WKWebViewConfiguration *cfg) {
  @try {
    [cfg.preferences setValue:@YES forKey:@"allowFileAccessFromFileURLs"];
  } @catch (NSException *) {
  }
  @try {
    [cfg setValue:@YES forKey:@"allowUniversalAccessFromFileURLs"];
  } @catch (NSException *) {
  }
}

static void enable_webgpu(webview_t w) {
  WKWebView *wv = (WKWebView *)webview_get_native_handle(
      w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (wv) {
    enable_webgpu_prefs(wv.configuration.preferences);
    enable_file_access(wv.configuration);
  }
}
#endif

// -----------------------------------------------------------------------------

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
        ReplyReq *rr = new ReplyReq;
        rr->composite = line.substr(4, sp1 - 4);
        rr->status = std::atoi(line.c_str() + sp1 + 1);
        rr->json = line.substr(sp2 + 1);
        webview_dispatch(g_w, do_reply, rr);
      } else if (line.rfind("EVAL", 0) == 0 &&
                 (line[4] == ' ' || line[4] == '@')) {
        // EVAL <js> (main) | EVAL@* <js> (broadcast) | EVAL@<id> <js>
        // js is esc()-escaped by the bridge so multi-line snippets keep their
        // newlines (a flattened // comment would swallow the rest); undo it.
        EvalReq *er = new EvalReq;
        if (line[4] == ' ') {
          er->win = "main";
          er->js = wire_unescape(line.substr(5));
        } else {
          size_t sp = line.find(' ', 5);
          if (sp == std::string::npos) { delete er; continue; }
          er->win = line.substr(5, sp - 5);
          er->js = wire_unescape(line.substr(sp + 1));
        }
        webview_dispatch(g_w, do_eval_win, er);
      } else if (line.rfind("TITLE@", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        EvalReq *tr = new EvalReq{line.substr(6, sp - 6), line.substr(sp + 1)};
        webview_dispatch(g_w, do_title_win, tr);
      } else if (line.rfind("TITLE ", 0) == 0) {
        webview_dispatch(g_w, do_title, new std::string(line.substr(6)));
      } else if (line.rfind("SIZE@", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos) continue;
        SizeReq *sr = new SizeReq{600, 400};
        sr->win = line.substr(5, sp - 5);
        std::sscanf(line.c_str() + sp + 1, "%d %d", &sr->width, &sr->height);
        webview_dispatch(g_w, do_size, sr);
      } else if (line.rfind("SIZE ", 0) == 0) {
        SizeReq *s = new SizeReq{960, 640};
        std::sscanf(line.c_str() + 5, "%d %d", &s->width, &s->height);
        webview_dispatch(g_w, do_size, s);
      } else if (line.rfind("WINOPEN ", 0) == 0) {
        // <id>\t<page>\t<title>\t<WxH>[\t<frame>\t<traffic>\t<transp>\t<vib>
        //   \t<square>\t<firstMouse>\t<x>\t<y>]
        std::vector<std::string> p = split_tabs(line.substr(8));
        WinOpenReq *wr = new WinOpenReq;
        wr->id = p.size() > 0 ? p[0] : "";
        wr->page = p.size() > 1 ? p[1] : "";
        wr->title = p.size() > 2 ? p[2] : "";
        if (p.size() > 3)
          std::sscanf(p[3].c_str(), "%dx%d", &wr->width, &wr->height);
        wr->frame = p.size() > 4 ? p[4] : "";
        wr->traffic = p.size() > 5 ? p[5] : "";
        wr->transparent = p.size() > 6 ? p[6] : "";
        wr->vibrancy = p.size() > 7 ? p[7] : "";
        wr->square = p.size() > 8 ? p[8] : "";
        wr->first_mouse = p.size() > 9 ? p[9] : "";
        if (p.size() > 11 && !p[10].empty() && !p[11].empty()) {
          wr->hasPos = true;
          wr->x = std::atoi(p[10].c_str());
          wr->y = std::atoi(p[11].c_str());
        }
        webview_dispatch(g_w, do_winopen, wr);
      } else if (line.rfind("WINCLOSE ", 0) == 0) {
        webview_dispatch(g_w, do_winclose, new std::string(line.substr(9)));
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
        pending_tray.primary = p.size() > 4 && p[4] == "1";
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
      } else if (line.rfind("WINOP@", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_winop,
                         new WinopReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("WINOP ", 0) == 0) {
        webview_dispatch(g_w, do_winop, new WinopReq{"main", line.substr(6)});
      } else if (line == "CTXBEGIN") {
        collapse_subs();
        build_stack.assign(1, {});
        in_ctx_block = true;
      } else if (line == "CTXEND") {
        in_ctx_block = false;
        webview_dispatch(g_w, apply_ctx, new CtxReq{take_root(), true});
      } else if (line == "CTXCLEAR") {
        webview_dispatch(g_w, apply_ctx, new CtxReq{{}, false});
      } else if (line.rfind("CTXSUPPRESS ", 0) == 0) {
        webview_dispatch(g_w, apply_ctx_suppress,
                         new CtxSuppressReq{line.substr(12) == "1"});
      } else if (line.rfind("HKREG ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(6));
        if (p.size() >= 2)
          webview_dispatch(g_w, do_hotkey, new HotkeyReq{p[0], p[1]});
      } else if (line.rfind("HKUNREG ", 0) == 0) {
        webview_dispatch(g_w, do_hotkey, new HotkeyReq{line.substr(8), ""});
      } else if (line.rfind("AUDIOTAP STOP", 0) == 0) {
        webview_dispatch(g_w, do_audiotap, new AudioTapReq{"", "__stop__", false, 0});
      } else if (line.rfind("AUDIOTAP ", 0) == 0) {
        // AUDIOTAP <qid> <scope>\t<excludeSelf>\t<interval>
        std::string rest = line.substr(9);
        size_t sp = rest.find(' ');
        std::string qid = sp == std::string::npos ? rest : rest.substr(0, sp);
        std::vector<std::string> p =
            split_tabs(sp == std::string::npos ? "" : rest.substr(sp + 1));
        webview_dispatch(g_w, do_audiotap,
                         new AudioTapReq{qid, p.size() > 0 ? p[0] : "app",
                                         p.size() > 1 && p[1] == "1",
                                         p.size() > 2 ? atoi(p[2].c_str()) : 80});
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
      } else if (line.rfind("NOTIFY ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(7));
        NotifReq *req = new NotifReq;
        req->id = p.size() > 0 ? p[0] : "";
        req->title = p.size() > 1 ? p[1] : "";
        req->body = p.size() > 2 ? p[2] : "";
        req->subtitle = p.size() > 3 ? p[3] : "";
        req->sound = p.size() > 4 && p[4] == "1";
        req->actions_json = p.size() > 5 ? wire_unescape(p[5]) : "";
        webview_dispatch(g_w, do_notify, req);
      } else if (line.rfind("CHROME", 0) == 0 &&
                 (line[6] == ' ' || line[6] == '@')) {
        std::string winid = "main";
        size_t body = 7;
        if (line[6] == '@') {
          size_t sp = line.find(' ', 7);
          if (sp == std::string::npos) continue;
          winid = line.substr(7, sp - 7);
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        ChromeReq *req = new ChromeReq;
        req->win = winid;
        req->frame = p.size() > 0 ? p[0] : "";
        req->traffic = p.size() > 1 ? p[1] : "";
        req->transparent = p.size() > 2 ? p[2] : "";
        req->vibrancy = p.size() > 3 ? p[3] : "";
        req->square = p.size() > 4 ? p[4] : "";
        req->first_mouse = p.size() > 5 ? p[5] : "";
        webview_dispatch(g_w, do_chrome, req);
      } else if (line == "DRAGWIN") {
        webview_dispatch(g_w, do_dragwin, nullptr);
      } else if (line.rfind("CLIPWRITE ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(10));
        ClipWriteReq *req = new ClipWriteReq;
        req->text = p.size() > 0 ? wire_unescape(p[0]) : "";
        req->html = p.size() > 1 ? wire_unescape(p[1]) : "";
        req->image = p.size() > 2 ? wire_unescape(p[2]) : "";
        req->color = p.size() > 3 ? wire_unescape(p[3]) : "";
        for (size_t i = 4; i < p.size(); i++)
          if (!p[i].empty())
            req->paths.push_back(wire_unescape(p[i]));
        webview_dispatch(g_w, do_clip_write, req);
      } else if (line.rfind("CLIPWATCH ", 0) == 0) {
        webview_dispatch(g_w, do_clip_watch,
                         new int(std::atoi(line.c_str() + 10)));
      } else if (line.rfind("DRAGOUT", 0) == 0 &&
                 (line[7] == ' ' || line[7] == '@')) {
        std::string winid = "main";
        size_t body = 8;
        if (line[7] == '@') {
          size_t sp = line.find(' ', 8);
          if (sp == std::string::npos) continue;
          winid = line.substr(8, sp - 8);
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        DragOutReq *req = new DragOutReq;
        req->win = winid;
        req->image = p.size() > 0 ? wire_unescape(p[0]) : "";
        for (size_t i = 1; i < p.size(); i++)
          if (!p[i].empty())
            req->paths.push_back(wire_unescape(p[i]));
        webview_dispatch(g_w, do_dragout, req);
      } else if (line.rfind("KEYSTROKE ", 0) == 0) {
        size_t sp = line.find(' ', 10);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_keystroke,
                         new KeystrokeReq{line.substr(10, sp - 10),
                                          line.substr(sp + 1)});
      } else if (line.rfind("PERMCHK ", 0) == 0 ||
                 line.rfind("PERMREQ ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_perm,
                         new PermReq{line.substr(8, sp - 8), line.substr(sp + 1),
                                     line.rfind("PERMREQ ", 0) == 0});
      } else if (line.rfind("SHELL ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        ShellReq *req = new ShellReq;
        req->qid = line.substr(6, sp - 6);
        req->op = p.size() > 0 ? p[0] : "";
        req->target = p.size() > 1 ? wire_unescape(p[1]) : "";
        webview_dispatch(g_w, do_shell, req);
      } else if (line.rfind("LOGIN ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        std::string rest = line.substr(sp + 1); // "get" | "set 0|1"
        int set = rest == "set 1" ? 1 : rest == "set 0" ? 0 : -1;
        webview_dispatch(g_w, do_login,
                         new LoginReq{line.substr(6, sp - 6), set});
      } else if (line.rfind("POWER ", 0) == 0) {
        // POWER <qid> on\t<display01>\t<reason> | POWER <qid> off
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        PowerReq *req = new PowerReq;
        req->qid = line.substr(6, sp - 6);
        req->on = p.size() > 0 && p[0] == "on";
        req->display = p.size() > 1 && p[1] == "1";
        req->reason = p.size() > 2 ? wire_unescape(p[2]) : "";
        webview_dispatch(g_w, do_power, req);
      } else if (line.rfind("SOUND ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        SoundReq *req = new SoundReq;
        if (sp == std::string::npos) {
          req->qid = line.substr(6);
        } else {
          req->qid = line.substr(6, sp - 6);
          req->target = wire_unescape(line.substr(sp + 1));
        }
        webview_dispatch(g_w, do_sound, req);
      } else if (line.rfind("SHARE", 0) == 0 &&
                 (line[5] == ' ' || line[5] == '@')) {
        std::string winid = "main";
        size_t body = 6;
        if (line[5] == '@') {
          size_t sp = line.find(' ', 6);
          if (sp == std::string::npos) continue;
          winid = line.substr(6, sp - 6);
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        ShareReq *req = new ShareReq;
        req->win = winid;
        req->x = p.size() > 0 ? std::atoi(p[0].c_str()) : 0;
        req->y = p.size() > 1 ? std::atoi(p[1].c_str()) : 0;
        req->text = p.size() > 2 ? wire_unescape(p[2]) : "";
        req->url = p.size() > 3 ? wire_unescape(p[3]) : "";
        for (size_t i = 4; i < p.size(); i++)
          if (!p[i].empty())
            req->paths.push_back(wire_unescape(p[i]));
        webview_dispatch(g_w, do_share, req);
      } else if (line == "NOWPLAYING" || line.rfind("NOWPLAYING ", 0) == 0) {
        NowPlayingReq *req = new NowPlayingReq;
        req->json = line.size() > 11 ? wire_unescape(line.substr(11)) : "";
        webview_dispatch(g_w, do_nowplaying, req);
      } else if (line.rfind("SAY ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        SayReq *req = new SayReq;
        req->qid = line.substr(4, sp - 4);
        req->text = p.size() > 0 ? wire_unescape(p[0]) : "";
        req->voice = p.size() > 1 ? wire_unescape(p[1]) : "";
        req->rate = p.size() > 2 ? std::atof(p[2].c_str()) : 0;
        webview_dispatch(g_w, do_say, req);
      } else if (line.rfind("RECORD ", 0) == 0) {
        // RECORD <qid> start <display>\t<path> | RECORD <qid> stop
        size_t sp = line.find(' ', 7);
        if (sp == std::string::npos) continue;
        RecordReq *req = new RecordReq;
        req->qid = line.substr(7, sp - 7);
        std::string verb = line.substr(sp + 1); // "start …" | "stop"
        req->start = verb.rfind("start ", 0) == 0;
        req->display = 0;
        if (req->start) {
          std::vector<std::string> p = split_tabs(verb.substr(6));
          req->display = p.size() > 0 ? std::atol(p[0].c_str()) : 0;
          req->path = p.size() > 1 ? wire_unescape(p[1]) : "";
        }
        webview_dispatch(g_w, do_record, req);
      } else if (line.rfind("WINCTRL ", 0) == 0) {
        // WINCTRL <qid> <pid>\t<x>\t<y>\t<w>\t<h>
        size_t sp = line.find(' ', 8);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        WinCtrlReq *req = new WinCtrlReq;
        req->qid = line.substr(8, sp - 8);
        req->pid = p.size() > 0 ? std::atol(p[0].c_str()) : 0;
        req->x = p.size() > 1 ? std::atoi(p[1].c_str()) : 0;
        req->y = p.size() > 2 ? std::atoi(p[2].c_str()) : 0;
        req->w = p.size() > 3 ? std::atoi(p[3].c_str()) : 0;
        req->h = p.size() > 4 ? std::atoi(p[4].c_str()) : 0;
        webview_dispatch(g_w, do_winctrl, req);
      } else if (line == "SAYSTOP") {
        webview_dispatch(g_w, do_saystop, nullptr);
      } else if (line.rfind("VOICES ", 0) == 0) {
        webview_dispatch(g_w, do_voices, new VoicesReq{line.substr(7)});
      } else if (line.rfind("PICKCOLOR ", 0) == 0) {
        webview_dispatch(g_w, do_pickcolor, new std::string(line.substr(10)));
      } else if (line.rfind("OCR ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_ocr,
                         new OcrReq{line.substr(4, sp - 4),
                                    wire_unescape(line.substr(sp + 1))});
      } else if (line.rfind("THUMB ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        webview_dispatch(
            g_w, do_thumb,
            new ThumbReq{line.substr(6, sp - 6),
                         p.size() > 0 ? wire_unescape(p[0]) : "",
                         p.size() > 1 ? std::atoi(p[1].c_str()) : 0});
      } else if (line.rfind("SECRET ", 0) == 0) {
        size_t sp = line.find(' ', 7);
        if (sp == std::string::npos) continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        SecretReq *req = new SecretReq;
        req->qid = line.substr(7, sp - 7);
        req->op = p.size() > 0 ? p[0] : "";
        req->service = p.size() > 1 ? wire_unescape(p[1]) : "";
        req->account = p.size() > 2 ? wire_unescape(p[2]) : "";
        req->value = p.size() > 3 ? wire_unescape(p[3]) : "";
        webview_dispatch(g_w, do_secret, req);
      } else if (line.rfind("AUTH ", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_auth,
                         new AuthReq{line.substr(5, sp - 5),
                                     wire_unescape(line.substr(sp + 1))});
      } else if (line.rfind("OSA ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_osa,
                         new OsaReq{line.substr(4, sp - 4),
                                    wire_unescape(line.substr(sp + 1))});
      } else if (line == "QUICKLOOK" || line.rfind("QUICKLOOK ", 0) == 0) {
        QLReq *req = new QLReq;
        if (line.size() > 10)
          for (auto &p : split_tabs(line.substr(10)))
            if (!p.empty())
              req->paths.push_back(wire_unescape(p));
        webview_dispatch(g_w, do_quicklook, req);
      } else if (line.rfind("CAPTURE ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        CaptureReq *req = new CaptureReq;
        if (sp == std::string::npos) {
          req->qid = line.substr(8);
          req->display = 0;
        } else {
          req->qid = line.substr(8, sp - 8);
          req->display = std::atol(line.c_str() + sp + 1);
        }
        webview_dispatch(g_w, do_capture, req);
      } else if (line == "BADGE" || line.rfind("BADGE ", 0) == 0) {
        webview_dispatch(g_w, do_badge,
                         new std::string(
                             line.size() > 6 ? wire_unescape(line.substr(6))
                                             : ""));
      } else if (line.rfind("BOUNCE", 0) == 0) {
        webview_dispatch(g_w, do_bounce,
                         new int(line.size() > 7 ? std::atoi(line.c_str() + 7)
                                                 : 0));
      } else if (line.rfind("PDF ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_pdf,
                         new PdfReq{line.substr(4, sp - 4),
                                    wire_unescape(line.substr(sp + 1))});
      } else if (line.rfind("HAPTIC ", 0) == 0) {
        webview_dispatch(g_w, do_haptic, new std::string(line.substr(7)));
      } else if (line == "DOCKICON" || line.rfind("DOCKICON ", 0) == 0) {
        webview_dispatch(g_w, do_dockicon,
                         new std::string(line.size() > 9
                                             ? wire_unescape(line.substr(9))
                                             : ""));
      } else if (line.rfind("SPOTLIGHT ", 0) == 0) {
        size_t sp = line.find(' ', 10);
        if (sp == std::string::npos) continue;
        webview_dispatch(g_w, do_spotlight,
                         new SpotlightReq{line.substr(10, sp - 10),
                                          wire_unescape(line.substr(sp + 1))});
      } else if (line.rfind("AI ", 0) == 0) {
        // AI <op> <qid> [<prompt>\t<instructions>]
        size_t o = line.find(' ', 3);
        if (o == std::string::npos) continue;
        std::string op = line.substr(3, o - 3);
        size_t q = line.find(' ', o + 1);
        AiReq *req = new AiReq;
        req->op = op;
        if (q == std::string::npos) {
          req->qid = line.substr(o + 1);
        } else {
          req->qid = line.substr(o + 1, q - o - 1);
          std::vector<std::string> p = split_tabs(line.substr(q + 1));
          req->prompt = p.size() > 0 ? wire_unescape(p[0]) : "";
          req->instructions = p.size() > 1 ? wire_unescape(p[1]) : "";
        }
        webview_dispatch(g_w, do_ai, req);
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
// The launcher is a universal binary, so it starts up even on an Intel Mac —
// but the bundled `tjs` backend is arm64-only, so the app would otherwise hang
// with a cryptic "backend never connected". Detected at spawn time (posix_spawn
// returns EBADARCH), we show a plain apology and quit instead. Kept generic:
// it fires whenever `tjs` has no slice for the CPU we're running on.
static void show_arch_unsupported_alert() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert *a = [[[NSAlert alloc] init] autorelease];
    a.alertStyle = NSAlertStyleCritical;
    a.messageText = @"This app needs an Apple Silicon Mac";
    a.informativeText =
        @"Sorry — tinyjs apps currently run only on Apple Silicon (M1 and "
        @"later). This Mac has an Intel processor, so the app can’t start.";
    [a addButtonWithTitle:@"OK"];
    [a runModal];
  }
}

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
  // posix_spawn returns the error as its result (not via errno). On an Intel
  // Mac the arm64-only tjs has no runnable slice -> EBADARCH: apologise and
  // quit cleanly rather than leaving the app to hang on a backend that will
  // never attach.
  int rc = posix_spawn(&pid, tjs.c_str(), nullptr, nullptr,
                       const_cast<char *const *>(sargv), environ);
  if (rc != 0) {
    if (rc == EBADARCH) {
      show_arch_unsupported_alert();
      std::exit(1);
    }
    std::fprintf(stderr, "launcher: cannot spawn backend: %s\n",
                 std::strerror(rc));
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

  g_bundle_mode = bundle_mode;

#ifdef __APPLE__
  // Must precede webview_create: launch Apple Events (cold-start deep links /
  // file opens) can be dispatched during its internal event pumping, and the
  // notification-center delegate must exist before launch to receive the
  // banner click that started the app.
  install_early_open_handlers();
  install_notif_delegate();

  // Accessory activation (menu-bar agents): env in dev/spawn mode, plist in
  // bundle mode. Must also precede webview_create — that's where the library
  // would otherwise flash the Dock icon and the window.
  {
    const char *act = getenv("TINYJS_ACTIVATION");
    bool accessory = act && std::strcmp(act, "accessory") == 0;
    if (!accessory) {
      NSString *pa = [[NSBundle mainBundle]
          objectForInfoDictionaryKey:@"TinyjsActivation"];
      accessory = pa && [pa isEqualToString:@"accessory"];
    }
    if (accessory)
      install_accessory_mode();
  }
  // readAccess: widen the page's file:// read root (dev via env, packaged via
  // the Info.plist key cli.js writes). '~' expands to the home directory.
  {
    const char *ra = getenv("TINYJS_READ_ACCESS");
    NSString *root = ra ? [NSString stringWithUTF8String:ra] : nil;
    if (!root) {
      root = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"TinyjsReadAccess"];
    }
    if (root.length)
      g_read_access = [[root stringByExpandingTildeInPath] UTF8String];
  }
#endif

#ifdef __APPLE__
  // Must precede webview_create: it swizzles WKWebView init so the main
  // webview's config gets the tiny-media scheme handler before it's built.
  install_media_scheme_hook();
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
  install_media_capture_hook();
  install_first_mouse_hook();
  install_ctx_hook();
  install_system_observers();
  install_open_handlers();
  // Packaged apps can declare chrome in the plist (TinyjsChrome, same
  // tab-separated fields as the CHROME command) so the window never flashes
  // its default titlebar.
  if (bundle_mode) {
    NSString *cs = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"TinyjsChrome"];
    if (cs) {
      std::vector<std::string> p = split_tabs([cs UTF8String]);
      ChromeReq *req = new ChromeReq;
      req->frame = p.size() > 0 ? p[0] : "";
      req->traffic = p.size() > 1 ? p[1] : "";
      req->transparent = p.size() > 2 ? p[2] : "";
      req->vibrancy = p.size() > 3 ? p[3] : "";
      req->square = p.size() > 4 ? p[4] : "";
      req->first_mouse = p.size() > 5 ? p[5] : "";
      do_chrome(g_w, req);
    }
  }
#endif

  webview_set_title(g_w, title.c_str());
  webview_set_size(g_w, width, height, WEBVIEW_HINT_NONE);
#ifdef __APPLE__
  // Unified page RPC for every window, the main one included (replaces
  // webview_bind: the shim's __invoke wins because nothing else defines it).
  {
    WKWebView *mwv = (WKWebView *)webview_get_native_handle(
        g_w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
    if (mwv)
      attach_tiny_bridge(mwv.configuration.userContentController, "main");
  }
#endif

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

#ifdef __APPLE__
  // Accessory startup is done: everything up to here ran with order-front
  // suppressed, so the window was never on screen. Make sure it is genuinely
  // ordered out, then lift the suppression so WINOP show works normally (the
  // socket loop hasn't started yet, so no show can have been requested).
  if (g_suppress_order_front) {
    NSWindow *win = (NSWindow *)webview_get_native_handle(
        g_w, WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
    [win orderOut:nil];
    g_suppress_order_front = false;
  }
#endif

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
