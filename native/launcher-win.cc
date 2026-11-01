// tinyjs window launcher — Windows.
//
// The same dumb window process as native/launcher-macos.cc (macOS),
// speaking the identical newline-delimited wire protocol, but over a named pipe
// (\\.\pipe\tinyjs-…) and rendering with WebView2 via the vendored webview
// library. The backend (txiki.js) listens on the pipe and spawns this as a
// child: argv = <html-file-or-url> <pipe-name> [title] [WxH] [version].
//
// Page RPC: unlike the macOS launcher (which hand-rolls a WKScriptMessageHandler
// bridge for multi-window support), this uses webview_bind("__invoke", …)
// directly — the library injects a promise-returning window.__invoke, the
// callback forwards `CALL <id> <json-args>` to the backend, and RET lines
// resolve via webview_return. Dialog (DLG) replies short-circuit the same way.
//
// Implemented protocol subset (everything the OS has a sane native answer
// for): EVAL/TITLE/SIZE/RELOAD/QUIT, DLG (file panels via IFileDialog,
// alert/confirm via MessageBox, prompt via an in-memory dialog template),
// MENU*/MENUUPD (Win32 menu bar — one per window: a bare MENUBEGIN declares
// the APP menu that every window shows, MENUBEGIN@<win> one window's own,
// MENURESET@<win> drops it, MENUUPD@<win> patches one window's copy),
// TRAY* (Shell_NotifyIcon), WINOP (hide/show/
// center/minimize/restore/fullscreen/zoom/ontop/resizable/pos/hideonclose/
// clickthrough/level), CHROME (frame/squareCorners/menu), GET (win/mouse/screens/
// clipboard/battery/idle/frontmost/item/traypos/windows), CLIPWRITE/CLIPWATCH,
// HKREG/HKUNREG (RegisterHotKey), KEYSTROKE (SendInput; cmd ≡ ctrl), SHELL
// (open/reveal/trash), SECRET (Credential Manager), POWER
// (SetThreadExecutionState), SOUND (PlaySound/MessageBeep), NOTIFY (WinRT
// toast with action buttons / reply fields, falling back to a tray balloon on
// old Windows or any WinRT failure), ATTENTION (FlashWindowEx), CTX* (custom
// right-click menu via
// WebView2 ContextMenuRequested), SYS theme/sleep/wake events, DRAGWIN.
//
// macOS-only ops (vibrancy, dock, spaces, Quick Look, OCR, AppleScript, …)
// answer their query with ok:false/'unsupported' (never hang a promise) or
// are ignored when fire-and-forget. Multi-window (WINOPEN) is not yet ported.

#include "webview/webview.h"
#include "tiny_client.h" // generated from runtime/tiny.js (gen-client)
#include "combo.h"       // hotkey / keystroke combo parsing, shared by the three launchers

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincred.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <objbase.h>
#include <ole2.h>
#include <sapi.h>
#include <wincrypt.h>
#include <aclapi.h>       // instance-pipe DACL (#29.2)
#include <sddl.h>
#include <gdiplus.h>
#include <d2d1.h>         // colour emoji menu icons (loaded at runtime)
#include <dwrite.h>
#include <mmdeviceapi.h>  // WASAPI loopback capture (tiny.audioTap)
#include <audioclient.h>
#include <mmreg.h>        // WAVEFORMATEXTENSIBLE

// WinRT toast notifications — raw ABI. These headers only declare the C++ ABI
// interfaces (ABI::Windows::…) and the RuntimeClass_… strings; the combase
// entry points they reference (RoInitialize/RoGetActivationFactory/Windows*
// String) are loaded dynamically at runtime (see the notify section) so the
// binary links nothing new. Any WinRT GUIDs libuuid lacks are defined locally.
//
// MinGW's windows.foundation.h defines both IReference<BYTE> and
// IReference<boolean> — identical types (both `unsigned char`), so the second
// is a redefinition that won't compile. Pre-defining the boolean variant's
// interface guard suppresses it; we never reference IReference<boolean>.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <windows.security.credentials.ui.h> // Windows Hello (AUTH)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// globals

static webview_t g_w = nullptr;
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static std::mutex g_write_mutex;
static HWND g_hwnd = nullptr;
static WNDPROC g_orig_wndproc = nullptr;
static ICoreWebView2 *g_wv2 = nullptr; // stashed for Reload()/settings
static ICoreWebView2Controller *g_ctrl = nullptr;
// "mainWindow": false (TINYJS_NO_MAIN_WINDOW=1): no main window or WebView at
// all — g_w/g_hwnd/g_wv2/g_ctrl stay null, so WebView2's browser processes
// don't start until the app opens a named window. Tray agents.
static bool g_windowless = false;
// The window the app-wide plumbing hangs off: tray callbacks, hotkeys, the
// clipboard timer, posted UI work, system broadcasts. That's main itself
// normally; windowless, a hidden window that is never shown.
static HWND g_host = nullptr;
static bool g_accessory = false;       // tray-only app: no taskbar button
static std::string g_target;           // html path or http(s) url
static bool g_target_is_url = false;
static std::string g_app_name = "tinyjs";
static std::string g_app_version = "0.0.0";

// win.setMinSize for the main window, logical px (0 = no floor). Enforced in
// WM_GETMINMAXINFO; satellites keep their own in TinyWin.
static int g_min_w = 0, g_min_h = 0;
static bool g_hide_on_close = false;
static bool g_frameless = false;
static bool g_square = false;
// Main's transparency: the webview library reads TINYJS_TRANSPARENT at create
// time, so the launcher keeps its own copy — a transparent window has no
// redirection bitmap for GDI to draw a menu bar on.
static bool g_main_transparent = false;
static bool g_click_through = false;
static std::string g_level = "normal";
static bool g_fullscreen = false;
static bool g_programmatic_size = false; // see clamp_min_track
// TINYJS_DEFER_SHOW=1 (rememberWindows): main starts hidden until the bridge
// has put it back where it was and sends `WINOP show`; shown anyway after 3s.
static bool g_defer_show = false;
static WINDOWPLACEMENT g_fs_placement = {sizeof(WINDOWPLACEMENT)};
static LONG g_fs_style = 0, g_fs_exstyle = 0;

// menu / tray / ctx registries (string ids <-> Win32 command ints)
struct ItemReg {
  UINT cmd = 0;
  HMENU parent = nullptr;
  std::string id, label, kind; // kind: menu | tray | ctx
  bool checked = false, enabled = true;
  std::string key; // menu accelerator char ('' = none); fired via Ctrl+<key>
  bool needAlt = false, needShift = false;  // from "alt+" / "shift+" prefixes
  HWND owner = nullptr;  // menu-bar items only: the window whose bar holds
                         // THIS copy. Win32 menus belong to one window, so a
                         // menu shown in three windows is three sets of items
                         // wearing the same ids. null for tray / context.
  std::string role;      // a stock editing item (copy, paste, …): no id, and
                         // a click runs send_stock_edit instead of MENU <id>
  std::string icon;      // png path, so a check toggle can swap the bitmap
};
static void send_stock_edit(const std::string &role);  // below, by KEYSTROKE
static std::map<UINT, ItemReg *> g_cmd_reg;
// One id therefore names as many items as there are windows showing it: an
// app-wide MENUUPD patches every copy, MENUUPD@<win> just that window's.
static std::multimap<std::string, ItemReg *> g_id_reg;
static UINT g_next_cmd = 1000;
static HMENU g_tray_menu = nullptr;
static bool g_tray_primary = false;
static bool g_tray_added = false;
static HICON g_tray_icon = nullptr;
static HMENU g_ctx_menu = nullptr;
static bool g_ctx_suppress = false;
// tinyjs.json "debug" via TINYJS_DEBUG spawn env: absent = no devtools at
// all, "1" = F12 opens them (their own window), "open" = every window
// auto-opens them at creation. "browserAccelerators" via TINYJS_BROWSERACCEL:
// absent = WebView2's own key set (Ctrl+P print, Ctrl+F find, Ctrl+R reload,
// F12…) is suppressed; "1" keeps it.
static bool g_debug = false;
static bool g_debug_open = false;
static bool g_browser_accel = false;
static std::string g_last_notif_id;

static DWORD g_clip_last_seq = 0;
static DWORD g_clip_self_seq = 0;
static std::map<int, std::string> g_hotkeys; // atom id -> string id
static int g_next_hotkey = 1;
static bool g_theme_dark = false;
static bool g_asleep = false;

#define WM_TINY_TRAY (WM_APP + 2)
#define WM_TINY_DISPATCH (WM_APP + 3) // windowless: posted UI work, see below
#define TIMER_CLIPWATCH 0x7101
#define TIMER_SCREENS 0x7102   // one `SYS screens` per settled display change
#define TIMER_DEFERSHOW 0x7103 // rememberWindows: show main if the bridge never does

// Hop to the UI thread. webview_dispatch needs the library's instance, which
// a windowless launcher never creates — there the work rides a message to the
// host window instead, and the do_* handlers get a null webview_t (any that
// touch it check first).
//
// Once webview_run has returned, queued work is dropped: webview_destroy pumps
// messages while it tears the engine down, and an EVAL the bridge sent just
// before quitting (amp's satellites push window-state as main closes) ran
// eval on the half-destroyed engine — an access violation on every amp quit,
// silent until 0.58.1 started reporting the launcher's exit status. And the
// pipe thread must not post at all once destroy has begun (webview_dispatch
// on the freed engine was the other crash): it posts under g_dispatch_mx, and
// run() takes the same lock to set g_stopping before webview_destroy.
typedef void (*tiny_fn)(webview_t, void *);
static std::atomic<bool> g_stopping{false};
static std::mutex g_dispatch_mx;
struct TinyDispatch {
  tiny_fn fn;
  void *arg;
};
static void tiny_dispatch_run(webview_t w, void *a) {
  TinyDispatch *d = static_cast<TinyDispatch *>(a);
  if (!g_stopping)
    d->fn(w, d->arg); // when stopping, arg leaks: the process is exiting
  delete d;
}
static void tiny_dispatch(tiny_fn fn, void *arg) {
  if (g_w) {
    std::lock_guard<std::mutex> lk(g_dispatch_mx);
    if (!g_stopping) // webview_dispatch only posts; holding the lock is cheap
      webview_dispatch(g_w, tiny_dispatch_run, new TinyDispatch{fn, arg});
  } else
    PostMessageW(g_host, WM_TINY_DISPATCH, (WPARAM)fn, (LPARAM)arg);
}

static void drag_dbg(const std::string &msg); // drag&drop diagnostics (below)
static double window_scale(HWND hwnd);        // DPI: logical<->physical (below)
static double monitor_scale(HMONITOR mon);    // (below)

// multi-window plumbing (defined in the multi-window section below)
struct TinyWin;
static std::map<std::string, TinyWin *> g_windows;
static TinyWin *win_for_id(const std::string &id);
static HWND hwnd_for_win(const std::string &id);
static ICoreWebView2 *wv2_for_id(const std::string &id);
// focused: -1 derive from GetForegroundWindow(); 0/1 the WM_ACTIVATE truth
// (during deactivation the foreground handoff hasn't happened yet, so
// deriving would leave the losing window marked focused).
static void emit_winstate(HWND hwnd, int focused = -1);
static void route_ret(webview_t w, const std::string &composite, int status,
                      const std::string &json);
static void secwin_eval(const std::string &id, const std::string &js);
// A closed window's pending NAV re-issue state (#32), defined with NAVQ/NAVR.
static void nav_forget_window(const std::string &id);
// '' / "main" = the main webview; otherwise that secondary window's, so
// print and printToPDF capture the page that asked for them.
static ICoreWebView2 *wv2_for_id(const std::string &id);

// ---------------------------------------------------------------------------
// small helpers

static std::wstring widen(const std::string &s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}

static std::string narrow(const std::wstring &w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr,
                      nullptr);
  return s;
}

// The pipe handle is opened FILE_FLAG_OVERLAPPED: with a synchronous handle a
// blocked ReadFile on the reader thread would serialize the whole file object
// and every WriteFile from the UI thread would hang behind it. Overlapped I/O
// with per-operation events gives the full-duplex behavior a Unix socket has.
static bool overlapped_io(bool write, void *buf, DWORD len, DWORD *done) {
  OVERLAPPED ov = {};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent)
    return false;
  BOOL ok = write ? WriteFile(g_pipe, buf, len, nullptr, &ov)
                  : ReadFile(g_pipe, buf, len, nullptr, &ov);
  if (!ok && GetLastError() != ERROR_IO_PENDING) {
    CloseHandle(ov.hEvent);
    return false;
  }
  ok = GetOverlappedResult(g_pipe, &ov, done, TRUE);
  CloseHandle(ov.hEvent);
  return ok && *done > 0;
}

static void pipe_write_raw(const std::string &msg) {
  const char *p = msg.data();
  size_t left = msg.size();
  while (left > 0) {
    DWORD n = 0;
    if (!overlapped_io(true, (void *)p, (DWORD)left, &n))
      return;
    p += n;
    left -= n;
  }
}

static void pipe_write_line(const std::string &line) {
  // One line per message, always (#30.6). Every field that can carry a line
  // break is escaped before it gets here, so a raw CR/LF means a field
  // slipped through unescaped — and splitting it would hand the backend
  // lines nobody wrote. Drop the whole line instead.
  if (line.find_first_of("\r\n") != std::string::npos) {
    if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
      std::fprintf(stderr, "launcher: dropped a wire line with a raw line break\n");
    return;
  }
  std::lock_guard<std::mutex> lock(g_write_mutex);
  if (g_pipe == INVALID_HANDLE_VALUE)
    return;
  pipe_write_raw(line + "\n");
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

// Reverse of wire_unescape / the bridge's esc(): make text safe to carry in a
// tab-separated wire field (the bridge unescapes it on the way in). Used for
// notification reply text.
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

// C:\path\to\file -> file:///C:/path/to/file with minimal percent-encoding.
static std::string to_file_url(const std::string &path) {
  std::string p = path;
  for (auto &c : p)
    if (c == '\\')
      c = '/';
  static const char hex[] = "0123456789ABCDEF";
  std::string out = "file:///";
  if (!p.empty() && p[0] == '/')
    p.erase(0, 1);
  for (unsigned char c : p) {
    if (isalnum(c) || strchr("/:-._~!$&'()*+,;=@", c)) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// GDI+ (tray icons from png, clipboard image read)
static ULONG_PTR g_gdiplus_token = 0;
static bool ensure_gdiplus() {
  if (g_gdiplus_token)
    return true;
  Gdiplus::GdiplusStartupInput in;
  return Gdiplus::GdiplusStartup(&g_gdiplus_token, &in, nullptr) ==
         Gdiplus::Ok;
}

static HICON icon_from_png(const std::string &path) {
  if (!ensure_gdiplus())
    return nullptr;
  Gdiplus::Bitmap bmp(widen(path).c_str());
  if (bmp.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  HICON icon = nullptr;
  bmp.GetHICON(&icon);
  return icon;
}

static bool taskbar_is_light() {
  DWORD v = 0, sz = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\"
                   L"Personalize",
                   L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v,
                   &sz) == ERROR_SUCCESS)
    return v == 1;
  return false; // the taskbar has defaulted to dark since Win10
}

// 'emoji:<glyph>' icons — the asset-free analogue of macOS 'sf:' symbols.
// GDI/GDI+ have no color-emoji path, so the glyph renders as its monochrome
// fallback outline — a silhouette, which is exactly how template icons look
// in the macOS menu bar. White on a dark taskbar, black on a light one.
static HICON icon_from_emoji(const std::string &utf8) {
  if (!ensure_gdiplus())
    return nullptr;
  const int SZ = 32;
  Gdiplus::Bitmap bmp(SZ, SZ, PixelFormat32bppARGB);
  Gdiplus::Graphics gfx(&bmp);
  gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
  Gdiplus::FontFamily fam(L"Segoe UI Emoji");
  Gdiplus::Font font(&fam, 22, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  if (font.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  Gdiplus::Color col = taskbar_is_light() ? Gdiplus::Color(255, 0, 0, 0)
                                          : Gdiplus::Color(255, 255, 255, 255);
  Gdiplus::SolidBrush brush(col);
  Gdiplus::RectF rect(0, 0, SZ, SZ);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  gfx.DrawString(widen(utf8).c_str(), -1, &font, rect, &fmt, &brush);
  HICON icon = nullptr;
  bmp.GetHICON(&icon);
  return icon;
}

// Save an HBITMAP as a png in the temp dir; returns the path ('' on failure)
// and fills width/height. Used by captureScreen, thumbnail, clipboard read.
static std::string hbitmap_to_temp_png(HBITMAP hbm, UINT *w_out, UINT *h_out);

static CLSID g_png_clsid;
static bool png_encoder_clsid() {
  static int found = -1;
  if (found >= 0)
    return found == 1;
  found = 0;
  UINT num = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (!size)
    return false;
  std::vector<char> buf(size);
  Gdiplus::ImageCodecInfo *info = (Gdiplus::ImageCodecInfo *)buf.data();
  Gdiplus::GetImageEncoders(num, size, info);
  for (UINT i = 0; i < num; i++) {
    if (wcscmp(info[i].MimeType, L"image/png") == 0) {
      g_png_clsid = info[i].Clsid;
      found = 1;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// menus (menu bar + tray + context share the item registry)

struct MenuItemSpec {
  std::string id, label, key;
  std::string icon;  // ITEM 5th / SUB 3rd field: png path ('sf:' is macOS-only)
  std::string role;  // ROLEITEM: a stock editing item (copy, standard, …)
  bool separator = false;
  bool checked = false;
  bool disabled = false;
  std::vector<MenuItemSpec> submenu;
};
struct MenuSpec {
  std::string title;
  std::vector<MenuItemSpec> items;
  // MENUROLE: a standard menu the launcher would build itself. Win32 has none
  // to place, so these draw nothing (except an `edit` slot carrying items,
  // drawn as a plain "Edit" menu — see render_menu) — but the slot still has
  // to occupy an entry, because the parser flushes the items it has collected
  // into pending_menus.back() and skipping the push would aim that flush at
  // the PREVIOUS menu, emptying it. That is exactly what happened to a File menu
  // declared before { role: 'edit' }: it lost every item.
  std::string role;
};
struct TraySpec {
  std::string title, icon, tooltip;
  bool template_icon = true;
  bool primary = false;
  bool remove = false;
  std::vector<MenuItemSpec> items;
};

// A window's menu bar. Every window has one of these, main included, and
// resolves its contents by inheritance: its OWN declaration if it made one
// (MENUBEGIN@<win>), otherwise the app menu (a bare MENUBEGIN, which is what
// tiny.menu.set / app.setMenu send). macOS has a single bar for the whole
// app and always did; here each window draws its own copy of the same thing,
// so an app that says nothing per-window sees the same menu everywhere.
//
// The bar is BUILT even when hidden. Accelerators, MENUUPD and `item@` reads
// all run off the rendered items, and Ctrl+S must keep working in a window
// that merely doesn't show a bar — which is the macOS behaviour a
// chrome.menu:false window is imitating.
struct WinMenu {
  std::vector<MenuSpec> own;
  bool has_own = false;
  bool visible = true;   // chrome.menu
  HMENU bar = nullptr;   // ours to destroy: while hidden it is attached to no
                         // window, so nothing else will free it
};
static std::vector<MenuSpec> g_app_menu;  // the app menu (bare MENUBEGIN)
static WinMenu g_main_menu;               // main's; secondaries carry a WinMenu

// Defined down with the multi-window plumbing (they need TinyWin).
static WinMenu *menu_for(HWND hwnd);
static std::vector<HWND> menu_windows();  // main + every open secondary
// Frameless and transparent windows can't have one: a Win32 bar is GDI, and
// those windows are exactly the ones with no non-client area to draw it in
// (and, when transparent, no redirection bitmap to draw it on).
static bool menu_allowed(HWND hwnd);

static void forget_reg(ItemReg *reg) {
  auto range = g_id_reg.equal_range(reg->id);
  for (auto i = range.first; i != range.second; ++i) {
    if (i->second == reg) { g_id_reg.erase(i); break; }
  }
}

// kind = tray|ctx: clears the lot. kind = menu: clears one WINDOW's items,
// since the others' copies are still on screen and still theirs.
static void clear_registry(const std::string &kind, HWND owner = nullptr) {
  for (auto it = g_cmd_reg.begin(); it != g_cmd_reg.end();) {
    if (it->second->kind == kind && (!owner || it->second->owner == owner)) {
      forget_reg(it->second);
      delete it->second;
      it = g_cmd_reg.erase(it);
    } else {
      ++it;
    }
  }
}

// A menu key is Ctrl plus the character ("s" is Ctrl+S, "S" is Ctrl+Shift+S,
// the shift coming out of the character itself). Prefixes spell the rest:
// "alt+p" is Ctrl+Alt+P. Ctrl is always in — cmd maps to it here.
static void split_accel(const std::string &spec, std::string &key,
                        bool &alt, bool &shift) {
  key = spec;
  alt = false;
  shift = false;
  for (;;) {
    size_t plus = key.find('+');
    if (plus == std::string::npos || plus == 0)
      break;
    std::string mod = key.substr(0, plus);
    for (auto &c : mod) c = (char)tolower((unsigned char)c);
    if (mod == "alt" || mod == "opt" || mod == "option") alt = true;
    else if (mod == "shift") shift = true;
    else if (mod == "ctrl" || mod == "control" || mod == "cmd" ||
             mod == "command" || mod == "meta" || mod == "super") { /* already in */ }
    else break;                                 // not a modifier — it's the key
    key = key.substr(plus + 1);
  }
  // an uppercase letter carries its own shift, the same as on macOS
  if (key.size() == 1 && isupper((unsigned char)key[0])) shift = true;
}

static std::string display_key(const std::string &spec) {
  if (spec.empty())
    return "";
  std::string k;
  bool alt = false, shift = false;
  split_accel(spec, k, alt, shift);
  if (k.empty())
    return "";
  k[0] = (char)toupper((unsigned char)k[0]);
  return std::string("\tCtrl+") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + k;
}

// Stock editing items (ROLEITEM). WebView2 has no editing-command API, so a
// click replays the shortcut into the webview (send_stock_edit). The
// shortcut is label text only — never an accelerator: the webview already
// owns Ctrl+C in its text fields, and claiming it here would take it away.
// Always enabled; unlike macOS there is nothing to ask whether Copy applies.
struct StockItem { const char *role, *label, *shortcut; };
static const StockItem kStockItems[] = {
  {"undo", "Undo", "Ctrl+Z"},   {"redo", "Redo", "Ctrl+Y"},
  {"cut", "Cut", "Ctrl+X"},     {"copy", "Copy", "Ctrl+C"},
  {"paste", "Paste", "Ctrl+V"}, {"selectAll", "Select All", "Ctrl+A"},
};

static void build_menu_items(HMENU menu, const std::vector<MenuItemSpec> &items,
                             const std::string &kind, HWND owner = nullptr);

static void add_stock_items(HMENU menu, const std::string &role,
                            const std::string &kind, HWND owner) {
  if (role == "standard") {
    for (const char *r : {"undo", "redo", "-", "cut", "copy", "paste", "selectAll"}) {
      if (r[0] == '-') AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      else add_stock_items(menu, r, kind, owner);
    }
    return;
  }
  for (const StockItem &si : kStockItems) {
    if (role != si.role) continue;
    UINT cmd = g_next_cmd++;
    ItemReg *reg = new ItemReg{cmd, menu, "", si.label, kind, false, true,
                               "", false, false, owner, role};
    g_cmd_reg[cmd] = reg;
    AppendMenuW(menu, MF_STRING, cmd,
                widen(std::string(si.label) + "\t" + si.shortcut).c_str());
    return;
  }                                            // unknown role: ignored
}

// A menu item's optional icon: the png scaled to small-icon height (aspect
// kept) into a 32bpp premultiplied-alpha DIB, which themed Win32 menus draw
// with its transparency. Menus don't own their hbmpItem, so bitmaps are
// cached by path for the process's life — an app has a handful, and menus
// are rebuilt on every set. 'sf:' (macOS SF Symbols) and unreadable files
// give nullptr: the item shows its label alone.
//
// The bitmap sits in the check column and Win32 then draws no tick for a
// checked item (no frame either, Windows 11), so a checked item gets its own
// variant: the icon with a tick badge in the bottom-right corner.

// 'emoji:<glyph>' menu icons, in colour. GDI+ only draws a glyph's
// monochrome outline (fine for the tray's silhouettes, see icon_from_emoji),
// so these go through Direct2D + DirectWrite, which draw Segoe UI Emoji's
// colour layers. A px×px 32bpp premultiplied top-down DIB, *bits set;
// nullptr if either DLL or the font is missing. Loaded at runtime so the
// build's link line stays as it is.
static HBITMAP emoji_dib(const std::string &utf8, int px, void **bits_out) {
  using CreateD2D = HRESULT(WINAPI *)(D2D1_FACTORY_TYPE, REFIID,
                                      const D2D1_FACTORY_OPTIONS *, void **);
  using CreateDW = HRESULT(WINAPI *)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);
  static ID2D1Factory *d2d = nullptr;
  static IDWriteFactory *dw = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    HMODULE m1 = LoadLibraryW(L"d2d1.dll"), m2 = LoadLibraryW(L"dwrite.dll");
    auto f1 = m1 ? (CreateD2D)(void *)GetProcAddress(m1, "D2D1CreateFactory") : nullptr;
    auto f2 = m2 ? (CreateDW)(void *)GetProcAddress(m2, "DWriteCreateFactory") : nullptr;
    if (f1 && f2) {
      f1(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory), nullptr,
         (void **)&d2d);
      f2(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
         (IUnknown **)&dw);
    }
  }
  if (!d2d || !dw || px < 1)
    return nullptr;
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = px;
  bi.bmiHeader.biHeight = -px;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hbm || !bits) {
    if (hbm) DeleteObject(hbm);
    return nullptr;
  }
  HDC dc = CreateCompatibleDC(nullptr);
  HGDIOBJ old = SelectObject(dc, hbm);
  bool ok = false;
  D2D1_RENDER_TARGET_PROPERTIES props = {};
  props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
  props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
  ID2D1DCRenderTarget *rt = nullptr;
  IDWriteTextFormat *fmt = nullptr;
  ID2D1SolidColorBrush *brush = nullptr;
  RECT rc = {0, 0, px, px};
  std::wstring text = widen(utf8);
  if (SUCCEEDED(d2d->CreateDCRenderTarget(&props, &rt)) &&
      SUCCEEDED(rt->BindDC(dc, &rc)) &&
      SUCCEEDED(dw->CreateTextFormat(
          L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px * 0.8f,
          L"", &fmt))) {
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // A glyph with no colour layers (plain text, old emoji) draws in the
    // menu's text colour.
    COLORREF fg = GetSysColor(COLOR_MENUTEXT);
    D2D1_COLOR_F col = {GetRValue(fg) / 255.f, GetGValue(fg) / 255.f,
                        GetBValue(fg) / 255.f, 1.f};
    if (SUCCEEDED(rt->CreateSolidColorBrush(col, &brush))) {
      // Grayscale: ClearType's per-channel coverage has no alpha to give.
      rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
      rt->BeginDraw();
      rt->Clear(D2D1_COLOR_F{0, 0, 0, 0});
      D2D1_RECT_F box = {0, 0, (float)px, (float)px};
      rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, box, brush,
                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
      ok = SUCCEEDED(rt->EndDraw());
    }
  }
  if (brush) brush->Release();
  if (fmt) fmt->Release();
  if (rt) rt->Release();
  GdiFlush();
  SelectObject(dc, old);
  DeleteDC(dc);
  if (!ok) {
    DeleteObject(hbm);
    return nullptr;
  }
  *bits_out = bits;
  return hbm;
}

static std::map<std::string, HBITMAP> g_menu_bitmaps;
static HBITMAP menu_item_bitmap(const std::string &icon, bool checked) {
  if (icon.empty() || icon.rfind("sf:", 0) == 0)
    return nullptr;
  std::string cache_key = checked ? icon + "\n checked" : icon;
  auto hit = g_menu_bitmaps.find(cache_key);
  if (hit != g_menu_bitmaps.end())
    return hit->second;
  HBITMAP hbm = nullptr;
  int h = GetSystemMetrics(SM_CYSMICON);
  // The source: a png from disk, or an emoji drawn at the menu's size.
  HBITMAP emoji = nullptr;
  std::unique_ptr<Gdiplus::Bitmap> srcp;
  if (ensure_gdiplus()) {
    if (icon.rfind("emoji:", 0) == 0) {
      void *ebits = nullptr;
      if ((emoji = emoji_dib(icon.substr(6), h, &ebits)))
        srcp.reset(new Gdiplus::Bitmap(h, h, h * 4, PixelFormat32bppPARGB,
                                       (BYTE *)ebits));
    } else {
      srcp.reset(new Gdiplus::Bitmap(widen(icon).c_str()));
    }
  }
  if (srcp) {
    Gdiplus::Bitmap &src = *srcp;
    if (src.GetLastStatus() == Gdiplus::Ok && src.GetHeight() > 0) {
      int w = (int)((double)h * src.GetWidth() / src.GetHeight() + 0.5);
      if (w < 1) w = 1;
      BITMAPINFO bi = {};
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = w;
      bi.bmiHeader.biHeight = -h;  // top-down, matching GDI+'s row order
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      void *bits = nullptr;
      hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
      if (hbm && bits) {
        // Zero-filled by CreateDIBSection: transparent until drawn on.
        Gdiplus::Bitmap dst(w, h, w * 4, PixelFormat32bppPARGB, (BYTE *)bits);
        Gdiplus::Graphics gfx(&dst);
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        gfx.DrawImage(&src, 0, 0, w, h);
        if (checked) {
          // Accent-coloured disc, white tick: readable on light and dark
          // menus and over any icon. A thin menu-coloured ring separates
          // it from the icon underneath.
          gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
          float d = h * 0.62f, x = w - d, y = h - d;
          COLORREF hi = GetSysColor(COLOR_HIGHLIGHT);
          COLORREF bg = GetSysColor(COLOR_MENU);
          Gdiplus::SolidBrush ring(Gdiplus::Color(255, GetRValue(bg),
                                                  GetGValue(bg), GetBValue(bg)));
          Gdiplus::SolidBrush disc(Gdiplus::Color(255, GetRValue(hi),
                                                  GetGValue(hi), GetBValue(hi)));
          float r = d * 0.09f;
          gfx.FillEllipse(&ring, x - r, y - r, d + r, d + r);
          gfx.FillEllipse(&disc, x, y, d - r, d - r);
          Gdiplus::Pen tick(Gdiplus::Color(255, 255, 255, 255), d * 0.13f);
          tick.SetStartCap(Gdiplus::LineCapRound);
          tick.SetEndCap(Gdiplus::LineCapRound);
          tick.SetLineJoin(Gdiplus::LineJoinRound);
          float s = d - r;
          Gdiplus::PointF pts[3] = {{x + s * 0.27f, y + s * 0.52f},
                                    {x + s * 0.43f, y + s * 0.68f},
                                    {x + s * 0.74f, y + s * 0.34f}};
          gfx.DrawLines(&tick, pts, 3);
        }
      }
    }
  }
  srcp.reset();  // it borrows the emoji DIB's bits
  if (emoji)
    DeleteObject(emoji);
  g_menu_bitmaps[cache_key] = hbm;  // a miss is cached too: one failed load
  return hbm;
}

static void set_item_bitmap(HMENU menu, UINT item, BOOL by_pos,
                            const std::string &icon, bool checked = false) {
  HBITMAP hbm = menu_item_bitmap(icon, checked);
  if (!hbm)
    return;
  MENUITEMINFOW mii = {sizeof(mii)};
  mii.fMask = MIIM_BITMAP;
  mii.hbmpItem = hbm;
  SetMenuItemInfoW(menu, item, by_pos, &mii);
}

static void build_menu_items(HMENU menu, const std::vector<MenuItemSpec> &items,
                             const std::string &kind, HWND owner) {
  for (const auto &it : items) {
    if (!it.role.empty()) {
      // The tray has no text field to edit; menu bar and right-click do.
      if (kind != "tray") add_stock_items(menu, it.role, kind, owner);
      continue;
    }
    if (it.separator) {
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      continue;
    }
    if (!it.submenu.empty()) {
      HMENU sub = CreatePopupMenu();
      build_menu_items(sub, it.submenu, kind, owner);
      AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub,
                  widen(it.label.empty() ? it.id : it.label).c_str());
      set_item_bitmap(menu, (UINT)(GetMenuItemCount(menu) - 1), TRUE, it.icon);
      continue;
    }
    UINT cmd = g_next_cmd++;
    std::string bare;
    bool alt = false, shift = false;
    split_accel(it.key, bare, alt, shift);
    ItemReg *reg = new ItemReg{cmd, menu, it.id,
                               it.label.empty() ? it.id : it.label, kind,
                               it.checked, !it.disabled, bare, alt, shift,
                               owner};
    reg->icon = it.icon;
    g_cmd_reg[cmd] = reg;
    if (!it.id.empty())
      g_id_reg.emplace(it.id, reg);
    UINT flags = MF_STRING;
    if (it.checked)
      flags |= MF_CHECKED;
    if (it.disabled)
      flags |= MF_GRAYED;
    AppendMenuW(menu, flags, cmd,
                widen(reg->label + display_key(it.key)).c_str());
    set_item_bitmap(menu, cmd, FALSE, it.icon, it.checked);
  }
}

// SetMenu, keeping the PAGE's box. A Win32 bar eats client height, while
// `size`, setSize and getState all speak the page's box — so hand back
// whatever the client just lost (or reclaim it when a bar goes away). Linux
// had to make the same repair when its GTK bar first appeared inside the
// toplevel; before this, Windows quietly shortened the main window's page by
// a bar's worth the moment an app called setMenu.
static void attach_menu(HWND hwnd, HMENU bar) {
  if (GetMenu(hwnd) == bar)
    return;
  RECT before;
  GetClientRect(hwnd, &before);
  SetMenu(hwnd, bar);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  DrawMenuBar(hwnd);
  RECT after;
  GetClientRect(hwnd, &after);
  int dh = (before.bottom - before.top) - (after.bottom - after.top);
  // A maximized, minimized or fullscreen window has no size of its own to
  // give back — there the bar takes its row from the page, as it does in
  // every other Windows app.
  if (!dh || IsZoomed(hwnd) || IsIconic(hwnd) || (hwnd == g_hwnd && g_fullscreen))
    return;
  RECT wr;
  GetWindowRect(hwnd, &wr);
  SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left,
               (wr.bottom - wr.top) + dh,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// (Re)build one window's bar from its effective spec and attach it.
static void render_menu(HWND hwnd) {
  WinMenu *wm = hwnd ? menu_for(hwnd) : nullptr;
  if (!wm)
    return;
  clear_registry("menu", hwnd);
  const std::vector<MenuSpec> &spec = wm->has_own ? wm->own : g_app_menu;
  HMENU bar = nullptr;
  if (!spec.empty()) {
    bar = CreateMenu();
    bool edit_drawn = false;
    for (const auto &m : spec) {
      // A standard-menu slot. Win32 has no launcher-owned Edit menu (the
      // webview handles Ctrl+C/V itself), so a bare { role: 'edit' } draws
      // nothing — but one carrying items becomes a plain "Edit" menu of just
      // those, in that slot, matching what macOS appends under its stock
      // items. First edit block only, as on macOS; other roles draw nothing.
      std::wstring title = widen(m.title);
      if (!m.role.empty()) {
        if (m.role != "edit" || m.items.empty() || edit_drawn)
          continue;
        edit_drawn = true;
        title = L"Edit";
      }
      HMENU popup = CreatePopupMenu();
      build_menu_items(popup, m.items, "menu", hwnd);
      AppendMenuW(bar, MF_POPUP, (UINT_PTR)popup, title.c_str());
    }
    // Nothing but role slots: no bar rather than an empty strip.
    if (GetMenuItemCount(bar) == 0) {
      DestroyMenu(bar);
      bar = nullptr;
    }
  }
  HMENU old = wm->bar;
  wm->bar = bar;
  attach_menu(hwnd, wm->visible && menu_allowed(hwnd) ? bar : nullptr);
  if (old)
    DestroyMenu(old);  // detached by the SetMenu above (or never attached)
}

// win empty = the app menu, which every window that hasn't overridden shows.
struct ApplyMenuReq {
  std::string win;
  std::vector<MenuSpec> menus;
};

static void do_apply_menus(webview_t, void *arg) {
  ApplyMenuReq *req = static_cast<ApplyMenuReq *>(arg);
  if (req->win.empty()) {
    g_app_menu = req->menus;
    // Windows with a menu of their own are deliberately not showing this one.
    for (HWND h : menu_windows()) {
      WinMenu *wm = menu_for(h);
      if (wm && !wm->has_own)
        render_menu(h);
    }
  } else {
    HWND h = hwnd_for_win(req->win);
    if (WinMenu *wm = h ? menu_for(h) : nullptr) {
      wm->own = req->menus;
      wm->has_own = true;
      render_menu(h);
    }
  }
  delete req;
}

// MENURESET@<win>: drop the override, go back to inheriting the app menu.
static void do_reset_menu(webview_t, void *arg) {
  std::string *win = static_cast<std::string *>(arg);
  HWND h = hwnd_for_win(*win);
  if (WinMenu *wm = h ? menu_for(h) : nullptr) {
    wm->own.clear();
    wm->has_own = false;
    render_menu(h);
  }
  delete win;
}

struct MenuUpdReq {
  std::string win, id, label, checked, enabled;  // win empty = every window
};

// Patch the STORED spec as well as the live menu. Bars are rebuilt from these
// specs — by a later window, by MENURESET, by a chrome.menu toggle — so an
// update that only touched the live HMENUs evaporated on any rebuild and the
// stale checked/label state came back. Found on Linux 2026-07-28 and confirmed
// present here 2026-08-06: a window opened AFTER menu.update('tick',
// {checked:true, label:'Ticked!'}) reported the item as "Tick me", unchecked,
// while the app registry correctly said "Ticked!". macOS cannot have this bug
// (one live NSMenu, nothing rebuilds).
static bool patch_spec_items(std::vector<MenuItemSpec> &items,
                             const MenuUpdReq *req) {
  bool hit = false;
  for (auto &it : items) {
    if (it.id == req->id) {
      if (!req->label.empty())
        it.label = req->label;
      if (!req->checked.empty())
        it.checked = req->checked == "1";
      if (!req->enabled.empty())
        it.disabled = req->enabled != "1";
      hit = true;
    }
    if (!it.submenu.empty() && patch_spec_items(it.submenu, req))
      hit = true;
  }
  return hit;
}

static void patch_spec(std::vector<MenuSpec> &menus, const MenuUpdReq *req) {
  for (auto &m : menus)
    patch_spec_items(m.items, req);
}

static void do_menu_update(webview_t, void *arg) {
  MenuUpdReq *req = static_cast<MenuUpdReq *>(arg);
  HWND only = req->win.empty() ? nullptr : hwnd_for_win(req->win);
  if (!req->win.empty() && !only) {
    delete req;  // named a window that has since closed
    return;
  }
  if (only) {
    // One window's own declaration (or, if it inherits, nothing of its own to
    // patch — the app spec must not move for a window-scoped update).
    if (WinMenu *wm = menu_for(only))
      if (wm->has_own)
        patch_spec(wm->own, req);
  } else {
    // App-wide: the app spec, plus every window that copied it into an
    // override of its own.
    patch_spec(g_app_menu, req);
    for (HWND h : menu_windows())
      if (WinMenu *wm = menu_for(h))
        if (wm->has_own)
          patch_spec(wm->own, req);
  }
  auto range = g_id_reg.equal_range(req->id);
  for (auto i = range.first; i != range.second; ++i) {
    ItemReg *reg = i->second;
    if (only && reg->owner != only)
      continue;
    if (!req->label.empty()) {
      reg->label = req->label;
      // MIIM_STRING alone: ModifyMenu would replace the whole item, icon too.
      std::wstring text = widen(reg->label);
      MENUITEMINFOW mii = {sizeof(mii)};
      mii.fMask = MIIM_STRING;
      mii.dwTypeData = text.data();
      SetMenuItemInfoW(reg->parent, reg->cmd, FALSE, &mii);
    }
    if (!req->checked.empty()) {
      reg->checked = req->checked == "1";
      CheckMenuItem(reg->parent, reg->cmd,
                    MF_BYCOMMAND | (reg->checked ? MF_CHECKED : MF_UNCHECKED));
      set_item_bitmap(reg->parent, reg->cmd, FALSE, reg->icon, reg->checked);
    }
    if (!req->enabled.empty()) {
      reg->enabled = req->enabled == "1";
      EnableMenuItem(reg->parent, reg->cmd,
                     MF_BYCOMMAND | (reg->enabled ? MF_ENABLED : MF_GRAYED));
    }
    if (reg->kind == "menu" && reg->owner)
      DrawMenuBar(reg->owner);
  }
  delete req;
}

// ---------------------------------------------------------------------------
// tray

static NOTIFYICONDATAW g_nid = {};

static void tray_ensure_icon_struct() {
  memset(&g_nid, 0, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_host;
  g_nid.uID = 1;
}

static void apply_tray(webview_t, void *arg) {
  TraySpec *spec = static_cast<TraySpec *>(arg);
  if (spec->remove) {
    if (g_tray_added) {
      tray_ensure_icon_struct();
      Shell_NotifyIconW(NIM_DELETE, &g_nid);
      g_tray_added = false;
    }
    if (g_tray_menu) {
      clear_registry("tray");
      DestroyMenu(g_tray_menu);
      g_tray_menu = nullptr;
    }
    delete spec;
    return;
  }
  clear_registry("tray");
  if (g_tray_menu) {
    DestroyMenu(g_tray_menu);
    g_tray_menu = nullptr;
  }
  if (!spec->items.empty()) {
    g_tray_menu = CreatePopupMenu();
    build_menu_items(g_tray_menu, spec->items, "tray");
  }
  g_tray_primary = spec->primary;

  HICON icon = nullptr;
  if (spec->icon.rfind("emoji:", 0) == 0)
    icon = icon_from_emoji(spec->icon.substr(6));
  else if (!spec->icon.empty() && spec->icon.rfind("sf:", 0) != 0)
    icon = icon_from_png(spec->icon);
  if (!icon)
    icon = LoadIcon(nullptr, IDI_APPLICATION);
  if (g_tray_icon && g_tray_icon != icon)
    DestroyIcon(g_tray_icon);
  g_tray_icon = icon;

  tray_ensure_icon_struct();
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TINY_TRAY;
  g_nid.hIcon = icon;
  std::string tip = !spec->tooltip.empty() ? spec->tooltip
                    : !spec->title.empty() ? spec->title
                                           : g_app_name;
  wcsncpy(g_nid.szTip, widen(tip).c_str(), 127);
  Shell_NotifyIconW(g_tray_added ? NIM_MODIFY : NIM_ADD, &g_nid);
  g_tray_added = true;
  delete spec;
}

static void tray_popup(HMENU menu, const char *event_prefix) {
  if (!menu)
    return;
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_host);
  UINT cmd = (UINT)TrackPopupMenu(menu,
                                  TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_host, nullptr);
  PostMessageW(g_host, WM_NULL, 0, 0);
  if (cmd) {
    auto it = g_cmd_reg.find(cmd);
    if (it != g_cmd_reg.end() && !it->second->role.empty())
      send_stock_edit(it->second->role);
    else if (it != g_cmd_reg.end() && it->second->enabled)
      pipe_write_line(std::string(event_prefix) + " " + it->second->id);
  }
}

// NOTIFY — a real WinRT toast (action buttons + reply fields), falling back to
// a tray balloon on old Windows / any WinRT failure. The wire carries
// <id>\t<title>\t<body>\t<subtitle>\t<sound01>[\t<actions-json>] where
// actions-json = [{id, title, reply?, placeholder?, destructive?}].
struct NotifReq {
  std::string id, title, body, subtitle, actions_json;
  bool sound = false;
};

// --- WinRT toast plumbing ---------------------------------------------------
// combase is loaded dynamically so nothing new must link (see the include note
// at the top). The ABI interface types come from the WinRT headers; every
// GUID libuuid lacks is defined locally, matching the SAPI pattern above.
namespace WF = ABI::Windows::Foundation;
namespace WUN = ABI::Windows::UI::Notifications;
namespace WDX = ABI::Windows::Data::Xml::Dom;
namespace WSCU = ABI::Windows::Security::Credentials::UI; // Windows Hello (AUTH)

static const IID kIID_IActivationFactory =
    {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IXmlDocument =
    {0xf7f3a506, 0x1e87, 0x42d6, {0xbc, 0xfb, 0xb8, 0xc8, 0x09, 0xfa, 0x54, 0x94}};
static const IID kIID_IXmlDocumentIO =
    {0x6cd0e74e, 0xee65, 0x4489, {0x9e, 0xbf, 0xca, 0x43, 0xe8, 0x7b, 0xa6, 0x37}};
static const IID kIID_IToastNotificationFactory =
    {0x04124b20, 0x82c6, 0x4229, {0xb1, 0x09, 0xfd, 0x9e, 0xd4, 0x66, 0x2b, 0x53}};
static const IID kIID_IToastNotificationManagerStatics =
    {0x50ac103f, 0xd235, 0x4598, {0xbb, 0xef, 0x98, 0xfe, 0x4d, 0x1a, 0x3a, 0xd4}};
static const IID kIID_IToastActivatedEventArgs =
    {0xe3bf92f3, 0xc197, 0x436f, {0x82, 0x65, 0x06, 0x25, 0x82, 0x4f, 0x8d, 0xac}};
static const IID kIID_IToastActivatedEventArgs2 =
    {0xab7da512, 0xcc61, 0x568e, {0x81, 0xbe, 0x30, 0x4a, 0xc3, 0x10, 0x38, 0xfa}};
static const IID kIID_IPropertyValue =
    {0x4bd682dd, 0x7554, 0x40e9, {0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62}};
static const IID kIID_ITypedEventHandler_Toast =
    {0xab54de2d, 0x97d9, 0x5528, {0xb6, 0xad, 0x10, 0x5a, 0xfe, 0x15, 0x65, 0x30}};
// IMap<HSTRING, IInspectable*> — the ValueSet returned by get_UserInput. Not
// pre-instantiated in MinGW's headers, so a minimal vtable is hand-declared.
static const IID kIID_IMap_String_Object =
    {0x1b0d3570, 0x0877, 0x5ec2, {0x8a, 0x2c, 0x3b, 0x95, 0x39, 0x50, 0x6a, 0xca}};
struct IMapStringInspectable : IInspectable {
  virtual HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IInspectable **value) = 0;
};
// AppUserModelID property (fmtid + pid 5) stamped onto the Start-Menu shortcut.
static const PROPERTYKEY kPKEY_AppUserModel_ID = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}},
    5};
// Taskbar relaunch properties (same fmtid, pids 2/3/4). Pinning a window pins
// the window's PROCESS by default — that's launcher.exe, which can't start on
// its own (it needs the page + pipe argv from the app exe). These per-window
// values redirect the pin (and its later launches) to the app exe instead.
static const PROPERTYKEY kPKEY_AppUserModel_RelaunchCommand = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}},
    2};
static const PROPERTYKEY kPKEY_AppUserModel_RelaunchIconResource = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}},
    3};
static const PROPERTYKEY kPKEY_AppUserModel_RelaunchDisplayNameResource = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}},
    4};

// The app's own exe, handed over by the bridge for BUILT apps (dev spawns set
// nothing — there's no exe worth pinning). Cached on first use.
static const std::wstring &app_exe_env() {
  static std::wstring exe = [] {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"TINYJS_APP_EXE", buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring();
  }();
  return exe;
}

// AppUserModelID shared by windows, toasts, and the Start-Menu shortcut.
static std::wstring tinyjs_aumid() {
  std::string safe;
  for (char c : g_app_name)
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')
      safe += c;
  if (safe.empty())
    safe = "app";
  return L"tinyjs." + widen(safe);
}

// Stamp a top-level window so taskbar pins relaunch the app exe (with its
// name and icon) rather than trying to start a bare launcher.exe.
static void apply_relaunch_props(HWND hwnd) {
  const std::wstring &exe = app_exe_env();
  if (exe.empty() || !hwnd)
    return;
  typedef HRESULT(WINAPI *GetStoreFn)(HWND, REFIID, void **);
  static GetStoreFn get_store = (GetStoreFn)GetProcAddress(
      LoadLibraryW(L"shell32.dll"), "SHGetPropertyStoreForWindow");
  if (!get_store)
    return;
  IPropertyStore *store = nullptr;
  if (FAILED(get_store(hwnd, IID_IPropertyStore, (void **)&store)) || !store)
    return;
  auto set_str = [&](const PROPERTYKEY &key, const std::wstring &val) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_LPWSTR;
    pv.pwszVal = (LPWSTR)CoTaskMemAlloc((val.size() + 1) * sizeof(wchar_t));
    if (!pv.pwszVal)
      return;
    wcscpy(pv.pwszVal, val.c_str());
    store->SetValue(key, pv);
    PropVariantClear(&pv);
  };
  set_str(kPKEY_AppUserModel_ID, tinyjs_aumid());
  set_str(kPKEY_AppUserModel_RelaunchCommand, L"\"" + exe + L"\"");
  set_str(kPKEY_AppUserModel_RelaunchDisplayNameResource, widen(g_app_name));
  set_str(kPKEY_AppUserModel_RelaunchIconResource, exe + L",0");
  store->Commit();
  store->Release();
}

// Windows Hello (AUTH). MinGW libuuid lacks these WinRT GUIDs; the values match
// the DEFINE_GUID lines in windows.security.credentials.ui.h (the statics IID
// plus the pinterface GUIDs of the two IAsyncOperationCompletedHandler<>
// specializations we hand-roll).
static const IID kIID_IUserConsentVerifierStatics =
    {0xaf4f3f91, 0x564c, 0x4ddc, {0xb8, 0xb5, 0x97, 0x34, 0x47, 0x62, 0x7c, 0x65}};
static const IID kIID_IAsyncOpCompleted_UCVAvailability =
    {0x28988174, 0xace2, 0x5c15, {0xa0, 0xdf, 0x58, 0x0a, 0x26, 0xd9, 0x42, 0x94}};
static const IID kIID_IAsyncOpCompleted_UCVResult =
    {0x0cffc6c9, 0x4c2b, 0x5cd4, {0xb3, 0x8c, 0x7b, 0x8d, 0xf3, 0xff, 0x5a, 0xfb}};

typedef HRESULT(WINAPI *RoInitialize_t)(int);
typedef HRESULT(WINAPI *RoGetActivationFactory_t)(HSTRING, REFIID, void **);
typedef HRESULT(WINAPI *WindowsCreateString_t)(PCWSTR, UINT32, HSTRING *);
typedef HRESULT(WINAPI *WindowsDeleteString_t)(HSTRING);
typedef PCWSTR(WINAPI *WindowsGetStringRawBuffer_t)(HSTRING, UINT32 *);
static RoInitialize_t p_RoInitialize = nullptr;
static RoGetActivationFactory_t p_RoGetActivationFactory = nullptr;
static WindowsCreateString_t p_WindowsCreateString = nullptr;
static WindowsDeleteString_t p_WindowsDeleteString = nullptr;
static WindowsGetStringRawBuffer_t p_WindowsGetStringRawBuffer = nullptr;

static bool load_combase() {
  static int state = -1; // -1 untried, 0 failed, 1 ok
  if (state >= 0)
    return state == 1;
  state = 0;
  HMODULE m = LoadLibraryW(L"combase.dll");
  if (!m)
    return false;
  p_RoInitialize = (RoInitialize_t)GetProcAddress(m, "RoInitialize");
  p_RoGetActivationFactory =
      (RoGetActivationFactory_t)GetProcAddress(m, "RoGetActivationFactory");
  p_WindowsCreateString =
      (WindowsCreateString_t)GetProcAddress(m, "WindowsCreateString");
  p_WindowsDeleteString =
      (WindowsDeleteString_t)GetProcAddress(m, "WindowsDeleteString");
  p_WindowsGetStringRawBuffer = (WindowsGetStringRawBuffer_t)GetProcAddress(
      m, "WindowsGetStringRawBuffer");
  state = (p_RoInitialize && p_RoGetActivationFactory && p_WindowsCreateString &&
           p_WindowsDeleteString && p_WindowsGetStringRawBuffer)
              ? 1
              : 0;
  return state == 1;
}

// RAII HSTRING from a wide string.
struct HStr {
  HSTRING h = nullptr;
  HStr(const std::wstring &s) {
    p_WindowsCreateString(s.c_str(), (UINT32)s.size(), &h);
  }
  ~HStr() {
    if (h)
      p_WindowsDeleteString(h);
  }
};

static std::string hstring_to_utf8(HSTRING h) {
  if (!h)
    return "";
  UINT32 len = 0;
  PCWSTR buf = p_WindowsGetStringRawBuffer(h, &len);
  if (!buf || !len)
    return "";
  return narrow(std::wstring(buf, len));
}

static std::string xml_escape(const std::string &s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&': o += "&amp;"; break;
    case '<': o += "&lt;"; break;
    case '>': o += "&gt;"; break;
    case '"': o += "&quot;"; break;
    case '\'': o += "&apos;"; break;
    default: o += c;
    }
  }
  return o;
}

// Minimal, bounds-safe extractors over an actions-json object slice.
static bool json_str_field(const std::string &obj, const std::string &key,
                           std::string &out) {
  std::string needle = "\"" + key + "\"";
  size_t k = obj.find(needle);
  if (k == std::string::npos)
    return false;
  size_t i = obj.find(':', k + needle.size());
  if (i == std::string::npos)
    return false;
  i++;
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t'))
    i++;
  if (i >= obj.size() || obj[i] != '"')
    return false;
  i++;
  std::string v;
  while (i < obj.size() && obj[i] != '"') {
    if (obj[i] == '\\' && i + 1 < obj.size()) {
      char c = obj[++i];
      v += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
    } else {
      v += obj[i];
    }
    i++;
  }
  out = v;
  return true;
}

static bool json_bool_field(const std::string &obj, const std::string &key) {
  std::string needle = "\"" + key + "\"";
  size_t k = obj.find(needle);
  if (k == std::string::npos)
    return false;
  size_t i = obj.find(':', k + needle.size());
  if (i == std::string::npos)
    return false;
  i++;
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t'))
    i++;
  return obj.compare(i, 4, "true") == 0;
}

// Split a JSON array of objects into top-level `{…}` object slices.
static std::vector<std::string> json_objects(const std::string &arr) {
  std::vector<std::string> out;
  int depth = 0;
  bool in_str = false;
  size_t start = 0;
  for (size_t i = 0; i < arr.size(); i++) {
    char c = arr[i];
    if (in_str) {
      if (c == '\\')
        i++;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"')
      in_str = true;
    else if (c == '{') {
      if (depth == 0)
        start = i;
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0)
        out.push_back(arr.substr(start, i - start + 1));
    }
  }
  return out;
}

static std::string build_toast_xml(const NotifReq &req) {
  std::string xml = "<toast><visual><binding template=\"ToastGeneric\">";
  xml += "<text>" + xml_escape(req.title) + "</text>";
  if (!req.body.empty())
    xml += "<text>" + xml_escape(req.body) + "</text>";
  if (!req.subtitle.empty())
    xml += "<text>" + xml_escape(req.subtitle) + "</text>";
  xml += "</binding></visual>";

  std::string inputs, buttons;
  bool have_reply_input = false;
  if (!req.actions_json.empty()) {
    for (const std::string &obj : json_objects(req.actions_json)) {
      std::string aid, title, ph;
      if (!json_str_field(obj, "id", aid))
        continue;
      if (!json_str_field(obj, "title", title))
        title = aid;
      bool reply = json_bool_field(obj, "reply");
      std::string args = "action:" + aid;
      if (reply) {
        if (!have_reply_input) {
          json_str_field(obj, "placeholder", ph);
          inputs += "<input id=\"reply\" type=\"text\" placeHolderContent=\"" +
                    xml_escape(ph) + "\"/>";
          have_reply_input = true;
        }
        buttons += "<action content=\"" + xml_escape(title) +
                   "\" arguments=\"" + xml_escape(args) +
                   "\" hint-inputId=\"reply\"/>";
      } else {
        buttons += "<action content=\"" + xml_escape(title) +
                   "\" arguments=\"" + xml_escape(args) + "\"/>";
      }
    }
  }
  if (!inputs.empty() || !buttons.empty())
    xml += "<actions>" + inputs + buttons + "</actions>";
  if (!req.sound)
    xml += "<audio silent=\"true\"/>";
  xml += "</toast>";
  return xml;
}

// A toast's Activated callback: body clicks report NOTIFYCLICK, buttons/reply
// report NOTIFYACTION. Hand-rolled ITypedEventHandler<ToastNotification*,
// IInspectable*> answering only IUnknown + its exact IID.
struct ToastActivatedHandler
    : WF::ITypedEventHandler<WUN::ToastNotification *, IInspectable *> {
  ULONG refs = 1;
  std::string notif_id;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown || riid == kIID_ITypedEventHandler_Toast) {
      *ppv = static_cast<
          WF::ITypedEventHandler<WUN::ToastNotification *, IInspectable *> *>(
          this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(WUN::IToastNotification *,
                                   IInspectable *args) override {
    std::string arguments;
    WUN::IToastActivatedEventArgs *a1 = nullptr;
    if (args &&
        SUCCEEDED(args->QueryInterface(kIID_IToastActivatedEventArgs,
                                       (void **)&a1)) &&
        a1) {
      HSTRING h = nullptr;
      if (SUCCEEDED(a1->get_Arguments(&h))) {
        arguments = hstring_to_utf8(h);
        if (h)
          p_WindowsDeleteString(h);
      }
      a1->Release();
    }
    if (arguments.rfind("action:", 0) != 0) {
      // Body click (no per-button arguments).
      pipe_write_line("NOTIFYCLICK " + notif_id);
      return S_OK;
    }
    std::string action = arguments.substr(7);
    // Reply text lives in UserInput["reply"] (empty for a plain button).
    std::string reply;
    WUN::IToastActivatedEventArgs2 *a2 = nullptr;
    if (args &&
        SUCCEEDED(args->QueryInterface(kIID_IToastActivatedEventArgs2,
                                       (void **)&a2)) &&
        a2) {
      IInspectable *setInsp = nullptr;
      if (SUCCEEDED(a2->get_UserInput(
              (ABI::Windows::Foundation::Collections::IPropertySet **)
                  &setInsp)) &&
          setInsp) {
        IMapStringInspectable *map = nullptr;
        if (SUCCEEDED(setInsp->QueryInterface(kIID_IMap_String_Object,
                                              (void **)&map)) &&
            map) {
          HStr key(L"reply");
          IInspectable *val = nullptr;
          if (SUCCEEDED(map->Lookup(key.h, &val)) && val) {
            WF::IPropertyValue *pv = nullptr;
            if (SUCCEEDED(val->QueryInterface(kIID_IPropertyValue,
                                              (void **)&pv)) &&
                pv) {
              HSTRING h = nullptr;
              if (SUCCEEDED(pv->GetString(&h))) {
                reply = hstring_to_utf8(h);
                if (h)
                  p_WindowsDeleteString(h);
              }
              pv->Release();
            }
            val->Release();
          }
          map->Release();
        }
        setInsp->Release();
      }
      a2->Release();
    }
    pipe_write_line("NOTIFYACTION " + notif_id + "\t" + action + "\t" +
                    wire_escape(reply));
    return S_OK;
  }
};

// Live toasts are kept referenced so their Activated handler stays registered
// (releasing the notification would tear the handler down before the user
// interacts). Capped so a chatty app can't grow the list without bound.
static std::vector<WUN::IToastNotification *> g_live_toasts;

// Unpackaged apps need an explicit AppUserModelID plus a Start-Menu shortcut
// carrying it, or CreateToastNotifier fails. Done once, lazily, on first toast.
static std::wstring g_aumid;

// What an existing .lnk points at, and the AppUserModelID it carries (both
// empty if it can't be read). The AUMID matters as much as the target: a
// shortcut written by something that can't set it — shelf uses WScript.Shell,
// which has no way to — looks right but won't serve as toast identity.
struct LnkInfo {
  std::wstring target;
  std::wstring aumid;
};

static LnkInfo read_shortcut(const std::wstring &lnk) {
  LnkInfo info;
  IShellLinkW *link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkW, (void **)&link)) ||
      !link)
    return info;
  IPersistFile *pf = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void **)&pf)) && pf) {
    if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
      wchar_t buf[MAX_PATH] = {0};
      // SLGP_RAWPATH: we want what was stored, not a resolved/relocated guess
      if (SUCCEEDED(link->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)))
        info.target = buf;
      IPropertyStore *store = nullptr;
      if (SUCCEEDED(link->QueryInterface(IID_IPropertyStore, (void **)&store)) &&
          store) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(store->GetValue(kPKEY_AppUserModel_ID, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal)
          info.aumid = pv.pwszVal;
        PropVariantClear(&pv);
        store->Release();
      }
    }
    pf->Release();
  }
  link->Release();
  return info;
}

static void create_start_menu_shortcut(const std::wstring &aumid,
                                       const std::wstring &name) {
  wchar_t appdata[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
    return;
  std::wstring dir =
      std::wstring(appdata) + L"\\Microsoft\\Windows\\Start Menu\\Programs";
  std::wstring lnk = dir + L"\\" + name + L".lnk";
  // Point the shortcut at the APP exe when the bridge handed us one (a bare
  // launcher.exe can't start by itself); dev/legacy spawns fall back to us.
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring exe = app_exe_env().empty() ? std::wstring(self) : app_exe_env();
  // An existing shortcut is NOT reason enough to bail. Two ways one can be
  // there but wrong:
  //   - a `tinyjs dev` run has no TINYJS_APP_EXE, so it stamps one pointing at
  //     launcher-win.exe, which can't start alone (four such were found on a
  //     dev box); bailing on "file exists" froze that forever, so a later real
  //     install could never correct it;
  //   - an installer wrote a plain shortcut with no AUMID — shelf does exactly
  //     this, since WScript.Shell can't set one — which looks fine in Start but
  //     is useless as toast identity.
  // So rewrite unless BOTH the target and the AUMID already match; an
  // untouched correct shortcut is left alone so this doesn't churn on every
  // toast or clobber a user's own edits.
  if (GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES) {
    LnkInfo cur = read_shortcut(lnk);
    if (_wcsicmp(cur.target.c_str(), exe.c_str()) == 0 && cur.aumid == aumid)
      return;
  }
  IShellLinkW *link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkW, (void **)&link)) ||
      !link)
    return;
  link->SetPath(exe.c_str());
  IPropertyStore *store = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_IPropertyStore, (void **)&store)) &&
      store) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_LPWSTR;
    pv.pwszVal = (LPWSTR)CoTaskMemAlloc((aumid.size() + 1) * sizeof(wchar_t));
    if (pv.pwszVal) {
      wcscpy(pv.pwszVal, aumid.c_str());
      store->SetValue(kPKEY_AppUserModel_ID, pv);
      store->Commit();
    }
    PropVariantClear(&pv);
    store->Release();
  }
  IPersistFile *pf = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void **)&pf)) && pf) {
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    pf->Save(lnk.c_str(), TRUE);
    pf->Release();
  }
  link->Release();
}

static bool ensure_toast_identity() {
  if (!g_aumid.empty())
    return true;
  g_aumid = tinyjs_aumid();
  SetCurrentProcessExplicitAppUserModelID(g_aumid.c_str());
  create_start_menu_shortcut(g_aumid, g_aumid.substr(7) /* strip "tinyjs." */);
  return true;
}

// Try to show a real WinRT toast. Returns false (leaving nothing shown) on any
// failure so do_notify can fall back to the tray balloon.
static bool do_notify_toast(const NotifReq &req) {
  if (!load_combase())
    return false;
  HRESULT hr = p_RoInitialize(RO_INIT_SINGLETHREADED);
  // The UI thread is already an STA (CoInitialize'd by OLE/webview); a redundant
  // RoInitialize returns S_FALSE, and a mode mismatch RPC_E_CHANGED_MODE — both
  // are fine, the apartment is usable either way.
  if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
    return false;
  ensure_toast_identity();

  std::wstring xml = widen(build_toast_xml(req));

  // XmlDocument via its activation factory → LoadXml.
  IActivationFactory *xmlFactory = nullptr;
  {
    HStr cls(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument);
    if (FAILED(p_RoGetActivationFactory(cls.h, kIID_IActivationFactory,
                                        (void **)&xmlFactory)) ||
        !xmlFactory)
      return false;
  }
  IInspectable *xmlInsp = nullptr;
  hr = xmlFactory->ActivateInstance(&xmlInsp);
  xmlFactory->Release();
  if (FAILED(hr) || !xmlInsp)
    return false;
  WDX::IXmlDocumentIO *xmlIO = nullptr;
  if (FAILED(xmlInsp->QueryInterface(kIID_IXmlDocumentIO, (void **)&xmlIO)) ||
      !xmlIO) {
    xmlInsp->Release();
    return false;
  }
  {
    HStr hxml(xml);
    hr = xmlIO->LoadXml(hxml.h);
  }
  if (FAILED(hr)) {
    xmlIO->Release();
    xmlInsp->Release();
    return false;
  }
  WDX::IXmlDocument *xmlDoc = nullptr;
  hr = xmlIO->QueryInterface(kIID_IXmlDocument, (void **)&xmlDoc);
  xmlIO->Release();
  if (FAILED(hr) || !xmlDoc) {
    xmlInsp->Release();
    return false;
  }

  // ToastNotification via its factory.
  WUN::IToastNotificationFactory *toastFactory = nullptr;
  {
    HStr cls(RuntimeClass_Windows_UI_Notifications_ToastNotification);
    hr = p_RoGetActivationFactory(cls.h, kIID_IToastNotificationFactory,
                                  (void **)&toastFactory);
  }
  if (FAILED(hr) || !toastFactory) {
    xmlDoc->Release();
    xmlInsp->Release();
    return false;
  }
  WUN::IToastNotification *toast = nullptr;
  hr = toastFactory->CreateToastNotification(xmlDoc, &toast);
  toastFactory->Release();
  xmlDoc->Release();
  xmlInsp->Release();
  if (FAILED(hr) || !toast)
    return false;

  ToastActivatedHandler *handler = new ToastActivatedHandler();
  handler->notif_id = req.id;
  EventRegistrationToken tok = {};
  toast->add_Activated(handler, &tok);
  handler->Release();

  // Notifier via the manager statics, keyed by our AUMID.
  WUN::IToastNotificationManagerStatics *statics = nullptr;
  {
    HStr cls(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager);
    hr = p_RoGetActivationFactory(cls.h, kIID_IToastNotificationManagerStatics,
                                  (void **)&statics);
  }
  if (FAILED(hr) || !statics) {
    toast->Release();
    return false;
  }
  WUN::IToastNotifier *notifier = nullptr;
  {
    HStr aumid(g_aumid);
    hr = statics->CreateToastNotifierWithId(aumid.h, &notifier);
  }
  statics->Release();
  if (FAILED(hr) || !notifier) {
    toast->Release();
    return false;
  }
  hr = notifier->Show(toast);
  notifier->Release();
  if (FAILED(hr)) {
    toast->Release();
    return false;
  }
  // Keep the toast (and thus its handler) alive; cap the retained set.
  g_live_toasts.push_back(toast);
  while (g_live_toasts.size() > 32) {
    g_live_toasts.front()->Release();
    g_live_toasts.erase(g_live_toasts.begin());
  }
  g_last_notif_id = req.id;
  return true;
}

// Fallback: a balloon on the tray icon (created on demand when the app has no
// tray). Clicks come back as NOTIFYCLICK via NIN_BALLOONUSERCLICK; the balloon
// has no action buttons.
static void do_notify_balloon(const NotifReq &req) {
  if (!g_tray_added) {
    tray_ensure_icon_struct();
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TINY_TRAY;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy(g_nid.szTip, widen(g_app_name).c_str(), 127);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_tray_added = true;
  }
  g_last_notif_id = req.id;
  tray_ensure_icon_struct();
  g_nid.uFlags = NIF_INFO;
  std::string body = req.body;
  if (!req.subtitle.empty())
    body = req.subtitle + "\n" + body;
  wcsncpy(g_nid.szInfoTitle, widen(req.title).c_str(), 63);
  wcsncpy(g_nid.szInfo, widen(body).c_str(), 255);
  g_nid.dwInfoFlags = NIIF_INFO | (req.sound ? 0 : NIIF_NOSOUND);
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void do_notify(webview_t, void *arg) {
  NotifReq *req = static_cast<NotifReq *>(arg);
  if (!do_notify_toast(*req))
    do_notify_balloon(*req);
  delete req;
}

// ---------------------------------------------------------------------------
// dialogs

struct DlgReq {
  std::string id, op;
  std::vector<std::string> args;
};

// prompt: an in-memory DLGTEMPLATE (message + edit + OK/Cancel).
struct PromptState {
  std::wstring message, value;
  bool ok = false;
};

static INT_PTR CALLBACK prompt_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  PromptState *st = (PromptState *)GetWindowLongPtrW(dlg, GWLP_USERDATA);
  switch (msg) {
  case WM_INITDIALOG:
    st = (PromptState *)lp;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)st);
    SetDlgItemTextW(dlg, 100, st->message.c_str());
    SetDlgItemTextW(dlg, 101, st->value.c_str());
    SendDlgItemMessageW(dlg, 101, EM_SETSEL, 0, -1);
    SetFocus(GetDlgItem(dlg, 101));
    return FALSE;
  case WM_COMMAND:
    if (LOWORD(wp) == IDOK) {
      wchar_t buf[2048];
      GetDlgItemTextW(dlg, 101, buf, 2048);
      st->value = buf;
      st->ok = true;
      EndDialog(dlg, 1);
      return TRUE;
    }
    if (LOWORD(wp) == IDCANCEL) {
      EndDialog(dlg, 0);
      return TRUE;
    }
  }
  return FALSE;
}

// Append a word-aligned dialog item to the template buffer.
static void dlg_align(std::vector<WORD> &t) {
  while (t.size() % 2)
    t.push_back(0);
}
static void dlg_str(std::vector<WORD> &t, const std::wstring &s) {
  for (wchar_t c : s)
    t.push_back((WORD)c);
  t.push_back(0);
}
static void dlg_item(std::vector<WORD> &t, DWORD style, short x, short y,
                     short cx, short cy, WORD id, WORD cls,
                     const std::wstring &text) {
  dlg_align(t);
  DLGITEMTEMPLATE it = {};
  it.style = style | WS_CHILD | WS_VISIBLE;
  it.x = x;
  it.y = y;
  it.cx = cx;
  it.cy = cy;
  it.id = id;
  size_t off = t.size();
  t.resize(off + sizeof(it) / 2);
  memcpy(&t[off], &it, sizeof(it));
  t.push_back(0xFFFF);
  t.push_back(cls); // 0x0080 button, 0x0081 edit, 0x0082 static
  dlg_str(t, text);
  t.push_back(0); // no creation data
}

// A button label as Windows draws it: a lone '&' would turn the next letter
// into an underlined access key ("Save & Close" shows "Save _Close"), so
// double it. Empty means "use the fallback".
static std::wstring button_label(const std::string &label,
                                 const wchar_t *fallback) {
  if (label.empty())
    return fallback;
  std::wstring out;
  for (wchar_t c : widen(label)) {
    out += c;
    if (c == L'&')
      out += L'&';
  }
  return out;
}

// Raw core shared by the backend dialog op and the page's window.prompt()
// (browser affordances): returns true on OK with the typed text in *out.
// `owner` is the window the prompt belongs to — a page's window.prompt() in a
// popup must be modal to THAT window, not to main (nullptr = main). Empty
// labels keep OK/Cancel, which is all the browser's own prompt() ever passes.
static bool run_prompt_raw(const std::string &message, const std::string &defval,
                           std::wstring *out, HWND owner = nullptr,
                           const std::string &okLabel = "",
                           const std::string &cancelLabel = "") {
  std::wstring okText = button_label(okLabel, L"OK");
  std::wstring cancelText = button_label(cancelLabel, L"Cancel");
  // Template units are dialog units, where an average Segoe UI character is
  // 4 wide. A button grows past the stock 50 to fit its label, capped so the
  // pair still fits the 224-wide content area.
  auto fit = [](const std::wstring &s) {
    return (short)std::min<size_t>(108, std::max<size_t>(50, s.size() * 4 + 12));
  };
  short okW = fit(okText), cancelW = fit(cancelText);
  std::vector<WORD> t;
  DLGTEMPLATE hdr = {};
  hdr.style = DS_MODALFRAME | DS_SETFONT | WS_CAPTION | WS_SYSMENU | WS_POPUP |
              DS_CENTER;
  hdr.cdit = 4;
  hdr.cx = 240;
  hdr.cy = 78;
  size_t off = t.size();
  t.resize(off + sizeof(hdr) / 2);
  memcpy(&t[off], &hdr, sizeof(hdr));
  t.push_back(0); // menu
  t.push_back(0); // class
  dlg_str(t, widen(g_app_name));
  t.push_back(9); // font size
  dlg_str(t, L"Segoe UI");
  dlg_item(t, SS_LEFT, 8, 8, 224, 18, 100, 0x0082, L"");
  dlg_item(t, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 8, 30, 224, 13, 101,
           0x0081, L"");
  dlg_item(t, BS_DEFPUSHBUTTON | WS_TABSTOP, 232 - cancelW - 4 - okW, 56, okW,
           14, IDOK, 0x0080, okText);
  dlg_item(t, BS_PUSHBUTTON | WS_TABSTOP, 232 - cancelW, 56, cancelW, 14,
           IDCANCEL, 0x0080, cancelText);
  PromptState st;
  st.message = widen(message);
  st.value = widen(defval);
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                          (LPCDLGTEMPLATEW)t.data(), owner ? owner : g_host,
                          prompt_proc, (LPARAM)&st);
  if (st.ok && out)
    *out = st.value;
  return st.ok;
}

static std::string run_prompt(const std::string &message,
                              const std::string &defval,
                              const std::string &okLabel,
                              const std::string &cancelLabel) {
  std::wstring value;
  return run_prompt_raw(message, defval, &value, nullptr, okLabel, cancelLabel)
             ? json_escape(narrow(value))
             : "null";
}

// MessageBoxW can't name its buttons, so relabel them from a CBT hook:
// HCBT_ACTIVATE arrives once the box is built and before it's shown. The hook
// is thread-local, installed on the UI thread right before MessageBoxW and
// removed right after. It handles the first dialog it sees and then stands
// down, so a nested box (a dialog op dispatched inside this one's modal loop)
// brings its own hook and labels.
struct MsgBoxLabels {
  std::wstring ok, cancel; // empty = leave Windows' own
  bool done = false;
};
static MsgBoxLabels *g_msgbox_labels = nullptr;

// A widened box paints its grey button band only as wide as the client it was
// laid out in, leaving a white notch under the right-hand button. After each
// paint, stretch the last laid-out column across the added width: that copies
// the white body and the band alike, with no guessing at the band's height.
static const wchar_t *MSGBOX_PROC = L"tinyjs.msgbox.proc";
static const wchar_t *MSGBOX_WIDTH = L"tinyjs.msgbox.width";
static LRESULT CALLBACK widened_msgbox_proc(HWND box, UINT msg, WPARAM wp,
                                            LPARAM lp) {
  WNDPROC orig = (WNDPROC)GetPropW(box, MSGBOX_PROC);
  LRESULT r = CallWindowProcW(orig, box, msg, wp, lp);
  if (msg == WM_PAINT) {
    int laid = (int)(INT_PTR)GetPropW(box, MSGBOX_WIDTH);
    RECT cr;
    GetClientRect(box, &cr);
    if (laid > 0 && cr.right > laid) {
      // Children clipped out: the moved buttons sit in that strip.
      HDC dc = GetDCEx(box, nullptr, DCX_CACHE | DCX_CLIPCHILDREN);
      StretchBlt(dc, laid, 0, cr.right - laid, cr.bottom, dc, laid - 1, 0, 1,
                 cr.bottom, SRCCOPY);
      ReleaseDC(box, dc);
    }
  } else if (msg == WM_NCDESTROY) {
    RemovePropW(box, MSGBOX_PROC);
    RemovePropW(box, MSGBOX_WIDTH);
  }
  return r;
}

static void relabel_msgbox(HWND box, const MsgBoxLabels &l) {
  HWND btns[2] = {GetDlgItem(box, IDOK), GetDlgItem(box, IDCANCEL)};
  // MB_OK's lone button carries IDCANCEL's id (so Esc closes the box) while
  // MessageBoxW still returns IDOK: it's the ok button whatever its id says.
  if (!btns[0]) {
    btns[0] = btns[1];
    btns[1] = nullptr;
  }
  const std::wstring *texts[2] = {&l.ok, &l.cancel};
  HFONT font = (HFONT)SendMessageW(box, WM_GETFONT, 0, 0);
  HDC dc = GetDC(box);
  HGDIOBJ old = SelectObject(dc, font);
  int need = 0, n = 0;
  RECT r[2] = {};
  for (int i = 0; i < 2; i++) {
    if (!btns[i])
      continue;
    n++;
    if (!texts[i]->empty())
      SetWindowTextW(btns[i], texts[i]->c_str());
    wchar_t buf[256];
    GetWindowTextW(btns[i], buf, 256);
    RECT tr = {0, 0, 0, 0};
    DrawTextW(dc, buf, -1, &tr, DT_CALCRECT | DT_SINGLELINE);
    need = std::max(need, (int)(tr.right - tr.left) + 24);
    GetWindowRect(btns[i], &r[i]);
    MapWindowPoints(nullptr, box, (POINT *)&r[i], 2);
  }
  SelectObject(dc, old);
  ReleaseDC(box, dc);
  // The stock buttons fit "Cancel" and little more, so a longer label clips.
  // Widen every button to the widest need, keep them right-aligned with their
  // gap, and grow the box if they'd run past its left margin.
  if (!n)
    return;
  const RECT &first = btns[0] ? r[0] : r[1];
  const RECT &last = btns[1] ? r[1] : r[0];
  if (need <= first.right - first.left)
    return;
  int gap = n == 2 ? r[1].left - r[0].right : 0;
  int left = last.right - (n * need + (n - 1) * gap);
  RECT cr;
  GetClientRect(box, &cr);
  int minLeft = cr.right - last.right; // the right margin, mirrored
  if (left < minLeft) {
    int grow = minLeft - left;
    RECT wr;
    GetWindowRect(box, &wr);
    SetPropW(box, MSGBOX_WIDTH, (HANDLE)(INT_PTR)cr.right);
    SetPropW(box, MSGBOX_PROC, (HANDLE)GetWindowLongPtrW(box, GWLP_WNDPROC));
    SetWindowLongPtrW(box, GWLP_WNDPROC, (LONG_PTR)widened_msgbox_proc);
    SetWindowPos(box, nullptr, wr.left - grow / 2, wr.top,
                 wr.right - wr.left + grow, wr.bottom - wr.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    left += grow;
  }
  for (int i = 0; i < 2; i++) {
    if (!btns[i])
      continue;
    SetWindowPos(btns[i], nullptr, left, r[i].top, need, r[i].bottom - r[i].top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    left += need + gap;
  }
}

static LRESULT CALLBACK msgbox_cbt(int code, WPARAM wp, LPARAM lp) {
  MsgBoxLabels *l = g_msgbox_labels;
  if (code == HCBT_ACTIVATE && l && !l->done) {
    wchar_t cls[16];
    if (GetClassNameW((HWND)wp, cls, 16) && wcscmp(cls, L"#32770") == 0) {
      l->done = true;
      relabel_msgbox((HWND)wp, *l);
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

static int labelled_message_box(HWND owner, const std::wstring &text,
                                const std::wstring &title, UINT flags,
                                const std::string &okLabel,
                                const std::string &cancelLabel) {
  if (okLabel.empty() && cancelLabel.empty())
    return MessageBoxW(owner, text.c_str(), title.c_str(), flags);
  MsgBoxLabels l;
  l.ok = button_label(okLabel, L"");
  l.cancel = button_label(cancelLabel, L"");
  MsgBoxLabels *prev = g_msgbox_labels;
  g_msgbox_labels = &l;
  HHOOK hook =
      SetWindowsHookExW(WH_CBT, msgbox_cbt, nullptr, GetCurrentThreadId());
  int r = MessageBoxW(owner, text.c_str(), title.c_str(), flags);
  if (hook)
    UnhookWindowsHookEx(hook);
  g_msgbox_labels = prev;
  return r;
}

static std::string run_file_dialog(const std::string &op,
                                   const std::string &types,
                                   const std::string &name = "") {
  std::string json = "null";
  bool save = op == "save";
  IFileDialog *dlg = nullptr;
  HRESULT hr = CoCreateInstance(
      save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg)
    return json;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  if (op == "dir")
    opts |= FOS_PICKFOLDERS;
  if (op == "openmulti")
    opts |= FOS_ALLOWMULTISELECT;
  dlg->SetOptions(opts);
  // types: comma-separated extensions from the bridge (pre-normalized). One
  // combined filter plus an explicit "All files" — a lone filter would
  // hard-hide everything else, which openFile() without types never did.
  std::wstring pattern, first_ext;
  if (!types.empty() && op != "dir") {
    size_t start = 0;
    while (start <= types.size()) {
      size_t comma = types.find(',', start);
      std::string ext = types.substr(
          start,
          comma == std::string::npos ? std::string::npos : comma - start);
      if (!ext.empty()) {
        if (first_ext.empty())
          first_ext = widen(ext);
        if (!pattern.empty())
          pattern += L";";
        pattern += L"*." + widen(ext);
      }
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
    if (!pattern.empty()) {
      COMDLG_FILTERSPEC specs[2] = {{pattern.c_str(), pattern.c_str()},
                                    {L"All files", L"*.*"}};
      dlg->SetFileTypes(2, specs);
      dlg->SetFileTypeIndex(1);
      if (save)
        dlg->SetDefaultExtension(first_ext.c_str());
    }
  }
  if (save && !name.empty())
    dlg->SetFileName(widen(name).c_str()); // suggested name (#36)
  hr = dlg->Show(g_host);
  if (SUCCEEDED(hr)) {
    if (op == "openmulti") {
      IFileOpenDialog *odlg = nullptr;
      if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&odlg))) && odlg) {
        IShellItemArray *items = nullptr;
        if (SUCCEEDED(odlg->GetResults(&items)) && items) {
          DWORD count = 0;
          items->GetCount(&count);
          json = "[";
          for (DWORD i = 0; i < count; i++) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
              PWSTR path = nullptr;
              if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                if (json.size() > 1)
                  json += ",";
                json += json_escape(narrow(path));
                CoTaskMemFree(path);
              }
              item->Release();
            }
          }
          json += "]";
          items->Release();
        }
        odlg->Release();
      }
    } else {
      IShellItem *item = nullptr;
      if (SUCCEEDED(dlg->GetResult(&item)) && item) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          json = json_escape(narrow(path));
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
  }
  dlg->Release();
  return json;
}

static void do_dialog(webview_t w, void *arg) {
  DlgReq *req = static_cast<DlgReq *>(arg);
  auto a = [&](size_t i) {
    return i < req->args.size() ? req->args[i] : std::string();
  };
  std::string json = "null";
  if (req->op == "alert" || req->op == "confirm") {
    std::string text = a(0).empty() ? g_app_name : a(0);
    if (!a(1).empty())
      text += "\n\n" + a(1);
    UINT flags = req->op == "confirm" ? (MB_OKCANCEL | MB_ICONQUESTION)
                                      : (MB_OK | MB_ICONINFORMATION);
    // args: message, detail, okLabel, cancelLabel ("" = Windows' own)
    int r = labelled_message_box(g_host, widen(text), widen(g_app_name), flags,
                                 a(2), req->op == "confirm" ? a(3) : "");
    json = req->op == "alert" ? "true" : (r == IDOK ? "true" : "false");
  } else if (req->op == "prompt") {
    // args: message, default, okLabel, cancelLabel
    json = run_prompt(a(0).empty() ? g_app_name : a(0), a(1), a(2), a(3));
  } else {
    // args (open/openmulti/save): typeFilter (comma-separated extensions),
    // then for save an optional suggested file name
    json = run_file_dialog(req->op, a(0), a(1));
  }
  route_ret(w, req->id, 0, json);
  delete req;
}

// ---------------------------------------------------------------------------
// window ops

static void set_style_bits(HWND hwnd, LONG bits, bool on) {
  LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  style = on ? (style | bits) : (style & ~bits);
  SetWindowLongW(hwnd, GWL_STYLE, style);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

// Same, but the PAGE keeps its size instead of the window frame.
// SWP_NOSIZE above holds the outer rect, so dropping WS_CAPTION donates the
// title bar's space to the client: a window sized 320x172 while it still had a
// caption (the startup order — size first, then apply chrome.frame=false) came
// out with a 333x208 page, +13 wide and +36 tall, exactly the overlapped-window
// frame. Every frameless app was oversized by its own title bar.
static void set_style_bits_keep_client(HWND hwnd, LONG bits, bool on) {
  RECT before;
  GetClientRect(hwnd, &before);
  set_style_bits(hwnd, bits, on);
  RECT after;
  GetClientRect(hwnd, &after);
  if (after.right == before.right && after.bottom == before.bottom)
    return;
  RECT o;
  GetWindowRect(hwnd, &o);
  SetWindowPos(hwnd, nullptr, 0, 0,
               (o.right - o.left) + (before.right - after.right),
               (o.bottom - o.top) + (before.bottom - after.bottom),
               SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

// Taskbar button on/off for main (accessory activation, app.presence) without
// touching its chrome. This used to be WS_EX_TOOLWINDOW, which also buys the
// tool-window look: small caption, no icon, no min/max, a thin close (#56).
// The shell's other documented rule is that an OWNED top-level window gets no
// button, so main is handed a never-shown owner instead. It stays in Alt+Tab
// while shown (Alt+Tab lists an invisible owner's last visible popup; only
// WS_EX_TOOLWINDOW keeps a window out) — seen and accepted on #56.
// Main is created by the webview library, so the owner can only be retrofitted
// through GWLP_HWNDPARENT; the "half-works" caveat on win.open's parent is
// about z-order against a VISIBLE owner, which this one never is.
static HWND g_taskbar_owner = nullptr;
static bool g_no_taskbar = false; // main currently has no taskbar button

static void set_taskbar_button(bool show) {
  if (!show && !g_taskbar_owner) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TinyjsTaskbarOwner";
    RegisterClassW(&wc);
    g_taskbar_owner =
        CreateWindowExW(0, L"TinyjsTaskbarOwner", L"", WS_OVERLAPPED, 0, 0, 0,
                        0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_taskbar_owner)
      return;
  }
  g_no_taskbar = !show;
  // The shell only re-reads a window's button when it is shown, so flip the
  // owner while hidden.
  bool visible = IsWindowVisible(g_hwnd);
  if (visible)
    ShowWindow(g_hwnd, SW_HIDE);
  SetWindowLongPtrW(g_hwnd, GWLP_HWNDPARENT,
                    show ? 0 : (LONG_PTR)g_taskbar_owner);
  if (visible)
    ShowWindow(g_hwnd, SW_SHOWNA);
}

static void set_click_through(bool on) {
  LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
  if (on) {
    ex |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
  } else {
    ex &= ~WS_EX_TRANSPARENT;
  }
  SetWindowLongW(g_hwnd, GWL_EXSTYLE, ex);
  if (on)
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
  g_click_through = on;
}

static void set_fullscreen(bool on) {
  if (on == g_fullscreen)
    return;
  if (on) {
    g_fs_style = GetWindowLongW(g_hwnd, GWL_STYLE);
    g_fs_exstyle = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    GetWindowPlacement(g_hwnd, &g_fs_placement);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongW(g_hwnd, GWL_STYLE,
                   (g_fs_style & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP);
    SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  } else {
    SetWindowLongW(g_hwnd, GWL_STYLE, g_fs_style);
    SetWindowLongW(g_hwnd, GWL_EXSTYLE, g_fs_exstyle);
    SetWindowPlacement(g_hwnd, &g_fs_placement);
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  }
  g_fullscreen = on;
  emit_winstate(g_hwnd);
}

static void do_center(HWND hwnd) {
  RECT r;
  GetWindowRect(hwnd, &r);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
  int w = r.right - r.left, h = r.bottom - r.top;
  int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
  int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static ICoreWebView2Controller *ctrl_for_win(const std::string &id);
static void set_min_size(const std::string &id, int w, int h);

// A window nobody can see or grab is a lost window: apps restore saved
// positions blindly, and coordinates from an unplugged external display land
// the window in empty space. Rule: if less than a 24px-square sliver of the
// window overlaps any monitor's work area, pull it onto the nearest monitor,
// title bar first. Windows deliberately parked half-off-screen keep more
// than a sliver and are never touched.
//
// WHEN it runs is the bridge's call, not ours — some apps (coo3d) fling
// windows off-screen on purpose, so nothing here fires on ordinary pos/show.
// Triggers: the `onscreen` op (sent by the bridge on a boot whose screen
// fingerprint changed, and by win.ensureOnScreen()), and WM_DISPLAYCHANGE
// while running, gated by `rescue 0|1` ("offscreenRescue" in tinyjs.json,
// default on).
static bool g_rescue_on = true;
static void rescue_offscreen(HWND hwnd) {
  if (!hwnd || IsZoomed(hwnd) || IsIconic(hwnd))
    return;
  RECT r;
  if (!GetWindowRect(hwnd, &r))
    return;
  HMONITOR on = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
  if (on) {
    MONITORINFO mi = {sizeof(mi)};
    RECT ix;
    if (GetMonitorInfoW(on, &mi) && IntersectRect(&ix, &r, &mi.rcWork) &&
        ix.right - ix.left >= 24 && ix.bottom - ix.top >= 24)
      return; // reachable — leave it exactly where the app put it
  }
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
    return;
  const RECT &v = mi.rcWork;
  int w = r.right - r.left, h = r.bottom - r.top;
  int x = r.left > v.right - w ? v.right - w : r.left;
  if (x < v.left)
    x = v.left; // wider than the monitor pins the LEFT edge
  int y = r.top > v.bottom - h ? v.bottom - h : r.top;
  if (y < v.top)
    y = v.top; // taller pins the TOP (title bar)
  SetWindowPos(hwnd, nullptr, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// --- screens: one global logical space (#70) ---------------------------------
// Windows positions everything in PHYSICAL pixels; the wire speaks logical
// units (DIPs), like macOS points. Until 0.59 every API divided by whichever
// scale was handy — screens() by the monitor's own, setPosition by the
// window's current one, getState by the window's, mouse by the cursor's — so
// on mixed-DPI setups no two agreed (#70: a 1.25 laptop right of a 1.0
// 1920-wide monitor reported x=1536, a point INSIDE the left monitor).
//
// Now there is one layout, built the way Chromium builds its own (ScreenWin,
// docs/ui/display/display_conversion.md): the primary at its physical origin,
// then breadth-first, each monitor placed against a neighbour it shares an
// edge with — flush to that edge, offset along it by the neighbour's scale,
// sized by its own. Adjacency survives (the laptop above sits at x=1920).
// Limits, as Chromium's: a scaled-down monitor with several neighbours can't
// always make room for all of them, so DIP rects may overlap — a DIP point
// then maps through the FIRST monitor holding it; monitors touching nothing
// (Windows normally forbids that) keep physical origin / their own scale.
// Every screen-shaped answer and every position op goes through here.
struct ScreenLayout {
  HMONITOR mon;
  RECT px, work;     // physical
  double scale;
  long dx, dy, dw, dh; // the monitor's rect in the global DIP space
  bool primary;
  std::wstring device; // \\.\DISPLAYn
};

static std::vector<ScreenLayout> screen_layout() {
  std::vector<HMONITOR> mons;
  // Same order as captureScreen / recording's display index — it IS the id.
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        ((std::vector<HMONITOR> *)lp)->push_back(m);
        return TRUE;
      },
      (LPARAM)&mons);
  std::vector<ScreenLayout> L;
  for (HMONITOR m : mons) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    ScreenLayout s = {};
    s.mon = m;
    s.scale = monitor_scale(m);
    if (GetMonitorInfoW(m, (MONITORINFO *)&mi)) {
      s.px = mi.rcMonitor;
      s.work = mi.rcWork;
      s.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
      s.device = mi.szDevice;
    }
    s.dw = lround((s.px.right - s.px.left) / s.scale);
    s.dh = lround((s.px.bottom - s.px.top) / s.scale);
    L.push_back(s);
  }
  if (L.empty())
    return L;
  std::vector<bool> placed(L.size(), false);
  size_t root = 0;
  for (size_t i = 0; i < L.size(); i++)
    if (L[i].primary)
      root = i;
  L[root].dx = lround(L[root].px.left / L[root].scale); // primary: 0,0
  L[root].dy = lround(L[root].px.top / L[root].scale);
  placed[root] = true;
  std::vector<size_t> queue = {root};
  for (size_t qi = 0; qi < queue.size(); qi++) {
    const ScreenLayout &P = L[queue[qi]];
    for (size_t c = 0; c < L.size(); c++) {
      if (placed[c])
        continue;
      ScreenLayout &C = L[c];
      bool vov = C.px.top < P.px.bottom && C.px.bottom > P.px.top;
      bool hov = C.px.left < P.px.right && C.px.right > P.px.left;
      long along_y = P.dy + lround((C.px.top - P.px.top) / P.scale);
      long along_x = P.dx + lround((C.px.left - P.px.left) / P.scale);
      if (C.px.left == P.px.right && vov) {
        C.dx = P.dx + P.dw;
        C.dy = along_y;
      } else if (C.px.right == P.px.left && vov) {
        C.dx = P.dx - C.dw;
        C.dy = along_y;
      } else if (C.px.top == P.px.bottom && hov) {
        C.dy = P.dy + P.dh;
        C.dx = along_x;
      } else if (C.px.bottom == P.px.top && hov) {
        C.dy = P.dy - C.dh;
        C.dx = along_x;
      } else {
        continue;
      }
      placed[c] = true;
      queue.push_back(c);
    }
  }
  for (size_t c = 0; c < L.size(); c++)
    if (!placed[c]) {
      L[c].dx = lround(L[c].px.left / L[c].scale);
      L[c].dy = lround(L[c].px.top / L[c].scale);
    }
  return L;
}

static int layout_index(const std::vector<ScreenLayout> &L, HMONITOR m) {
  for (size_t i = 0; i < L.size(); i++)
    if (L[i].mon == m)
      return (int)i;
  return L.empty() ? -1 : 0;
}

// physical point -> the monitor holding it (nearest, off-screen)
static const ScreenLayout *layout_at_px(const std::vector<ScreenLayout> &L,
                                        POINT p) {
  int i = layout_index(L, MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST));
  return i < 0 ? nullptr : &L[i];
}

static double dip_dist2(const ScreenLayout &s, double x, double y) {
  double cx = x < s.dx ? s.dx : x > s.dx + s.dw ? s.dx + s.dw : x;
  double cy = y < s.dy ? s.dy : y > s.dy + s.dh ? s.dy + s.dh : y;
  return (x - cx) * (x - cx) + (y - cy) * (y - cy);
}

// DIP point -> the first monitor whose DIP rect holds it, else the nearest
static const ScreenLayout *layout_at_dip(const std::vector<ScreenLayout> &L,
                                         double x, double y) {
  const ScreenLayout *best = nullptr;
  double bestD = 0;
  for (const ScreenLayout &s : L) {
    double d = dip_dist2(s, x, y);
    if (d == 0 && x < s.dx + s.dw && y < s.dy + s.dh)
      return &s;
    if (!best || d < bestD) {
      best = &s;
      bestD = d;
    }
  }
  return best;
}

// DIP rect -> the monitor holding most of it (falls back to its top-left).
// What a window position is measured against both ways, so set(get())
// round-trips even when the top-left pokes onto a neighbour.
static const ScreenLayout *layout_for_dip_rect(const std::vector<ScreenLayout> &L,
                                               double x, double y, double w,
                                               double h) {
  const ScreenLayout *best = nullptr;
  double bestA = 0;
  for (const ScreenLayout &s : L) {
    double iw = (std::min)(x + w, (double)s.dx + s.dw) - (std::max)(x, (double)s.dx);
    double ih = (std::min)(y + h, (double)s.dy + s.dh) - (std::max)(y, (double)s.dy);
    if (iw > 0 && ih > 0 && iw * ih > bestA) {
      bestA = iw * ih;
      best = &s;
    }
  }
  return best ? best : layout_at_dip(L, x, y);
}

static void px_to_dip(const ScreenLayout &s, LONG px, LONG py, double *x,
                      double *y) {
  *x = s.dx + (px - s.px.left) / s.scale;
  *y = s.dy + (py - s.px.top) / s.scale;
}

static POINT dip_to_px(const ScreenLayout &s, double x, double y) {
  return {(LONG)lround(s.px.left + (x - s.dx) * s.scale),
          (LONG)lround(s.px.top + (y - s.dy) * s.scale)};
}

// A global DIP point (WINOPEN x/y, popup features) -> physical, plus the
// scale of the monitor it lands on (to size the new window in its units).
static POINT dip_point_to_px(double x, double y, double *scale_out) {
  std::vector<ScreenLayout> L = screen_layout();
  const ScreenLayout *s = layout_at_dip(L, x, y);
  if (!s) {
    if (scale_out)
      *scale_out = 1.0;
    return {(LONG)lround(x), (LONG)lround(y)};
  }
  if (scale_out)
    *scale_out = s->scale;
  return dip_to_px(*s, x, y);
}

// The monitor holding most of a window — what "the window is on" means.
static const ScreenLayout *layout_of_window(const std::vector<ScreenLayout> &L,
                                            HWND hwnd) {
  int i = layout_index(L, MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
  return i < 0 ? nullptr : &L[i];
}

// uid / friendly name / built-in-ness per \\.\DISPLAYn, from the display
// config — the identity GetMonitorInfo doesn't have. uid is the monitor
// device path (EDID-derived hardware id + the instance the port gets), which
// survives reboots; identical monitors still differ by instance.
struct MonIdent {
  std::string uid, name;
  bool internal = false;
};

static std::map<std::wstring, MonIdent> monitor_idents() {
  std::map<std::wstring, MonIdent> out;
  UINT32 np = 0, nm = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS)
    return out;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm,
                         modes.data(), nullptr) != ERROR_SUCCESS)
    return out;
  for (UINT32 i = 0; i < np; i++) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
    src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    src.header.size = sizeof(src);
    src.header.adapterId = paths[i].sourceInfo.adapterId;
    src.header.id = paths[i].sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS)
      continue;
    if (out.count(src.viewGdiDeviceName))
      continue; // cloned outputs: the first target names the source
    DISPLAYCONFIG_TARGET_DEVICE_NAME tgt = {};
    tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    tgt.header.size = sizeof(tgt);
    tgt.header.adapterId = paths[i].targetInfo.adapterId;
    tgt.header.id = paths[i].targetInfo.id;
    MonIdent mi;
    if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS) {
      mi.uid = narrow(tgt.monitorDevicePath);
      mi.name = narrow(tgt.monitorFriendlyDeviceName);
    }
    auto t = paths[i].targetInfo.outputTechnology;
    mi.internal = t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                  t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                  t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
    out[src.viewGdiDeviceName] = mi;
  }
  return out;
}

static std::string screens_json() {
  std::vector<ScreenLayout> L = screen_layout();
  std::map<std::wstring, MonIdent> ids = monitor_idents();
  std::string json = "[";
  for (size_t i = 0; i < L.size(); i++) {
    const ScreenLayout &s = L[i];
    MonIdent id;
    auto it = ids.find(s.device);
    if (it != ids.end())
      id = it->second;
    if (id.uid.empty())
      id.uid = narrow(s.device);
    if (id.name.empty())
      id.name = id.internal ? "Built-in display" : narrow(s.device);
    char buf[1024];
    std::snprintf(
        buf, sizeof(buf),
        "{\"id\":%zu,\"uid\":%s,\"name\":%s,"
        "\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
        "\"visible\":{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld},"
        "\"scale\":%.2f,\"primary\":%s,\"internal\":%s,"
        "\"pixels\":{\"width\":%ld,\"height\":%ld,"
        "\"visible\":{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld}}}",
        i, json_escape(id.uid).c_str(), json_escape(id.name).c_str(), s.dx,
        s.dy, s.dw, s.dh, s.dx + lround((s.work.left - s.px.left) / s.scale),
        s.dy + lround((s.work.top - s.px.top) / s.scale),
        lround((s.work.right - s.work.left) / s.scale),
        lround((s.work.bottom - s.work.top) / s.scale), s.scale,
        s.primary ? "true" : "false", id.internal ? "true" : "false",
        (long)(s.px.right - s.px.left), (long)(s.px.bottom - s.px.top),
        (long)(s.work.left - s.px.left), (long)(s.work.top - s.px.top),
        (long)(s.work.right - s.work.left), (long)(s.work.bottom - s.work.top));
    if (i)
      json += ",";
    json += buf;
  }
  return json + "]";
}

// Put a window on the target monitor's DPI before placing it there. Windows
// rescales a window only when most of its PHYSICAL rect changes monitor, so a
// 150% window sent to straddle a 100% neighbour (mostly on the 100% side in
// DIPs) kept its 1.5x size, sat half-and-half physically, never rescaled, and
// read back through the 150% monitor 100 DIPs off (#70). A hop inside the
// target fires WM_DPICHANGED synchronously; the webview's handler resizes.
// The hop goes to the spot nearest `want` (the final physical top-left) that
// keeps the window inside the target, so a drag across (amp's lockstep group
// moves call this every frame) never jumps to the monitor's corner.
static void match_monitor_dpi(HWND hwnd, const ScreenLayout &s, POINT want) {
  if (std::fabs(window_scale(hwnd) - s.scale) < 0.01)
    return;
  RECT r;
  GetWindowRect(hwnd, &r);
  long w = r.right - r.left, h = r.bottom - r.top;
  long x = (std::max)(s.px.left, (std::min)(want.x, s.px.right - w));
  long y = (std::max)(s.px.top, (std::min)(want.y, s.px.bottom - h));
  SetWindowPos(hwnd, nullptr, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// WINOP pos: a global DIP top-left. The monitor is picked by where most of
// the window WOULD be (its current size in DIPs), the same rule getState
// reads it back by.
static void move_window_dip(HWND hwnd, double x, double y) {
  std::vector<ScreenLayout> L = screen_layout();
  RECT r;
  GetWindowRect(hwnd, &r);
  double ws = window_scale(hwnd);
  const ScreenLayout *s = layout_for_dip_rect(
      L, x, y, (r.right - r.left) / ws, (r.bottom - r.top) / ws);
  if (!s)
    return;
  POINT p = dip_to_px(*s, x, y);
  match_monitor_dpi(hwnd, *s, p);
  // SWP_NOACTIVATE: lockstep group-drags reposition N windows per frame —
  // without it every move steals activation and the drag crawls.
  SetWindowPos(hwnd, nullptr, p.x, p.y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// WINOP spos <idx> <x> <y> <pt|px>: relative to that monitor's top-left.
static void move_window_on_screen(HWND hwnd, int idx, double x, double y,
                                  bool px) {
  std::vector<ScreenLayout> L = screen_layout();
  if (idx < 0 || idx >= (int)L.size())
    return;
  const ScreenLayout &s = L[idx];
  double k = px ? 1.0 : s.scale;
  POINT p = {(LONG)lround(s.px.left + x * k), (LONG)lround(s.px.top + y * k)};
  match_monitor_dpi(hwnd, s, p);
  SetWindowPos(hwnd, nullptr, p.x, p.y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Centre on a monitor's work area (shrinking to fit) — the first half of
// fullscreen-on-a-screen; fullscreen then fills whichever monitor holds it.
static void center_on_screen(HWND hwnd, int idx) {
  std::vector<ScreenLayout> L = screen_layout();
  if (idx < 0 || idx >= (int)L.size())
    return;
  const RECT &v = L[idx].work;
  RECT r;
  GetWindowRect(hwnd, &r);
  int w = (std::min)(r.right - r.left, v.right - v.left);
  int h = (std::min)(r.bottom - r.top, v.bottom - v.top);
  SetWindowPos(hwnd, nullptr, v.left + ((v.right - v.left) - w) / 2,
               v.top + ((v.bottom - v.top) - h) / 2, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

// WINGEOM <win> after a move/resize ends — only while the bridge asked
// (rememberWindows). A user drag reports once, at WM_EXITSIZEMOVE.
static bool g_geom_events = false;
static void emit_geom(HWND hwnd);

struct WinopReq {
  std::string win, op;
};

static void windowless_app_op(const std::string &op); // below TinyWin

static void do_winop(webview_t, void *arg) {
  WinopReq *req = static_cast<WinopReq *>(arg);
  const std::string &op = req->op;
  if (op == "geom 1" || op == "geom 0") { // app-wide, windowless too
    g_geom_events = op.back() == '1';
    delete req;
    return;
  }
  HWND hwnd = hwnd_for_win(req->win);
  bool main = hwnd == g_hwnd;
  if (!hwnd) {
    if (g_windowless && (req->win.empty() || req->win == "main"))
      windowless_app_op(op);
    delete req;
    return;
  }
  auto starts = [&](const char *p) { return op.rfind(p, 0) == 0; };
  // "hidewin" is hide({ app: false }) — window-scoped, which is what a hide
  // has always been here (only macOS's main-window hide takes the app with it).
  if (op == "hide" || op == "hidewin") {
    ShowWindow(hwnd, SW_HIDE);
    emit_winstate(hwnd, 0); // hidden can stay thread-active: no WM_ACTIVATE
  } else if (starts("show")) {
    bool activate = op != "show 0";
    if (main)
      g_defer_show = false;
    // A window minimized while it had no taskbar button by some route we
    // don't intercept (Win+M) would come back still minimized — un-minimize.
    if (IsIconic(hwnd))
      ShowWindow(hwnd, activate ? SW_RESTORE : SW_SHOWNOACTIVATE);
    ShowWindow(hwnd, activate ? SW_SHOW : SW_SHOWNA);
    if (activate) {
      SetForegroundWindow(hwnd);
      // WM_ACTIVATE can't be trusted to report this: an accessory main is
      // hidden at startup while still its thread's active window, so the
      // first show re-activates nothing and sends none — focused stayed
      // false and the dedupe then swallowed the blur that a hide-on-blur
      // palette waits for. Report what the foreground actually is.
      emit_winstate(hwnd);
    }
  } else if (op == "center") {
    do_center(hwnd);
  } else if (op == "minimize") {
    // No taskbar button = nowhere to minimize TO (an owned window collapses
    // to a caption strip at the desktop's bottom-left); hide instead, as the
    // title-bar button does — see WM_SYSCOMMAND.
    ShowWindow(hwnd, main && g_no_taskbar ? SW_HIDE : SW_MINIMIZE);
  } else if (op == "restore") {
    ShowWindow(hwnd, SW_RESTORE);
  } else if (op == "zoom") {
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
  } else if (starts("minsize ")) {
    // win.setMinSize — a resize floor, enforced in WM_GETMINMAXINFO. Was
    // missing entirely, so a window could be dragged (or opened) smaller than
    // its content needs; amp's EQ lost its bottom selector that way.
    int w = 0, h = 0;
    if (std::sscanf(op.c_str() + 8, "%dx%d", &w, &h) == 2)
      set_min_size(req->win, w, h);
  } else if (starts("zoomfactor ")) {
    // win.setZoom — native page zoom (amp's 2x "double size"). The page keeps
    // laying out in CSS px and WebView2 scales the rendering, which is what
    // the API promises. Distinct from the "zoom" op above, which maximizes.
    // This was missing entirely: the op reached the else-if chain, matched
    // nothing and was dropped, so setZoom resolved true and did nothing on
    // Windows while working on macOS.
    double f = std::atof(op.c_str() + 11);
    ICoreWebView2Controller *c = ctrl_for_win(req->win);
    if (c && f > 0.0)
      c->put_ZoomFactor(f);
  } else if (op == "fullscreen" && main) {
    set_fullscreen(!g_fullscreen);
  } else if (starts("fullscreen ") && main) {
    set_fullscreen(op.substr(11) == "1");
  } else if (starts("fullscreen")) {
    // secondary windows: approximate with maximize
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
  } else if (starts("ontop ")) {
    SetWindowPos(hwnd, op.substr(6) == "1" ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  } else if (starts("resizable ")) {
    // Keep the PAGE's size: WS_THICKFRAME's border is wider than the thin
    // frame left without it, so holding the outer rect handed the difference
    // to the client — setResizable(false) grew the page +10x+10 on Win10 (#57).
    set_style_bits_keep_client(hwnd, WS_THICKFRAME | WS_MAXIMIZEBOX,
                               op.substr(10) == "1");
  } else if (starts("clickthrough ") && main) {
    set_click_through(op.substr(13) == "1");
  } else if (starts("level ")) {
    std::string level = op.substr(6);
    if (main)
      g_level = level;
    if (level == "floating" || level == "overlay") {
      SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else if (level == "desktop") {
      SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
      SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else {
      SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
      if (main)
        g_level = "normal";
    }
  } else if (starts("pos ")) {
    int x = 0, y = 0;
    if (std::sscanf(op.c_str() + 4, "%d %d", &x, &y) == 2)
      move_window_dip(hwnd, x, y); // global DIPs, see screen_layout
  } else if (starts("spos ")) {
    int idx = -1;
    double x = 0, y = 0;
    char units[4] = "pt";
    if (std::sscanf(op.c_str() + 5, "%d %lf %lf %3s", &idx, &x, &y, units) >= 3)
      move_window_on_screen(hwnd, idx, x, y, std::strcmp(units, "px") == 0);
  } else if (starts("pxsize ")) {
    // setSize(w, h, { units: 'px' }): the client size in physical pixels,
    // which is what Windows speaks anyway — no scale involved.
    int pw = 0, ph = 0;
    if (std::sscanf(op.c_str() + 7, "%d %d", &pw, &ph) == 2 && pw > 0 && ph > 0) {
      RECT o, c;
      GetWindowRect(hwnd, &o);
      GetClientRect(hwnd, &c);
      g_programmatic_size = true;
      SetWindowPos(hwnd, nullptr, 0, 0, pw + (o.right - o.left) - c.right,
                   ph + (o.bottom - o.top) - c.bottom, SWP_NOMOVE | SWP_NOZORDER);
      g_programmatic_size = false;
    }
  } else if (starts("fsscreen ")) {
    // fullscreen on a chosen monitor: leave fullscreen, move there, re-enter.
    // Secondaries "fullscreen" by maximizing (see above), so they maximize
    // on that monitor instead.
    int idx = std::atoi(op.c_str() + 9);
    if (main) {
      set_fullscreen(false);
      center_on_screen(hwnd, idx);
      set_fullscreen(true);
    } else {
      if (IsZoomed(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
      center_on_screen(hwnd, idx);
      ShowWindow(hwnd, SW_MAXIMIZE);
    }
  } else if (op == "onscreen") {
    rescue_offscreen(hwnd); // the bridge (or the app) decided this window
                            // must be reachable — see the note on the helper
  } else if (starts("rescue ")) {
    g_rescue_on = op.substr(7) == "1";
  } else if (starts("hideonclose ") && main) {
    g_hide_on_close = op.substr(12) == "1";
  } else if (starts("presence ") && main) {
    // app.presence maps to the taskbar button (menubar = none, the tray-only
    // app look) — see set_taskbar_button.
    set_taskbar_button(op.substr(9) == "1");
  }
  // allspaces: no Windows equivalent — ignored.
  delete req;
}

struct ChromeReq {
  std::string win, frame, traffic, transparent, vibrancy, square, first_mouse,
      menu;
};

static void set_win_transparent_flag(const std::string &id, bool on);

static void do_chrome(webview_t, void *arg) {
  ChromeReq *req = static_cast<ChromeReq *>(arg);
  HWND hwnd = hwnd_for_win(req->win);
  bool main = hwnd == g_hwnd;
  if (!hwnd) {
    delete req;
    return;
  }
  // Whether a bar can be drawn at all turns on frame/square/transparent, and
  // whether one is wanted turns on menu — so any of the four re-resolves it.
  bool remenu = !req->menu.empty();
  if (!req->frame.empty()) {
    bool frameless = req->frame == "0";
    if (main)
      g_frameless = frameless;
    set_style_bits_keep_client(hwnd, WS_CAPTION, !frameless);
    remenu = true;  // the caption going away takes the bar's strip with it
  }
  if (!req->menu.empty()) {
    if (WinMenu *wm = menu_for(hwnd))
      wm->visible = req->menu == "1";
  }
  if (!req->traffic.empty()) {
    // windowControls: '' keep · 'all' · 'none' · comma list of
    // close/minimize/maximize. UNTESTED — written on macOS.
    // Win32 is coarser than macOS here: the min/max boxes REQUIRE WS_SYSMENU,
    // and dropping WS_SYSMENU removes the close button with them. So
    // "minimize without close" isn't expressible; asking for it gets close
    // too, which is why capabilities() flags this as approximate.
    const std::string &c = req->traffic;
    const bool all = c == "all", none = c == "none";
    auto want = [&](const char *nm) {
      return all ? true : none ? false : c.find(nm) != std::string::npos;
    };
    const bool close_on = want("close"), min_on = want("minimize"),
               max_on = want("maximize");
    const bool any = close_on || min_on || max_on;
    set_style_bits_keep_client(hwnd, WS_SYSMENU, any);
    set_style_bits_keep_client(hwnd, WS_MINIMIZEBOX, any && min_on);
    set_style_bits_keep_client(hwnd, WS_MAXIMIZEBOX, any && max_on);
  }
  if (!req->square.empty()) {
    remenu = true;  // borderless: same story as frame
    bool square = req->square == "1";
    if (main)
      g_square = square;
    if (square) {
      // Borderless like macOS: square corners, no titlebar; resize edges kept.
      if (main)
        g_frameless = true;
      // keep_client here too: g_square/g_frameless flip what WM_NCCALCSIZE
      // hands back, so even when WS_CAPTION is already off the FRAMECHANGED
      // re-calc donates the border inset to the page (main came out +12 wide
      // and +6 tall — exactly the left/right and bottom resize borders).
      set_style_bits_keep_client(hwnd, WS_CAPTION, false);
    }
    DWORD pref = square ? 1 /* DWMWCP_DONOTROUND */ : 0 /* DEFAULT */;
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &pref, sizeof(pref));
  }
  if (!req->transparent.empty()) {
    // Page background becomes see-through where the page itself is
    // transparent (pair with a DWM backdrop or a frameless window).
    // Secondary controllers are created async: a setChrome right after
    // win.open can land before the controller exists, so record the wish on
    // the TinyWin — SecCtrlHandler applies tw->transparent on creation.
    if (main)
      g_main_transparent = req->transparent == "1";
    else
      set_win_transparent_flag(req->win, req->transparent == "1");
    remenu = true;  // a cleared background has nothing to draw a bar on
    ICoreWebView2Controller *ctrl = ctrl_for_win(req->win);
    ICoreWebView2Controller2 *c2 = nullptr;
    HRESULT setbg = E_FAIL;
    if (ctrl &&
        SUCCEEDED(ctrl->QueryInterface(IID_ICoreWebView2Controller2,
                                       (void **)&c2)) &&
        c2) {
      COREWEBVIEW2_COLOR clear = {0, 0, 0, 0}, opaque = {255, 255, 255, 255};
      setbg =
          c2->put_DefaultBackgroundColor(req->transparent == "1" ? clear
                                                                 : opaque);
      c2->Release();
    }
    drag_dbg("chrome '" + req->win + "' transparent=" + req->transparent +
             " ctrl=" + (ctrl ? "1" : "0") +
             " setbg=" + std::to_string((long)setbg));
  }
  if (!req->vibrancy.empty()) {
    // macOS vibrancy materials map to Windows 11 system backdrops: 'none'
    // resets; 'hud'/'popover'/'menu' get acrylic, the rest mica. Silently a
    // no-op before Win11 22H2.
    DWORD backdrop = req->vibrancy == "none" ? 1 /* DWMSBT_NONE */
                     : (req->vibrancy == "hud" || req->vibrancy == "popover" ||
                        req->vibrancy == "menu")
                         ? 3 /* DWMSBT_TRANSIENTWINDOW (acrylic) */
                         : 2 /* DWMSBT_MAINWINDOW (mica) */;
    DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */,
                          &backdrop, sizeof(backdrop));
  }
  if (remenu)
    render_menu(hwnd);
  delete req;
}

// The WebView2 child (a separate process) holds the mouse capture from the
// page's mousedown; a plain ReleaseCapture() only affects OUR thread's queue,
// so the window-move loop / OLE drag never receives the mouse. Attaching our
// input queue to the capture-owning thread makes ReleaseCapture reach it.
static void release_webview_capture() {
  POINT pt;
  GetCursorPos(&pt);
  HWND under = WindowFromPoint(pt);
  DWORD other = under ? GetWindowThreadProcessId(under, nullptr) : 0;
  DWORD cur = GetCurrentThreadId();
  if (other && other != cur) {
    AttachThreadInput(cur, other, TRUE);
    ReleaseCapture();
    AttachThreadInput(cur, other, FALSE);
  } else {
    ReleaseCapture();
  }
}

// win.startDrag / win.startResize — hand the gesture to the OS by faking a
// non-client button press. Both must honour @win: a bare op means 'main', so a
// satellite's grip used to move the deck instead (DRAGWIN matched exactly, so
// DRAGWIN@id was dropped outright, and RESIZEWIN was never handled at all —
// every frameless window gets auto grips from tiny.js that call it).
struct NcDragReq {
  std::string win;
  int ht;
};

static int ht_for_edge(const std::string &e) {
  if (e == "n") return HTTOP;
  if (e == "s") return HTBOTTOM;
  if (e == "e") return HTRIGHT;
  if (e == "w") return HTLEFT;
  if (e == "ne") return HTTOPRIGHT;
  if (e == "nw") return HTTOPLEFT;
  if (e == "sw") return HTBOTTOMLEFT;
  return HTBOTTOMRIGHT; // 'se', and anything unrecognised
}

static void do_ncdrag(webview_t, void *arg) {
  NcDragReq *r = static_cast<NcDragReq *>(arg);
  HWND h = hwnd_for_win(r->win);
  // DefWindowProc turns this into SC_MOVE/SC_SIZE and runs a MODAL loop that
  // ends on the next button transition — so entered with the button already
  // UP, the window just sticks to the cursor until the user clicks again.
  // (macOS demands a held button for the same reason, launcher-macos.cc; the
  // comment there claiming Windows "returns instead of hanging" was wrong.)
  // The trip here is two process hops — page -> WebView2 IPC -> launcher ->
  // pipe -> backend -> pipe -> launcher — so a *click* near an edge, rather
  // than a drag, easily releases inside that window.
  bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  // setResizable(false) means the USER can't resize; the app's own setSize
  // still can. A page-drawn grip is a user resize, so honour the lock — and
  // read it live, because tiny.js gates its grips on one getState at page
  // load and the window may have been locked since.
  bool locked = r->ht != HTCAPTION && h &&
                !(GetWindowLongW(h, GWL_STYLE) & WS_THICKFRAME);
  // That modal loop pumps and dispatches our posted WM_APP work, so a second
  // RESIZEWIN arriving mid-drag would nest a modal loop inside this one.
  static bool in_ncdrag = false;
  if (h && held && !locked && !in_ncdrag) {
    // Screen coords: GetCursorPos and WM_NCLBUTTONDOWN are both physical px
    // and the process is per-monitor-DPI-aware, so nothing is converted here
    // (unlike every op that carries a logical wire coordinate). DefWindowProc
    // anchors from the global cursor anyway, but macOS and GTK both pass the
    // live point, and MAKELPARAM round-trips a monitor left of primary
    // (negative x) because POINTS sign-extends through short.
    POINT pt;
    GetCursorPos(&pt);
    release_webview_capture();
    in_ncdrag = true;
    SendMessageW(h, WM_NCLBUTTONDOWN, r->ht, MAKELPARAM(pt.x, pt.y));
    in_ncdrag = false;
  }
  delete r;
}

// ---------------------------------------------------------------------------
// clipboard

// tinyjs's coordinate contract is LOGICAL (CSS-pixel / macOS-point) units:
// the process is per-monitor-DPI-aware, so every Win32 coordinate is
// physical and must be converted at the wire boundary — otherwise windows
// open at half size and app-driven drags run at half speed on scaled
// displays.
static double window_scale(HWND hwnd) {
  typedef UINT(WINAPI * GetDpiForWindowT)(HWND);
  static GetDpiForWindowT fn = (GetDpiForWindowT)GetProcAddress(
      GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  UINT dpi = fn ? fn(hwnd ? hwnd : g_host) : 0;
  return dpi ? dpi / 96.0 : 1.0;
}

static double monitor_scale(HMONITOR mon) {
  typedef HRESULT(WINAPI * GetDpiForMonitorT)(HMONITOR, int, UINT *, UINT *);
  static GetDpiForMonitorT fn = []() -> GetDpiForMonitorT {
    HMODULE m = LoadLibraryW(L"shcore.dll");
    return m ? (GetDpiForMonitorT)GetProcAddress(m, "GetDpiForMonitor")
             : nullptr;
  }();
  if (fn) {
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(fn(mon, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy)))
      return dx / 96.0;
  }
  return 1.0;
}

static std::string clipboard_json(bool count_only) {
  DWORD seq = GetClipboardSequenceNumber();
  if (count_only)
    return "{\"changeCount\":" + std::to_string(seq) + "}";
  if (!OpenClipboard(g_host))
    return "{\"kind\":\"empty\",\"changeCount\":" + std::to_string(seq) + "}";
  // paths is [] when there are no files, as on macOS/Linux and in tiny.d.ts
  std::string kind = "empty", text = "null", html = "null", paths = "[]",
              image = "null", imageSize = "null";
  // files
  if (IsClipboardFormatAvailable(CF_HDROP)) {
    HANDLE h = GetClipboardData(CF_HDROP);
    if (h) {
      HDROP drop = (HDROP)h;
      UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      paths = "[";
      for (UINT i = 0; i < n; i++) {
        wchar_t buf[MAX_PATH];
        DragQueryFileW(drop, i, buf, MAX_PATH);
        if (i)
          paths += ",";
        paths += json_escape(narrow(buf));
      }
      paths += "]";
      kind = "files";
    }
  }
  // image -> temp png
  if (kind == "empty" && IsClipboardFormatAvailable(CF_BITMAP) &&
      ensure_gdiplus() && png_encoder_clsid()) {
    HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (hbm) {
      Gdiplus::Bitmap *bmp = Gdiplus::Bitmap::FromHBITMAP(hbm, nullptr);
      if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        // One file per clipboard CHANGE, not per read. Every read used to
        // write a fresh temp png (plus GetTempFileName's empty placeholder)
        // and nothing removed them. Now the path is named by the change
        // count, reused while the clipboard is unchanged, and the previous
        // one deleted when it changes — valid until the clipboard changes,
        // as on macOS.
        static std::wstring last_png;
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring png = std::wstring(tmp) + L"tinyjs-clip-" +
                           std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(seq) + L".png";
        bool have = png == last_png &&
                    GetFileAttributesW(png.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!have && !last_png.empty() && last_png != png)
          DeleteFileW(last_png.c_str());
        if (have || bmp->Save(png.c_str(), &g_png_clsid, nullptr) == Gdiplus::Ok) {
          last_png = png;
          image = json_escape(narrow(png));
          imageSize = "{\"width\":" + std::to_string(bmp->GetWidth()) +
                      ",\"height\":" + std::to_string(bmp->GetHeight()) + "}";
          kind = "image";
        }
      }
      delete bmp;
    }
  }
  // text
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
      wchar_t *p = (wchar_t *)GlobalLock(h);
      if (p) {
        text = json_escape(narrow(p));
        GlobalUnlock(h);
        if (kind == "empty")
          kind = "text";
      }
    }
  }
  // html fragment
  UINT cf_html = RegisterClipboardFormatW(L"HTML Format");
  if (IsClipboardFormatAvailable(cf_html)) {
    HANDLE h = GetClipboardData(cf_html);
    if (h) {
      char *p = (char *)GlobalLock(h);
      if (p) {
        std::string raw(p);
        GlobalUnlock(h);
        size_t s = raw.find("StartFragment:");
        size_t e = raw.find("EndFragment:");
        if (s != std::string::npos && e != std::string::npos) {
          long so = atol(raw.c_str() + s + 14), eo = atol(raw.c_str() + e + 12);
          if (so >= 0 && eo > so && (size_t)eo <= raw.size())
            html = json_escape(raw.substr(so, eo - so));
        }
      }
    }
  }
  CloseClipboard();
  return "{\"kind\":\"" + kind + "\",\"changeCount\":" + std::to_string(seq) +
         ",\"text\":" + text + ",\"html\":" + html + ",\"paths\":" + paths +
         ",\"image\":" + image + ",\"imageSize\":" + imageSize +
         ",\"color\":null,\"concealed\":false,\"sourceApp\":null,"
         "\"sourceURL\":null}";
}

struct ClipWriteReq {
  std::string text, html, image, color;
  std::vector<std::string> paths;
};

static HGLOBAL global_from(const void *data, size_t size) {
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!h)
    return nullptr;
  void *p = GlobalLock(h);
  memcpy(p, data, size);
  GlobalUnlock(h);
  return h;
}

// Decode an image field (png path, base64, or data: URL) into a GDI+ bitmap.
static Gdiplus::Bitmap *decode_image_field(const std::string &image) {
  if (!ensure_gdiplus())
    return nullptr;
  std::string b64 = image;
  if (b64.rfind("data:", 0) == 0) {
    size_t comma = b64.find(',');
    b64 = comma == std::string::npos ? "" : b64.substr(comma + 1);
  }
  if (GetFileAttributesW(widen(image).c_str()) != INVALID_FILE_ATTRIBUTES) {
    Gdiplus::Bitmap *bmp = Gdiplus::Bitmap::FromFile(widen(image).c_str());
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok)
      return bmp;
    delete bmp;
    return nullptr;
  }
  // base64 -> IStream -> bitmap
  DWORD len = 0;
  if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr,
                            &len, nullptr, nullptr) || !len)
    return nullptr;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
  if (!mem)
    return nullptr;
  BYTE *dst = (BYTE *)GlobalLock(mem);
  CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, dst, &len, nullptr,
                       nullptr);
  GlobalUnlock(mem);
  IStream *stream = nullptr;
  if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
    GlobalFree(mem);
    return nullptr;
  }
  Gdiplus::Bitmap *bmp = Gdiplus::Bitmap::FromStream(stream);
  stream->Release();
  if (bmp && bmp->GetLastStatus() == Gdiplus::Ok)
    return bmp;
  delete bmp;
  return nullptr;
}

// Pack a GDI+ bitmap as CF_DIB (BITMAPINFOHEADER + 32bpp bits) for the
// clipboard.
static HGLOBAL bitmap_to_dib(Gdiplus::Bitmap *bmp) {
  UINT w = bmp->GetWidth(), h = bmp->GetHeight();
  Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
  Gdiplus::BitmapData bd;
  if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
                    &bd) != Gdiplus::Ok)
    return nullptr;
  size_t stride = (size_t)w * 4;
  size_t bits = stride * h;
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bits);
  if (mem) {
    BYTE *p = (BYTE *)GlobalLock(mem);
    BITMAPINFOHEADER hdr = {};
    hdr.biSize = sizeof(hdr);
    hdr.biWidth = (LONG)w;
    hdr.biHeight = (LONG)h; // bottom-up
    hdr.biPlanes = 1;
    hdr.biBitCount = 32;
    hdr.biCompression = BI_RGB;
    memcpy(p, &hdr, sizeof(hdr));
    BYTE *out = p + sizeof(hdr);
    for (UINT y = 0; y < h; y++) // flip rows: GDI+ is top-down
      memcpy(out + (size_t)(h - 1 - y) * stride,
             (BYTE *)bd.Scan0 + (size_t)y * bd.Stride, stride);
    GlobalUnlock(mem);
  }
  bmp->UnlockBits(&bd);
  return mem;
}

static void do_clip_write(webview_t, void *arg) {
  ClipWriteReq *req = static_cast<ClipWriteReq *>(arg);
  if (!OpenClipboard(g_host)) {
    delete req;
    return;
  }
  EmptyClipboard();
  if (!req->image.empty()) {
    Gdiplus::Bitmap *bmp = decode_image_field(req->image);
    if (bmp) {
      HGLOBAL dib = bitmap_to_dib(bmp);
      if (dib)
        SetClipboardData(CF_DIB, dib);
      delete bmp;
    }
  }
  // color has no native Windows clipboard format; expose it as text so
  // paste targets still get the value.
  if (!req->color.empty() && req->text.empty())
    req->text = req->color;
  if (!req->text.empty()) {
    std::wstring w = widen(req->text);
    SetClipboardData(CF_UNICODETEXT,
                     global_from(w.c_str(), (w.size() + 1) * sizeof(wchar_t)));
  }
  if (!req->html.empty()) {
    // CF_HTML needs its offset header.
    const char *tpl = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\n"
                      "StartFragment:%010d\r\nEndFragment:%010d\r\n";
    std::string pre = "<html><body><!--StartFragment-->";
    std::string post = "<!--EndFragment--></body></html>";
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr), tpl, 0, 0, 0, 0);
    int hlen = (int)strlen(hdr);
    int start_html = hlen;
    int start_frag = hlen + (int)pre.size();
    int end_frag = start_frag + (int)req->html.size();
    int end_html = end_frag + (int)post.size();
    std::snprintf(hdr, sizeof(hdr), tpl, start_html, end_html, start_frag,
                  end_frag);
    std::string all = std::string(hdr) + pre + req->html + post;
    SetClipboardData(RegisterClipboardFormatW(L"HTML Format"),
                     global_from(all.c_str(), all.size() + 1));
  }
  if (!req->paths.empty()) {
    std::wstring list;
    for (const auto &p : req->paths) {
      list += widen(p);
      list.push_back(0);
    }
    list.push_back(0);
    size_t bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
      DROPFILES *df = (DROPFILES *)GlobalLock(h);
      memset(df, 0, sizeof(DROPFILES));
      df->pFiles = sizeof(DROPFILES);
      df->fWide = TRUE;
      memcpy((char *)df + sizeof(DROPFILES), list.data(),
             list.size() * sizeof(wchar_t));
      GlobalUnlock(h);
      SetClipboardData(CF_HDROP, h);
    }
  }
  CloseClipboard();
  g_clip_self_seq = GetClipboardSequenceNumber();
  delete req;
}

static void do_clip_watch(webview_t, void *arg) {
  int *ms = static_cast<int *>(arg);
  if (*ms > 0) {
    g_clip_last_seq = GetClipboardSequenceNumber();
    SetTimer(g_host, TIMER_CLIPWATCH, (UINT)*ms, nullptr);
  } else {
    KillTimer(g_host, TIMER_CLIPWATCH);
  }
  delete ms;
}

// ---------------------------------------------------------------------------
// keystrokes / hotkeys

static bool parse_key_token(const std::string &tok, WORD &vk) {
  std::string k = tok;
  for (auto &c : k)
    c = (char)tolower((unsigned char)c);
  if (k.size() == 1) {
    char c = k[0];
    if (c >= 'a' && c <= 'z') { vk = (WORD)(c - 'a' + 'A'); return true; }
    if (c >= '0' && c <= '9') { vk = (WORD)c; return true; }
    SHORT r = VkKeyScanW((wchar_t)c);
    if (r != -1) { vk = (WORD)(r & 0xFF); return true; }
    return false;
  }
  if (k[0] == 'f' && k.size() <= 3) {
    int n = atoi(k.c_str() + 1);
    if (n >= 1 && n <= 24) { vk = (WORD)(VK_F1 + n - 1); return true; }
  }
  static const std::map<std::string, WORD> named = {
      {"enter", VK_RETURN},  {"return", VK_RETURN}, {"tab", VK_TAB},
      {"space", VK_SPACE},   {"esc", VK_ESCAPE},    {"escape", VK_ESCAPE},
      // "delete" reaches here as "backspace" (combo.h canonical_key), as on
      // macOS and Linux; it used to be the forward Delete key here only.
      {"backspace", VK_BACK}, {"forwarddelete", VK_DELETE}, {"up", VK_UP},
      {"down", VK_DOWN},     {"left", VK_LEFT},     {"right", VK_RIGHT},
      {"home", VK_HOME},     {"end", VK_END},       {"pageup", VK_PRIOR},
      {"pagedown", VK_NEXT}, {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD},
      // punctuation by name, as macOS and Linux take it (US-layout keys)
      {"minus", VK_OEM_MINUS}, {"equal", VK_OEM_PLUS}, {"slash", VK_OEM_2},
      {"semicolon", VK_OEM_1}, {"quote", VK_OEM_7}, {"backslash", VK_OEM_5},
      {"grave", VK_OEM_3}, {"bracketleft", VK_OEM_4}, {"bracketright", VK_OEM_6}};
  auto it = named.find(k);
  if (it == named.end())
    return false;
  vk = it->second;
  return true;
}

// combo 'cmd+shift+k' -> modifier vks + key vk. cmd maps to Ctrl on Windows.
// The words come from combo.h, shared with the other launchers: modifiers in
// any order (this used to need them before the key), exactly one key, and a
// combo with anything else is refused rather than registered as a bare key.
static bool parse_combo(const std::string &combo, std::vector<WORD> &mods,
                        WORD &key, UINT *hotkey_mods) {
  mods.clear();
  key = 0;
  if (hotkey_mods)
    *hotkey_mods = 0;
  Combo c = read_combo(combo);
  if (!c.ok)
    return false;
  if (c.primary || c.ctrl) {
    mods.push_back(VK_CONTROL);
    if (hotkey_mods) *hotkey_mods |= MOD_CONTROL;
  }
  if (c.alt) {
    mods.push_back(VK_MENU);
    if (hotkey_mods) *hotkey_mods |= MOD_ALT;
  }
  if (c.shift) {
    mods.push_back(VK_SHIFT);
    if (hotkey_mods) *hotkey_mods |= MOD_SHIFT;
  }
  if (c.os_key) {
    mods.push_back(VK_LWIN);
    if (hotkey_mods) *hotkey_mods |= MOD_WIN;
  }
  if (!parse_key_token(c.key, key))
    return false;
  return key != 0;
}

struct KeystrokeReq {
  std::string qid, combo;
};

static void do_keystroke(webview_t, void *arg) {
  KeystrokeReq *req = static_cast<KeystrokeReq *>(arg);
  std::vector<WORD> mods;
  WORD key = 0;
  bool ok = parse_combo(req->combo, mods, key, nullptr);
  if (ok) {
    std::vector<INPUT> ins;
    auto push = [&](WORD vk, bool up) {
      INPUT in = {};
      in.type = INPUT_KEYBOARD;
      in.ki.wVk = vk;
      in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
      ins.push_back(in);
    };
    for (WORD m : mods)
      push(m, false);
    push(key, false);
    push(key, true);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it)
      push(*it, true);
    ok = SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT)) == ins.size();
  }
  pipe_write_line("GOT " + req->qid + " {\"ok\":" + (ok ? "true" : "false") +
                  ",\"trusted\":true}");
  delete req;
}

// A stock Edit item was clicked: replay its shortcut into the webview, which
// holds keyboard focus through a menu-bar click. The shortcut carries no
// menu accelerator (see kStockItems), so AcceleratorKeyPressed passes it
// straight to WebView2 — unless the app bound that same combo to an item of
// its own, which then wins, as it would for a real keypress.
static void send_stock_edit(const std::string &role) {
  for (const StockItem &si : kStockItems) {
    if (role != si.role) continue;
    std::vector<WORD> mods;
    WORD key = 0;
    if (!parse_combo(si.shortcut, mods, key, nullptr)) return;
    std::vector<INPUT> ins;
    auto push = [&](WORD vk, bool up) {
      INPUT in = {};
      in.type = INPUT_KEYBOARD;
      in.ki.wVk = vk;
      in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
      ins.push_back(in);
    };
    for (WORD m : mods) push(m, false);
    push(key, false);
    push(key, true);
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) push(*it, true);
    SendInput((UINT)ins.size(), ins.data(), sizeof(INPUT));
    return;
  }
}

struct HotkeyReq {
  std::string id, combo; // empty combo = unregister
  std::string qid;       // HKREG's: answered GOT <qid> {"ok":…}; empty = no reply
};
static void got(const std::string &qid, const std::string &json);  // below

static void do_hotkey(webview_t, void *arg) {
  HotkeyReq *req = static_cast<HotkeyReq *>(arg);
  // unregister an existing binding for this id either way
  for (auto it = g_hotkeys.begin(); it != g_hotkeys.end();) {
    if (it->second == req->id) {
      UnregisterHotKey(g_host, it->first);
      it = g_hotkeys.erase(it);
    } else {
      ++it;
    }
  }
  std::string why;  // set when a registration fails; empty = ok
  if (!req->combo.empty()) {
    std::vector<WORD> mods;
    WORD key = 0;
    UINT hmods = 0;
    if (parse_combo(req->combo, mods, key, &hmods)) {
      int id = g_next_hotkey++;
      if (RegisterHotKey(g_host, id, hmods | MOD_NOREPEAT, key))
        g_hotkeys[id] = req->id;
      else
        why = "the system refused it (another app may hold it)";
    } else {
      why = "needs exactly one known key plus modifiers";
    }
    if (!why.empty())
      std::fprintf(stderr, "tinyjs: hotkey \"%s\" not registered: %s\n", req->combo.c_str(),
                   why.c_str());
  }
  if (!req->qid.empty())
    got(req->qid, why.empty() ? "{\"ok\":true}"
                              : "{\"ok\":false,\"error\":" + json_escape(why) + "}");
  delete req;
}

// ---------------------------------------------------------------------------
// GOT-answering ops

struct QReq {
  std::string qid, rest;
  std::string win;   // window-targeted ops (PDF@<id>); '' = the main window
};

static void got(const std::string &qid, const std::string &json) {
  pipe_write_line("GOT " + qid + " " + json);
}

static void got_unsupported(const std::string &qid) {
  got(qid, "{\"ok\":false,\"error\":\"unsupported on windows\"}");
}

static void do_shell(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::vector<std::string> p = split_tabs(req->rest);
  std::string op = p.size() > 0 ? p[0] : "";
  std::string target = p.size() > 1 ? wire_unescape(p[1]) : "";
  std::string json = "{\"ok\":true}";
  if (op == "open") {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", widen(target).c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
      json = "{\"ok\":false,\"error\":" +
             json_escape("cannot open (" + std::to_string((INT_PTR)r) + ")") +
             "}";
  } else if (op == "reveal") {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(widen(target).c_str());
    if (pidl) {
      SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
      ILFree(pidl);
    } else {
      json = "{\"ok\":false,\"error\":\"no such file\"}";
    }
  } else if (op == "trash") {
    if (GetFileAttributesW(widen(target).c_str()) == INVALID_FILE_ATTRIBUTES) {
      json = "{\"ok\":false,\"error\":\"no such file\"}";
    } else {
      std::wstring wpath = widen(target);
      wpath.push_back(0); // double-null terminated
      SHFILEOPSTRUCTW fo = {};
      fo.hwnd = g_host;
      fo.wFunc = FO_DELETE;
      fo.pFrom = wpath.c_str();
      fo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
      int r = SHFileOperationW(&fo);
      if (r != 0 || fo.fAnyOperationsAborted)
        json = "{\"ok\":false,\"error\":\"trash failed\"}";
    }
  } else {
    json = "{\"ok\":false,\"error\":\"unknown op\"}";
  }
  got(req->qid, json);
  delete req;
}

static void do_perm(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  // No TCC on Windows: input synthesis, screen reading, notifications are
  // unrestricted for a desktop app; mic/camera go through WebView2's own
  // permission flow.
  static const char *known[] = {"accessibility", "screen", "notifications",
                                "microphone", "camera"};
  std::string status = "unsupported";
  for (const char *k : known)
    if (req->rest == k)
      status = "granted";
  got(req->qid, "{\"status\":\"" + status + "\"}");
  delete req;
}

static void do_power(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::vector<std::string> p = split_tabs(req->rest);
  bool on = p.size() > 0 && p[0] == "on";
  bool display = p.size() > 1 && p[1] == "1";
  EXECUTION_STATE flags = ES_CONTINUOUS;
  if (on) {
    flags |= ES_SYSTEM_REQUIRED;
    if (display)
      flags |= ES_DISPLAY_REQUIRED;
  }
  bool ok = SetThreadExecutionState(flags) != 0;
  got(req->qid, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  delete req;
}

static void do_sound(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::string target = wire_unescape(req->rest);
  bool ok;
  if (target.empty()) {
    ok = MessageBeep(MB_OK) != 0;
  } else {
    std::wstring w = widen(target);
    // A file path plays from disk; otherwise try a system sound alias.
    if (GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES)
      ok = PlaySoundW(w.c_str(), nullptr, SND_FILENAME | SND_ASYNC) != 0;
    else
      ok = PlaySoundW(w.c_str(), nullptr, SND_ALIAS | SND_ASYNC) != 0;
  }
  got(req->qid, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  delete req;
}

static void do_secret(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::vector<std::string> p = split_tabs(req->rest);
  std::string op = p.size() > 0 ? p[0] : "";
  std::string service = p.size() > 1 ? wire_unescape(p[1]) : "";
  std::string account = p.size() > 2 ? wire_unescape(p[2]) : "";
  std::string value = p.size() > 3 ? wire_unescape(p[3]) : "";
  std::wstring target = widen(service + "/" + account);
  std::string json;
  if (op == "get") {
    PCREDENTIALW cred = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
      std::string v((char *)cred->CredentialBlob, cred->CredentialBlobSize);
      CredFree(cred);
      json = "{\"ok\":true,\"value\":" + json_escape(v) + "}";
    } else if (GetLastError() == ERROR_NOT_FOUND) {
      json = "{\"ok\":true,\"value\":null}";
    } else {
      json = "{\"ok\":false,\"error\":\"credential read failed\"}";
    }
  } else if (op == "set") {
    // Credential Manager caps a blob at 2560 bytes — a HARDER limit than the
    // macOS keychain, which takes 4 KB of emoji without complaint. Over the
    // cap CredWriteW fails with 1783 (RPC_X_BAD_STUB_DATA), which names
    // nothing, so check up front and say what the caller actually hit. The
    // limit is BYTES of UTF-8, not characters: 4-byte emoji reach it four
    // times faster than ASCII.
    //
    // Hardcoded, NOT CRED_MAX_CREDENTIAL_BLOB_SIZE: MinGW's wincred.h still
    // defines that as the pre-Vista 512, and using it rejected writes this
    // machine accepts. Bisected against a live CredWriteW on Windows 11 26200
    // (2026-07-28): 2560 writes, 2561 fails — the documented Vista+ 5*512.
    const size_t kMaxSecretBytes = 2560;
    if (value.size() > kMaxSecretBytes) {
      json = "{\"ok\":false,\"error\":\"secret too large: " +
             std::to_string(value.size()) + " bytes (Windows Credential " +
             "Manager allows " + std::to_string(kMaxSecretBytes) + ")\"}";
    } else {
      CREDENTIALW cred = {};
      cred.Type = CRED_TYPE_GENERIC;
      cred.TargetName = (LPWSTR)target.c_str();
      cred.CredentialBlob = (LPBYTE)value.data();
      cred.CredentialBlobSize = (DWORD)value.size();
      cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
      std::wstring user = widen(account);
      cred.UserName = (LPWSTR)user.c_str();
      json = CredWriteW(&cred, 0)
                 ? "{\"ok\":true}"
                 : "{\"ok\":false,\"error\":\"credential write failed (" +
                       std::to_string(GetLastError()) + ")\"}";
    }
  } else if (op == "del") {
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) ||
        GetLastError() == ERROR_NOT_FOUND)
      json = "{\"ok\":true}";
    else
      json = "{\"ok\":false,\"error\":\"credential delete failed\"}";
  } else {
    json = "{\"ok\":false,\"error\":\"unknown op\"}";
  }
  got(req->qid, json);
  delete req;
}

// ---------------------------------------------------------------------------
// pngs from HBITMAPs (captureScreen / thumbnail / clipboard image)

static std::string hbitmap_to_temp_png(HBITMAP hbm, UINT *w_out, UINT *h_out) {
  if (!ensure_gdiplus() || !png_encoder_clsid())
    return "";
  Gdiplus::Bitmap *bmp = Gdiplus::Bitmap::FromHBITMAP(hbm, nullptr);
  if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
    delete bmp;
    return "";
  }
  wchar_t tmp[MAX_PATH], file[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  GetTempFileNameW(tmp, L"tjp", 0, file);
  // GetTempFileName creates `file` (empty) to reserve the name; the png goes
  // beside it, so drop the placeholder rather than leave one per call.
  DeleteFileW(file);
  std::wstring png = std::wstring(file) + L".png";
  bool ok = bmp->Save(png.c_str(), &g_png_clsid, nullptr) == Gdiplus::Ok;
  if (w_out)
    *w_out = bmp->GetWidth();
  if (h_out)
    *h_out = bmp->GetHeight();
  delete bmp;
  return ok ? narrow(png) : "";
}

// captureScreen: BitBlt the monitor into a png. No permission dance on
// Windows. display = index into the same enumeration screens_json uses.
static BOOL CALLBACK enum_mon_proc(HMONITOR mon, HDC, LPRECT, LPARAM lp);

static void do_capture(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  int want = atoi(req->rest.c_str());
  std::vector<HMONITOR> mons;
  EnumDisplayMonitors(nullptr, nullptr, enum_mon_proc, (LPARAM)&mons);
  std::string json = "{\"ok\":false,\"error\":\"no such display\"}";
  if (want >= 0 && want < (int)mons.size()) {
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mons[want], &mi);
    int w = mi.rcMonitor.right - mi.rcMonitor.left;
    int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP hbm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, hbm);
    BitBlt(mem, 0, 0, w, h, screen, mi.rcMonitor.left, mi.rcMonitor.top,
           SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    UINT pw = 0, ph = 0;
    std::string path = hbitmap_to_temp_png(hbm, &pw, &ph);
    DeleteObject(hbm);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!path.empty())
      json = "{\"ok\":true,\"path\":" + json_escape(path) +
             ",\"width\":" + std::to_string(pw) +
             ",\"height\":" + std::to_string(ph) + "}";
    else
      json = "{\"ok\":false,\"error\":\"capture failed\"}";
  }
  got(req->qid, json);
  delete req;
}

// thumbnail: the shell's preview image for ANY registered file type.
static void do_thumb(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::vector<std::string> p = split_tabs(req->rest);
  std::string path = p.size() > 0 ? wire_unescape(p[0]) : "";
  int size = p.size() > 1 ? atoi(p[1].c_str()) : 256;
  if (size <= 0) size = 256;
  std::string json = "{\"ok\":false,\"error\":\"no thumbnail\"}";
  IShellItemImageFactory *factory = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(widen(path).c_str(), nullptr,
                                            IID_PPV_ARGS(&factory))) &&
      factory) {
    HBITMAP hbm = nullptr;
    SIZE sz = {size, size};
    if (SUCCEEDED(factory->GetImage(sz, SIIGBF_RESIZETOFIT, &hbm)) && hbm) {
      UINT pw = 0, ph = 0;
      std::string png = hbitmap_to_temp_png(hbm, &pw, &ph);
      DeleteObject(hbm);
      if (!png.empty())
        json = "{\"ok\":true,\"path\":" + json_escape(png) +
               ",\"width\":" + std::to_string(pw) +
               ",\"height\":" + std::to_string(ph) + "}";
    }
    factory->Release();
  }
  got(req->qid, json);
  delete req;
}

// printToPDF via WebView2 (vector pdf of the current page).
struct PdfHandler : public ICoreWebView2PrintToPdfCompletedHandler {
  ULONG refs = 1;
  std::string qid, path;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2PrintToPdfCompletedHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, BOOL ok) override {
    if (SUCCEEDED(hr) && ok)
      got(qid, "{\"ok\":true,\"path\":" + json_escape(path) + "}");
    else
      got(qid, "{\"ok\":false,\"error\":\"pdf failed\"}");
    return S_OK;
  }
};

static void do_pdf(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::string path = wire_unescape(req->rest);
  ICoreWebView2 *wv2 = wv2_for_id(req->win);
  ICoreWebView2_7 *wv7 = nullptr;
  if (wv2 &&
      SUCCEEDED(wv2->QueryInterface(IID_ICoreWebView2_7, (void **)&wv7)) &&
      wv7) {
    PdfHandler *h = new PdfHandler();
    h->qid = req->qid;
    h->path = path;
    if (FAILED(wv7->PrintToPdf(widen(path).c_str(), nullptr, h))) {
      got(req->qid, "{\"ok\":false,\"error\":\"pdf failed to start\"}");
      h->Release();
    }
    wv7->Release();
  } else {
    got(req->qid, "{\"ok\":false,\"error\":\"WebView2 runtime too old for PrintToPdf\"}");
  }
  delete req;
}

// ---------------------------------------------------------------------------
// AUTH — Windows Hello via Windows.Security.Credentials.UI.UserConsentVerifier.
//
// Mirrors the macOS LAContext flow (resolve true when the user verifies with
// fingerprint/face/PIN, false on cancel/denial/unavailable, and NEVER hang the
// promise on any failure). Two chained WinRT async ops: CheckAvailabilityAsync
// (short-circuit to false unless Available), then RequestVerificationAsync whose
// UserConsentVerificationResult is Verified(0) → true, else false. combase is
// loaded dynamically like the toast path; the completion handlers arrive on a
// thread-pool thread, so reply plumbing goes through the thread-safe got().

// Refcounted call context shared by the two completion handlers and the
// synchronous error paths. reply() fires got() exactly once, ever.
struct AuthCall {
  std::atomic<int> refs{1};
  std::string qid;
  std::atomic<bool> replied{false};
  void AddRefC() { refs.fetch_add(1); }
  void ReleaseC() {
    if (refs.fetch_sub(1) == 1)
      delete this;
  }
  void reply(bool ok) {
    bool expected = false;
    if (replied.compare_exchange_strong(expected, true))
      got(qid, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  }
};

// Carried across the UI-thread hop that starts RequestVerificationAsync (that
// call shows system UI, so it runs on the UI thread like the rest of AUTH).
struct AuthVerifyStart {
  AuthCall *call;
  WSCU::IUserConsentVerifierStatics *statics; // owns one ref
  std::wstring reason;
};

static void do_auth_verify(webview_t, void *arg); // fwd

// RequestVerificationAsync completion: Verified(0) → true, anything else false.
// Hand-rolled IAsyncOperationCompletedHandler<UserConsentVerificationResult>
// answering only IUnknown + its exact pinterface IID.
struct AuthResultHandler
    : WF::IAsyncOperationCompletedHandler<WSCU::UserConsentVerificationResult> {
  ULONG refs = 1;
  AuthCall *call = nullptr;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    if (call)
      call->ReleaseC();
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown || riid == kIID_IAsyncOpCompleted_UCVResult) {
      *ppv = static_cast<WF::IAsyncOperationCompletedHandler<
          WSCU::UserConsentVerificationResult> *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(WF::IAsyncOperation<WSCU::UserConsentVerificationResult> *op,
         AsyncStatus status) override {
    WSCU::UserConsentVerificationResult r =
        WSCU::UserConsentVerificationResult_Canceled;
    bool ok = op && status == Completed && SUCCEEDED(op->GetResults(&r)) &&
              r == WSCU::UserConsentVerificationResult_Verified;
    call->reply(ok);
    return S_OK;
  }
};

// CheckAvailabilityAsync completion: unless Available, reply false; otherwise
// hop to the UI thread to start the actual verification prompt.
struct AuthAvailHandler
    : WF::IAsyncOperationCompletedHandler<WSCU::UserConsentVerifierAvailability> {
  ULONG refs = 1;
  AuthCall *call = nullptr;
  WSCU::IUserConsentVerifierStatics *statics = nullptr; // owns one ref
  std::wstring reason;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    if (statics)
      statics->Release();
    if (call)
      call->ReleaseC();
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown || riid == kIID_IAsyncOpCompleted_UCVAvailability) {
      *ppv = static_cast<WF::IAsyncOperationCompletedHandler<
          WSCU::UserConsentVerifierAvailability> *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(WF::IAsyncOperation<WSCU::UserConsentVerifierAvailability> *op,
         AsyncStatus status) override {
    WSCU::UserConsentVerifierAvailability avail =
        WSCU::UserConsentVerifierAvailability_DeviceNotPresent;
    if (!op || status != Completed || FAILED(op->GetResults(&avail)) ||
        avail != WSCU::UserConsentVerifierAvailability_Available) {
      call->reply(false);
      return S_OK;
    }
    // Available: start the prompt on the UI thread. Hand ownership of one ref
    // each on call + statics to the dispatched AuthVerifyStart.
    AuthVerifyStart *vs = new AuthVerifyStart{call, statics, reason};
    call->AddRefC();
    statics->AddRef();
    tiny_dispatch(do_auth_verify, vs);
    return S_OK;
  }
};

// UI thread: kick off RequestVerificationAsync and wire up AuthResultHandler.
static void do_auth_verify(webview_t, void *arg) {
  AuthVerifyStart *vs = static_cast<AuthVerifyStart *>(arg);
  WF::IAsyncOperation<WSCU::UserConsentVerificationResult> *op = nullptr;
  HRESULT hr;
  {
    HStr msg(vs->reason.empty() ? L"authenticate" : vs->reason);
    hr = vs->statics->RequestVerificationAsync(msg.h, &op);
  }
  if (FAILED(hr) || !op) {
    vs->call->reply(false);
  } else {
    AuthResultHandler *h = new AuthResultHandler();
    h->call = vs->call;
    vs->call->AddRefC();
    if (FAILED(op->put_Completed(h))) {
      // put_Completed failed: the handler never fires, so reply now.
      vs->call->reply(false);
    }
    h->Release();
    op->Release();
  }
  vs->statics->Release();
  vs->call->ReleaseC();
  delete vs;
}

struct AuthReq {
  std::string qid, reason;
};

// UI thread: get the statics, start CheckAvailabilityAsync. Any failure at this
// stage replies false immediately (exactly once, via AuthCall::reply).
static void do_auth(webview_t, void *arg) {
  AuthReq *req = static_cast<AuthReq *>(arg);
  AuthCall *call = new AuthCall();
  call->qid = req->qid;
  std::wstring reason = widen(req->reason);
  delete req;

  if (!load_combase()) {
    call->reply(false);
    call->ReleaseC();
    return;
  }
  HRESULT hr = p_RoInitialize(RO_INIT_SINGLETHREADED);
  if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
    call->reply(false);
    call->ReleaseC();
    return;
  }

  WSCU::IUserConsentVerifierStatics *statics = nullptr;
  {
    HStr cls(RuntimeClass_Windows_Security_Credentials_UI_UserConsentVerifier);
    if (FAILED(p_RoGetActivationFactory(cls.h, kIID_IUserConsentVerifierStatics,
                                        (void **)&statics)) ||
        !statics) {
      call->reply(false);
      call->ReleaseC();
      return;
    }
  }

  WF::IAsyncOperation<WSCU::UserConsentVerifierAvailability> *op = nullptr;
  hr = statics->CheckAvailabilityAsync(&op);
  if (FAILED(hr) || !op) {
    statics->Release();
    call->reply(false);
    call->ReleaseC();
    return;
  }

  AuthAvailHandler *h = new AuthAvailHandler();
  h->call = call;   // transfers the original ref
  h->statics = statics; // transfers the statics ref
  h->reason = reason;
  if (FAILED(op->put_Completed(h))) {
    // Handler will never fire; reply now. h->Release() drops call + statics.
    call->reply(false);
  }
  op->Release();
  h->Release();
}

// ---------------------------------------------------------------------------
// text-to-speech (SAPI)

#ifndef SPCAT_VOICES
#define SPCAT_VOICES L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices"
#endif

// MinGW's libuuid does not carry the SAPI GUIDs; these match sapi.h's
// MIDL_INTERFACE / coclass annotations.
static const CLSID kCLSID_SpVoice =
    {0x96749377, 0x3391, 0x11D2, {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
static const IID kIID_ISpVoice =
    {0x6C44DF74, 0x72B9, 0x4992, {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};
static const CLSID kCLSID_SpObjectTokenCategory =
    {0xA910187F, 0x0C7A, 0x45AC, {0x92, 0xCC, 0x59, 0xED, 0xAF, 0xB7, 0x7B, 0x53}};
static const IID kIID_ISpObjectTokenCategory =
    {0x2D3D3845, 0x39AF, 0x4850, {0xBB, 0xF9, 0x40, 0xB4, 0x97, 0x80, 0x01, 0x1D}};

static ISpVoice *g_voice = nullptr;

static bool ensure_voice() {
  if (g_voice)
    return true;
  return SUCCEEDED(CoCreateInstance(kCLSID_SpVoice, nullptr, CLSCTX_ALL,
                                    kIID_ISpVoice, (void **)&g_voice));
}

static ISpObjectToken *voice_token_by_id(const std::wstring &want) {
  ISpObjectTokenCategory *cat = nullptr;
  if (FAILED(CoCreateInstance(kCLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                              kIID_ISpObjectTokenCategory, (void **)&cat)))
    return nullptr;
  ISpObjectToken *found = nullptr;
  if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))) {
    IEnumSpObjectTokens *en = nullptr;
    if (SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &en)) && en) {
      ISpObjectToken *tok = nullptr;
      while (!found && en->Next(1, &tok, nullptr) == S_OK) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(tok->GetId(&id)) && id) {
          if (want == id)
            found = tok;
          CoTaskMemFree(id);
        }
        if (!found)
          tok->Release();
      }
      en->Release();
    }
  }
  cat->Release();
  return found;
}

// Bumped by every say() and stopSpeaking(): a waiting say() whose number is
// no longer current was cut off.
static std::atomic<unsigned> g_say_gen{0};

static void do_say(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::vector<std::string> p = split_tabs(req->rest);
  std::string text = p.size() > 0 ? wire_unescape(p[0]) : "";
  std::string voice = p.size() > 1 ? wire_unescape(p[1]) : "";
  double rate = p.size() > 2 ? atof(p[2].c_str()) : 0;
  if (!ensure_voice()) {
    got(req->qid, "{\"ok\":false,\"error\":\"speech unavailable\"}");
    delete req;
    return;
  }
  if (!voice.empty()) {
    ISpObjectToken *tok = voice_token_by_id(widen(voice));
    if (tok) {
      g_voice->SetVoice(tok);
      tok->Release();
    }
  }
  // mac rate is 0..1 (~0.5 = normal); SAPI wants -10..10.
  if (rate > 0)
    g_voice->SetRate((long)((rate - 0.5) * 20.0));
  std::string qid = req->qid;
  unsigned gen = ++g_say_gen;  // a newer say() or stopSpeaking() moves it on
  g_voice->Speak(widen(text).c_str(),
                 SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
  // Resolve when playback finishes (mirrors macOS `say`); a worker waits so
  // the UI thread stays free. WaitUntilDone also returns when this utterance
  // is purged by a newer Speak — that one resolves false (interrupted), as on
  // macOS, where it used to resolve true like a finished one.
  ISpVoice *v = g_voice;
  v->AddRef();
  std::thread([v, qid, gen]() {
    v->WaitUntilDone(INFINITE);
    got(qid, g_say_gen.load() == gen ? "{\"ok\":true}" : "{\"ok\":false}");
    v->Release();
  }).detach();
  delete req;
}

static void do_saystop(webview_t, void *) {
  ++g_say_gen;  // whatever is speaking now resolves false (interrupted)
  if (g_voice)
    g_voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
}

static void do_voices(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::string voices = "[";
  ISpObjectTokenCategory *cat = nullptr;
  if (SUCCEEDED(CoCreateInstance(kCLSID_SpObjectTokenCategory, nullptr,
                                 CLSCTX_ALL, kIID_ISpObjectTokenCategory,
                                 (void **)&cat)) &&
      cat) {
    if (SUCCEEDED(cat->SetId(SPCAT_VOICES, FALSE))) {
      IEnumSpObjectTokens *en = nullptr;
      if (SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &en)) && en) {
        ISpObjectToken *tok = nullptr;
        bool first = true;
        while (en->Next(1, &tok, nullptr) == S_OK) {
          LPWSTR id = nullptr, desc = nullptr;
          tok->GetId(&id);
          tok->GetStringValue(nullptr, &desc);
          std::string lang;
          ISpDataKey *attrs = nullptr;
          if (SUCCEEDED(tok->OpenKey(L"Attributes", &attrs)) && attrs) {
            LPWSTR lw = nullptr;
            if (SUCCEEDED(attrs->GetStringValue(L"Language", &lw)) && lw) {
              lang = narrow(lw);
              CoTaskMemFree(lw);
            }
            attrs->Release();
          }
          if (!first)
            voices += ",";
          first = false;
          voices += "{\"id\":" + json_escape(id ? narrow(id) : "") +
                    ",\"name\":" + json_escape(desc ? narrow(desc) : "") +
                    ",\"lang\":" + json_escape(lang) +
                    ",\"quality\":\"default\"}";
          if (id) CoTaskMemFree(id);
          if (desc) CoTaskMemFree(desc);
          tok->Release();
        }
        en->Release();
      }
    }
    cat->Release();
  }
  voices += "]";
  got(req->qid, "{\"ok\":true,\"voices\":" + voices + "}");
  delete req;
}

// ---------------------------------------------------------------------------
// drag & drop — real filesystem paths both directions

// IN: WebView2 normally swallows OS drops (pages get path-less File objects).
// We turn its handling off (AllowExternalDrop=false) and register our own
// IDropTarget on the host window — OLE walks up from the child under the
// cursor to the nearest registered ancestor, so drops anywhere in the window
// land here and go out as `DROP <json-paths>`.
struct DropTarget : public IDropTarget {
  ULONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  static bool has_files(IDataObject *d) {
    FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return d && d->QueryGetData(&fmt) == S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *d, DWORD, POINTL,
                                      DWORD *effect) override {
    m_files = has_files(d);
    drag_dbg(std::string("DropTarget::DragEnter, files=") +
             (m_files ? "yes" : "no"));
    *effect = m_files ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD *effect) override {
    *effect = m_files ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject *d, DWORD, POINTL,
                                 DWORD *effect) override {
    *effect = DROPEFFECT_NONE;
    FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med;
    if (d && SUCCEEDED(d->GetData(&fmt, &med))) {
      HDROP drop = (HDROP)GlobalLock(med.hGlobal);
      if (drop) {
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::string json = "[";
        for (UINT i = 0; i < n; i++) {
          wchar_t buf[MAX_PATH];
          DragQueryFileW(drop, i, buf, MAX_PATH);
          if (i) json += ",";
          json += json_escape(narrow(buf));
        }
        json += "]";
        pipe_write_line("DROP " + json);
        GlobalUnlock(med.hGlobal);
        *effect = DROPEFFECT_COPY;
      }
      ReleaseStgMedium(&med);
    }
    return S_OK;
  }
  bool m_files = false;
};

// Dropped-file paths, the official WebView2 way: the child process owns the
// OLE drop (its target rejects when AllowExternalDrop is off and never lets
// the walk reach the host hwnd), so instead the page forwards the dropped
// File objects via chrome.webview.postMessageWithAdditionalObjects and the
// host reads each ICoreWebView2File's real path.
static const char *DROP_FORWARD_JS =
    "(() => {"
    // capture at document-start (window.chrome is shadowable by page code)
    "const wv = window.chrome && window.chrome.webview;"
    "const postObjs = wv && wv.postMessageWithAdditionalObjects"
    "  && wv.postMessageWithAdditionalObjects.bind(wv);"
    "window.addEventListener('dragover', (e) => e.preventDefault());"
    "window.addEventListener('drop', (e) => {"
    "  e.preventDefault();"
    "  if (postObjs && e.dataTransfer && e.dataTransfer.files.length)"
    "    postObjs('tinyjs-drop', e.dataTransfer.files);"
    "});"
    "})();";

struct DropMsgHandler : public ICoreWebView2WebMessageReceivedEventHandler {
  ULONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2WebMessageReceivedEventArgs *args) override {
    LPWSTR s = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&s)) || !s)
      return S_OK;
    bool ours = wcscmp(s, L"tinyjs-drop") == 0;
    CoTaskMemFree(s);
    if (!ours)
      return S_OK;
    ICoreWebView2WebMessageReceivedEventArgs2 *a2 = nullptr;
    if (FAILED(args->QueryInterface(IID_ICoreWebView2WebMessageReceivedEventArgs2,
                                    (void **)&a2)) || !a2) {
      drag_dbg("drop-in: Args2 unavailable (runtime too old)");
      return S_OK;
    }
    ICoreWebView2ObjectCollectionView *objs = nullptr;
    std::string json = "[";
    bool any = false;
    if (SUCCEEDED(a2->get_AdditionalObjects(&objs)) && objs) {
      UINT count = 0;
      objs->get_Count(&count);
      for (UINT i = 0; i < count; i++) {
        IUnknown *u = nullptr;
        if (SUCCEEDED(objs->GetValueAtIndex(i, &u)) && u) {
          ICoreWebView2File *file = nullptr;
          if (SUCCEEDED(u->QueryInterface(IID_ICoreWebView2File,
                                          (void **)&file)) && file) {
            LPWSTR path = nullptr;
            if (SUCCEEDED(file->get_Path(&path)) && path) {
              if (any) json += ",";
              any = true;
              json += json_escape(narrow(path));
              CoTaskMemFree(path);
            }
            file->Release();
          }
          u->Release();
        }
      }
      objs->Release();
    }
    json += "]";
    a2->Release();
    drag_dbg("drop-in: " + json);
    if (any)
      pipe_write_line("DROP " + json);
    return S_OK;
  }
};

// UNC media un-tainting: file://server/share/… has a HOST, so it's a
// different origin from the page's plain file:// and WebAudio taints it
// (network shares / Parallels mounts played silently through media-element
// graphs). Serve those requests ourselves with Access-Control-Allow-Origin.
struct UncHandler : public ICoreWebView2WebResourceRequestedEventHandler {
  ULONG refs = 1;
  ICoreWebView2Environment *env = nullptr;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    if (env) env->Release();
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2WebResourceRequestedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  static std::string url_decode(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '%' && i + 2 < s.size()) {
        out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
        i += 2;
      } else {
        out += s[i];
      }
    }
    return out;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2WebResourceRequestedEventArgs *args) override {
    ICoreWebView2WebResourceRequest *req = nullptr;
    if (FAILED(args->get_Request(&req)) || !req)
      return S_OK;
    LPWSTR uri = nullptr;
    req->get_Uri(&uri);
    std::string u = uri ? narrow(uri) : "";
    if (uri) CoTaskMemFree(uri);
    // only file URLs WITH a host (UNC); plain file:/// keeps default handling
    if (u.rfind("file://", 0) != 0 || u.size() < 8 || u[7] == '/') {
      req->Release();
      return S_OK;
    }
    // Range header: media elements seek — without 206 support a seek stalls
    // the element (the viz twin jumps to the live elapsed position at once).
    std::string range;
    ICoreWebView2HttpRequestHeaders *hdrs = nullptr;
    if (SUCCEEDED(req->get_Headers(&hdrs)) && hdrs) {
      LPWSTR rv = nullptr;
      if (SUCCEEDED(hdrs->GetHeader(L"Range", &rv)) && rv) {
        range = narrow(rv);
        CoTaskMemFree(rv);
      }
      hdrs->Release();
    }
    req->Release();
    std::string rest = url_decode(u.substr(7)); // server/share/path
    for (auto &c : rest)
      if (c == '/') c = '\\';
    std::string path = "\\\\" + rest;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &fad))
      return S_OK;
    long long size = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    IStream *stream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(widen(path).c_str(),
                                      STGM_READ | STGM_SHARE_DENY_NONE,
                                      FILE_ATTRIBUTE_NORMAL, FALSE, nullptr,
                                      &stream)) || !stream)
      return S_OK; // fall through to default (will 404 like before)
    long long start = 0, end = size - 1;
    bool partial = false;
    if (range.rfind("bytes=", 0) == 0) {
      partial = true;
      start = atoll(range.c_str() + 6);
      size_t dash = range.find('-', 6);
      if (dash != std::string::npos && dash + 1 < range.size())
        end = atoll(range.c_str() + dash + 1);
      if (start < 0 || start >= size) { start = 0; partial = false; }
      if (end >= size || end < start) end = size - 1;
    }
    std::wstring headers = L"Access-Control-Allow-Origin: *\n"
                           L"Accept-Ranges: bytes\n"
                           L"Content-Length: " +
                           std::to_wstring(end - start + 1);
    IStream *body = stream;
    if (partial) {
      LARGE_INTEGER li;
      li.QuadPart = start;
      stream->Seek(li, STREAM_SEEK_SET, nullptr);
      if (end < size - 1) {
        // bounded range: hand Chromium exactly the slice it asked for
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(end - start + 1));
        if (mem) {
          void *p = GlobalLock(mem);
          ULONG got = 0;
          stream->Read(p, (ULONG)(end - start + 1), &got);
          GlobalUnlock(mem);
          IStream *slice = nullptr;
          if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &slice)) && slice) {
            stream->Release();
            body = slice;
          } else {
            GlobalFree(mem);
          }
        }
      }
      headers += L"\nContent-Range: bytes " + std::to_wstring(start) + L"-" +
                 std::to_wstring(end) + L"/" + std::to_wstring(size);
    }
    ICoreWebView2WebResourceResponse *resp = nullptr;
    if (env && SUCCEEDED(env->CreateWebResourceResponse(
                   body, partial ? 206 : 200,
                   partial ? L"Partial Content" : L"OK", headers.c_str(),
                   &resp)) && resp) {
      args->put_Response(resp);
      resp->Release();
    }
    body->Release();
    return S_OK;
  }
};

// Register the UNC interceptor on ANY webview (main + every secondary —
// a satellite window playing a network-share track needs it just as much).
// Never unregistered, like every handler here: it lives exactly as long as
// its webview. A secondary window's WM_DESTROY Close()s the controller,
// which drops its handlers (this one releasing its env ref with it), then
// releases ctrl + wv; main's lives as long as the app (#30.7 asked).
static void install_unc_for(ICoreWebView2 *wv) {
  if (!wv)
    return;
  ICoreWebView2_2 *wv22 = nullptr;
  if (FAILED(wv->QueryInterface(IID_ICoreWebView2_2, (void **)&wv22)) ||
      !wv22)
    return;
  UncHandler *h = new UncHandler();
  wv22->get_Environment(&h->env);
  wv22->Release();
  wv->AddWebResourceRequestedFilter(L"file://*",
                                    COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
  EventRegistrationToken tok;
  wv->add_WebResourceRequested(h, &tok);
  h->Release();
}

static void install_unc_handler() { install_unc_for(g_wv2); }

static void install_drop_target() {
  // Keep WebView2's default drop handling ON (the page prevents navigation
  // and forwards the files); the host-side OLE target stays as a harmless
  // fallback for regions without a webview.
  RegisterDragDrop(g_hwnd, new DropTarget());
  if (g_wv2) {
    EventRegistrationToken tok;
    g_wv2->add_WebMessageReceived(new DropMsgHandler(), &tok);
  }
  webview_init(g_w, DROP_FORWARD_JS);
}

// Minimal IDataObject serving one CF_HDROP — Explorer and the desktop accept
// this where SHCreateDataObject's idlist-flavored object was refused.
struct HDropDataObject : public IDataObject {
  ULONG refs = 1;
  HGLOBAL hdrop; // owned
  HDropDataObject(HGLOBAL h) : hdrop(h) {}
  ~HDropDataObject() { GlobalFree(hdrop); }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  static bool is_hdrop(FORMATETC *f) {
    return f && f->cfFormat == CF_HDROP && (f->tymed & TYMED_HGLOBAL) &&
           f->dwAspect == DVASPECT_CONTENT;
  }
  HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f, STGMEDIUM *m) override {
    if (!is_hdrop(f)) return DV_E_FORMATETC;
    SIZE_T len = GlobalSize(hdrop);
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!copy) return E_OUTOFMEMORY;
    memcpy(GlobalLock(copy), GlobalLock(hdrop), len);
    GlobalUnlock(copy);
    GlobalUnlock(hdrop);
    m->tymed = TYMED_HGLOBAL;
    m->hGlobal = copy;
    m->pUnkForRelease = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override {
    return is_hdrop(f) ? S_OK : DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir,
                                          IEnumFORMATETC **out) override {
    if (dir != DATADIR_GET) return E_NOTIMPL;
    FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SHCreateStdEnumFmtEtc(1, &fmt, out);
  }
  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *o) override {
    if (o) o->ptd = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }
};

// Drag diagnostics (TINYJS_LAUNCHER_DEBUG=1) — greppable as "dragdbg".
static void drag_dbg(const std::string &msg) {
  if (!GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
    return;
  std::fprintf(stderr, "dragdbg: %s\n", msg.c_str());
  std::fflush(stderr);
}

// OUT: startDrag({ files }) — a shell IDataObject over the paths plus a
// minimal IDropSource, so files land in Explorer/apps as real copies.
struct DropSource : public IDropSource {
  ULONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  int calls = 0;
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD) override {
    // The drag runs on a fresh thread whose queue never saw the mousedown, so
    // OLE's grfKeyState reads the button as already released and would end
    // the drag instantly. Track the PHYSICAL state instead.
    if (++calls == 1)
      drag_dbg("QueryContinueDrag: first call (loop is alive)");
    if (esc || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
      drag_dbg("QueryContinueDrag: CANCEL (esc)");
      return DRAGDROP_S_CANCEL;
    }
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
      drag_dbg("QueryContinueDrag: DROP (button released) after " +
               std::to_string(calls) + " polls");
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }
  DWORD last_effect = 0xFFFFFFFF;
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override {
    if (effect != last_effect) {
      last_effect = effect;
      POINT pt;
      GetCursorPos(&pt);
      HWND under = WindowFromPoint(pt);
      char cls[64] = "?";
      if (under)
        GetClassNameA(under, cls, sizeof(cls));
      drag_dbg("GiveFeedback: effect=" + std::to_string(effect) +
               " over class '" + cls + "'");
    }
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }
};

struct DragOutReq {
  std::string win, image;
  std::vector<std::string> paths;
};

static void do_dragout(webview_t, void *arg) {
  DragOutReq *req = static_cast<DragOutReq *>(arg);
  // DoDragDrop cannot run on the webview UI thread: the WebView2 child (a
  // separate process) holds the mouse capture from the page's mousedown, and
  // the OLE drag loop would deadlock against it. A dedicated STA thread with
  // its own message pump works — the standard WebView2 drag-out recipe.
  std::vector<std::string> paths = req->paths;
  delete req;
  std::thread([paths]() {
    if (FAILED(OleInitialize(nullptr)))
      return;
    drag_dbg("thread start, button=" +
             std::string((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? "down" : "UP"));
    // Attach to the mouse-owning thread for the WHOLE drag: this thread has
    // no window receiving input, so without the attachment OLE's target
    // tracking sees no real mouse moves — the effect froze at the start
    // position and drops always landed as DROPEFFECT_NONE.
    POINT start_pt;
    GetCursorPos(&start_pt);
    HWND under0 = WindowFromPoint(start_pt);
    DWORD mouse_tid = under0 ? GetWindowThreadProcessId(under0, nullptr) : 0;
    bool attached = mouse_tid && mouse_tid != GetCurrentThreadId() &&
                    AttachThreadInput(GetCurrentThreadId(), mouse_tid, TRUE);
    drag_dbg(std::string("input attach: ") + (attached ? "ok" : "no"));
    ReleaseCapture();
    // A DoDragDrop that never returns wedges drag&drop SYSTEM-WIDE (it holds
    // the OLE drag state until the process dies). Two guards: never start
    // once the button is already up, and a watchdog that pokes this thread's
    // queue so the OLE loop (which only re-evaluates on input messages)
    // always gets a chance to see the released button and exit.
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
      drag_dbg("ABORT: button already up before DoDragDrop");
      OleUninitialize();
      return;
    }
    DWORD drag_tid = GetCurrentThreadId();
    std::atomic<bool> drag_done{false};
    std::thread watchdog([drag_tid, &drag_done]() {
      while (!drag_done.load()) {
        PostThreadMessageW(drag_tid, WM_MOUSEMOVE, 0, 0);
        Sleep(100);
      }
    });
    // Pack the paths as one CF_HDROP block (same layout as the clipboard).
    std::wstring list;
    for (const auto &p : paths) {
      std::wstring w = widen(p);
      list += w;
      list.push_back(0);
    }
    list.push_back(0);
    size_t bytes = sizeof(DROPFILES) + list.size() * sizeof(wchar_t);
    HGLOBAL hglobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hglobal) {
      DROPFILES *df = (DROPFILES *)GlobalLock(hglobal);
      memset(df, 0, sizeof(DROPFILES));
      df->pFiles = sizeof(DROPFILES);
      df->fWide = TRUE;
      memcpy((char *)df + sizeof(DROPFILES), list.data(),
             list.size() * sizeof(wchar_t));
      GlobalUnlock(hglobal);
      IDataObject *data = new HDropDataObject(hglobal); // owns hglobal
      {
        DropSource *src = new DropSource();
        DWORD effect = 0;
        drag_dbg("DoDragDrop: entering (" + std::to_string(paths.size()) + " file(s))");
        HRESULT hr = DoDragDrop(data, src,
                                DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
        char hrbuf[64];
        std::snprintf(hrbuf, sizeof(hrbuf), "DoDragDrop: returned hr=0x%lx effect=%lu",
                      (unsigned long)hr, (unsigned long)effect);
        drag_dbg(hrbuf);
        src->Release();
        data->Release();
      }
    }
    drag_done.store(true);
    watchdog.join();
    if (attached)
      AttachThreadInput(GetCurrentThreadId(), mouse_tid, FALSE);
    OleUninitialize();
  }).detach();
}

// ---------------------------------------------------------------------------
// menu accelerators — WebView2 owns the keyboard, so Ctrl+<key> combos are
// caught in its AcceleratorKeyPressed event and routed to the menu registry.

struct AccelHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
  ULONG refs = 1;
  // Which window's keyboard this is. Each window has its own copy of the
  // items (its own bar), so the combo must resolve against that window's —
  // a Save greyed out in one window's menu is not greyed out in another's.
  std::string winid = "main";
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2AcceleratorKeyPressedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2Controller *,
         ICoreWebView2AcceleratorKeyPressedEventArgs *args) override {
    COREWEBVIEW2_KEY_EVENT_KIND kind;
    args->get_KeyEventKind(&kind);
    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN)
      return S_OK;
    UINT vk = 0;
    args->get_VirtualKey(&vk);
    // Launcher-owned devtools key: F12 on every platform. The engine's own
    // F12 is gone with the browser accelerator set suppressed, and this
    // event fires regardless of that setting.
    if (vk == VK_F12 && g_debug) {
      args->put_Handled(TRUE);
      ICoreWebView2 *wv = wv2_for_id(winid);
      if (wv)
        wv->OpenDevToolsWindow();
      return S_OK;
    }
    if (!(GetKeyState(VK_CONTROL) & 0x8000))
      return S_OK;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    char c = 0;
    if (vk >= 'A' && vk <= 'Z') c = (char)tolower((int)vk);
    else if (vk >= '0' && vk <= '9') c = (char)vk;
    if (!c)
      return S_OK;
    HWND owner = hwnd_for_win(winid);
    if (!owner)
      return S_OK;
    for (auto &kv : g_cmd_reg) {
      ItemReg *reg = kv.second;
      if (reg->kind == "menu" && reg->owner == owner &&
          reg->key.size() == 1 &&
          reg->needAlt == alt && reg->needShift == shift &&
          tolower((unsigned char)reg->key[0]) == c) {
        // A disabled item still owns its combo (AppKit semantics). Swallow
        // either way, fire only when enabled — otherwise the key falls
        // through to WebView2's own Ctrl+P/Ctrl+F/Ctrl+R (nib's Ctrl+P
        // print-sheet bug, TODO-verify.md 2026-08-01).
        args->put_Handled(TRUE);
        if (reg->enabled)
          pipe_write_line("MENU " + reg->id);
        break;
      }
    }
    return S_OK;
  }
};

static void install_accel_handler() {
  if (!g_ctrl)
    return;
  EventRegistrationToken tok;
  g_ctrl->add_AcceleratorKeyPressed(new AccelHandler(), &tok);
}

// ---------------------------------------------------------------------------
// launch at login (HKCU Run key; the bridge appends the app exe path)

static void do_login(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  // rest: "get\t<exe>" | "set 0|1\t<exe>"
  std::vector<std::string> p = split_tabs(req->rest);
  std::string verb = p.size() > 0 ? p[0] : "";
  std::string exe = p.size() > 1 ? wire_unescape(p[1]) : "";
  std::string base;
  {
    size_t slash = exe.find_last_of("\\/");
    base = slash == std::string::npos ? exe : exe.substr(slash + 1);
    for (auto &ch : base) ch = (char)tolower((unsigned char)ch);
  }
  // A dev run's exe is tjs.exe — registering that would relaunch the bare
  // runtime, not the app. Only built apps (dist/<name>.exe) qualify.
  if (exe.empty() || base == "tjs.exe") {
    got(req->qid, "{\"status\":\"unsupported\"}");
    delete req;
    return;
  }
  std::wstring name = widen("tinyjs-" + g_app_name);
  HKEY key;
  std::string status = "unsupported";
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_READ | KEY_WRITE, &key) == ERROR_SUCCESS) {
    if (verb == "get") {
      status = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr,
                                nullptr) == ERROR_SUCCESS
                   ? "enabled"
                   : "disabled";
    } else if (verb == "set 1") {
      std::wstring val = L"\"" + widen(exe) + L"\"";
      status = RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE *)val.c_str(),
                              (DWORD)((val.size() + 1) * sizeof(wchar_t))) ==
                       ERROR_SUCCESS
                   ? "enabled"
                   : "unsupported";
    } else if (verb == "set 0") {
      RegDeleteValueW(key, name.c_str());
      status = "disabled";
    }
    RegCloseKey(key);
  }
  got(req->qid, "{\"status\":\"" + status + "\"}");
  delete req;
}

// ---------------------------------------------------------------------------
// GET

struct GetReq {
  std::string qid, what;
};

static std::string win_state_json(HWND hwnd) {
  bool main = hwnd == g_hwnd;
  RECT r;
  GetWindowRect(hwnd, &r);
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  double sc = window_scale(hwnd);
  auto L = [sc](LONG v) { return (long)lround(v / sc); }; // physical -> logical
  // position: the global DIP layout, measured against the monitor holding
  // most of the window (what setPosition picks by too)
  std::vector<ScreenLayout> lay = screen_layout();
  const ScreenLayout *on = layout_of_window(lay, hwnd);
  double gx = r.left / sc, gy = r.top / sc;
  if (on)
    px_to_dip(*on, r.left, r.top, &gx, &gy);
  LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
  bool frameless = main ? (g_frameless || g_square) : !(style & WS_CAPTION);
  char buf[1280]; // truncation here would emit half a JSON object
  // Read the styles back so set -> get round-trips. WS_MINIMIZEBOX and
  // WS_MAXIMIZEBOX only mean anything while WS_SYSMENU is present.
  std::string controls = "[";
  if (style & WS_SYSMENU) {
    controls += "\"close\"";
    if (style & WS_MINIMIZEBOX) controls += ",\"minimize\"";
    if (style & WS_MAXIMIZEBOX) controls += ",\"maximize\"";
  }
  controls += "]";
  // width/height are the CLIENT area — the page's own box, the same units
  // CreateWindow, win.setSize and setMinSize speak, so set -> get round-trips.
  // x/y stay the window rect's top-left, because that is what setPosition
  // takes. `outer` is the on-screen footprint, border and caption included;
  // for the main window and frameless satellites the two are identical
  // (WM_NCCALCSIZE leaves no non-client area), so only a titled satellite
  // shows a difference.
  RECT cr;
  GetClientRect(hwnd, &cr);
  std::snprintf(
      buf, sizeof(buf),
      "{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
      "\"outer\":{\"width\":%ld,\"height\":%ld},"
      "\"fullscreen\":%s,\"minimized\":%s,\"visible\":%s,\"focused\":%s,"
      "\"alwaysOnTop\":%s,\"resizable\":%s,"
      "\"clickThrough\":%s,\"level\":\"%s\",\"allSpaces\":false,"
      "\"chrome\":{\"frame\":%s,\"windowControls\":%s,\"transparent\":false,"
      "\"vibrancy\":null,\"squareCorners\":%s,\"acceptsFirstMouse\":true},"
      "\"screen\":{\"width\":%ld,\"height\":%ld,\"scale\":%.2f},"
      "\"screenId\":%d,\"local\":{\"x\":%ld,\"y\":%ld},"
      "\"pixels\":{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld}}",
      lround(gx), lround(gy), L(cr.right), L(cr.bottom),
      L(r.right - r.left), L(r.bottom - r.top),
      (main && g_fullscreen) ? "true" : "false",
      IsIconic(hwnd) ? "true" : "false",
      IsWindowVisible(hwnd) ? "true" : "false",
      GetForegroundWindow() == hwnd ? "true" : "false",
      (ex & WS_EX_TOPMOST) ? "true" : "false",
      (style & WS_THICKFRAME) ? "true" : "false",
      (main && g_click_through) ? "true" : "false",
      main ? g_level.c_str() : "normal",
      frameless ? "false" : "true", controls.c_str(),
      (main && g_square) ? "true" : "false",
      L(mi.rcMonitor.right - mi.rcMonitor.left),
      L(mi.rcMonitor.bottom - mi.rcMonitor.top), monitor_scale(mon),
      on ? (int)(on - lay.data()) : 0,
      on ? lround((r.left - on->px.left) / on->scale) : 0L,
      on ? lround((r.top - on->px.top) / on->scale) : 0L,
      on ? (long)(r.left - on->px.left) : 0L, on ? (long)(r.top - on->px.top) : 0L,
      (long)cr.right, (long)cr.bottom);
  return buf;
}

static BOOL CALLBACK enum_mon_proc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
  std::vector<HMONITOR> *v = (std::vector<HMONITOR> *)lp;
  v->push_back(mon);
  return TRUE;
}

static void do_get(webview_t, void *arg) {
  GetReq *req = static_cast<GetReq *>(arg);
  std::string json = "null";
  const std::string &what = req->what;
  if (what == "windows") {
    json = g_hwnd ? "[\"main\"" : "["; // windowless: no main
    for (auto &kv : g_windows)
      json += (json.size() > 1 ? "," : "") + json_escape(kv.first);
    json += "]";
  } else if (what == "win" || what.rfind("win:", 0) == 0) {
    HWND h = hwnd_for_win(what == "win" ? "main" : what.substr(4));
    if (h)
      json = win_state_json(h);
  } else if (what == "clipboard" || what == "clipboard:count") {
    json = clipboard_json(what == "clipboard:count");
  } else if (what == "mouse" || what.rfind("mouse:", 0) == 0) {
    HWND target = hwnd_for_win(what == "mouse" ? "main" : what.substr(6));
    if (!target)
      target = g_host;
    POINT p;
    GetCursorPos(&p);
    POINT c = p;
    ScreenToClient(target, &c);
    RECT cr;
    GetClientRect(target, &cr);
    bool inside = c.x >= 0 && c.y >= 0 && c.x < cr.right && c.y < cr.bottom;
    std::vector<ScreenLayout> lay = screen_layout();
    const ScreenLayout *on = layout_at_px(lay, p);
    double ws = window_scale(target), gx = p.x, gy = p.y;
    if (on)
      px_to_dip(*on, p.x, p.y, &gx, &gy);
    char buf[384];
    std::snprintf(
        buf, sizeof(buf),
        "{\"x\":%ld,\"y\":%ld,\"window\":{\"x\":%ld,\"y\":%ld,\"inside\":%s},"
        "\"screen\":{\"id\":%d,\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
        "\"scale\":%.2f}}",
        lround(gx), lround(gy), (long)lround(c.x / ws), (long)lround(c.y / ws),
        inside ? "true" : "false", on ? (int)(on - lay.data()) : 0,
        on ? on->dx : 0L, on ? on->dy : 0L, on ? on->dw : 0L, on ? on->dh : 0L,
        on ? on->scale : 1.0);
    json = buf;
  } else if (what == "screens") {
    json = screens_json();
  } else if (what == "fgscreen") {
    // the monitor holding the focused window of ANY app; the cursor's when
    // nothing is focused (desktop)
    std::vector<ScreenLayout> lay = screen_layout();
    HWND fg = GetForegroundWindow();
    POINT cp;
    GetCursorPos(&cp);
    int i = fg ? layout_index(lay, MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST))
               : layout_index(lay, MonitorFromPoint(cp, MONITOR_DEFAULTTONEAREST));
    json = i < 0 ? "null" : std::to_string(i);
  } else if (what == "idle") {
    LASTINPUTINFO li = {sizeof(li)};
    if (GetLastInputInfo(&li)) {
      double s = (GetTickCount() - li.dwTime) / 1000.0;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "{\"seconds\":%.3f}", s);
      json = buf;
    }
  } else if (what == "battery") {
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps) && !(ps.BatteryFlag & 128) &&
        ps.BatteryFlag != 255) {
      std::string mins = ps.BatteryLifeTime == (DWORD)-1
                             ? "null"
                             : std::to_string(ps.BatteryLifeTime / 60);
      json = "{\"percent\":" +
             (ps.BatteryLifePercent == 255
                  ? std::string("null")
                  : std::to_string((int)ps.BatteryLifePercent)) +
             ",\"charging\":" + ((ps.BatteryFlag & 8) ? "true" : "false") +
             ",\"plugged\":" + (ps.ACLineStatus == 1 ? "true" : "false") +
             ",\"minutesRemaining\":" + mins + "}";
    }
  } else if (what == "frontmost") {
    HWND fg = GetForegroundWindow();
    if (fg) {
      DWORD pid = 0;
      GetWindowThreadProcessId(fg, &pid);
      std::string name = "null";
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (h) {
        wchar_t buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
          std::string full = narrow(buf);
          size_t slash = full.find_last_of("\\/");
          std::string base =
              slash == std::string::npos ? full : full.substr(slash + 1);
          size_t dot = base.rfind(".exe");
          if (dot != std::string::npos)
            base = base.substr(0, dot);
          name = json_escape(base);
        }
        CloseHandle(h);
      }
      json = "{\"name\":" + name + ",\"bundleId\":null,\"pid\":" +
             std::to_string(pid) + "}";
    }
  } else if (what == "traypos") {
    if (g_tray_added) {
      NOTIFYICONIDENTIFIER nii = {sizeof(nii)};
      nii.hWnd = g_host;
      nii.uID = 1;
      RECT r;
      if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &r))) {
        POINT tp = {r.left, r.top};
        std::vector<ScreenLayout> lay = screen_layout();
        const ScreenLayout *on = layout_at_px(lay, tp);
        double ms = on ? on->scale : 1.0, gx = r.left / ms, gy = r.top / ms;
        if (on)
          px_to_dip(*on, r.left, r.top, &gx, &gy);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld}",
                      lround(gx), lround(gy),
                      (long)lround((r.right - r.left) / ms),
                      (long)lround((r.bottom - r.top) / ms));
        json = buf;
      }
    }
  } else if (what.rfind("item:", 0) == 0 || what.rfind("item@", 0) == 0) {
    // item:<id> — any copy (tray/context items live here too). item@<win>:<id>
    // — that window's copy. Window ids don't contain ':' (win.open picks
    // them), so the first colon after '@' ends the window name.
    std::string id = what.substr(5);
    HWND only = nullptr;
    if (what[4] == '@') {
      size_t colon = id.find(':');
      only = hwnd_for_win(colon == std::string::npos ? id : id.substr(0, colon));
      id = colon == std::string::npos ? "" : id.substr(colon + 1);
    }
    ItemReg *found = nullptr;
    auto range = g_id_reg.equal_range(id);
    for (auto i = range.first; i != range.second && !found; ++i)
      if (!only || i->second->owner == only)
        found = i->second;
    if (found) {
      ItemReg *reg = found;
      json = "{\"exists\":true,\"label\":" + json_escape(reg->label) +
             ",\"checked\":" + (reg->checked ? "true" : "false") +
             ",\"enabled\":" + (reg->enabled ? "true" : "false") + "}";
    } else {
      json = "{\"exists\":false}";
    }
  }
  // wifi/selectedtext/otherwindows/debug:* -> null
  got(req->qid, json);
  delete req;
}

// ---------------------------------------------------------------------------
// context menu (WebView2 ContextMenuRequested; falls back to default menus)

struct CtxHandler : public ICoreWebView2ContextMenuRequestedEventHandler {
  ULONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2ContextMenuRequestedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2ContextMenuRequestedEventArgs *args) override {
    if (g_ctx_menu) {
      args->put_Handled(TRUE);
      tray_popup(g_ctx_menu, "CTX");
      return S_OK;
    }
    if (g_ctx_suppress)
      args->put_Handled(TRUE);
    return S_OK;
  }
};

static bool g_ctx_intercept = false; // ContextMenuRequested available?

static void install_ctx_handler() {
  if (!g_wv2)
    return;
  ICoreWebView2_11 *wv11 = nullptr;
  if (SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_11, (void **)&wv11)) &&
      wv11) {
    EventRegistrationToken tok;
    wv11->add_ContextMenuRequested(new CtxHandler(), &tok);
    wv11->Release();
    g_ctx_intercept = true;
  }
}

// Runtimes older than ICoreWebView2_11 can't intercept the menu, but the
// settings toggle still suppresses the default one for contextMenu:false.
static void apply_ctx_suppress_fallback(bool suppress) {
  if (g_ctx_intercept || !g_wv2)
    return;
  ICoreWebView2Settings *settings = nullptr;
  if (SUCCEEDED(g_wv2->get_Settings(&settings)) && settings) {
    settings->put_AreDefaultContextMenusEnabled(suppress ? FALSE : TRUE);
    settings->Release();
  }
}

// Devtools + browser-accelerator policy, per webview: main in run(), each
// secondary in SecCtrlHandler::Invoke. Settings3 arrived in runtime 1.0.864
// (2021); on anything older the QI fails and the engine keys stay enabled —
// old behavior, not breakage. Menu accelerators are unaffected either way:
// they ride AcceleratorKeyPressed, which Settings3 does not touch.
// Custom User-Agent (TINYJS_UA, createApp's userAgent) for any webview: the
// main one in run(), and every secondary and popup through
// apply_webview_policy — which used to skip it, so a wrapped site's sign-in
// popup went out with the stock UA (the macOS launcher sets it on popups).
static void apply_user_agent(ICoreWebView2 *wv) {
  static std::wstring ua = []() {
    char buf[2048];
    DWORD n = GetEnvironmentVariableA("TINYJS_UA", buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) ? widen(std::string(buf, n)) : std::wstring();
  }();
  if (!wv || ua.empty())
    return;
  ICoreWebView2Settings *settings = nullptr;
  if (SUCCEEDED(wv->get_Settings(&settings)) && settings) {
    ICoreWebView2Settings2 *s2 = nullptr;
    if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings2,
                                           (void **)&s2)) &&
        s2) {
      s2->put_UserAgent(ua.c_str());
      s2->Release();
    }
    settings->Release();
  }
}

static void apply_webview_policy(ICoreWebView2 *wv) {
  if (!wv)
    return;
  apply_user_agent(wv);
  ICoreWebView2Settings *settings = nullptr;
  if (FAILED(wv->get_Settings(&settings)) || !settings)
    return;
  settings->put_AreDevToolsEnabled(g_debug ? TRUE : FALSE);
  ICoreWebView2Settings3 *s3 = nullptr;
  if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings3,
                                         (void **)&s3)) &&
      s3) {
    s3->put_AreBrowserAcceleratorKeysEnabled(g_browser_accel ? TRUE : FALSE);
    s3->Release();
  }
  settings->Release();
}

struct CtxReq {
  std::vector<MenuItemSpec> items;
  bool set = false;
};

static void apply_ctx(webview_t, void *arg) {
  CtxReq *req = static_cast<CtxReq *>(arg);
  clear_registry("ctx");
  if (g_ctx_menu) {
    DestroyMenu(g_ctx_menu);
    g_ctx_menu = nullptr;
  }
  if (req->set && !req->items.empty()) {
    g_ctx_menu = CreatePopupMenu();
    build_menu_items(g_ctx_menu, req->items, "ctx");
  }
  delete req;
}

// ---------------------------------------------------------------------------
// multi-window — secondary windows host their own WebView2 controller (from
// the main webview's environment) with the same injected bridge; page-call
// ids are "<winid>:<seq>" and resolve via __tinyResolve, exactly like the
// macOS launcher. The main window keeps webview_bind/webview_return.

struct TinyWin {
  HWND hwnd = nullptr;
  ICoreWebView2Controller *ctrl = nullptr;
  ICoreWebView2 *wv = nullptr;
  bool transparent = false;            // clear WebView2 background on create
  bool frameless = false;              // no caption -> client covers the frame
  int min_w = 0, min_h = 0;            // win.setMinSize, logical px (0 = none)
  std::string url;                     // navigated once the controller exists
  std::vector<std::string> pending_js; // eval'd once the controller exists
  WinMenu menu;                        // this window's bar (see WinMenu)
};

static TinyWin *win_for_id(const std::string &id) {
  auto it = g_windows.find(id);
  return it == g_windows.end() ? nullptr : it->second;
}

static ICoreWebView2 *wv2_for_id(const std::string &id) {
  if (id.empty() || id == "main") return g_wv2;
  TinyWin *tw = win_for_id(id);
  return tw ? tw->wv : nullptr;
}

static void set_min_size(const std::string &id, int w, int h) {
  if (id.empty() || id == "main") {
    g_min_w = w;
    g_min_h = h;
    return;
  }
  TinyWin *tw = win_for_id(id);
  if (tw) {
    tw->min_w = w;
    tw->min_h = h;
  }
}

// setMinSize is "a floor under USER resizes" (bridge.js) — an explicit
// win.setSize from the app must still be obeyed. WM_GETMINMAXINFO can't tell
// the two apart, so do_size raises this around its SetWindowPos; the message
// is delivered synchronously from inside that call, on this same thread.
// Without it, a window whose min IS its design size can never be collapsed by
// its own code — amp's satellites windowshaded their content away but the
// frame refused to shrink, while main (which sets no floor) was fine.
// (g_programmatic_size itself is declared up with the window globals.)

// Clamp a MINMAXINFO to a logical-px floor. ptMinTrackSize is the OUTER window
// size in physical px, matching what setSize/getState now speak.
static void clamp_min_track(HWND hwnd, LPARAM lp, int min_w, int min_h) {
  if (g_programmatic_size)
    return;
  if (min_w <= 0 && min_h <= 0)
    return;
  double sc = window_scale(hwnd);
  MINMAXINFO *mmi = (MINMAXINFO *)lp;
  if (min_w > 0)
    mmi->ptMinTrackSize.x = (LONG)lround(min_w * sc);
  if (min_h > 0)
    mmi->ptMinTrackSize.y = (LONG)lround(min_h * sc);
}

static HWND hwnd_for_win(const std::string &id) {
  if (id.empty() || id == "main")
    return g_hwnd;
  TinyWin *tw = win_for_id(id);
  return tw ? tw->hwnd : nullptr;
}

static std::string id_for_hwnd(HWND h) {
  for (auto &kv : g_windows)
    if (kv.second->hwnd == h)
      return kv.first;
  return "";
}

// --- window state events ----------------------------------------------------
// WINSTATE <id> {"fullscreen":b,"maximized":b,"minimized":b,"focused":b} —
// deduped per window, because WM_SIZE fires the whole way through a drag
// resize and only the transitions belong on the wire. Fullscreen is the
// launcher-managed main-window kind (set_fullscreen); a secondary's
// "fullscreen" op maximizes, so it reports as maximized — honest, since
// that's what the window actually did.
static std::map<std::string, std::string> g_last_winstate;

static void emit_winstate(HWND hwnd, int focused) {
  if (!hwnd)
    return;
  std::string id = hwnd == g_hwnd ? "main" : id_for_hwnd(hwnd);
  if (id.empty())
    return;
  bool fs = hwnd == g_hwnd && g_fullscreen;
  bool foc = focused >= 0 ? focused != 0 : GetForegroundWindow() == hwnd;
  auto b = [](bool v) { return v ? "true" : "false"; };
  std::string s = std::string("{\"fullscreen\":") + b(fs) +
                  ",\"maximized\":" + b(!fs && !!IsZoomed(hwnd)) +
                  ",\"minimized\":" + b(!!IsIconic(hwnd)) +
                  ",\"focused\":" + b(foc) +
                  ",\"screenId\":" +
                  std::to_string(layout_index(
                      screen_layout(), MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST))) +
                  "}";
  auto it = g_last_winstate.find(id);
  if (it != g_last_winstate.end() && it->second == s)
    return;
  g_last_winstate[id] = s;
  pipe_write_line("WINSTATE " + id + " " + s);
}

// A user drag/resize reports once, when the modal size-move loop ends;
// programmatic moves report as they happen (the bridge debounces both).
static HWND g_sizemove_hwnd = nullptr;
static void emit_geom(HWND hwnd) {
  if (!g_geom_events || !hwnd || hwnd == g_sizemove_hwnd)
    return;
  std::string id = hwnd == g_hwnd ? "main" : id_for_hwnd(hwnd);
  if (!id.empty())
    pipe_write_line("WINGEOM " + id);
}

// Shared by main's and the secondaries' procs.
static void geom_msg(HWND hwnd, UINT msg) {
  switch (msg) {
  case WM_ENTERSIZEMOVE:
    g_sizemove_hwnd = hwnd;
    break;
  case WM_EXITSIZEMOVE:
    g_sizemove_hwnd = nullptr;
    emit_geom(hwnd);
    break;
  case WM_MOVE:
    emit_winstate(hwnd); // deduped: only a change of monitor gets out
    emit_geom(hwnd);
    break;
  case WM_SIZE:
    emit_geom(hwnd);
    break;
  }
}

static ICoreWebView2Controller *ctrl_for_win(const std::string &id) {
  if (id.empty() || id == "main")
    return g_ctrl;
  TinyWin *tw = win_for_id(id);
  return tw ? tw->ctrl : nullptr;
}

// --- menu-bar resolution (declared up in the menu section) -----------------

static WinMenu *menu_for(HWND hwnd) {
  if (!hwnd)
    return nullptr;
  if (hwnd == g_hwnd)
    return &g_main_menu;
  for (auto &kv : g_windows)
    if (kv.second->hwnd == hwnd)
      return &kv.second->menu;
  return nullptr;
}

static std::vector<HWND> menu_windows() {
  std::vector<HWND> out;
  if (g_hwnd)
    out.push_back(g_hwnd);
  for (auto &kv : g_windows)
    if (kv.second->hwnd)
      out.push_back(kv.second->hwnd);
  return out;
}

// setHideOnClose is a macOS idea: there an app outlives its last window and
// the Dock icon brings it back. Windows has nowhere to put that — a hidden
// window takes its taskbar button with it — so honouring the flag with
// nothing else on screen leaves a process the user can neither see nor quit.
// Hide only when there IS a way back: a tray icon, accessory mode, or another
// window still up. Otherwise the close means what it means everywhere else on
// this OS, and the app exits.
//
// Deliberately only consulted for a USER close of the hide-on-close window.
// A programmatic win.hide() still hides whatever it is told to, and the last
// SECONDARY closing is left alone — an app that answers that by showing its
// main window again (nib brings the Welcome screen back) would otherwise be
// killed in the gap before it could.
static bool can_live_hidden() {
  if (g_accessory || g_tray_added)
    return true;
  for (auto &kv : g_windows)
    if (kv.second->hwnd && IsWindowVisible(kv.second->hwnd))
      return true;
  return false;
}

static bool menu_allowed(HWND hwnd) {
  if (!hwnd)
    return false;
  // No caption, no bar. A frameless (or squareCorners) window answers
  // WM_NCCALCSIZE with "the client IS the window", so there is no non-client
  // strip for GDI to draw a menu in — asked anyway, Win32 takes the row out
  // of the page instead. Read the live style rather than the creation flag:
  // a setChrome({ frame: false }) later is the same window either way.
  if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION))
    return false;
  if (hwnd == g_hwnd)
    return !g_main_transparent;
  for (auto &kv : g_windows)
    if (kv.second->hwnd == hwnd)
      return !kv.second->transparent;
  return false;
}

// Windowless app.hide()/app.show(): there's no main window to hide, so hide
// the app — every visible named window — and bring exactly those back on
// show (macOS hides/unhides NSApp here, Linux does this). The other
// main-window ops have nothing to act on, bar the app-wide rescue switch.
static std::vector<std::string> g_app_hidden;

static void windowless_app_op(const std::string &op) {
  // Only the app-wide hide: hide({ app: false }) ("hidewin") targets the main
  // window, and there isn't one — a no-op, as on macOS and Linux.
  if (op == "hide") {
    for (auto &kv : g_windows)
      if (kv.second->hwnd && IsWindowVisible(kv.second->hwnd)) {
        g_app_hidden.push_back(kv.first);
        ShowWindow(kv.second->hwnd, SW_HIDE);
      }
  } else if (op.rfind("show", 0) == 0) {
    bool activate = op != "show 0";
    HWND last = nullptr;
    for (auto &id : g_app_hidden)
      if (TinyWin *tw = win_for_id(id)) {
        ShowWindow(tw->hwnd, activate ? SW_SHOW : SW_SHOWNA);
        last = tw->hwnd;
      }
    if (activate && last)
      SetForegroundWindow(last);
    g_app_hidden.clear();
  } else if (op.rfind("rescue ", 0) == 0) {
    g_rescue_on = op.substr(7) == "1";
  }
}

static void set_win_transparent_flag(const std::string &id, bool on) {
  TinyWin *tw = win_for_id(id);
  if (tw)
    tw->transparent = on;
}

static void secwin_eval(const std::string &id, const std::string &js) {
  TinyWin *tw = win_for_id(id);
  if (!tw)
    return;
  if (tw->wv)
    tw->wv->ExecuteScript(widen(js).c_str(), nullptr);
  else
    tw->pending_js.push_back(js);
}

// Resolve a page call by composite id: "<seq-only>" = a main-window
// webview_bind id (webview_return), "<winid>:<seq>" = a secondary window
// (evaluate __tinyResolve there). Dialog replies route here too.
static void route_ret(webview_t w, const std::string &composite, int status,
                      const std::string &json) {
  // q<digits>: a dialog the BACKEND opened (#36, a page printToPDF's save
  // panel) — the answer goes back to it as a read-back, not into a page.
  if (composite.size() > 1 && composite[0] == 'q' &&
      composite.find_first_not_of("0123456789", 1) == std::string::npos) {
    pipe_write_line("GOT " + composite.substr(1) + " " + json);
    return;
  }
  size_t c = composite.find(':');
  if (c == std::string::npos) {
    if (w)
      webview_return(w, composite.c_str(), status == 0 ? 0 : 1, json.c_str());
    return;
  }
  std::string winid = composite.substr(0, c);
  std::string seq = composite.substr(c + 1);
  if (seq.empty() || seq.find_first_not_of("0123456789") != std::string::npos)
    return;
  secwin_eval(winid, "window.__tinyResolve(" + seq + "," +
                         (status == 0 ? "true" : "false") + "," +
                         json_escape(json) + ")");
}

static std::string sec_shim_js(const std::string &winid, bool frameless) {
  // A frameless secondary has no reachable resize edge: WM_NCCALCSIZE leaves
  // no non-client area and WebView2's child HWNDs — another process — cover
  // the whole window rect, so WindowFromPoint never resolves to us and our
  // WM_NCHITTEST is never asked. WS_THICKFRAME is inert for grabbing; the
  // page has to provide the grips. Same story as an undecorated GTK window,
  // so tell the client the same way (launcher-linux.cc). The main window is
  // NOT marked: it keeps real left/right/bottom borders (tiny_wndproc's
  // WM_NCCALCSIZE reclaims only the top), and 5px grips over a ~6px native
  // border would just make an 11px band with two different latencies.
  // The value only marks the host — the page is injected before any later
  // CHROME op, so tiny.js re-asks the window itself via win.getState.
  return "(() => {"
         "if (window.__tinyShim) return; window.__tinyShim = true;"
         "window.__TINY_WIN = '" + winid + "';"
         "window.__TINY_FRAMELESS = " + (frameless ? "true" : "false") + ";"
         // capture at document-start: page code can shadow window.chrome
         // (e.g. a global `function chrome(){}`) and a lazy read would break
         "const post = window.chrome.webview.postMessage.bind(window.chrome.webview);"
         "let seq = 0; const pending = {};"
         "window.__invoke = (payload) => new Promise((res, rej) => {"
         "  const s = ++seq; pending[s] = { res, rej };"
         "  post(String(s) + ':' + String(payload));"
         "});"
         "window.__tinyResolve = (s, ok, jsonText) => {"
         "  const p = pending[s]; if (!p) return; delete pending[s];"
         "  let v = null; try { v = JSON.parse(jsonText); } catch (e) {}"
         "  ok ? p.res(v) : p.rej(v);"
         "};"
         "})();";
}

// Browser affordances for wrapped sites (defined below do_winclose): NAV
// events + policy, downloads, JS dialogs, window.open popups, per-frame
// origin stamping. One call wires a webview; main and every secondary get it.
static void install_browser_affordances(ICoreWebView2 *wv,
                                        const std::string &winid);
static std::string origin_from_uri(const std::string &uri);

struct SecMsgHandler : public ICoreWebView2WebMessageReceivedEventHandler {
  ULONG refs = 1;
  std::string winid;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2WebMessageReceivedEventArgs *args) override {
    LPWSTR s = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&s)) || !s)
      return S_OK;
    std::string body = narrow(s);
    CoTaskMemFree(s);
    size_t c = body.find(':');
    if (c == std::string::npos)
      return S_OK;
    {
      // The seq is the page's own counter and round-trips in RET, which only
      // accepts digits. Anything else isn't our client talking (#30.6).
      std::string seq = body.substr(0, c);
      if (seq.empty() || seq.size() > 15 ||
          seq.find_first_not_of("0123456789") != std::string::npos)
        return S_OK;
    }
    // Second array element: the calling document's origin, from the
    // WebView2-attested message Source (a hostile page can't spoof it).
    // The bridge's "api" gate keys origin sub-gates on it.
    std::string origin = "null";
    LPWSTR src = nullptr;
    if (SUCCEEDED(args->get_Source(&src)) && src) {
      origin = origin_from_uri(narrow(src));
      CoTaskMemFree(src);
    }
    pipe_write_line("CALL " + winid + ":" + body.substr(0, c) + " [" +
                    json_escape(body.substr(c + 1)) + "," +
                    json_escape(origin) + "]");
    return S_OK;
  }
};

struct SecCtrlHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
  ULONG refs = 1;
  std::string winid;
  // Both carried here: the tw lookup can race a close/reopen storm and must
  // not be what decides them.
  bool transparent = false;
  bool frameless = false;
  // window.open popups (site wrappers): the completion hands the new webview
  // back to WebView2 via put_NewWindow instead of navigating, and completes
  // the held deferral — WebView2 then drives the load itself, preserving
  // window.opener / postMessage for OAuth popups.
  ICoreWebView2NewWindowRequestedEventArgs *popup_args = nullptr;
  ICoreWebView2Deferral *popup_deferral = nullptr;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  // A failed creation must still complete a held popup deferral, or WebView2
  // waits on it forever (and the args leak).
  // put_Handled(TRUE) FIRST, and that ordering is the whole point: a deferral
  // completed with Handled still FALSE means WebView2's documented fallback
  // fires and it opens ITS OWN browser window — so a 'deny' verdict that beat
  // controller creation used to show the user a bare browser window. Invisible
  // to any page-side assertion; count top-level windows to see it.
  void abandon_popup() {
    if (popup_args)
      popup_args->put_Handled(TRUE);
    if (popup_deferral) {
      popup_deferral->Complete();
      popup_deferral->Release();
      popup_deferral = nullptr;
    }
    if (popup_args) {
      popup_args->Release();
      popup_args = nullptr;
    }
  }
  // Safety net: if WebView2 drops its reference without ever invoking us (a
  // creation that neither succeeds nor fails), window.open() would otherwise
  // hang on the deferral forever.
  ~SecCtrlHandler() { abandon_popup(); }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Controller *ctrl) override {
    TinyWin *tw = win_for_id(winid);
    if (!tw || FAILED(hr) || !ctrl) {
      // The window went away under us — a 'deny' verdict that closed the
      // hidden popup before creation finished takes exactly this path. Close
      // the controller too: it and its renderer process are otherwise orphaned
      // on a destroyed HWND for the life of the app.
      if (ctrl)
        ctrl->Close();
      abandon_popup();
      return S_OK;
    }
    ctrl->AddRef();
    tw->ctrl = ctrl;
    ctrl->get_CoreWebView2(&tw->wv);
    RECT rc;
    GetClientRect(tw->hwnd, &rc);
    ctrl->put_Bounds(rc);
    // show() can beat controller creation on a show: false window; the
    // WM_WINDOWPOSCHANGED catch-up in secwin_proc had no controller to wake.
    if (IsWindowVisible(tw->hwnd))
      ctrl->put_IsVisible(TRUE);
    {
      // The chrome 'transparent' flag: without this only the MAIN window
      // honored it and secondary pet/overlay windows painted white.
      ICoreWebView2Controller2 *c2 = nullptr;
      HRESULT qi = ctrl->QueryInterface(IID_ICoreWebView2Controller2,
                                        (void **)&c2);
      HRESULT hr = E_ABORT;
      if ((transparent || tw->transparent) && SUCCEEDED(qi) && c2) {
        COREWEBVIEW2_COLOR clear = {0, 0, 0, 0};
        hr = c2->put_DefaultBackgroundColor(clear);
      }
      drag_dbg("secwin '" + winid + "' transparent=" +
               std::to_string(transparent || tw->transparent) +
               " qi=" + std::to_string((long)qi) +
               " setbg=" + std::to_string((long)hr));
      if (c2) c2->Release();
    }
    if (tw->wv) {
      tw->wv->AddScriptToExecuteOnDocumentCreated(
          widen(sec_shim_js(winid, frameless || tw->frameless)).c_str(),
          nullptr);
      tw->wv->AddScriptToExecuteOnDocumentCreated(widen(TINY_CLIENT_JS).c_str(),
                                                  nullptr);
      DWORD need = GetEnvironmentVariableA("TINYJS_INJECT", nullptr, 0);
      if (need > 0) {
        std::vector<char> inj(need + 1);
        DWORD n = GetEnvironmentVariableA("TINYJS_INJECT", inj.data(),
                                          (DWORD)inj.size());
        if (n > 0 && n < inj.size())
          tw->wv->AddScriptToExecuteOnDocumentCreated(widen(inj.data()).c_str(),
                                                      nullptr);
      }
      SecMsgHandler *mh = new SecMsgHandler();
      mh->winid = winid;
      EventRegistrationToken tok;
      tw->wv->add_WebMessageReceived(mh, &tok);
      mh->Release();
      // Custom context menus + contextMenu:false must work in EVERY window,
      // not just main (the ctx state is global, matching macOS).
      ICoreWebView2_11 *wv11 = nullptr;
      if (SUCCEEDED(tw->wv->QueryInterface(IID_ICoreWebView2_11,
                                           (void **)&wv11)) && wv11) {
        EventRegistrationToken ctok;
        wv11->add_ContextMenuRequested(new CtxHandler(), &ctok);
        wv11->Release();
      }
      // …and the rest of the per-webview wiring main gets: UNC media
      // untainting (satellite viz analysing network-share tracks), drop-in
      // file forwarding, and Ctrl+key menu accelerators.
      install_unc_for(tw->wv);
      tw->wv->AddScriptToExecuteOnDocumentCreated(widen(DROP_FORWARD_JS).c_str(),
                                                  nullptr);
      DropMsgHandler *dh = new DropMsgHandler();
      EventRegistrationToken dtok;
      tw->wv->add_WebMessageReceived(dh, &dtok);
      dh->Release();
      EventRegistrationToken atok;
      AccelHandler *ah = new AccelHandler();
      ah->winid = winid;
      ctrl->add_AcceleratorKeyPressed(ah, &atok);
      ah->Release();
      apply_webview_policy(tw->wv);
      // Browser affordances (dialogs, downloads, nav policy, popups) must be
      // registered BEFORE the first navigation or they miss it.
      install_browser_affordances(tw->wv, winid);
      // Hand the keyboard to the page. The host HWND has focus, but WebView2's
      // child does not until something moves it there — so a brand-new window
      // answered document.hasFocus() with FALSE until the user clicked inside
      // it, and anything the app gates on that (nib drops every page-side menu
      // action for "someone else's window") silently did nothing. macOS makes
      // the webview first responder on its own; here it has to be asked.
      ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
      if (popup_args) {
        // window.open popup: hand WebView2 the webview and let IT navigate —
        // an explicit Navigate here would sever window.opener.
        popup_args->put_NewWindow(tw->wv);
        apply_user_agent(tw->wv);
        popup_args->put_Handled(TRUE);
        if (popup_deferral) {
          popup_deferral->Complete();
          popup_deferral->Release();
          popup_deferral = nullptr;
        }
        popup_args->Release();
        popup_args = nullptr;
      } else {
        tw->wv->Navigate(widen(tw->url).c_str());
      }
      for (auto &js : tw->pending_js)
        tw->wv->ExecuteScript(widen(js).c_str(), nullptr);
      tw->pending_js.clear();
      if (g_debug_open)
        tw->wv->OpenDevToolsWindow();
    }
    abandon_popup(); // no-op when the popup path consumed them above
    return S_OK;
  }
};

static std::map<HWND, UINT> g_secwin_old_dpi; // WM_GETDPISCALEDSIZE -> WM_DPICHANGED

// Resize so the CLIENT (the page's box) is exactly cw x ch physical pixels,
// whatever the frame and menu row measure on this monitor. Top-left stays.
static void fit_client(HWND hwnd, int cw, int ch) {
  RECT o, c;
  GetWindowRect(hwnd, &o);
  GetClientRect(hwnd, &c);
  if (c.right == cw && c.bottom == ch)
    return;
  SetWindowPos(hwnd, nullptr, 0, 0, (o.right - o.left) + cw - c.right,
               (o.bottom - o.top) + ch - c.bottom,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK secwin_proc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp) {
  switch (msg) {
  case WM_NCCALCSIZE: {
    // Frameless: the client fills the whole window, matching the main window
    // and macOS (where a borderless window's content IS its frame). Only the
    // inset goes away; WS_THICKFRAME stays, but NOT for edge hit-testing —
    // WebView2's child HWNDs cover every client pixel from another process,
    // so WindowFromPoint never lands on us and this proc's WM_NCHITTEST is
    // never asked. The bit is kept because it is what DefWindowProc gates
    // SC_SIZE on (so win.startResize works) and what getState reports as
    // `resizable`. Believing it bought hit-testing is what hid the fact that
    // frameless satellites had NO grabbable edge on Windows at all — the
    // page's grips are the whole story here (see sec_shim_js).
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (wp && tw && tw->frameless)
      return 0;
    break;
  }
  case WM_GETMINMAXINFO: {
    // let the default fill in the usual limits, then lower-bound them
    LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (tw)
      clamp_min_track(hwnd, lp, tw->min_w, tw->min_h);
    return r;
  }
  case WM_SIZE: {
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (tw && tw->ctrl) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      tw->ctrl->put_Bounds(rc);
    }
    emit_winstate(hwnd); // maximize/minimize/restore land here
    geom_msg(hwnd, msg);
    break;
  }
  case WM_WINDOWPOSCHANGED: {
    // A controller created on a never-shown HWND (win.open show: false, #102)
    // stays IsVisible = FALSE through the window's first show: the frame came
    // up black with document.visibilityState still 'hidden'. Any show route
    // (WINOP show, the windowless app show, a maximize) passes through here.
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (tw && tw->ctrl && (((WINDOWPOS *)lp)->flags & SWP_SHOWWINDOW))
      tw->ctrl->put_IsVisible(TRUE);
    break; // DefWindowProc still turns it into WM_SIZE / WM_MOVE
  }
  case WM_MOVE:
  case WM_ENTERSIZEMOVE:
  case WM_EXITSIZEMOVE:
    geom_msg(hwnd, msg);
    break;
  // Keep a secondary's size in DIPs across a DPI change, as main's vendored
  // handler does. Without this one kept its PHYSICAL size: 1568x1060 on a
  // 100% screen came out 1045x707 on a 150% one (#70). The old DPI is only
  // readable here, before the change; WM_DPICHANGED follows at once.
  case 0x02E4 /* WM_GETDPISCALEDSIZE */:
    g_secwin_old_dpi[hwnd] = (UINT)lround(window_scale(hwnd) * 96);
    return FALSE; // let Windows suggest; WM_DPICHANGED sets the real size
  case 0x02E0 /* WM_DPICHANGED */: {
    const RECT *sug = (const RECT *)lp;
    UINT nd = HIWORD(wp);
    auto it = g_secwin_old_dpi.find(hwnd);
    UINT od = it != g_secwin_old_dpi.end() ? it->second : 0;
    if (it != g_secwin_old_dpi.end())
      g_secwin_old_dpi.erase(it);
    RECT c;
    GetClientRect(hwnd, &c);
    SetWindowPos(hwnd, nullptr, sug->left, sug->top, sug->right - sug->left,
                 sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
    // the frame doesn't scale linearly with DPI: set the page box exactly
    if (od && nd) {
      double k = (double)nd / od;
      fit_client(hwnd, (int)lround(c.right * k), (int)lround(c.bottom * k));
    }
    return 0;
  }
  case WM_ACTIVATE:
    emit_winstate(hwnd, LOWORD(wp) != WA_INACTIVE);
    break;
  case WM_SETFOCUS: {
    // Activation lands on the host window; pass it down to the page, or the
    // page believes it is unfocused until clicked (see the MoveFocus note in
    // SecCtrlHandler). Alt-Tab and title-bar clicks come through here.
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (tw && tw->ctrl)
      tw->ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    break;
  }
  case WM_COMMAND: {
    // Same routing as the main window (tiny_wndproc): a secondary's bar is a
    // different HMENU carrying different command ints, but they resolve to
    // the same string ids, so the backend sees a plain `MENU <id>` from
    // whichever window the user clicked in — exactly what one shared macOS
    // bar reports.
    auto it = g_cmd_reg.find((UINT)LOWORD(wp));
    if (it != g_cmd_reg.end() && it->second->kind == "menu") {
      if (!it->second->role.empty())
        send_stock_edit(it->second->role);
      else if (it->second->enabled)
        pipe_write_line("MENU " + it->second->id);
      return 0;
    }
    break;
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY: {
    std::string id = id_for_hwnd(hwnd);
    if (!id.empty()) {
      TinyWin *tw = g_windows[id];
      // Drop this window's menu items before the map entry goes: their owner
      // hwnd is about to be a stale handle, and a later app-wide MENUUPD
      // would walk right into them.
      clear_registry("menu", hwnd);
      // Only if it isn't the attached one — DestroyWindow frees that itself.
      if (tw->menu.bar && GetMenu(hwnd) != tw->menu.bar)
        DestroyMenu(tw->menu.bar);
      tw->menu.bar = nullptr;
      g_windows.erase(id);
      g_last_winstate.erase(id);
      nav_forget_window(id);
      pipe_write_line("WINCLOSED " + id);
      if (tw->ctrl) {
        tw->ctrl->Close();
        tw->ctrl->Release();
      }
      if (tw->wv)
        tw->wv->Release();
      delete tw;
    }
    return 0;
  }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// -- the WebView2 environment named windows share ----------------------------
// Normally every named window borrows the main webview's environment. A
// windowless launcher has no main webview, so the first named window creates
// the environment — with exactly what the webview library's embed() passes
// (per-app profile, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS), so localStorage
// and cookies are the same whichever mode the app runs in. That first open is
// also the moment WebView2's processes start. Controllers asked for while it
// is being created wait here.
static ICoreWebView2Environment *g_env = nullptr;
struct EnvWaiter {
  HWND hwnd;
  SecCtrlHandler *ch;
};
static std::vector<EnvWaiter> g_env_waiters;
static bool g_env_creating = false;
static unsigned g_env_attempts = 0;

// embed()'s user data folder (see the tinyjs patch in win32_edge.hh): the
// bridge's TINYJS_WEBVIEW2_DATA, else stock's %APPDATA%\<exe name>.
static std::wstring webview2_data_dir() {
  wchar_t own[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"TINYJS_WEBVIEW2_DATA", own, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    SHCreateDirectoryExW(nullptr, own, nullptr);
    return own;
  }
  wchar_t exe[MAX_PATH], data[MAX_PATH], dir[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, data)))
    return L"";
  PathCombineW(dir, data, PathFindFileNameW(exe));
  return dir;
}

static void flush_env_waiters() {
  std::vector<EnvWaiter> waiters;
  waiters.swap(g_env_waiters);
  for (auto &w : waiters) {
    // The window may have closed (or closed and reopened as a new HWND)
    // while the environment was on its way.
    TinyWin *tw = win_for_id(w.ch->winid);
    if (g_env && tw && tw->hwnd == w.hwnd)
      g_env->CreateCoreWebView2Controller(w.hwnd, w.ch);
    w.ch->Release(); // a never-started popup completes its deferral here
  }
}

static void try_create_env();

// Retries mirror embed(): a profile still held by an exiting instance fails
// with ERROR_INVALID_STATE for a moment.
static void env_failed(HRESULT hr) {
  if (++g_env_attempts < 60) {
    SetTimer(nullptr, 0, 200, [](HWND, UINT, UINT_PTR t, DWORD) {
      KillTimer(nullptr, t);
      try_create_env();
    });
    return;
  }
  std::fprintf(stderr,
               "launcher: failed to create the WebView2 environment (0x%08lx; "
               "is the WebView2 runtime installed?)\n",
               (unsigned long)hr);
  g_env_creating = false;
  flush_env_waiters();
}

struct EnvCreatedHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
  ULONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1) return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Environment *env) override {
    if (FAILED(hr) || !env) {
      env_failed(FAILED(hr) ? hr : E_FAIL);
      return S_OK;
    }
    env->AddRef();
    g_env = env;
    g_env_creating = false;
    flush_env_waiters();
    return S_OK;
  }
};

static void try_create_env() {
  static webview::detail::mswebview2::loader loader;
  std::wstring dir = webview2_data_dir();
  EnvCreatedHandler *h = new EnvCreatedHandler();
  HRESULT hr = loader.create_environment_with_options(
      nullptr, dir.empty() ? nullptr : dir.c_str(),
      webview::detail::tinyjs_environment_options(), h);
  h->Release();
  if (FAILED(hr)) // no callback is coming
    env_failed(hr);
}

// Start a named window's controller: on the main webview's environment, on
// our own (windowless), or once ours exists. False = no environment can come.
static bool create_sec_controller(HWND hwnd, SecCtrlHandler *ch) {
  if (!g_env && g_wv2) {
    ICoreWebView2_2 *wv22 = nullptr;
    if (SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_2, (void **)&wv22)) &&
        wv22) {
      wv22->get_Environment(&g_env);
      wv22->Release();
    }
  }
  if (g_env)
    return SUCCEEDED(g_env->CreateCoreWebView2Controller(hwnd, ch));
  if (!g_windowless)
    return false;
  ch->AddRef();
  g_env_waiters.push_back({hwnd, ch});
  if (!g_env_creating) {
    g_env_creating = true;
    g_env_attempts = 0;
    try_create_env();
  }
  return true;
}

struct WinOpenReq {
  std::string id, page, title;
  int width = 600, height = 400;
  std::string frame, traffic, transparent, vibrancy, square, first_mouse, menu;
  bool hasPos = false;
  int x = 0, y = 0;
  std::string parent; // '' | 'main' | a win id: owner window (stays above it)
  bool hidden = false; // show: false — built + loaded, shown by WINOP show (#102)
};

static void ensure_secwin_class() {
  static bool registered = false;
  if (registered)
    return;
  WNDCLASSW wc = {};
  wc.lpfnWndProc = secwin_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"TinyjsSecondary";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr; // no erase — a brush paints white behind
                              // transparent webviews
  RegisterClassW(&wc);
  registered = true;
}

static void apply_app_icon(HWND hwnd); // below, with app.icon

static void do_winopen(webview_t, void *arg) {
  WinOpenReq *wr = static_cast<WinOpenReq *>(arg);
  // 'main' is reserved (the bridge throws): a secondary keyed "main" would
  // have every main-addressed op routed past it to g_wv2.
  if (wr->id.empty() || wr->id == "main") {
    delete wr;
    return;
  }
  if (TinyWin *ex = win_for_id(wr->id)) {
    ShowWindow(ex->hwnd, SW_SHOW);
    SetForegroundWindow(ex->hwnd);
    delete wr;
    return;
  }
  ensure_secwin_class();
  bool frameless = wr->frame == "0" || wr->square == "1";
  DWORD style = frameless ? (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                             WS_MAXIMIZEBOX | WS_SYSMENU)
                          : WS_OVERLAPPEDWINDOW;
  double sc = window_scale(g_hwnd); // logical wire units -> physical pixels
  // A position is a global DIP point (screen_layout); the window is sized in
  // the units of the monitor it will open on, not main's.
  POINT at = {CW_USEDEFAULT, CW_USEDEFAULT};
  if (wr->hasPos)
    at = dip_point_to_px(wr->x, wr->y, &sc);
  RECT rc = {0, 0, (LONG)lround(wr->width * sc), (LONG)lround(wr->height * sc)};
  // `size` is the page's box. Frameless secondaries answer WM_NCCALCSIZE with
  // "no non-client area" (see secwin_proc), exactly as the main window does, so
  // frame == page and there is nothing for AdjustWindowRect to add. Running it
  // anyway inflated the page by the resize border, which back when setSize
  // spoke frame units made a windowshaded satellite collapse to about half the
  // bar height it asked for, while main (already borderless-client) was right.
  if (!frameless)
    AdjustWindowRect(&rc, style, FALSE);
  int px = at.x;
  int py = at.y;
  if (wr->hasPos) {
    // AppKit-style constraint: never open a window fully off-screen (stale
    // saved positions — e.g. pre-DPI-fix physical coords — otherwise land
    // in the void). Keep at least a grabbable sliver on the virtual screen.
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const int margin = 60;
    if (px + w < vx + margin) px = vx + margin - w;
    if (px > vx + vw - margin) px = vx + vw - margin;
    if (py < vy) py = vy;
    if (py > vy + vh - margin) py = vy + vh - margin;
  }
  // Drop the GDI redirection bitmap for the windows that need it gone: with
  // one, alpha content that renders via DirectComposition (WebGL swapchains)
  // composites OPAQUE, and a setChrome({transparent}) AFTER creation leaves
  // the stale white GDI surface showing behind the cleared webview (macOS
  // apps set chrome late).
  //
  // Not for ALL of them any more, though — a window with no redirection
  // bitmap can't draw a Win32 menu bar either (the bar is GDI), and that
  // silently cost every secondary window its menu. Frameless and transparent
  // windows are the HUD/overlay/WebGL cases and never wanted a bar; a plain
  // titled window keeps its bitmap so a bar can appear whenever the app asks
  // for one. The tradeoff is the documented Windows rule, unchanged:
  // transparency has to be declared at open time, not set later.
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif
  DWORD exStyle = (wr->transparent == "1" || frameless)
                      ? WS_EX_NOREDIRECTIONBITMAP
                      : 0;
  // parent (win.open parent:) becomes the Win32 OWNER — for top-level windows
  // the hWndParent argument means owner, not parent. Owned windows stay above
  // their owner, minimize/hide with it, are destroyed with it, and get no
  // taskbar button of their own. Owner is create-time only (retrofitting via
  // GWLP_HWNDPARENT half-works and is officially discouraged), which is why
  // the bridge documents `parent` as open-time only.
  HWND owner = wr->parent.empty() ? nullptr : hwnd_for_win(wr->parent);
  HWND hwnd = CreateWindowExW(
      exStyle, L"TinyjsSecondary",
      widen(wr->title.empty() ? wr->id : wr->title).c_str(),
      style, px, py, rc.right - rc.left,
      rc.bottom - rc.top, owner, nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (!hwnd) {
    delete wr;
    return;
  }
  apply_relaunch_props(hwnd);
  if (g_windowless)
    apply_app_icon(hwnd);
  if (wr->square == "1") {
    DWORD pref = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));
  }
  TinyWin *tw = new TinyWin();
  tw->hwnd = hwnd;
  tw->transparent = wr->transparent == "1";
  tw->frameless = frameless;
  tw->menu.visible = wr->menu != "0";  // chrome.menu:false — see WinMenu
  bool is_url = wr->page.rfind("http://", 0) == 0 ||
                wr->page.rfind("https://", 0) == 0;
  tw->url = is_url ? wr->page : to_file_url(wr->page);
  g_windows[wr->id] = tw;
  // The first WM_NCCALCSIZE fired during CreateWindowExW, before this window
  // was in g_windows, so secwin_proc couldn't yet know it was frameless and
  // let the default insets stand. Re-ask now that the flag is readable.
  if (frameless)
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  // The app menu (or nothing, if the app never set one) goes on BEFORE the
  // first paint, so a new window neither flashes a bar into place nor opens
  // a menu-bar's-worth short: attach_menu hands the row back to the frame.
  render_menu(hwnd);
  // AdjustWindowRect above measured the frame at the SYSTEM DPI; on a monitor
  // of another scale the real frame differs and the page opened short
  // (600x400 asked on a 150% screen gave 596x389). Measure and correct.
  fit_client(hwnd, (int)lround(wr->width * sc), (int)lround(wr->height * sc));
  // show: false stays hidden (page still loads) until WINOP show. Owner
  // windows are set at create time, which shows nothing, so `parent` needs
  // no deferral here (unlike macOS's addChildWindow).
  if (!wr->hidden)
    ShowWindow(hwnd, SW_SHOW);
  SecCtrlHandler *ch = new SecCtrlHandler();
  ch->winid = wr->id;
  ch->transparent = tw->transparent;
  ch->frameless = tw->frameless;
  create_sec_controller(hwnd, ch);
  ch->Release();
  delete wr;
}

static void do_winclose(webview_t, void *arg) {
  std::string *id = static_cast<std::string *>(arg);
  HWND h = hwnd_for_win(*id);
  if (h && h != g_hwnd)
    PostMessageW(h, WM_CLOSE, 0, 0);
  delete id;
}

// ---------------------------------------------------------------------------
// browser affordances for wrapped sites — the WebView2 twin of the
// launcher-macos.cc section of the same name (which carries the canonical
// wire doc). Summary:
//   emit:  NAV {"window","kind","url","error"?}   start|commit|finish|fail|crash
//          NAVQ <qid> <winid>\t<url>\t<frame>     policy ask (frame main|''): main-frame
//                                                  http(s), or another scheme in any
//                                                  frame (unanswered = deny, #30)
//          DOWNLOAD {"id","url","filename","path","state","bytes"?,"total"?,"error"?}
//          POPUPQ <qid> <pid|->\t<opener>\t<url>\t<mode>
//          POPUP {"window","url","action"}
//   recv:  NAVR <qid> allow|deny|external
//          POPUPR <qid> window|external|deny
//          FIND <id> <term>\t<forward>\t<matchCase>   (call-style, no RET line:
//          STOPFIND <id>                               answered via route_ret)
// Everything here runs on the UI thread: WebView2 events, WM_TIMER timeouts,
// and the tiny_dispatch'd do_* handlers — no locking needed.
//
// One Windows-only divergence worth knowing: NavigationStarting has no
// deferral, so a policy ask CANCELS the navigation and re-Navigates on
// 'allow' (marked allow-once so it isn't re-asked). That turns an asked POST
// into a GET — macOS holds the original action and doesn't have this hole.

static std::string g_downloads_mode; // "" = auto | ask | deny (TINYJS_DOWNLOADS)
static std::string g_popup_mode;     // "" = external | window | deny (TINYJS_POPUPS)

// -- origin stamping ---------------------------------------------------------
// Reduce a document URI to its origin the way the macOS launcher builds it
// from WKSecurityOrigin: scheme://host[:port], default ports elided,
// bare "file://" for local pages, the literal string "null" when unknown.
static std::string origin_from_uri(const std::string &uri) {
  size_t p = uri.find("://");
  if (p == std::string::npos)
    return "null";
  std::string scheme = uri.substr(0, p);
  for (auto &c : scheme)
    c = (char)tolower((unsigned char)c);
  if (scheme == "file")
    return "file://";
  size_t hs = p + 3;
  size_t he = uri.find_first_of("/?#", hs);
  std::string hostport =
      he == std::string::npos ? uri.substr(hs) : uri.substr(hs, he - hs);
  if (hostport.empty())
    return scheme + "://";
  size_t cp = hostport.rfind(':');
  if (cp != std::string::npos && hostport.find(']', cp) == std::string::npos) {
    std::string port = hostport.substr(cp + 1);
    if (port == "0" || (scheme == "http" && port == "80") ||
        (scheme == "https" && port == "443"))
      hostport.erase(cp);
  }
  return scheme + "://" + hostport;
}

// -- NAV events --------------------------------------------------------------
// Only URLs a page could name get on the wire (internal schemes stay off it).
static bool nav_url_visible(const std::string &u) {
  return u.empty() || u == "about:blank" || u.rfind("http://", 0) == 0 ||
         u.rfind("https://", 0) == 0 || u.rfind("file://", 0) == 0;
}

static void nav_event(const std::string &win, const char *kind,
                      const std::string &url, const std::string &err = "") {
  if (!nav_url_visible(url))
    return;
  std::string j = "{\"window\":" + json_escape(win) + ",\"kind\":\"" + kind +
                  "\",\"url\":" + json_escape(url);
  if (!err.empty())
    j += ",\"error\":" + json_escape(err);
  pipe_write_line("NAV " + j + "}");
}

// The FAILING url for fail events — wv->get_Source() is the page still being
// looked at, so track what each window last STARTED loading instead (same
// distinction NSURLErrorFailingURLStringErrorKey carries on macOS).
static std::map<std::string, std::string> g_nav_last_url;

static std::string web_error_name(int st) {
  switch (st) {
  case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:
    return "host name not resolved";
  case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:
    return "cannot connect";
  case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED:
    return "internet disconnected";
  case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:
    return "timeout";
  case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED:
    return "connection aborted";
  case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
    return "connection reset";
  case COREWEBVIEW2_WEB_ERROR_STATUS_ERROR_HTTP_INVALID_SERVER_RESPONSE:
    return "invalid server response";
  default:
    if (st >= COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT &&
        st <= COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID)
      return "certificate error";
    return "web error " + std::to_string(st);
  }
}

// -- NAV policy (NAVQ/NAVR) --------------------------------------------------
static long g_nav_seq = 0;
struct NavPending {
  std::string win, url;
  UINT_PTR timer = 0;
  bool reload = false; // re-issue with Reload(), not Navigate()
  bool other = false;  // other URL scheme (#30): only 'external' launches it
  UINT64 nav_id = 0;   // the navigation we cancelled for this ask
};
static std::map<std::string, NavPending> g_nav_pending; // qid -> ask
static std::map<UINT_PTR, std::string> g_nav_timer_qid; // timer -> qid
// The re-issue has to wait for WebView2 to finish the navigation we cancelled
// (#32). A Navigate() issued before that cancel's NavigationCompleted returns
// S_OK and is silently dropped: no NavigationStarting, no completion, the
// window stays on about:blank. At startup the browser process is cold and the
// cancel takes ~800ms to complete while the backend answers in the same tick,
// so the app's FIRST http load (a dev server, a wrapped site) was always lost.
// Later navigations got away with it because their cancel finished first.
// win -> id of a cancelled navigation whose completion hasn't arrived yet:
static std::map<std::string, UINT64> g_nav_cancel_open;
// win -> an allowed re-issue held until that completion (or the fallback):
struct NavReissue {
  NavPending p;
  UINT_PTR timer = 0;
};
static std::map<std::string, NavReissue> g_nav_reissue;
static std::map<UINT_PTR, std::string> g_nav_reissue_timer; // timer -> win
static const UINT kNavReissueFallbackMs = 2000;
// win -> the navigation we cancelled for a policy ask and then re-issued, so
// its second NavigationStarting passes without asking again. Stamped, because
// an entry that is never matched used to sit here for the life of the process
// and a LATER navigation to the same url then skipped the ask entirely — a
// security marker failing OPEN. Cleared three ways now: matched, expired, or
// superseded by any other navigation in that window. A retry (the verify
// below) re-stamps it and never adds a second use: a spare use let the
// page's own reload of that url, 93ms after the retried load, through with
// no NAVQ (2026-10-14), and nothing tells that apart from the swallowed
// attempt starting late. So a late start is asked like any navigation; the
// cost is one extra ask in a rare case instead of a skipped one.
struct NavAllowOnce {
  std::string url;
  ULONGLONG at = 0;
};
static std::map<std::string, NavAllowOnce> g_nav_allow_once;
static const ULONGLONG kNavAllowOnceMs = 10000;
// Waiting for the cancel is not always enough (#32, again on v0.52): a cold
// browser process can still swallow the re-issue, leaving NAVR allow with no
// NavigationStarting after it and the window on about:blank for good. So a
// re-issue is verified: if nothing at all has started in that window a
// second later, send it again, a few times at most. Any start counts, ours
// or a newer navigation; both mean the window is no longer stuck.
// win -> NavigationStarting events seen, the verify's "did anything start":
static std::map<std::string, unsigned long> g_nav_starts;
struct NavVerify {
  NavPending p;
  unsigned long starts = 0;
  int tries = 0;
};
static std::map<UINT_PTR, NavVerify> g_nav_verify; // timer -> pending check
static const UINT kNavVerifyMs = 1000;
static const int kNavVerifyTries = 4;

static void CALLBACK nav_verify_timer_proc(HWND, UINT, UINT_PTR id, DWORD);

// allow: re-issue the navigation we cancelled, marked so the second
// NavigationStarting passes without another ask. A reload has to go back
// through Reload() — Navigate()ing the same url instead would drop the
// page's history entry and turn app.reload() into a fresh load.
static void nav_reissue(const NavPending &p, int tries = 0) {
  ICoreWebView2 *wv = wv2_for_id(p.win);
  if (!wv)
    return;
  g_nav_allow_once[p.win] = {p.url, GetTickCount64()};
  UINT_PTR t = SetTimer(nullptr, 0, kNavVerifyMs, nav_verify_timer_proc);
  g_nav_verify[t] = {p, g_nav_starts[p.win], tries};
  if (p.reload)
    wv->Reload();
  else
    wv->Navigate(widen(p.url).c_str());
}

static void CALLBACK nav_verify_timer_proc(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  auto it = g_nav_verify.find(id);
  if (it == g_nav_verify.end())
    return;
  NavVerify v = it->second;
  g_nav_verify.erase(it);
  if (g_nav_starts[v.p.win] != v.starts || v.tries + 1 >= kNavVerifyTries)
    return;
  nav_reissue(v.p, v.tries + 1);
}

// Drop a held re-issue without running it: the window navigated somewhere
// else meanwhile (the newer navigation wins, as in a browser) or closed.
static void nav_reissue_drop(const std::string &win) {
  auto it = g_nav_reissue.find(win);
  if (it == g_nav_reissue.end())
    return;
  if (it->second.timer) {
    KillTimer(nullptr, it->second.timer);
    g_nav_reissue_timer.erase(it->second.timer);
  }
  g_nav_reissue.erase(it);
}

static void nav_forget_window(const std::string &id) {
  nav_reissue_drop(id);
  g_nav_cancel_open.erase(id);
  g_nav_starts.erase(id);
  for (auto it = g_nav_verify.begin(); it != g_nav_verify.end();) {
    if (it->second.p.win == id) {
      KillTimer(nullptr, it->first);
      it = g_nav_verify.erase(it);
    } else {
      ++it;
    }
  }
}

static void nav_reissue_release(const std::string &win) {
  auto it = g_nav_reissue.find(win);
  if (it == g_nav_reissue.end())
    return;
  NavPending p = it->second.p;
  nav_reissue_drop(win);
  nav_reissue(p);
}

// Fallback: the cancelled navigation's completion never came. Re-issue
// anyway; that is what happened before, just late enough to usually stick.
static void CALLBACK nav_reissue_timer_proc(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  auto it = g_nav_reissue_timer.find(id);
  if (it == g_nav_reissue_timer.end())
    return;
  std::string win = it->second;
  g_nav_reissue_timer.erase(it);
  auto r = g_nav_reissue.find(win);
  if (r != g_nav_reissue.end())
    r->second.timer = 0; // already killed
  g_nav_cancel_open.erase(win);
  nav_reissue_release(win);
}

// NavigationCompleted for `nav_id` in `win`: if it is the cancel a re-issue
// is waiting on, the re-issue can go now.
static void nav_cancel_completed(const std::string &win, UINT64 nav_id) {
  auto c = g_nav_cancel_open.find(win);
  if (c == g_nav_cancel_open.end() || c->second != nav_id)
    return;
  g_nav_cancel_open.erase(c);
  auto r = g_nav_reissue.find(win);
  if (r != g_nav_reissue.end() && r->second.p.nav_id == nav_id)
    nav_reissue_release(win);
}

static void nav_apply_verdict(const std::string &qid, int verdict) {
  auto it = g_nav_pending.find(qid);
  if (it == g_nav_pending.end())
    return; // first resolver won (reply vs timeout, either order)
  NavPending p = it->second;
  g_nav_pending.erase(it);
  if (p.timer) {
    KillTimer(nullptr, p.timer);
    g_nav_timer_qid.erase(p.timer);
  }
  if (p.other) { // the launch was cancelled up front; external = do it ourselves
    if (verdict == 2)
      ShellExecuteW(nullptr, L"open", widen(p.url).c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
    return;
  }
  if (verdict == 1) // deny — the cancel already happened
    return;
  if (verdict == 2) { // external — hand it to the browser
    ShellExecuteW(nullptr, L"open", widen(p.url).c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
    return;
  }
  // allow — but not before WebView2 has finished the cancel (#32, above).
  auto c = g_nav_cancel_open.find(p.win);
  if (c != g_nav_cancel_open.end() && c->second == p.nav_id) {
    nav_reissue_drop(p.win);
    UINT_PTR t =
        SetTimer(nullptr, 0, kNavReissueFallbackMs, nav_reissue_timer_proc);
    g_nav_reissue[p.win] = {p, t};
    g_nav_reissue_timer[t] = p.win;
    return;
  }
  nav_reissue(p);
}

static void CALLBACK nav_timer_proc(HWND, UINT, UINT_PTR id, DWORD) {
  auto it = g_nav_timer_qid.find(id);
  KillTimer(nullptr, id);
  if (it == g_nav_timer_qid.end())
    return;
  std::string qid = it->second;
  g_nav_timer_qid.erase(it);
  // unanswered after 400ms = allow, so a wrapper can't deadlock its own load
  nav_apply_verdict(qid, 0);
}

struct NavrReq {
  std::string qid, verdict;
};
static void do_navr(webview_t, void *arg) {
  NavrReq *r = static_cast<NavrReq *>(arg);
  int v = r->verdict == "deny" ? 1 : r->verdict == "external" ? 2 : 0;
  nav_apply_verdict(r->qid, v);
  delete r;
}

#define TINY_COM_BOILERPLATE(IFACE)                                            \
  ULONG refs = 1;                                                              \
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }                 \
  ULONG STDMETHODCALLTYPE Release() override {                                 \
    if (refs > 1)                                                              \
      return --refs;                                                           \
    delete this;                                                               \
    return 0;                                                                  \
  }                                                                            \
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override { \
    if (!ppv)                                                                  \
      return E_POINTER;                                                        \
    if (riid == IID_IUnknown || riid == IID_##IFACE) {                         \
      *ppv = this;                                                             \
      AddRef();                                                                \
      return S_OK;                                                             \
    }                                                                          \
    *ppv = nullptr;                                                            \
    return E_NOINTERFACE;                                                      \
  }

// --- app.cookies (#46): the WebView2 profile's CookieManager ----------------
// Every window shares the one environment/profile, so the main webview's
// manager is the whole app's jar, HttpOnly included. The bridge filters.
// COOKIES <qid> get\t<url>   ('' = whole jar — GetCookies' own convention)
// COOKIES <qid> set\t<name>\t<value>\t<domain>\t<path>\t<expiresMs>\t<secure01>\t<httpOnly01>\t<samesite>
// COOKIES <qid> delete\t<name>\t<domain>\t<path>     GOT {ok, removed}
static std::string take_wstr(LPWSTR s) {
  std::string out = s ? narrow(s) : "";
  if (s)
    CoTaskMemFree(s);
  return out;
}

static std::string cookie_str(ICoreWebView2Cookie *c,
                              HRESULT (STDMETHODCALLTYPE ICoreWebView2Cookie::*get)(LPWSTR *)) {
  LPWSTR s = nullptr;
  return SUCCEEDED((c->*get)(&s)) ? take_wstr(s) : "";
}

static std::string wv2_cookie_json(ICoreWebView2Cookie *c) {
  BOOL session = TRUE, secure = FALSE, http_only = FALSE;
  double expires = -1;
  COREWEBVIEW2_COOKIE_SAME_SITE_KIND same = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE;
  c->get_IsSession(&session);
  c->get_Expires(&expires);
  c->get_IsSecure(&secure);
  c->get_IsHttpOnly(&http_only);
  c->get_SameSite(&same);
  std::string exp = session || expires < 0
                        ? "null"
                        : std::to_string((long long)(expires * 1000.0));
  const char *ss = same == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX      ? "\"lax\""
                   : same == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT ? "\"strict\""
                                                                      : "\"none\"";
  return "{\"name\":" + json_escape(cookie_str(c, &ICoreWebView2Cookie::get_Name)) +
         ",\"value\":" + json_escape(cookie_str(c, &ICoreWebView2Cookie::get_Value)) +
         ",\"domain\":" + json_escape(cookie_str(c, &ICoreWebView2Cookie::get_Domain)) +
         ",\"path\":" + json_escape(cookie_str(c, &ICoreWebView2Cookie::get_Path)) +
         ",\"expires\":" + exp + ",\"secure\":" + (secure ? "true" : "false") +
         ",\"httpOnly\":" + (http_only ? "true" : "false") + ",\"sameSite\":" + ss + "}";
}

struct CookiesGetHandler : public ICoreWebView2GetCookiesCompletedHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2GetCookiesCompletedHandler)
  std::string qid;
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2CookieList *list) override {
    UINT32 n = 0;
    if (FAILED(hr) || !list || FAILED(list->get_Count(&n))) {
      got(qid, "{\"ok\":false,\"error\":\"GetCookies failed\"}");
      return S_OK;
    }
    std::string json = "[";
    for (UINT32 i = 0; i < n; i++) {
      ICoreWebView2Cookie *c = nullptr;
      if (FAILED(list->GetValueAtIndex(i, &c)) || !c)
        continue;
      if (json.size() > 1)
        json += ",";
      json += wv2_cookie_json(c);
      c->Release();
    }
    got(qid, "{\"ok\":true,\"cookies\":" + json + "]}");
    return S_OK;
  }
};

// DeleteCookie takes the cookie object itself, so find the exact
// name/domain/path match in the whole jar rather than trusting
// DeleteCookiesWithDomainAndPath's idea of a leading-dot domain.
struct CookiesDeleteHandler : public ICoreWebView2GetCookiesCompletedHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2GetCookiesCompletedHandler)
  std::string qid, name, domain, path;
  ICoreWebView2CookieManager *mgr = nullptr; // owned ref
  ~CookiesDeleteHandler() {
    if (mgr)
      mgr->Release();
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2CookieList *list) override {
    UINT32 n = 0, removed = 0;
    if (FAILED(hr) || !list || FAILED(list->get_Count(&n))) {
      got(qid, "{\"ok\":false,\"error\":\"GetCookies failed\"}");
      return S_OK;
    }
    for (UINT32 i = 0; i < n; i++) {
      ICoreWebView2Cookie *c = nullptr;
      if (FAILED(list->GetValueAtIndex(i, &c)) || !c)
        continue;
      if (cookie_str(c, &ICoreWebView2Cookie::get_Name) == name &&
          cookie_str(c, &ICoreWebView2Cookie::get_Domain) == domain &&
          cookie_str(c, &ICoreWebView2Cookie::get_Path) == path &&
          SUCCEEDED(mgr->DeleteCookie(c)))
        removed++;
      c->Release();
    }
    got(qid, "{\"ok\":true,\"removed\":" + std::to_string(removed) + "}");
    return S_OK;
  }
};

static ICoreWebView2CookieManager *cookie_manager() {
  ICoreWebView2_2 *wv22 = nullptr;
  ICoreWebView2CookieManager *mgr = nullptr;
  if (g_wv2 && SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_2, (void **)&wv22))) {
    wv22->get_CookieManager(&mgr);
    wv22->Release();
  }
  return mgr;
}

static void do_cookies(webview_t, void *arg) {
  QReq *req = static_cast<QReq *>(arg);
  std::string qid = req->qid;
  std::vector<std::string> p = split_tabs(req->rest);
  delete req;
  auto f = [&](size_t i) { return i < p.size() ? wire_unescape(p[i]) : std::string(); };
  std::string op = p.empty() ? "" : p[0];
  ICoreWebView2CookieManager *mgr = cookie_manager();
  if (!mgr) {
    got(qid, "{\"ok\":false,\"error\":\"no webview\"}");
    return;
  }

  if (op == "get") {
    CookiesGetHandler *h = new CookiesGetHandler();
    h->qid = qid;
    if (FAILED(mgr->GetCookies(widen(f(1)).c_str(), h)))
      got(qid, "{\"ok\":false,\"error\":\"GetCookies failed\"}");
    h->Release();
  } else if (op == "set") {
    ICoreWebView2Cookie *c = nullptr;
    HRESULT hr = mgr->CreateCookie(widen(f(1)).c_str(), widen(f(2)).c_str(),
                                   widen(f(3)).c_str(), widen(f(4)).c_str(), &c);
    if (SUCCEEDED(hr) && c) {
      if (!f(5).empty())
        c->put_Expires(std::atof(f(5).c_str()) / 1000.0);
      c->put_IsSecure(f(6) == "1");
      c->put_IsHttpOnly(f(7) == "1");
      std::string same = f(8);
      if (same == "lax")
        c->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
      else if (same == "strict")
        c->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT);
      else if (same == "none")
        c->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE);
      hr = mgr->AddOrUpdateCookie(c);
      c->Release();
    }
    got(qid, SUCCEEDED(hr) ? "{\"ok\":true}"
                           : "{\"ok\":false,\"error\":\"invalid cookie\"}");
  } else if (op == "delete") {
    CookiesDeleteHandler *h = new CookiesDeleteHandler();
    h->qid = qid;
    h->name = f(1);
    h->domain = f(2);
    h->path = f(3);
    mgr->AddRef();
    h->mgr = mgr;
    if (FAILED(mgr->GetCookies(L"", h)))
      got(qid, "{\"ok\":false,\"error\":\"GetCookies failed\"}");
    h->Release();
  } else {
    got(qid, "{\"ok\":false,\"error\":\"bad cookies op\"}");
  }
  mgr->Release();
}

struct NavStartHandler : public ICoreWebView2NavigationStartingEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2NavigationStartingEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2NavigationStartingEventArgs *args) override {
    LPWSTR u = nullptr;
    if (FAILED(args->get_Uri(&u)) || !u)
      return S_OK;
    std::string url = narrow(u);
    CoTaskMemFree(u);
    ++g_nav_starts[winid]; // a re-issue's verify: something started (#32)
    // Consume the allow-once marker — and drop it on ANY other navigation in
    // this window, or on age. A redirect or a failure means the url we marked
    // may never arrive, and a marker left behind waves a later navigation to
    // that same url straight past the policy ask.
    auto ao = g_nav_allow_once.find(winid);
    if (ao != g_nav_allow_once.end()) {
      bool fresh = GetTickCount64() - ao->second.at <= kNavAllowOnceMs;
      bool mine = ao->second.url == url;
      g_nav_allow_once.erase(ao);
      if (mine && fresh) {
        g_nav_last_url[winid] = url;
        nav_event(winid, "start", url);
        return S_OK;
      }
    }
    // Any other navigation supersedes a re-issue still waiting on its cancel.
    nav_reissue_drop(winid);
    bool http = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
    // What kind of navigation this is decides whether it can survive the ask
    // at all: the ask is implemented as cancel + re-issue (NavigationStarting
    // carries no deferral), and a cancelled back/forward cannot be re-issued —
    // Navigate()ing its url pushes a NEW history entry, so Back grows the
    // stack and Forward dies. History moves therefore skip the ask rather than
    // quietly corrupting the stack. A reload is askable, because Reload() can
    // re-issue it faithfully.
    COREWEBVIEW2_NAVIGATION_KIND kind = COREWEBVIEW2_NAVIGATION_KIND_NEW_DOCUMENT;
    {
      ICoreWebView2NavigationStartingEventArgs3 *a3 = nullptr;
      if (SUCCEEDED(args->QueryInterface(
              IID_ICoreWebView2NavigationStartingEventArgs3, (void **)&a3)) &&
          a3) {
        a3->get_NavigationKind(&kind);
        a3->Release();
      }
    }
    if (!http || kind == COREWEBVIEW2_NAVIGATION_KIND_BACK_OR_FORWARD) {
      g_nav_last_url[winid] = url;
      nav_event(winid, "start", url);
      return S_OK;
    }
    // Policy ask. No deferral on this event, so cancel now and re-navigate on
    // 'allow'; the cancelled attempt emits no start (the re-issue does), and
    // its OPERATION_CANCELED completion is suppressed as noise below.
    args->put_Cancel(TRUE);
    UINT64 nav_id = 0;
    args->get_NavigationId(&nav_id);
    g_nav_cancel_open[winid] = nav_id; // re-issue waits for its completion
    std::string qid = "n" + std::to_string(++g_nav_seq);
    UINT_PTR t = SetTimer(nullptr, 0, 400, nav_timer_proc);
    g_nav_pending[qid] = {winid, url, t,
                          kind == COREWEBVIEW2_NAVIGATION_KIND_RELOAD, false,
                          nav_id};
    g_nav_timer_qid[t] = qid;
    pipe_write_line("NAVQ " + qid + " " + winid + "\t" + url + "\tmain");
    return S_OK;
  }
};

// Other URL schemes (#30): mailto:, ms-settings:, someapp://… WebView2 hands
// these to the OS behind its own "open this app?" prompt — from the main
// frame, an iframe or a popup alike, and NavigationStarting (http(s)-only
// asks above) never put them to the app. LaunchingExternalUriScheme sees
// every one: cancel it up front, so the prompt never shows, and ask
// onNavigate; only an 'external' verdict launches it, through the shell.
// No answer in 400ms (the timer's 'allow'), or no backend, is a deny. The
// frame is unknown here (flag left empty). Runtimes without
// ICoreWebView2_18 keep WebView2's prompt.
struct ExtSchemeHandler
    : public ICoreWebView2LaunchingExternalUriSchemeEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2LaunchingExternalUriSchemeEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2LaunchingExternalUriSchemeEventArgs *args) override {
    args->put_Cancel(TRUE);
    LPWSTR u = nullptr;
    if (FAILED(args->get_Uri(&u)) || !u)
      return S_OK;
    std::string url = narrow(u);
    CoTaskMemFree(u);
    std::string qid = "n" + std::to_string(++g_nav_seq);
    UINT_PTR t = SetTimer(nullptr, 0, 400, nav_timer_proc);
    NavPending p;
    p.win = winid;
    p.url = url;
    p.timer = t;
    p.other = true;
    g_nav_pending[qid] = p;
    g_nav_timer_qid[t] = qid;
    pipe_write_line("NAVQ " + qid + " " + winid + "\t" + url + "\t");
    return S_OK;
  }
};

struct ContentLoadingHandler : public ICoreWebView2ContentLoadingEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2ContentLoadingEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *sender, ICoreWebView2ContentLoadingEventArgs *) override {
    LPWSTR s = nullptr;
    std::string url;
    if (SUCCEEDED(sender->get_Source(&s)) && s) {
      url = narrow(s);
      CoTaskMemFree(s);
    }
    nav_event(winid, "commit", url);
    return S_OK;
  }
};

struct NavDoneHandler : public ICoreWebView2NavigationCompletedEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2NavigationCompletedEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *sender,
         ICoreWebView2NavigationCompletedEventArgs *args) override {
    UINT64 nav_id = 0;
    args->get_NavigationId(&nav_id);
    nav_cancel_completed(winid, nav_id); // may release a held re-issue (#32)
    BOOL ok = FALSE;
    args->get_IsSuccess(&ok);
    if (ok) {
      LPWSTR s = nullptr;
      std::string url;
      if (SUCCEEDED(sender->get_Source(&s)) && s) {
        url = narrow(s);
        CoTaskMemFree(s);
      }
      nav_event(winid, "finish", url);
      return S_OK;
    }
    COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    args->get_WebErrorStatus(&st);
    // Noise, not failures: our own policy cancels and rapid re-navigation
    // report OPERATION_CANCELED; a navigation that became a download reports
    // CONNECTION_ABORTED (Chromium's ERR_ABORTED — measured against a live
    // 300MB download, where the fail arrived BEFORE DownloadStarting, so it
    // can't be suppressed by looking the download up). Painting an offline
    // screen for either would be exactly wrong. Genuine network failures
    // come in as CANNOT_CONNECT / CONNECTION_RESET / TIMEOUT / name errors.
    // Same shape as macOS suppressing NSURLErrorCancelled + WebKitError 102.
    if (st == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED ||
        st == COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED)
      return S_OK;
    auto lu = g_nav_last_url.find(winid);
    nav_event(winid, "fail", lu == g_nav_last_url.end() ? "" : lu->second,
              web_error_name((int)st));
    return S_OK;
  }
};

struct ProcFailHandler : public ICoreWebView2ProcessFailedEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2ProcessFailedEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *sender, ICoreWebView2ProcessFailedEventArgs *) override {
    LPWSTR s = nullptr;
    std::string url;
    if (SUCCEEDED(sender->get_Source(&s)) && s) {
      url = narrow(s);
      CoTaskMemFree(s);
    }
    nav_event(winid, "crash", url);
    return S_OK;
  }
};

// popup windows adopt the page's own title once it has one (macOS twin:
// didFinishNavigation's popup branch)
struct PopupTitleHandler
    : public ICoreWebView2DocumentTitleChangedEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2DocumentTitleChangedEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *) override {
    LPWSTR t = nullptr;
    if (SUCCEEDED(sender->get_DocumentTitle(&t)) && t) {
      HWND h = hwnd_for_win(winid);
      if (h && wcslen(t))
        SetWindowTextW(h, t);
      CoTaskMemFree(t);
    }
    return S_OK;
  }
};

// -- JS dialogs (alert/confirm/prompt) ---------------------------------------
// Answered natively and synchronously inside the handler, like the backend's
// DLG dialogs — no wire exchange. Headline = the page origin's host, falling
// back to the app name (a third-party page shouldn't speak AS the app).
static std::string js_dialog_title(const std::string &uri) {
  std::string o = origin_from_uri(uri);
  size_t p = o.find("://");
  std::string host = p == std::string::npos ? "" : o.substr(p + 3);
  return host.empty() ? g_app_name : host;
}

struct JsDialogHandler : public ICoreWebView2ScriptDialogOpeningEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2ScriptDialogOpeningEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2ScriptDialogOpeningEventArgs *args) override {
    COREWEBVIEW2_SCRIPT_DIALOG_KIND kind =
        COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;
    args->get_Kind(&kind);
    LPWSTR w = nullptr;
    std::string msg, uri;
    if (SUCCEEDED(args->get_Message(&w)) && w) {
      msg = narrow(w);
      CoTaskMemFree(w);
      w = nullptr;
    }
    if (SUCCEEDED(args->get_Uri(&w)) && w) {
      uri = narrow(w);
      CoTaskMemFree(w);
      w = nullptr;
    }
    HWND owner = hwnd_for_win(winid);
    if (!owner)
      owner = g_host;
    std::wstring title = widen(js_dialog_title(uri));
    if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT) {
      MessageBoxW(owner, widen(msg).c_str(), title.c_str(), MB_OK);
      args->Accept();
    } else if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM) {
      if (MessageBoxW(owner, widen(msg).c_str(), title.c_str(), MB_OKCANCEL) ==
          IDOK)
        args->Accept();
    } else if (kind == COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT) {
      std::string defval;
      if (SUCCEEDED(args->get_DefaultText(&w)) && w) {
        defval = narrow(w);
        CoTaskMemFree(w);
      }
      std::wstring typed;
      if (run_prompt_raw(msg, defval, &typed, owner)) {
        args->put_ResultText(typed.c_str());
        args->Accept();
      }
    } else {
      // beforeunload. Auto-accepting it (what this used to do) means a wrapped
      // app with unsaved work navigates away silently — macOS routes it
      // through the confirm panel, so this does too. Chromium supplies no
      // page text for these any more, so the message is ours; not accepting
      // is what keeps the user on the page.
      std::string body =
          msg.empty() ? "Changes you made may not be saved." : msg;
      if (MessageBoxW(owner, widen(body).c_str(), L"Leave this page?",
                      MB_OKCANCEL | MB_ICONWARNING) == IDOK)
        args->Accept();
    }
    return S_OK;
  }
};

// -- downloads ---------------------------------------------------------------
static long g_download_seq = 0;

static void download_event(long id, const std::string &url,
                           const std::string &fname, const std::string &path,
                           const char *state, const std::string &err = "",
                           long long bytes = -1, long long total = -1) {
  std::string j = "{\"id\":" + std::to_string(id) +
                  ",\"url\":" + json_escape(url) +
                  ",\"filename\":" + json_escape(fname) + ",\"path\":" +
                  (path.empty() ? "null" : json_escape(path)) +
                  ",\"state\":\"" + state + "\"";
  if (bytes >= 0)
    j += ",\"bytes\":" + std::to_string(bytes) +
         ",\"total\":" + std::to_string(total > 0 ? total : -1);
  if (!err.empty())
    j += ",\"error\":" + json_escape(err);
  pipe_write_line("DOWNLOAD " + j + "}");
}

static std::string downloads_dir() {
  // FOLDERID_Downloads, spelled out so MinGW needs no uuid lib for it
  static const GUID kDownloads = {0x374DE290, 0x123F, 0x4565,
                                  {0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46,
                                   0x7B}};
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(kDownloads, 0, nullptr, &p)) && p) {
    out = narrow(p);
    CoTaskMemFree(p);
  }
  if (out.empty()) {
    wchar_t up[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
      out = narrow(up) + "\\Downloads";
  }
  return out;
}

// "name.ext" -> "name (2).ext" … up to 999, macOS-style, so a repeat download
// never silently overwrites.
static std::string dedup_download_path(const std::string &dir,
                                       const std::string &fname) {
  size_t dot = fname.rfind('.');
  std::string base = dot == std::string::npos ? fname : fname.substr(0, dot);
  std::string ext = dot == std::string::npos ? "" : fname.substr(dot);
  for (int n = 1; n <= 999; n++) {
    std::string cand = n == 1 ? dir + "\\" + fname
                              : dir + "\\" + base + " (" + std::to_string(n) +
                                    ")" + ext;
    if (GetFileAttributesW(widen(cand).c_str()) == INVALID_FILE_ATTRIBUTES)
      return cand;
  }
  return dir + "\\" + fname;
}

// download "ask" mode: a save panel seeded with the suggested name in the
// Downloads folder, like the macOS NSSavePanel arm.
static std::string download_save_panel(const std::string &suggested) {
  std::string out;
  IFileSaveDialog *dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) ||
      !dlg)
    return out;
  dlg->SetFileName(widen(suggested).c_str());
  IShellItem *folder = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(widen(downloads_dir()).c_str(),
                                            nullptr, IID_PPV_ARGS(&folder))) &&
      folder) {
    dlg->SetFolder(folder);
    folder->Release();
  }
  if (SUCCEEDED(dlg->Show(g_host))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && item) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        out = narrow(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

// Progress + terminal state for one download, throttled like the macOS KVO
// observer (one progress event per 250ms).
struct DlWatch : public ICoreWebView2BytesReceivedChangedEventHandler {
  ULONG refs = 1;
  long id = 0;
  std::string url, fname, path;
  ULONGLONG last_emit = 0;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2BytesReceivedChangedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation *op,
                                   IUnknown *) override {
    ULONGLONG now = GetTickCount64();
    if (now - last_emit < 250)
      return S_OK;
    last_emit = now;
    INT64 br = 0, tot = 0;
    op->get_BytesReceived(&br);
    op->get_TotalBytesToReceive(&tot);
    download_event(id, url, fname, path, "progress", "", (long long)br,
                   (long long)tot);
    return S_OK;
  }
};

struct DlStateWatch : public ICoreWebView2StateChangedEventHandler {
  ULONG refs = 1;
  long id = 0;
  std::string url, fname, path;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    if (refs > 1)
      return --refs;
    delete this;
    return 0;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv)
      return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_ICoreWebView2StateChangedEventHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation *op,
                                   IUnknown *) override {
    COREWEBVIEW2_DOWNLOAD_STATE st = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
    op->get_State(&st);
    if (st == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
      download_event(id, url, fname, path, "done");
    } else if (st == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
      COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON r =
          COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
      op->get_InterruptReason(&r);
      if (r == COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED)
        download_event(id, url, fname, "", "cancelled");
      else
        download_event(id, url, fname, path, "failed",
                       "download interrupted (" + std::to_string((int)r) + ")");
    }
    return S_OK;
  }
};

// Point a download at its final destination and start watching it. An empty
// path means the ask-mode panel was cancelled.
static void download_finish(ICoreWebView2DownloadStartingEventArgs *args,
                            ICoreWebView2DownloadOperation *op, long id,
                            const std::string &url,
                            const std::string &suggested,
                            const std::string &path) {
  if (path.empty()) {
    args->put_Cancel(TRUE);
    download_event(id, url, suggested, "", "cancelled");
    return;
  }
  args->put_ResultFilePath(widen(path).c_str());
  // Report the name of the file that will actually EXIST. The suggestion is
  // not it: ` (2)` dedup renames it, and an ask-mode panel can rename it
  // outright — so "Downloaded <name>" used to name a file nobody could find.
  // macOS reassigns in both arms; this is the twin.
  size_t sl = path.find_last_of("\\/");
  std::string fname = sl == std::string::npos ? path : path.substr(sl + 1);
  download_event(id, url, fname, path, "started");
  if (!op)
    return;
  EventRegistrationToken tok;
  DlWatch *pw = new DlWatch();
  pw->id = id;
  pw->url = url;
  pw->fname = fname;
  pw->path = path;
  op->add_BytesReceivedChanged(pw, &tok);
  pw->Release();
  DlStateWatch *sw = new DlStateWatch();
  sw->id = id;
  sw->url = url;
  sw->fname = fname;
  sw->path = path;
  op->add_StateChanged(sw, &tok);
  sw->Release();
}

// ask mode, deferred: IFileSaveDialog::Show spins a NESTED MODAL LOOP, and
// running one inside a WebView2 event callback re-enters the engine and lets
// our own 400ms nav/popup timers fire mid-callback. Take the deferral, hop out
// of the handler, then show the panel.
struct DownloadAskReq {
  long id = 0;
  std::string url, fname;
  ICoreWebView2DownloadStartingEventArgs *args = nullptr;
  ICoreWebView2DownloadOperation *op = nullptr;
  ICoreWebView2Deferral *def = nullptr;
};

static void do_download_ask(webview_t, void *arg) {
  DownloadAskReq *r = static_cast<DownloadAskReq *>(arg);
  download_finish(r->args, r->op, r->id, r->url, r->fname,
                  download_save_panel(r->fname));
  r->def->Complete();
  r->def->Release();
  r->args->Release();
  if (r->op)
    r->op->Release();
  delete r;
}

struct DownloadStartHandler
    : public ICoreWebView2DownloadStartingEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2DownloadStartingEventHandler)
  std::string winid;
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2DownloadStartingEventArgs *args) override {
    long id = ++g_download_seq;
    ICoreWebView2DownloadOperation *op = nullptr;
    args->get_DownloadOperation(&op);
    std::string url, fname, defpath;
    LPWSTR w = nullptr;
    if (op && SUCCEEDED(op->get_Uri(&w)) && w) {
      url = narrow(w);
      CoTaskMemFree(w);
      w = nullptr;
    }
    // The suggested filename is the leaf of WebView2's own default result
    // path — the engine already derived an extension from the MIME type.
    if (SUCCEEDED(args->get_ResultFilePath(&w)) && w) {
      defpath = narrow(w);
      CoTaskMemFree(w);
      w = nullptr;
      size_t sl = defpath.find_last_of("\\/");
      fname = sl == std::string::npos ? defpath : defpath.substr(sl + 1);
    }
    if (fname.empty())
      fname = "download";
    std::string mode = g_downloads_mode.empty() ? "auto" : g_downloads_mode;
    args->put_Handled(TRUE); // our events replace the default download UI
    if (mode == "deny") {
      args->put_Cancel(TRUE);
      download_event(id, url, fname, "", "denied");
    } else if (mode == "ask") {
      ICoreWebView2Deferral *def = nullptr;
      if (SUCCEEDED(args->GetDeferral(&def)) && def) {
        DownloadAskReq *r = new DownloadAskReq();
        r->id = id;
        r->url = url;
        r->fname = fname;
        r->args = args;
        r->op = op;
        r->def = def;
        args->AddRef(); // both released by do_download_ask
        tiny_dispatch(do_download_ask, r);
        return S_OK;    // op ownership moved with the request
      }
      // No deferral (shouldn't happen): the old in-callback panel, which
      // works — it just re-enters the engine while it is up.
      download_finish(args, op, id, url, fname, download_save_panel(fname));
    } else {
      // No known Downloads folder (and no USERPROFILE either) is the one case
      // where we keep the engine's own destination rather than cancelling.
      std::string dir = downloads_dir();
      download_finish(args, op, id, url, fname,
                      dir.empty() ? defpath : dedup_download_path(dir, fname));
    }
    if (op)
      op->Release();
    return S_OK;
  }
};

// -- window.open / target=_blank (POPUPQ/POPUPR/POPUP) -----------------------
static long g_popup_seq = 0, g_popup_qseq = 0;
struct PopupPending {
  std::string pid, url, mode;
  UINT_PTR timer = 0;
};
static std::map<std::string, PopupPending> g_popup_pending;
static std::map<UINT_PTR, std::string> g_popup_timer_qid;

static void popup_event(const std::string &pid, const std::string &url,
                        const std::string &action) {
  pipe_write_line("POPUP {\"window\":" +
                  (pid.empty() ? std::string("null") : json_escape(pid)) +
                  ",\"url\":" + json_escape(url) + ",\"action\":\"" + action +
                  "\"}");
}

static void popup_apply_verdict(const std::string &qid,
                                const std::string &verdict) {
  auto it = g_popup_pending.find(qid);
  if (it == g_popup_pending.end())
    return; // first resolver won
  PopupPending p = it->second;
  g_popup_pending.erase(it);
  if (p.timer) {
    KillTimer(nullptr, p.timer);
    g_popup_timer_qid.erase(p.timer);
  }
  bool http = p.url.rfind("http://", 0) == 0 || p.url.rfind("https://", 0) == 0;
  if (p.pid != "-") {
    // window mode: the popup exists, hidden and loading; the verdict shows it
    // or closes it unseen ('window' unless the hook said otherwise).
    std::string v =
        (verdict == "external" || verdict == "deny") ? verdict : "window";
    HWND h = hwnd_for_win(p.pid);
    if (v == "window") {
      if (h) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
      }
    } else {
      if (v == "external" && http)
        ShellExecuteW(nullptr, L"open", widen(p.url).c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
      if (h)
        PostMessageW(h, WM_CLOSE, 0, 0); // fires WINCLOSED via WM_DESTROY
    }
    popup_event(p.pid, p.url, v);
  } else {
    // no window was built: only external/deny can be honored, like macOS
    std::string v =
        (verdict == "external" || verdict == "deny") ? verdict : p.mode;
    if (v != "external" && v != "deny")
      v = "external";
    if (v == "external" && http)
      ShellExecuteW(nullptr, L"open", widen(p.url).c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
    popup_event("", p.url, v);
  }
}

static void CALLBACK popup_timer_proc(HWND, UINT, UINT_PTR id, DWORD) {
  auto it = g_popup_timer_qid.find(id);
  KillTimer(nullptr, id);
  if (it == g_popup_timer_qid.end())
    return;
  std::string qid = it->second;
  g_popup_timer_qid.erase(it);
  auto p = g_popup_pending.find(qid);
  if (p == g_popup_pending.end())
    return;
  // unanswered after 400ms = the configured mode
  popup_apply_verdict(qid, p->second.mode);
}

static void popup_ask(const std::string &pid, const std::string &opener,
                      const std::string &url, const std::string &mode) {
  std::string qid = "p" + std::to_string(++g_popup_qseq);
  UINT_PTR t = SetTimer(nullptr, 0, 400, popup_timer_proc);
  g_popup_pending[qid] = {pid, url, mode, t};
  g_popup_timer_qid[t] = qid;
  pipe_write_line("POPUPQ " + qid + " " + pid + "\t" + opener + "\t" + url +
                  "\t" + mode);
}

struct PopuprReq {
  std::string qid, verdict;
};
static void do_popupr(webview_t, void *arg) {
  PopuprReq *r = static_cast<PopuprReq *>(arg);
  popup_apply_verdict(r->qid, r->verdict);
  delete r;
}

struct NewWindowHandler : public ICoreWebView2NewWindowRequestedEventHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2NewWindowRequestedEventHandler)
  std::string winid; // the opener
  HRESULT STDMETHODCALLTYPE
  Invoke(ICoreWebView2 *,
         ICoreWebView2NewWindowRequestedEventArgs *args) override {
    LPWSTR w = nullptr;
    std::string url;
    if (SUCCEEDED(args->get_Uri(&w)) && w) {
      url = narrow(w);
      CoTaskMemFree(w);
    }
    std::string mode = g_popup_mode.empty() ? "external" : g_popup_mode;
    if (mode != "window") {
      // external (default) | deny: no window either way; the verdict decides
      // between shell-open and nothing.
      args->put_Handled(TRUE);
      popup_ask("-", winid, url, mode);
      return S_OK;
    }
    // window mode: build a real (hidden) window + webview and hand the
    // webview back via put_NewWindow so window.opener / postMessage survive.
    std::string pid;
    do {
      pid = "popup" + std::to_string(++g_popup_seq);
    } while (g_windows.count(pid));
    UINT32 fw = 0, fh = 0, fx = 0, fy = 0;
    BOOL hasSize = FALSE, hasPos = FALSE;
    ICoreWebView2WindowFeatures *feat = nullptr;
    if (SUCCEEDED(args->get_WindowFeatures(&feat)) && feat) {
      feat->get_HasSize(&hasSize);
      feat->get_HasPosition(&hasPos);
      if (hasSize) {
        feat->get_Width(&fw);
        feat->get_Height(&fh);
      }
      if (hasPos) {
        feat->get_Left(&fx);
        feat->get_Top(&fy);
      }
      feat->Release();
    }
    int cw = (int)fw, ch = (int)fh;
    if (cw < 120)
      cw = 900;
    if (ch < 90)
      ch = 600;
    ensure_secwin_class();
    double sc = window_scale(g_hwnd);
    POINT at = {CW_USEDEFAULT, CW_USEDEFAULT};
    if (hasPos)
      at = dip_point_to_px(fx, fy, &sc); // global DIPs, see screen_layout
    RECT rc = {0, 0, (LONG)lround(cw * sc), (LONG)lround(ch * sc)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int px = at.x;
    int py = at.y;
    HWND hwnd = CreateWindowExW(0, L"TinyjsSecondary", widen(pid).c_str(),
                                WS_OVERLAPPEDWINDOW, px, py,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
    if (!hwnd) {
      args->put_Handled(TRUE);
      popup_ask("-", winid, url, "external");
      return S_OK;
    }
    apply_relaunch_props(hwnd);
    TinyWin *tw = new TinyWin();
    tw->hwnd = hwnd;
    g_windows[pid] = tw;
    render_menu(hwnd);
    // NOT shown — it loads hidden until the POPUPQ verdict.
    ICoreWebView2Deferral *def = nullptr;
    args->GetDeferral(&def);
    args->AddRef();
    SecCtrlHandler *ch2 = new SecCtrlHandler();
    ch2->winid = pid;
    bool kicked = create_sec_controller(hwnd, ch2);
    if (kicked) {
      ch2->popup_args = args;
      ch2->popup_deferral = def;
    }
    ch2->Release();
    if (!kicked) {
      if (def) {
        def->Complete();
        def->Release();
      }
      args->put_Handled(TRUE);
      args->Release();
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      popup_ask("-", winid, url, "external");
      return S_OK;
    }
    popup_ask(pid, winid, url, "window");
    return S_OK;
  }
};

// -- find in page (FIND/STOPFIND) --------------------------------------------
// WebView2 has no find API at all, so the page does both halves: Chromium's
// window.find() selects and scrolls, and the same text-walk the macOS
// launcher layers on its native find produces {matches, activeMatch} (approx
// by construction: hidden text counts, shadow DOM doesn't).
struct FindReq {
  std::string id, term;
  bool forward = true, matchCase = false;
};

struct FindResultHandler
    : public ICoreWebView2ExecuteScriptCompletedHandler {
  TINY_COM_BOILERPLATE(ICoreWebView2ExecuteScriptCompletedHandler)
  std::string id;
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   LPCWSTR resultObjectAsJson) override {
    std::string json =
        SUCCEEDED(hr) && resultObjectAsJson ? narrow(resultObjectAsJson) : "";
    if (json.empty() || json == "null")
      json = "{\"found\":false}";
    route_ret(g_w, id, 0, json);
    return S_OK;
  }
};

static void do_find(webview_t w, void *arg) {
  FindReq *r = static_cast<FindReq *>(arg);
  size_t c = r->id.find(':');
  std::string winid = c == std::string::npos ? "main" : r->id.substr(0, c);
  ICoreWebView2 *wv = wv2_for_id(winid);
  if (!wv || r->term.empty()) {
    route_ret(w, r->id, 0, "{\"found\":false}");
    delete r;
    return;
  }
  std::string js =
      "(function(t,cs,fwd){try{"
      "var f=window.find(t,cs,!fwd,true,false,false,false);"
      "if(!f)return{found:false,matches:0,activeMatch:0};"
      "var sel=getSelection(),sn=null,so=0;"
      "if(sel.rangeCount){var r=sel.getRangeAt(0);sn=r.startContainer;so=r.startOffset;}"
      "var w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT,{acceptNode:function(n){"
      "var p=n.parentNode&&n.parentNode.nodeName;"
      "return(p==='SCRIPT'||p==='STYLE'||p==='NOSCRIPT')?NodeFilter.FILTER_REJECT:NodeFilter.FILTER_ACCEPT;}});"
      "var text='',selAt=-1,nd;"
      "while((nd=w.nextNode())){if(nd===sn)selAt=text.length+so;text+=nd.nodeValue;}"
      "if(!cs){t=t.toLowerCase();text=text.toLowerCase();}"
      "var n=0,act=0,i=0;"
      "while((i=text.indexOf(t,i))!==-1){n++;if(selAt>=0&&i<=selAt)act=n;i+=t.length;}"
      "return{found:true,matches:n,activeMatch:act};"
      "}catch(e){return{found:true}}})(" +
      json_escape(r->term) + "," + (r->matchCase ? "true" : "false") + "," +
      (r->forward ? "true" : "false") + ")";
  FindResultHandler *h = new FindResultHandler();
  h->id = r->id;
  wv->ExecuteScript(widen(js).c_str(), h);
  h->Release();
  delete r;
}

static void do_stopfind(webview_t w, void *arg) {
  std::string *id = static_cast<std::string *>(arg);
  size_t c = id->find(':');
  std::string winid = c == std::string::npos ? "main" : id->substr(0, c);
  ICoreWebView2 *wv = wv2_for_id(winid);
  if (wv)
    wv->ExecuteScript(L"getSelection().removeAllRanges()", nullptr);
  route_ret(w, *id, 0, "true");
  delete id;
}

// -- wiring ------------------------------------------------------------------
static void install_browser_affordances(ICoreWebView2 *wv,
                                        const std::string &winid) {
  if (!wv)
    return;
  EventRegistrationToken tok;
  {
    NavStartHandler *h = new NavStartHandler();
    h->winid = winid;
    wv->add_NavigationStarting(h, &tok);
    h->Release();
  }
  {
    ICoreWebView2_18 *wv18 = nullptr;
    if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_18, (void **)&wv18)) &&
        wv18) {
      ExtSchemeHandler *h = new ExtSchemeHandler();
      h->winid = winid;
      wv18->add_LaunchingExternalUriScheme(h, &tok);
      h->Release();
      wv18->Release();
    }
  }
  {
    ContentLoadingHandler *h = new ContentLoadingHandler();
    h->winid = winid;
    wv->add_ContentLoading(h, &tok);
    h->Release();
  }
  {
    NavDoneHandler *h = new NavDoneHandler();
    h->winid = winid;
    wv->add_NavigationCompleted(h, &tok);
    h->Release();
  }
  {
    ProcFailHandler *h = new ProcFailHandler();
    h->winid = winid;
    wv->add_ProcessFailed(h, &tok);
    h->Release();
  }
  {
    NewWindowHandler *h = new NewWindowHandler();
    h->winid = winid;
    wv->add_NewWindowRequested(h, &tok);
    h->Release();
  }
  {
    // ScriptDialogOpening only fires with the engine's own dialogs off.
    ICoreWebView2Settings *s = nullptr;
    if (SUCCEEDED(wv->get_Settings(&s)) && s) {
      s->put_AreDefaultScriptDialogsEnabled(FALSE);
      s->Release();
    }
    JsDialogHandler *h = new JsDialogHandler();
    h->winid = winid;
    wv->add_ScriptDialogOpening(h, &tok);
    h->Release();
  }
  {
    // Downloads need ICoreWebView2_4; a runtime too old for it keeps the
    // engine's default download UI (old behavior, not breakage).
    ICoreWebView2_4 *wv4 = nullptr;
    if (SUCCEEDED(wv->QueryInterface(IID_ICoreWebView2_4, (void **)&wv4)) &&
        wv4) {
      DownloadStartHandler *h = new DownloadStartHandler();
      h->winid = winid;
      wv4->add_DownloadStarting(h, &tok);
      h->Release();
      wv4->Release();
    }
  }
  if (winid.rfind("popup", 0) == 0) {
    PopupTitleHandler *h = new PopupTitleHandler();
    h->winid = winid;
    wv->add_DocumentTitleChanged(h, &tok);
    h->Release();
  }
}

// -- TINYJS_TEST_AUTODLG=ok|cancel -------------------------------------------
// Test hook (env-gated, inert otherwise), ported from the macOS launcher:
// polls for a modal #32770 owned by this process and answers it the way a
// user would, so dialog paths run headless. MessageBox, the prompt template
// and the common file dialog all use ctrl IDs IDOK/IDCANCEL.
static void autodlg_arm() {
  char v[16];
  DWORD n = GetEnvironmentVariableA("TINYJS_TEST_AUTODLG", v, sizeof(v));
  if (!(n > 0 && n < sizeof(v)))
    return;
  bool okmode = std::string(v) == "ok";
  std::thread([okmode]() {
    DWORD pid = GetCurrentProcessId();
    for (;;) {
      Sleep(200);
      struct Ctx {
        DWORD pid;
        HWND found;
      } ctx = {pid, nullptr};
      EnumWindows(
          [](HWND h, LPARAM lp) -> BOOL {
            Ctx *c = (Ctx *)lp;
            DWORD wp = 0;
            GetWindowThreadProcessId(h, &wp);
            if (wp != c->pid || !IsWindowVisible(h))
              return TRUE;
            wchar_t cls[32];
            GetClassNameW(h, cls, 32);
            if (wcscmp(cls, L"#32770") != 0)
              return TRUE;
            c->found = h;
            return FALSE;
          },
          (LPARAM)&ctx);
      if (!ctx.found)
        continue;
      // MB_OK quirk (probed live): a single-button MessageBox gives its OK
      // button ctrl id 2 (IDCANCEL) so that close works — an ok-mode answer
      // must fall back to it or an alert() hangs forever.
      HWND ok = GetDlgItem(ctx.found, IDOK);
      HWND cancel = GetDlgItem(ctx.found, IDCANCEL);
      int want = okmode ? (ok ? IDOK : IDCANCEL) : (cancel ? IDCANCEL : IDOK);
      HWND btn = GetDlgItem(ctx.found, want);
      if (btn)
        PostMessageW(ctx.found, WM_COMMAND, MAKEWPARAM(want, BN_CLICKED),
                     (LPARAM)btn);
      else
        PostMessageW(ctx.found, WM_CLOSE, 0, 0);
      // Give the dialog time to actually close before scanning again, or a
      // slow dismiss reads as a second dialog and gets a second answer.
      Sleep(400);
    }
  }).detach();
}

struct EvalReq {
  std::string win, js;
};

static void do_eval_win(webview_t w, void *arg) {
  EvalReq *er = static_cast<EvalReq *>(arg);
  if (er->win == "main") {
    if (w)
      webview_eval(w, er->js.c_str());
  } else if (er->win == "*") {
    if (w)
      webview_eval(w, er->js.c_str());
    for (auto &kv : g_windows)
      secwin_eval(kv.first, er->js);
  } else {
    secwin_eval(er->win, er->js);
  }
  delete er;
}

// ---------------------------------------------------------------------------
// theme / system events

static bool read_theme_dark() {
  HKEY key;
  DWORD val = 1, size = sizeof(val);
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\"
                    L"Personalize",
                    0, KEY_READ, &key) == ERROR_SUCCESS) {
    RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                     (LPBYTE)&val, &size);
    RegCloseKey(key);
  }
  return val == 0;
}

static void send_theme() {
  g_theme_dark = read_theme_dark();
  pipe_write_line(std::string("SYS theme ") + (g_theme_dark ? "dark" : "light"));
}

// ---------------------------------------------------------------------------
// window proc subclass

static LRESULT CALLBACK tiny_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
  switch (msg) {
  case WM_GETMINMAXINFO: {
    LRESULT r = CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
    clamp_min_track(hwnd, lp, g_min_w, g_min_h);
    return r;
  }
  case WM_NCCALCSIZE:
    // Frameless polish: WS_CAPTION removal leaves a top frame sliver; extend
    // the client to the true top while keeping left/right/bottom resize
    // borders (top-edge resize is traded away — the standard frameless
    // compromise, since the WebView2 child owns all client hit-testing).
    if (wp && (g_frameless || g_square)) {
      RECT *rc = &((NCCALCSIZE_PARAMS *)lp)->rgrc[0];
      LONG top = rc->top;
      LRESULT r = CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
      rc->top = IsZoomed(hwnd)
                    ? top + GetSystemMetrics(SM_CYFRAME) +
                          GetSystemMetrics(SM_CXPADDEDBORDER)
                    : top;
      return r;
    }
    break;
  // The library's own WM_DPICHANGED sizes the frame for the scaled client but
  // leaves the menu bar out of its sum, so a window with a menu lost one menu
  // bar of height per screen crossing (Tiny Deck: 720 -> 700 -> 680). Let it
  // run, then fit the client to the scaled size exactly as secondaries do.
  case 0x02E4 /* WM_GETDPISCALEDSIZE */:
    g_secwin_old_dpi[hwnd] = (UINT)lround(window_scale(hwnd) * 96);
    break;
  case 0x02E0 /* WM_DPICHANGED */: {
    auto it = g_secwin_old_dpi.find(hwnd);
    UINT od = it != g_secwin_old_dpi.end() ? it->second : 0, nd = HIWORD(wp);
    if (it != g_secwin_old_dpi.end())
      g_secwin_old_dpi.erase(it);
    RECT c;
    GetClientRect(hwnd, &c);
    LRESULT r = CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
    if (od && nd && !IsZoomed(hwnd)) {
      double k = (double)nd / od;
      fit_client(hwnd, (int)lround(c.right * k), (int)lround(c.bottom * k));
    }
    return r;
  }
  case WM_TINY_DISPATCH: // windowless tiny_dispatch
    ((tiny_fn)wp)(g_w, (void *)lp);
    return 0;
  case WM_CLOSE:
    // The windowless host is plumbing, not a window anyone closes — it can be
    // the foreground window after a tray menu, where Alt+F4 would land on it.
    if (g_windowless)
      return 0;
    if (g_hide_on_close && can_live_hidden()) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    break;
  case WM_COMMAND: {
    auto it = g_cmd_reg.find((UINT)LOWORD(wp));
    if (it != g_cmd_reg.end() && it->second->kind == "menu") {
      if (!it->second->role.empty())
        send_stock_edit(it->second->role);
      else if (it->second->enabled)
        pipe_write_line("MENU " + it->second->id);
      return 0;
    }
    break;
  }
  case WM_SYSCOMMAND:
    // Minimize with no taskbar button (accessory / presence('menubar')): the
    // tray or the app is the way back, so hide rather than leave an owned
    // window's caption strip at the desktop's bottom-left (#56).
    if ((wp & 0xFFF0) == SC_MINIMIZE && hwnd == g_hwnd && g_no_taskbar) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    break;
  case WM_SETFOCUS:
    // Same as the secondaries: activation stops at the host window unless the
    // page is handed the keyboard explicitly.
    if (g_ctrl)
      g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    break;
  case WM_SIZE:
    // The webview library's own proc does the bounds work for main; this is
    // only the state event (maximize/minimize/restore, deduped in the emit).
    emit_winstate(hwnd);
    if (hwnd == g_hwnd)
      geom_msg(hwnd, msg);
    break;
  case WM_MOVE:
  case WM_ENTERSIZEMOVE:
  case WM_EXITSIZEMOVE:
    if (hwnd == g_hwnd)
      geom_msg(hwnd, msg);
    break;
  case WM_ACTIVATE:
    emit_winstate(hwnd, LOWORD(wp) != WA_INACTIVE);
    break;
  case WM_DISPLAYCHANGE:
    // screens() changed: tell the bridge once things settle (a replug sends
    // a burst of these)
    SetTimer(hwnd, TIMER_SCREENS, 500, nullptr);
    // A monitor departed (or the layout changed) mid-session: windows it was
    // holding are now stranded in space. One pass over what's VISIBLE — the
    // main wndproc alone handles this so N windows don't run N passes.
    // Hidden windows get their chance from the bridge when shown.
    if (g_rescue_on) {
      if (IsWindowVisible(hwnd))
        rescue_offscreen(hwnd);
      for (auto &kv : g_windows)
        if (kv.second && kv.second->hwnd && IsWindowVisible(kv.second->hwnd))
          rescue_offscreen(kv.second->hwnd);
    }
    break;
  case WM_TINY_TRAY:
    switch (LOWORD(lp)) {
    case WM_LBUTTONUP:
      if (g_tray_primary || !g_tray_menu)
        pipe_write_line("TRAYCLICK");
      else
        tray_popup(g_tray_menu, "TRAY");
      break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      if (g_tray_menu)
        tray_popup(g_tray_menu, "TRAY");
      else
        pipe_write_line("TRAYCLICK");
      break;
    case NIN_BALLOONUSERCLICK:
      pipe_write_line("NOTIFYCLICK " + g_last_notif_id);
      break;
    }
    return 0;
  case WM_HOTKEY: {
    auto it = g_hotkeys.find((int)wp);
    if (it != g_hotkeys.end())
      pipe_write_line("HOTKEY " + it->second);
    return 0;
  }
  case WM_TIMER:
    if (wp == TIMER_SCREENS) {
      KillTimer(hwnd, TIMER_SCREENS);
      pipe_write_line("SYS screens");
      return 0;
    }
    if (wp == TIMER_DEFERSHOW) {
      KillTimer(hwnd, TIMER_DEFERSHOW);
      if (g_defer_show && g_hwnd) {
        g_defer_show = false;
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
      }
      return 0;
    }
    if (wp == TIMER_CLIPWATCH) {
      DWORD seq = GetClipboardSequenceNumber();
      if (seq != g_clip_last_seq) {
        g_clip_last_seq = seq;
        bool self = seq == g_clip_self_seq;
        pipe_write_line("CLIPCHANGE " + std::to_string(seq) + " " +
                        (self ? "1" : "0"));
      }
      return 0;
    }
    break;
  case WM_SETTINGCHANGE: {
    if (wp == SPI_SETWORKAREA) // taskbar moved/resized: visible rects changed
      SetTimer(hwnd, TIMER_SCREENS, 500, nullptr);
    bool dark = read_theme_dark();
    if (dark != g_theme_dark) {
      g_theme_dark = dark;
      pipe_write_line(std::string("SYS theme ") + (dark ? "dark" : "light"));
    }
    break;
  }
  case WM_POWERBROADCAST:
    if (wp == PBT_APMSUSPEND && !g_asleep) {
      g_asleep = true;
      pipe_write_line("SYS sleep");
    } else if ((wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) &&
               g_asleep) {
      g_asleep = false;
      pipe_write_line("SYS wake");
    }
    break;
  }
  return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// simple UI-thread thunks

static void do_terminate(webview_t w, void *) {
  if (w)
    webview_terminate(w);
  else
    PostQuitMessage(0); // windowless: our own loop (see run)
}

static void do_eval(webview_t w, void *arg) {
  std::string *js = static_cast<std::string *>(arg);
  if (w)
    webview_eval(w, js->c_str());
  delete js;
}

static void do_title(webview_t w, void *arg) {
  std::string *t = static_cast<std::string *>(arg);
  if (w)
    webview_set_title(w, t->c_str());
  delete t;
}

struct SizeReq {
  int width, height;
  std::string win = "main";
};

// win.setSize takes the CLIENT size — the page's own box, the same units
// window CREATION takes and the same units win.getState() now reports back, so
// set -> get round-trips. That contract matters more than it looks: an app that
// re-asserts its own size (read getState, hand the width back to setSize) is
// doing something the API invites, and if the two ends disagree by the window
// border the size ratchets up by that border on every pass. It did once, when
// this set the client area while getState returned GetWindowRect: a plain
// read-modify-write grew a window 13px wider and 36px taller each time (amp's
// windowshade guard walked off the right of the screen). That was fixed by
// moving BOTH ends to frame units, which round-tripped but left creation — a
// client size — as the odd one out. Both ends are client units now.
//
// The insets are measured off the live window rather than computed with
// AdjustWindowRect: the main window and frameless satellites answer
// WM_NCCALCSIZE with "no non-client area", so their real insets are zero while
// AdjustWindowRect would still add a resize border and inflate the page.
static void do_size(webview_t w, void *arg) {
  SizeReq *s = static_cast<SizeReq *>(arg);
  HWND h = s->win == "main" ? g_hwnd : hwnd_for_win(s->win);
  if (h) {
    double sc = window_scale(h); // wire is logical; SetWindowPos physical
    RECT o, c;
    GetWindowRect(h, &o);
    GetClientRect(h, &c);
    LONG pad_w = (o.right - o.left) - c.right;   // 0 once borderless-client
    LONG pad_h = (o.bottom - o.top) - c.bottom;
    g_programmatic_size = true;  // the app's own size beats the user floor
    SetWindowPos(h, nullptr, 0, 0, (int)lround(s->width * sc) + pad_w,
                 (int)lround(s->height * sc) + pad_h, SWP_NOMOVE | SWP_NOZORDER);
    g_programmatic_size = false;
  }
  delete s;
}

static void do_title_win(webview_t, void *arg) {
  EvalReq *er = static_cast<EvalReq *>(arg); // win + text ride in EvalReq
  HWND h = hwnd_for_win(er->win);
  if (h)
    SetWindowTextW(h, widen(er->js).c_str());
  delete er;
}

static void do_reload(webview_t w, void *) {
  if (g_wv2)
    g_wv2->Reload();
  else if (w)
    webview_eval(w, "location.reload()");
  else // windowless: app.reload() means the named windows that are open
    for (auto &kv : g_windows)
      if (kv.second->wv)
        kv.second->wv->Reload();
}

struct ReplyReq {
  std::string id;
  int status;
  std::string json;
};

static void do_reply(webview_t w, void *arg) {
  ReplyReq *req = static_cast<ReplyReq *>(arg);
  route_ret(w, req->id, req->status, req->json);
  delete req;
}

// ---- taskbar button: progress bar (ITaskbarList3) --------------------------
// Windows gives this to us natively — unlike macOS, where the bar has to be
// drawn into the Dock tile by hand. The UI thread is already an STA, so the
// interface can be created lazily here and reused.
static ITaskbarList3 *taskbar_list() {
  static ITaskbarList3 *tb = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    // explicit IID rather than IID_PPV_ARGS, matching CoCreateInstance use
    // elsewhere in this file (MinGW headers, not the MSVC SDK)
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                                   (void **)&tb))) {
      if (FAILED(tb->HrInit())) {
        tb->Release();
        tb = nullptr;
      }
    }
  }
  return tb;
}

// ---- app.badge: an overlay icon on the taskbar button ----------------------
// UNTESTED — written on macOS, never run on Windows. capabilities() still
// reports badge:false; flip it only once this has been SEEN, not merely
// compiled. The taskbar overlay is ~16px, so this is 1-2 glyphs or a dot.
//
// The trap: GDI writes RGB but leaves the alpha byte of a 32bpp DIB alone, so
// text drawn the obvious way is fully transparent. Everything is painted in
// colours that are never pure black, then the alpha byte is set for every
// pixel that got written.
static HICON g_overlay = nullptr;

static HICON badge_icon(const std::string &text) {
  int n = GetSystemMetrics(SM_CXSMICON);
  if (n <= 0) n = 16;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = n;
  bi.bmiHeader.biHeight = -n; // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP colour = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!colour) {
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return nullptr;
  }
  std::memset(bits, 0, (size_t)n * n * 4);
  HGDIOBJ oldBmp = SelectObject(dc, colour);

  // filled disc
  HBRUSH brush = CreateSolidBrush(RGB(214, 69, 65));
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(214, 69, 65));
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  Ellipse(dc, 0, 0, n, n);

  // 1-2 glyphs, centred; longer text can't fit at this size
  std::wstring w = widen(text);
  if (w.size() > 2) w = L"\u2022"; // bullet
  if (!w.empty()) {
    LOGFONTW lf = {};
    lf.lfHeight = -(n * (w.size() > 1 ? 5 : 7) / 10);
    lf.lfWeight = FW_BOLD;
    lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = {0, 0, n, n};
    DrawTextW(dc, w.c_str(), (int)w.size(), &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    SelectObject(dc, oldFont);
    DeleteObject(font);
  }

  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
  DeleteObject(brush);

  // GDI never touched alpha — opaque wherever anything was drawn
  unsigned char *px = static_cast<unsigned char *>(bits);
  for (int i = 0; i < n * n; i++) {
    unsigned char *p = px + i * 4;
    p[3] = (p[0] || p[1] || p[2]) ? 255 : 0;
  }

  HBITMAP mask = CreateBitmap(n, n, 1, 1, nullptr); // unused with an alpha DIB
  ICONINFO ii = {};
  ii.fIcon = TRUE;
  ii.hbmColor = colour;
  ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);

  SelectObject(dc, oldBmp);
  DeleteObject(mask);
  DeleteObject(colour);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return icon;
}

static void do_badge(webview_t, void *arg) {
  std::string *text = static_cast<std::string *>(arg);
  ITaskbarList3 *tb = taskbar_list();
  if (tb && g_hwnd) {
    HICON next = text->empty() ? nullptr : badge_icon(*text);
    tb->SetOverlayIcon(g_hwnd, next, next ? L"badge" : nullptr);
    if (g_overlay) DestroyIcon(g_overlay); // only after the shell has the new one
    g_overlay = next;
  }
  delete text;
}

static void do_progress(webview_t, void *arg) {
  double *v = static_cast<double *>(arg);
  ITaskbarList3 *tb = taskbar_list();
  if (tb && g_hwnd) {
    if (*v < 0.0) {
      tb->SetProgressState(g_hwnd, TBPF_NOPROGRESS);
    } else {
      double p = *v > 1.0 ? 1.0 : *v;
      tb->SetProgressState(g_hwnd, TBPF_NORMAL);
      tb->SetProgressValue(g_hwnd, (ULONGLONG)(p * 1000.0), 1000ULL);
    }
  }
  delete v;
}

// ---- app.icon: the window icon (title bar + Alt-Tab) ----------------------
// NOT the taskbar button: measured on Windows 11, WM_SETICON moves the title
// bar and Alt-Tab entry while the button keeps showing launcher-win.exe's own
// icon. See TODO-verify.md.
//
// .ico goes through LoadImage, which picks the right sub-image per size;
// everything else (png/jpg/bmp/gif) goes through GDI+, the same decoder the
// startup icon and tray icons already use. png must work here — icon.png is
// what every tinyjs app actually ships.
static HICON g_icon_default = nullptr; // startup icon (TINYJS_ICON), for ''
static HICON g_icon_big = nullptr;
static HICON g_icon_small = nullptr;

// Release what app.icon installed. Two traps: the GDI+ path uses ONE HICON for
// both sizes, so a naive pair of DestroyIcon calls double-frees it; and the
// startup icon is shared, so destroying it would break a later icon('').
static void free_app_icons() {
  if (g_icon_big && g_icon_big != g_icon_default)
    DestroyIcon(g_icon_big);
  if (g_icon_small && g_icon_small != g_icon_big &&
      g_icon_small != g_icon_default)
    DestroyIcon(g_icon_small);
  g_icon_big = g_icon_small = nullptr;
}

// Windowless there's no main window to carry the app icon — the named
// windows do (they also get it at open, see do_winopen).
static void send_app_icon(HICON big, HICON small) {
  std::vector<HWND> targets;
  if (g_hwnd)
    targets.push_back(g_hwnd);
  else
    for (auto &kv : g_windows)
      if (kv.second->hwnd)
        targets.push_back(kv.second->hwnd);
  for (HWND h : targets) {
    SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big);
    SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)small);
  }
}

// Windowless: a new named window wears the current app icon, the way main
// would have.
static void apply_app_icon(HWND hwnd) {
  HICON big = g_icon_big ? g_icon_big : g_icon_default;
  HICON small = g_icon_small ? g_icon_small : g_icon_default;
  if (!big)
    return;
  SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

static void do_appicon(webview_t, void *arg) {
  std::string *path = static_cast<std::string *>(arg);
  if (path->empty()) {
    // Back to the icon the app started with. Sending 0 instead would fall
    // through to the window class icon and lose icon.png for the session.
    free_app_icons();
    send_app_icon(g_icon_default, g_icon_default);
  } else {
    std::wstring w = widen(*path);
    HICON big = (HICON)LoadImageW(nullptr, w.c_str(), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXICON),
                                  GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    HICON small = big ? (HICON)LoadImageW(nullptr, w.c_str(), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON),
                                          GetSystemMetrics(SM_CYSMICON),
                                          LR_LOADFROMFILE)
                      : nullptr;
    if (!big) // not an .ico (or an unreadable one) — try it as an image
      big = small = icon_from_png(*path);
    if (big) {
      free_app_icons(); // only after the new icon is in hand
      g_icon_big = big;
      g_icon_small = small ? small : big;
      send_app_icon(g_icon_big, g_icon_small);
    }
  }
  delete path;
}

static void do_attention(webview_t, void *arg) {
  int *critical = static_cast<int *>(arg);
  if (!g_hwnd) { // windowless: no main taskbar button to flash
    delete critical;
    return;
  }
  FLASHWINFO fi = {sizeof(fi)};
  fi.hwnd = g_hwnd;
  fi.dwFlags = FLASHW_ALL | (*critical ? FLASHW_TIMER : FLASHW_TIMERNOFG);
  fi.uCount = *critical ? 20 : 3;
  FlashWindowEx(&fi);
  delete critical;
}

// ============================ tiny.audioTap ================================
// Read the whole system's *rendered* audio output as PCM chunks, for VU meters
// / visualizers — including audio that never touches Web Audio. On macOS this
// uses Core Audio process taps (see native/launcher-macos.cc); Windows has no
// per-process render tap in a MinGW-linkable form, so this implements the
// 'system' scope only, via WASAPI loopback capture off the default render
// endpoint. Loopback hears the whole mix, so `excludeSelf` cannot be honoured
// (there is no way to subtract our own render) and is ignored. scope 'app'
// answers 'unsupported'.
//
// A dedicated thread (its own MTA CoInitialize) opens IAudioClient in shared
// loopback mode, polls IAudioCaptureClient every ~10ms, converts the mix format
// (usually float32) to interleaved Int16, and every `interval` ms base64-encodes
// the accumulated PCM and pipes it out as `AUDIOTAP <b64>\t<sr>\t<ch>\t<frames>\t<t>`.
// To match the macOS tap's steady cadence, an interval that saw no packets
// (nothing playing) still emits a silent chunk sized to the interval, so chunks
// always arrive on schedule. Only one tap runs at a time; a new start (or STOP)
// stops the old one first. Raw COM throughout, matching the SAPI/WinRT sections;
// the GUIDs libuuid may lack are defined locally below.

struct AudioTapReq { std::string qid; std::string scope; bool excludeSelf; int interval; };

static const CLSID kCLSID_MMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID kIID_IMMDeviceEnumerator =
    {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID kIID_IAudioClient =
    {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID kIID_IAudioCaptureClient =
    {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
// WAVE_FORMAT_EXTENSIBLE subformats (mmreg.h's are DEFINE_GUIDEX, not always
// linkable under MinGW libuuid, so define our own for comparison).
static const GUID kSUBTYPE_IEEE_FLOAT =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID kSUBTYPE_PCM =
    {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static std::string tap_base64(const uint8_t *data, size_t len) {
  static const char t[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out += t[(n >> 18) & 63]; out += t[(n >> 12) & 63];
    out += t[(n >> 6) & 63];  out += t[n & 63];
  }
  if (i < len) {
    uint32_t n = (uint32_t)data[i] << 16;
    if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
    out += t[(n >> 18) & 63];
    out += t[(n >> 12) & 63];
    out += (i + 1 < len) ? t[(n >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

static std::atomic<bool> g_tap_running{false};
static std::thread g_tap_thread;

// Stop the running capture thread (idempotent). Must NOT be called from the
// capture thread itself.
static void tiny_audiotap_stop() {
  g_tap_running = false;
  if (g_tap_thread.joinable())
    g_tap_thread.join();
}

// The capture thread. Owns all COM objects; replies GOT once capturing (or on
// failure), then loops emitting chunks until g_tap_running clears.
static void tiny_audiotap_thread(AudioTapReq req) {
  int interval = req.interval < 20 ? 20 : (req.interval > 500 ? 500 : req.interval);
  bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

  IMMDeviceEnumerator *en = nullptr;
  IMMDevice *dev = nullptr;
  IAudioClient *ac = nullptr;
  IAudioCaptureClient *cap = nullptr;
  WAVEFORMATEX *mix = nullptr;
  auto fail = [&](const char *msg) {
    if (cap) cap->Release();
    if (ac) ac->Release();
    if (dev) dev->Release();
    if (en) en->Release();
    if (mix) CoTaskMemFree(mix);
    if (com) CoUninitialize();
    g_tap_running = false;
    got(req.qid, std::string("{\"ok\":false,\"code\":\"failed\",\"message\":\"") +
                     msg + "\"}");
  };

  if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                              kIID_IMMDeviceEnumerator, (void **)&en)))
    return fail("MMDeviceEnumerator unavailable");
  if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
    return fail("no default render device");
  if (FAILED(dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&ac)))
    return fail("IAudioClient activation failed");
  if (FAILED(ac->GetMixFormat(&mix)) || !mix)
    return fail("GetMixFormat failed");
  if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                            10000000 /*1s*/, 0, mix, nullptr)))
    return fail("IAudioClient Initialize (loopback) failed");
  if (FAILED(ac->GetService(kIID_IAudioCaptureClient, (void **)&cap)))
    return fail("IAudioCaptureClient unavailable");
  if (FAILED(ac->Start()))
    return fail("IAudioClient Start failed");

  // Decode the mix format once. Shared-mode mix is almost always float32, but
  // handle 16/24/32-bit PCM too.
  int channels = mix->nChannels > 0 ? mix->nChannels : 2;
  int sampleRate = mix->nSamplesPerSec > 0 ? (int)mix->nSamplesPerSec : 48000;
  int bits = mix->wBitsPerSample;
  int bytesPerSample = bits / 8;
  bool isFloat = false;
  WORD tag = mix->wFormatTag;
  if (tag == WAVE_FORMAT_EXTENSIBLE &&
      mix->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)mix;
    if (IsEqualGUID(ext->SubFormat, kSUBTYPE_IEEE_FLOAT)) isFloat = true;
    else if (IsEqualGUID(ext->SubFormat, kSUBTYPE_PCM)) isFloat = false;
  } else if (tag == WAVE_FORMAT_IEEE_FLOAT) {
    isFloat = true;
  }

  got(req.qid, "{\"ok\":true,\"sampleRate\":" + std::to_string(sampleRate) +
                   ",\"channels\":" + std::to_string(channels) + "}");

  std::vector<int16_t> acc; // interleaved Int16 accumulated since last emit
  ULONGLONG lastEmit = GetTickCount64();

  auto append_samples = [&](const BYTE *pdata, UINT32 frames, bool silent) {
    size_t n = (size_t)frames * channels;
    size_t base = acc.size();
    acc.resize(base + n);
    if (silent || !pdata) {
      std::fill(acc.begin() + base, acc.end(), (int16_t)0);
      return;
    }
    for (UINT32 f = 0; f < frames; f++) {
      for (int c = 0; c < channels; c++) {
        const BYTE *s = pdata + ((size_t)f * channels + c) * bytesPerSample;
        int16_t v = 0;
        if (isFloat && bytesPerSample == 4) {
          float fv; memcpy(&fv, s, 4);
          if (fv > 1.0f) fv = 1.0f; else if (fv < -1.0f) fv = -1.0f;
          v = (int16_t)lrintf(fv * 32767.0f);
        } else if (!isFloat && bytesPerSample == 2) {
          memcpy(&v, s, 2);
        } else if (!isFloat && bytesPerSample == 4) {
          int32_t iv; memcpy(&iv, s, 4);
          v = (int16_t)(iv >> 16);
        } else if (!isFloat && bytesPerSample == 3) {
          int32_t iv = (s[0] << 8) | (s[1] << 16) | (s[2] << 24);
          v = (int16_t)(iv >> 16);
        }
        acc[base + (size_t)f * channels + c] = v;
      }
    }
  };

  while (g_tap_running) {
    // Drain all queued loopback packets into the accumulator.
    UINT32 pkt = 0;
    while (SUCCEEDED(cap->GetNextPacketSize(&pkt)) && pkt > 0) {
      BYTE *pdata = nullptr; UINT32 frames = 0; DWORD flags = 0;
      if (FAILED(cap->GetBuffer(&pdata, &frames, &flags, nullptr, nullptr)))
        break;
      append_samples(pdata, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
      cap->ReleaseBuffer(frames);
    }

    ULONGLONG now = GetTickCount64();
    if (now - lastEmit >= (ULONGLONG)interval) {
      std::vector<int16_t> chunk;
      chunk.swap(acc);
      if (chunk.empty()) {
        // Nothing played this interval — emit silence so chunks stay on cadence.
        int frames = (int)((ULONGLONG)sampleRate * interval / 1000);
        chunk.assign((size_t)frames * channels, 0);
      }
      int frames = (int)(chunk.size() / channels);
      std::string b64 = tap_base64((const uint8_t *)chunk.data(),
                                   chunk.size() * sizeof(int16_t));
      char meta[96];
      snprintf(meta, sizeof(meta), "\t%d\t%d\t%d\t%llu", sampleRate, channels,
               frames, (unsigned long long)now);
      pipe_write_line("AUDIOTAP " + b64 + meta);
      lastEmit = now;
    }
    Sleep(10);
  }

  ac->Stop();
  cap->Release();
  ac->Release();
  dev->Release();
  en->Release();
  CoTaskMemFree(mix);
  if (com) CoUninitialize();
}

// Handle an AUDIOTAP start request (from the pipe read loop). Answers GOT.
static void tiny_audiotap_start(AudioTapReq req) {
  if (req.scope == "app") {
    // Best-effort parity: Windows per-process capture needs the Win10 2004+
    // process-loopback path; until then scope 'app' rides the system
    // loopback. For the dominant use (a media app metering ITSELF) the mix
    // is a fine approximation — visualizers get real samples either way.
    req.scope = "system";
  }
  if (req.scope != "system") {
    got(req.qid, "{\"ok\":false,\"code\":\"unsupported\",\"message\":\"unknown "
                 "audioTap scope\"}");
    return;
  }
  tiny_audiotap_stop();          // only one tap at a time; stop any prior one
  g_tap_running = true;
  g_tap_thread = std::thread(tiny_audiotap_thread, std::move(req));
}

// ---------------------------------------------------------------------------
// pipe read loop (background thread; UI work hops via tiny_dispatch)

static void lock_instance_pipe(const std::string &name); // PIPELOCK, below

static void pipe_read_loop() {
  std::string buf;
  char chunk[4096];
  std::vector<MenuSpec> pending_menus;
  std::string pending_menu_win;  // MENUBEGIN@<win>; empty = the app menu
  bool in_menu_block = false;
  TraySpec pending_tray;
  bool in_tray_block = false;
  bool in_ctx_block = false;
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
  // Ops that carry a qid and MUST answer GOT so the backend promise settles;
  // unsupported ones are answered inline right here.
  auto qid_of = [](const std::string &line, size_t oplen) {
    size_t sp = line.find(' ', oplen);
    return sp == std::string::npos ? line.substr(oplen)
                                   : line.substr(oplen, sp - oplen);
  };
  for (;;) {
    DWORD n = 0;
    if (!overlapped_io(false, chunk, sizeof(chunk), &n))
      break;
    buf.append(chunk, n);
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
        rr->id = line.substr(4, sp1 - 4);
        rr->status = std::atoi(line.c_str() + sp1 + 1);
        rr->json = line.substr(sp2 + 1);
        tiny_dispatch(do_reply, rr);
      } else if (line.rfind("EVAL", 0) == 0 &&
                 (line[4] == ' ' || line[4] == '@')) {
        EvalReq *er = new EvalReq;
        if (line[4] == ' ') {
          er->win = "main";
          er->js = wire_unescape(line.substr(5));
        } else {
          size_t sp = line.find(' ', 5);
          if (sp == std::string::npos) {
            delete er;
            continue;
          }
          er->win = line.substr(5, sp - 5);
          er->js = wire_unescape(line.substr(sp + 1));
        }
        tiny_dispatch(do_eval_win, er);
      } else if (line.rfind("TITLE@", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_title_win,
                         new EvalReq{line.substr(6, sp - 6),
                                     line.substr(sp + 1)});
      } else if (line.rfind("TITLE ", 0) == 0) {
        tiny_dispatch(do_title, new std::string(line.substr(6)));
      } else if (line.rfind("SIZE@", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos)
          continue;
        SizeReq *s = new SizeReq{600, 400};
        s->win = line.substr(5, sp - 5);
        std::sscanf(line.c_str() + sp + 1, "%d %d", &s->width, &s->height);
        tiny_dispatch(do_size, s);
      } else if (line.rfind("SIZE ", 0) == 0) {
        SizeReq *s = new SizeReq{960, 640};
        std::sscanf(line.c_str() + 5, "%d %d", &s->width, &s->height);
        tiny_dispatch(do_size, s);
      } else if (line.rfind("WINOPEN ", 0) == 0) {
        // <id>\t<page>\t<title>\t<WxH>[\t<frame>\t<traffic>\t<transp>\t<vib>
        //   \t<square>\t<firstMouse>\t<x>\t<y>\t<menu>\t<wcPos>\t<parent>
        //   \t<panel>\t<show: "0" = hidden>]
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
        wr->menu = p.size() > 12 ? p[12] : "";
        // p[13] is windowControlsPos (macOS-only), p[14] parent, p[15]
        // chrome.panel (macOS-only), p[16] "0" = show: false.
        wr->parent = p.size() > 14 ? p[14] : "";
        wr->hidden = p.size() > 16 && p[16] == "0";
        tiny_dispatch(do_winopen, wr);
      } else if (line.rfind("PIPELOCK ", 0) == 0) {
        lock_instance_pipe(line.substr(9)); // off the UI thread on purpose
      } else if (line.rfind("WINCLOSE ", 0) == 0) {
        tiny_dispatch(do_winclose, new std::string(line.substr(9)));
      } else if (line.rfind("DLG ", 0) == 0) {
        size_t sp1 = line.find(' ', 4);
        if (sp1 == std::string::npos)
          continue;
        std::vector<std::string> parts = split_tabs(line.substr(sp1 + 1));
        DlgReq *req = new DlgReq;
        req->id = line.substr(4, sp1 - 4);
        req->op = parts[0];
        req->args.assign(parts.begin() + 1, parts.end());
        tiny_dispatch(do_dialog, req);
      } else if (line == "MENUBEGIN" || line.rfind("MENUBEGIN@", 0) == 0) {
        // Bare: the app menu, shown by every window that hasn't overridden.
        // @<win>: that window's own, main included (main has to name itself —
        // a bare MENUBEGIN already means something else).
        pending_menu_win = line.size() > 10 ? line.substr(10) : "";
        pending_menus.clear();
        collapse_subs();
        build_stack.assign(1, {});
        in_menu_block = true;
      } else if (line.rfind("MENURESET@", 0) == 0) {
        tiny_dispatch(do_reset_menu, new std::string(line.substr(10)));
      } else if (in_menu_block && line.rfind("MENU ", 0) == 0) {
        flush_root();
        pending_menus.push_back(MenuSpec{line.substr(5), {}});
        build_stack.assign(1, {});
      } else if (in_menu_block && line.rfind("MENUROLE ", 0) == 0) {
        // A standard-menu slot (MENUROLE edit, macOS's Edit menu). Win32 has
        // no launcher-owned menu to place, so it draws only the items the app
        // put in it (see render_menu) — but even an empty one still
        // claims an entry, or the next MENU's flush lands on the menu before
        // it and empties that instead (see MenuSpec::role).
        flush_root();
        pending_menus.push_back(MenuSpec{"", {}, line.substr(9)});
        build_stack.assign(1, {});
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("ITEM ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(5));
        MenuItemSpec it;
        it.id = p.size() > 0 ? p[0] : "";
        it.label = p.size() > 1 ? p[1] : it.id;
        it.key = p.size() > 2 ? p[2] : "";
        if (p.size() > 3) {
          it.checked = p[3].find('c') != std::string::npos;
          it.disabled = p[3].find('d') != std::string::npos;
        }
        it.icon = p.size() > 4 ? p[4] : "";
        build_stack.back().push_back(it);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line == "SEP") {
        MenuItemSpec sep;
        sep.separator = true;
        build_stack.back().push_back(sep);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("ROLEITEM ", 0) == 0) {
        // a stock editing item (copy, paste, …, or the `standard` group)
        MenuItemSpec it;
        it.role = line.substr(9);
        build_stack.back().push_back(it);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("SUB ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(4));
        MenuItemSpec parent;
        parent.id = p.size() > 0 ? p[0] : "";
        parent.label = p.size() > 1 ? p[1] : parent.id;
        parent.icon = p.size() > 2 ? p[2] : "";
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
        tiny_dispatch(do_apply_menus,
                         new ApplyMenuReq{pending_menu_win, pending_menus});
      } else if (line.rfind("TRAYBEGIN", 0) == 0) {
        pending_tray = TraySpec{};
        std::vector<std::string> p =
            line.size() > 10 ? split_tabs(line.substr(10))
                             : std::vector<std::string>{};
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
        tiny_dispatch(apply_tray, new TraySpec(pending_tray));
      } else if (line == "TRAYREMOVE") {
        TraySpec *rm = new TraySpec{};
        rm->remove = true;
        tiny_dispatch(apply_tray, rm);
      } else if (line.rfind("WINOP", 0) == 0 &&
                 (line[5] == ' ' || line[5] == '@')) {
        std::string win = "main";
        size_t body = 6;
        if (line[5] == '@') {
          size_t sp = line.find(' ', 6);
          if (sp == std::string::npos)
            continue;
          win = line.substr(6, sp - 6);
          body = sp + 1;
        }
        tiny_dispatch(do_winop, new WinopReq{win, line.substr(body)});
      } else if (line == "CTXBEGIN") {
        collapse_subs();
        build_stack.assign(1, {});
        in_ctx_block = true;
      } else if (line == "CTXEND") {
        in_ctx_block = false;
        tiny_dispatch(apply_ctx, new CtxReq{take_root(), true});
      } else if (line == "CTXCLEAR") {
        tiny_dispatch(apply_ctx, new CtxReq{{}, false});
      } else if (line.rfind("CTXSUPPRESS ", 0) == 0) {
        g_ctx_suppress = line.substr(12) == "1";
        tiny_dispatch([](webview_t, void *arg) {
          apply_ctx_suppress_fallback(arg != nullptr);
        }, g_ctx_suppress ? (void *)1 : nullptr);
      } else if (line.rfind("HKREG ", 0) == 0) {
        // HKREG <qid> <id>\t<combo> — answered, so register() can say
        // whether it worked
        std::string rest = line.substr(6);
        size_t sp = rest.find(' ');
        std::vector<std::string> p =
            split_tabs(sp == std::string::npos ? "" : rest.substr(sp + 1));
        if (p.size() >= 2)
          tiny_dispatch(do_hotkey, new HotkeyReq{p[0], p[1], rest.substr(0, sp)});
      } else if (line.rfind("HKUNREG ", 0) == 0) {
        tiny_dispatch(do_hotkey, new HotkeyReq{line.substr(8), ""});
      } else if (line.rfind("MENUUPD", 0) == 0 &&
                 (line[7] == ' ' || line[7] == '@')) {
        // MENUUPD <fields> patches every window's copy of the id;
        // MENUUPD@<win> <fields> just that window's.
        std::string win;
        size_t body = 8;
        if (line[7] == '@') {
          size_t sp = line.find(' ', 8);
          if (sp == std::string::npos)
            continue;
          win = line.substr(8, sp - 8);
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        MenuUpdReq *req = new MenuUpdReq;
        req->win = win;
        req->id = p.size() > 0 ? p[0] : "";
        req->label = p.size() > 1 ? p[1] : "";
        req->checked = p.size() > 2 ? p[2] : "";
        req->enabled = p.size() > 3 ? p[3] : "";
        tiny_dispatch(do_menu_update, req);
      } else if (line.rfind("GET ", 0) == 0) {
        size_t sp1 = line.find(' ', 4);
        if (sp1 == std::string::npos)
          continue;
        tiny_dispatch(do_get,
                         new GetReq{line.substr(4, sp1 - 4),
                                    line.substr(sp1 + 1)});
      } else if (line.rfind("NOTIFY ", 0) == 0) {
        std::vector<std::string> p = split_tabs(line.substr(7));
        NotifReq *req = new NotifReq;
        req->id = p.size() > 0 ? p[0] : "";
        req->title = p.size() > 1 ? p[1] : "";
        req->body = p.size() > 2 ? p[2] : "";
        req->subtitle = p.size() > 3 ? p[3] : "";
        req->sound = p.size() > 4 && p[4] == "1";
        req->actions_json = p.size() > 5 ? wire_unescape(p[5]) : "";
        tiny_dispatch(do_notify, req);
      } else if (line.rfind("CHROME", 0) == 0 &&
                 (line[6] == ' ' || line[6] == '@')) {
        std::string win = "main";
        size_t body = 7;
        if (line[6] == '@') {
          size_t sp = line.find(' ', 7);
          if (sp == std::string::npos)
            continue;
          win = line.substr(7, sp - 7);
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        ChromeReq *req = new ChromeReq;
        req->win = win;
        req->frame = p.size() > 0 ? p[0] : "";
        req->traffic = p.size() > 1 ? p[1] : "";
        req->transparent = p.size() > 2 ? p[2] : "";
        req->vibrancy = p.size() > 3 ? p[3] : "";
        req->square = p.size() > 4 ? p[4] : "";
        req->first_mouse = p.size() > 5 ? p[5] : "";
        req->menu = p.size() > 6 ? p[6] : "";
        tiny_dispatch(do_chrome, req);
      } else if (line.rfind("DRAGWIN", 0) == 0 &&
                 (line.size() == 7 || line[7] == '@')) {
        std::string win = line.size() > 8 ? line.substr(8) : "main";
        tiny_dispatch(do_ncdrag, new NcDragReq{win, HTCAPTION});
      } else if (line.rfind("RESIZEWIN", 0) == 0 && line.size() > 9 &&
                 (line[9] == ' ' || line[9] == '@')) {
        std::string win = "main";
        size_t body = 10;
        if (line[9] == '@') {
          size_t sp = line.find(' ', 10);
          if (sp == std::string::npos)
            continue;
          win = line.substr(10, sp - 10);
          body = sp + 1;
        }
        tiny_dispatch(do_ncdrag,
                      new NcDragReq{win, ht_for_edge(line.substr(body))});
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
        tiny_dispatch(do_clip_write, req);
      } else if (line.rfind("CLIPWATCH ", 0) == 0) {
        tiny_dispatch(do_clip_watch,
                         new int(std::atoi(line.c_str() + 10)));
      } else if (line.rfind("KEYSTROKE ", 0) == 0) {
        size_t sp = line.find(' ', 10);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_keystroke,
                         new KeystrokeReq{line.substr(10, sp - 10),
                                          line.substr(sp + 1)});
      } else if (line.rfind("PERMCHK ", 0) == 0 ||
                 line.rfind("PERMREQ ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_perm,
                         new QReq{line.substr(8, sp - 8), line.substr(sp + 1)});
      } else if (line.rfind("SHELL ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_shell,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("POWER ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_power,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("SOUND", 0) == 0 &&
                 (line.size() == 5 || line[5] == ' ')) {
        std::string rest = line.size() > 6 ? line.substr(6) : "";
        size_t sp = rest.find(' ');
        std::string qid = sp == std::string::npos ? rest : rest.substr(0, sp);
        std::string target = sp == std::string::npos ? "" : rest.substr(sp + 1);
        tiny_dispatch(do_sound, new QReq{qid, target});
      } else if (line.rfind("SECRET ", 0) == 0) {
        size_t sp = line.find(' ', 7);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_secret,
                         new QReq{line.substr(7, sp - 7), line.substr(sp + 1)});
      } else if (line.rfind("COOKIES ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_cookies,
                         new QReq{line.substr(8, sp - 8), line.substr(sp + 1)});
      } else if (line.rfind("ATTENTION", 0) == 0) {
        tiny_dispatch(do_attention,
                         new int(line.size() > 10 ? std::atoi(line.c_str() + 10)
                                                  : 0));
      } else if (line == "BADGE" || line.rfind("BADGE ", 0) == 0) {
        tiny_dispatch(do_badge,
                         new std::string(line.size() > 6
                                             ? wire_unescape(line.substr(6))
                                             : ""));
      } else if (line.rfind("PROGRESS", 0) == 0) {
        tiny_dispatch(do_progress,
                         new double(line.size() > 9
                                        ? std::atof(line.c_str() + 9)
                                        : -1.0));
      } else if (line == "APPICON" || line.rfind("APPICON ", 0) == 0) {
        tiny_dispatch(do_appicon,
                         new std::string(line.size() > 8
                                             ? wire_unescape(line.substr(8))
                                             : ""));
      } else if (line.rfind("LOGIN ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) {
          got(line.substr(6), "{\"status\":\"unsupported\"}");
          continue;
        }
        tiny_dispatch(do_login,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("VOICES ", 0) == 0) {
        tiny_dispatch(do_voices, new QReq{line.substr(7), ""});
      } else if (line == "SAYSTOP") {
        tiny_dispatch(do_saystop, nullptr);
      } else if (line.rfind("SAY ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_say,
                         new QReq{line.substr(4, sp - 4), line.substr(sp + 1)});
      } else if (line.rfind("PDF ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_pdf,
                         new QReq{line.substr(4, sp - 4), line.substr(sp + 1), ""});
      } else if (line.rfind("PDF@", 0) == 0) {
        // PDF@<winid> <qid> <path>
        size_t sp0 = line.find(' ', 4);
        if (sp0 == std::string::npos)
          continue;
        size_t sp = line.find(' ', sp0 + 1);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_pdf,
                         new QReq{line.substr(sp0 + 1, sp - sp0 - 1),
                                  line.substr(sp + 1), line.substr(4, sp0 - 4)});
      } else if (line.rfind("CAPTURE ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        std::string qid = sp == std::string::npos ? line.substr(8)
                                                  : line.substr(8, sp - 8);
        std::string rest = sp == std::string::npos ? "0" : line.substr(sp + 1);
        tiny_dispatch(do_capture, new QReq{qid, rest});
      } else if (line.rfind("THUMB ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_thumb,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("DRAGOUT", 0) == 0 &&
                 (line[7] == ' ' || line[7] == '@')) {
        size_t body = 8;
        if (line[7] == '@') {
          size_t sp = line.find(' ', 8);
          if (sp == std::string::npos)
            continue;
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        DragOutReq *req = new DragOutReq;
        req->image = p.size() > 0 ? wire_unescape(p[0]) : "";
        for (size_t i = 1; i < p.size(); i++)
          if (!p[i].empty())
            req->paths.push_back(wire_unescape(p[i]));
        tiny_dispatch(do_dragout, req);
      } else if (line.rfind("AUTH ", 0) == 0) {
        // AUTH <qid> <wire-escaped-reason>. A missing reason must still reply
        // (never hang the promise), so treat it as empty rather than dropping.
        size_t sp = line.find(' ', 5);
        std::string qid =
            sp == std::string::npos ? line.substr(5) : line.substr(5, sp - 5);
        std::string reason =
            sp == std::string::npos ? "" : wire_unescape(line.substr(sp + 1));
        tiny_dispatch(do_auth, new AuthReq{qid, reason});
      } else if (line.rfind("OSA ", 0) == 0 || line.rfind("OCR ", 0) == 0) {
        got_unsupported(qid_of(line, 4));
      } else if (line.rfind("RECORD ", 0) == 0) {
        got_unsupported(qid_of(line, 7));
      } else if (line.rfind("WINCTRL ", 0) == 0) {
        got_unsupported(qid_of(line, 8));
      } else if (line.rfind("PICKCOLOR ", 0) == 0 ||
                 line.rfind("SPOTLIGHT ", 0) == 0) {
        got_unsupported(qid_of(line, 10));
      } else if (line.rfind("AI ", 0) == 0) {
        // AI <op> <qid> …
        size_t o = line.find(' ', 3);
        if (o == std::string::npos)
          continue;
        std::string op = line.substr(3, o - 3);
        std::string qid = qid_of(line, o + 1);
        if (op == "available")
          got(qid, "{\"status\":\"unsupported\"}");
        else
          got(qid, "{\"ok\":false,\"error\":\"not built in\"}");
      } else if (line.rfind("AUDIOTAP STOP", 0) == 0) {
        tiny_audiotap_stop();
      } else if (line.rfind("AUDIOTAP ", 0) == 0) {
        // AUDIOTAP <qid> <scope>\t<excludeSelf01>\t<intervalMs>
        std::string rest = line.substr(9);
        size_t sp = rest.find(' ');
        std::string qid = sp == std::string::npos ? rest : rest.substr(0, sp);
        std::vector<std::string> p =
            split_tabs(sp == std::string::npos ? "" : rest.substr(sp + 1));
        tiny_audiotap_start(AudioTapReq{
            qid, p.size() > 0 ? p[0] : "system",
            p.size() > 1 && p[1] == "1",
            p.size() > 2 ? atoi(p[2].c_str()) : 80});
      } else if (line == "PRINT") {
        tiny_dispatch(do_eval, new std::string("window.print()"));
      } else if (line.rfind("PRINT@", 0) == 0) {
        // the asking window prints its own page
        tiny_dispatch(do_eval_win,
                         new EvalReq{line.substr(6), "window.print()"});
      } else if (line == "RELOAD") {
        tiny_dispatch(do_reload, nullptr);
      } else if (line.rfind("NAVR ", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_navr,
                         new NavrReq{line.substr(5, sp - 5),
                                     line.substr(sp + 1)});
      } else if (line.rfind("POPUPR ", 0) == 0) {
        size_t sp = line.find(' ', 7);
        if (sp == std::string::npos)
          continue;
        tiny_dispatch(do_popupr,
                         new PopuprReq{line.substr(7, sp - 7),
                                       line.substr(sp + 1)});
      } else if (line.rfind("FIND ", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos)
          continue;
        std::vector<std::string> p = split_tabs(line.substr(sp + 1));
        FindReq *fr = new FindReq;
        fr->id = line.substr(5, sp - 5);
        fr->term = p.size() > 0 ? p[0] : "";
        fr->forward = !(p.size() > 1 && p[1] == "0");
        fr->matchCase = p.size() > 2 && p[2] == "1";
        tiny_dispatch(do_find, fr);
      } else if (line.rfind("STOPFIND ", 0) == 0) {
        tiny_dispatch(do_stopfind, new std::string(line.substr(9)));
      } else if (line == "QUIT") {
        tiny_dispatch(do_terminate, nullptr);
      }
      // WINCLOSE / SHARE / QUICKLOOK: not wired up yet. BADGE now draws an
      // overlay icon, but it has never been run on Windows, so
      // capabilities() still reports badge:false — see TODO-verify.md.
    }
  }
  tiny_dispatch(do_terminate, nullptr);
}

// ---------------------------------------------------------------------------
// icon embedding — `launcher-win.exe --embed-icon <exe> <png>` is the build
// step that stamps dist exes with the app icon (the launcher already links
// GDI+, so the CLI shells out to it instead of needing windres).

#pragma pack(push, 2)
struct GrpIconDirEntry {
  BYTE w, h, colors, reserved;
  WORD planes, bpp;
  DWORD bytes;
  WORD id;
};
struct GrpIconDir {
  WORD reserved, type, count;
};
#pragma pack(pop)

// Encode one icon frame as a png blob at the given square size.
static std::vector<BYTE> icon_frame_png(Gdiplus::Bitmap *src, int size) {
  std::vector<BYTE> out;
  Gdiplus::Bitmap frame(size, size, PixelFormat32bppARGB);
  Gdiplus::Graphics g(&frame);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.DrawImage(src, 0, 0, size, size);
  IStream *stream = nullptr;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
    return out;
  if (frame.Save(stream, &g_png_clsid, nullptr) == Gdiplus::Ok) {
    HGLOBAL mem = nullptr;
    GetHGlobalFromStream(stream, &mem);
    SIZE_T len = GlobalSize(mem);
    BYTE *p = (BYTE *)GlobalLock(mem);
    out.assign(p, p + len);
    GlobalUnlock(mem);
  }
  stream->Release();
  return out;
}

static int embed_icon(const std::string &exe, const std::string &png) {
  if (!ensure_gdiplus() || !png_encoder_clsid())
    return 1;
  Gdiplus::Bitmap *src = Gdiplus::Bitmap::FromFile(widen(png).c_str());
  if (!src || src->GetLastStatus() != Gdiplus::Ok) {
    std::fprintf(stderr, "embed-icon: cannot read %s\n", png.c_str());
    delete src;
    return 1;
  }
  // PNG-compressed icon frames are valid from Vista on for every size.
  const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
  std::vector<std::vector<BYTE>> frames;
  for (int s : sizes)
    frames.push_back(icon_frame_png(src, s));
  delete src;

  HANDLE upd = BeginUpdateResourceW(widen(exe).c_str(), FALSE);
  if (!upd) {
    std::fprintf(stderr, "embed-icon: cannot open %s for update\n", exe.c_str());
    return 1;
  }
  std::vector<BYTE> group(sizeof(GrpIconDir) +
                          frames.size() * sizeof(GrpIconDirEntry));
  GrpIconDir *dir = (GrpIconDir *)group.data();
  dir->reserved = 0;
  dir->type = 1;
  dir->count = (WORD)frames.size();
  bool ok = true;
  for (size_t i = 0; i < frames.size(); i++) {
    GrpIconDirEntry *e =
        (GrpIconDirEntry *)(group.data() + sizeof(GrpIconDir) +
                            i * sizeof(GrpIconDirEntry));
    int s = sizes[i];
    e->w = (BYTE)(s == 256 ? 0 : s);
    e->h = (BYTE)(s == 256 ? 0 : s);
    e->colors = 0;
    e->reserved = 0;
    e->planes = 1;
    e->bpp = 32;
    e->bytes = (DWORD)frames[i].size();
    e->id = (WORD)(i + 1);
    ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(3) /* RT_ICON */,
                               MAKEINTRESOURCEW(i + 1),
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                               frames[i].data(), (DWORD)frames[i].size());
  }
  ok = ok && UpdateResourceW(upd, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */,
                             MAKEINTRESOURCEW(1),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             group.data(), (DWORD)group.size());
  if (!EndUpdateResourceW(upd, !ok) || !ok) {
    std::fprintf(stderr, "embed-icon: resource update failed\n");
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// main

static void on_invoke(const char *id, const char *req, void *) {
  // req is the JSON argument array from the page: ["<payload>"] — exactly the
  // CALL body the backend expects. The id round-trips through RET (or DLG).
  // The calling document's origin is APPENDED as the last element: attested by
  // WebView2 (a hostile page can't spoof it) and taken from the queue the
  // vendored WebMessageReceived handler fills, matched to this call by id.
  // The bridge's "api" gate keys origin sub-gates on it — and reads the LAST
  // element precisely because the page controls everything before it (this
  // binding's argument array is whatever the page passed to __invoke).
  // The id round-trips in RET/DLG and is the webview library's own: 32 hex
  // chars from its generateId(). The page can post its own message with any
  // id, so refuse anything else rather than write it into the line (#30.6).
  {
    std::string sid = id ? id : "";
    if (sid.empty() || sid.size() > 64 ||
        sid.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
      webview::detail::tinyjs_take_msg_source(sid); // don't strand its queue entry
      return;
    }
  }
  std::string body = req ? req : "[]";
  std::string origin = "null";
  {
    std::wstring src = webview::detail::tinyjs_take_msg_source(id ? id : "");
    if (!src.empty())
      origin = origin_from_uri(narrow(src));
  }
  if (body.size() >= 2 && body.back() == ']') {
    bool empty = body.find_first_not_of(" \t\r\n", 1) == body.size() - 1;
    body.pop_back();
    body += (empty ? "" : ",") + json_escape(origin) + "]";
  }
  if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
    std::fprintf(stderr, "launcher: CALL %s %s\n", id, body.c_str());
  pipe_write_line(std::string("CALL ") + id + " " + body);
}

// --- the app's single-instance pipe (#29.2) ---------------------------------
// The bridge listens on \\.\pipe\tinyjs-app-<id>-<user> through libuv, which
// creates it with the default DACL (Everyone + Anonymous may connect for
// read) and keeps the handle to itself. Pipe names are machine-wide and this
// one is guessable, so two things make it this user's, the Windows twin of
// Linux's 0700 socket dir (#12):
//  - PIPELOCK: once the bridge owns the name, re-ACL it to this user + SYSTEM
//    with network logons denied. The DACL lives on the pipe, not an instance,
//    so the instances libuv creates for later accepts inherit it.
//  - every hand-off (--handoff, --open) first asks who the server is and
//    sends nothing unless it runs as this user — a name another user created
//    first would otherwise collect our URLs (OAuth callbacks) and file paths.

// A process's user SID (bytes), or empty.
static std::vector<BYTE> token_user(HANDLE proc) {
  std::vector<BYTE> out;
  HANDLE tok = nullptr;
  if (!OpenProcessToken(proc, TOKEN_QUERY, &tok))
    return out;
  DWORD n = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
  std::vector<BYTE> buf(n);
  if (n && GetTokenInformation(tok, TokenUser, buf.data(), n, &n)) {
    PSID sid = reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid;
    DWORD len = GetLengthSid(sid);
    out.assign((BYTE *)sid, (BYTE *)sid + len);
  }
  CloseHandle(tok);
  return out;
}

// Does the server end of this connected pipe run as us? Fails closed: a
// server we can't open (another user's process, typically) is not ours.
static bool pipe_server_is_us(HANDLE pipe) {
  ULONG pid = 0;
  if (!GetNamedPipeServerProcessId(pipe, &pid) || !pid)
    return false;
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc)
    return false;
  std::vector<BYTE> them = token_user(proc);
  CloseHandle(proc);
  std::vector<BYTE> me = token_user(GetCurrentProcess());
  return !me.empty() && them.size() == me.size() &&
         EqualSid((PSID)them.data(), (PSID)me.data());
}

static void lock_instance_pipe(const std::string &name) {
  std::vector<BYTE> me = token_user(GetCurrentProcess());
  LPWSTR sid = nullptr;
  if (me.empty() || !ConvertSidToStringSidW((PSID)me.data(), &sid)) {
    std::fprintf(stderr, "launcher: PIPELOCK: no user SID\n");
    return;
  }
  std::wstring sddl = L"D:P(D;;GA;;;NU)(A;;GA;;;" + std::wstring(sid) +
                      L")(A;;GA;;;SY)";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
    std::fprintf(stderr, "launcher: PIPELOCK: bad SDDL\n");
    return;
  }
  BOOL present = FALSE, defaulted = FALSE;
  PACL dacl = nullptr;
  GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
  // A client handle with WRITE_DAC is enough to set the pipe's DACL; the
  // bridge sees an empty connection and ignores it. Check it's really the
  // bridge's pipe first — re-ACLing a squatter's would hand it nothing, but
  // there's no reason to touch it either.
  DWORD err = 0;
  HANDLE h = CreateFileW(widen(name).c_str(), WRITE_DAC | READ_CONTROL, 0,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    err = GetLastError();
  else {
    if (!pipe_server_is_us(h))
      err = ERROR_ACCESS_DENIED;
    else
      err = SetSecurityInfo(h, SE_KERNEL_OBJECT,
                            DACL_SECURITY_INFORMATION |
                                PROTECTED_DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, dacl, nullptr);
    CloseHandle(h);
  }
  LocalFree(sd);
  if (err)
    std::fprintf(stderr, "launcher: PIPELOCK %s failed (%lu)\n", name.c_str(),
                 err);
}

// Connect to the instance pipe and send `msg` (one line) only if its server
// is this user's. 0 = delivered, 1 = nothing listening, 2 = not ours.
static int send_to_instance(const std::string &pipe, const std::string &msg) {
  std::wstring wpipe = widen(pipe);
  HANDLE h = INVALID_HANDLE_VALUE;
  for (int i = 0; i < 5; i++) { // every instance mid-accept: brief wait
    h = CreateFileW(wpipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    0, nullptr);
    if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY)
      break;
    WaitNamedPipeW(wpipe.c_str(), 200);
  }
  if (h == INVALID_HANDLE_VALUE)
    return GetLastError() == ERROR_ACCESS_DENIED ? 2 : 1;
  int rc = 2;
  if (pipe_server_is_us(h)) {
    DWORD n = 0;
    rc = WriteFile(h, msg.data(), (DWORD)msg.size(), &n, nullptr) &&
                 n == msg.size()
             ? 0
             : 1;
  }
  CloseHandle(h);
  return rc;
}

// `launcher-win.exe --handoff <pipe> <json>` — the bridge's second-instance
// hand-off (txiki can't ask a pipe who its server is). Exit code as above.
static int handoff_mode(char **argv) {
  return send_to_instance(argv[2], std::string(argv[3]) + "\n");
}

// `launcher-win.exe --open <pipe> <app-exe> [arg]` — the registered handler
// for URL schemes and file associations. Compiled txiki apps reject argv, so
// deep links can't go through the app exe: this mode forwards the argument
// over the app's single-instance pipe instead, starting the app first if it
// isn't running. Also gives double-launches single-instance behavior.
static int open_mode(int argc, char **argv) {
  std::string pipe = argv[2];
  std::string exe = argv[3];
  std::string arg = argc > 4 ? argv[4] : "";
  std::string json;
  bool is_url = arg.find("://", 0) != std::string::npos &&
                GetFileAttributesW(widen(arg).c_str()) == INVALID_FILE_ATTRIBUTES;
  if (arg.empty())
    json = "{\"activate\":true}";
  else if (is_url)
    json = "{\"url\":" + json_escape(arg) + "}";
  else
    json = "{\"paths\":[" + json_escape(arg) + "]}";
  json += "\n";

  // A pipe that isn't this user's gets nothing — not the link, and not a
  // fresh start of the app either (it would find the same squatter).
  int rc = send_to_instance(pipe, json);
  if (rc != 1)
    return rc;
  // Not running: start the app (no argv — txiki compiled binaries reject
  // any), then deliver once its instance pipe is up.
  std::wstring cmd = L"\"" + widen(exe) + L"\"";
  std::wstring dir = widen(exe.substr(0, exe.find_last_of("\\/")));
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr, dir.c_str(), &si,
                      &pi))
    return 1;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  for (int i = 0; i < 100; i++) { // up to ~15s for a cold start
    Sleep(150);
    if ((rc = send_to_instance(pipe, json)) != 1)
      return rc;
  }
  return 1;
}

// `launcher-win.exe --run <exe> [args...]` — run a console tool with no
// console window. tjs.spawn can't pass CREATE_NO_WINDOW, so a GUI-subsystem
// app spawning tasklist/tar/reg pops a terminal per call; routing through
// this mode (a GUI exe itself) suppresses it. Std handles are inherited, so
// piped stdout/stderr and the exit code flow through unchanged.
static std::wstring quote_arg(const std::wstring &a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
    return a;
  std::wstring out = L"\"";
  size_t bs = 0;
  for (wchar_t c : a) {
    if (c == L'\\') { bs++; out += c; continue; }
    if (c == L'"') { out.append(bs + 1, L'\\'); out += c; bs = 0; continue; }
    bs = 0; out += c;
  }
  out.append(bs, L'\\');
  out += L'"';
  return out;
}

static int run_hidden() {
  int wargc = 0;
  LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  if (!wargv || wargc < 3)
    return 127;
  std::wstring cmd;
  for (int i = 2; i < wargc; i++) {
    if (i > 2) cmd += L' ';
    cmd += quote_arg(wargv[i]);
  }
  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  // A job with KILL_ON_JOB_CLOSE ties the tool's life to this wrapper: the
  // caller only holds OUR handle, so a kill() from the backend (an aborted
  // curl-backed stream, say) must not leave the tool running with the pipe
  // still open. Start suspended so the assignment lands before any code —
  // and any grandchild — runs.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li,
                                 sizeof(li))) {
      CloseHandle(job);
      job = nullptr;
    }
  }
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW | (job ? CREATE_SUSPENDED : 0), nullptr,
                      nullptr, &si, &pi))
    return 127;
  if (job) {
    AssignProcessToJobObject(job, pi.hProcess); // nested jobs: Win8+
    ResumeThread(pi.hThread);
  }
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return (int)code;
}

// `launcher-win.exe --spawn <exe> [args...]` — start a process that outlives
// the caller, and return at once. libuv puts every tjs.spawn child in a
// KILL_ON_JOB_CLOSE job (txiki exposes no detached flag), so a child the
// backend starts dies when the backend exits — fatal for the post-update
// relaunch, which is spawned by the instance that's about to quit. That job
// allows silent breakaway, so OUR child (the backend's grandchild) lands
// outside it; no job of our own, no wait, no inherited handles.
static int spawn_detached() {
  int wargc = 0;
  LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  if (!wargv || wargc < 3)
    return 127;
  std::wstring cmd;
  for (int i = 2; i < wargc; i++) {
    if (i > 2) cmd += L' ';
    cmd += quote_arg(wargv[i]);
  }
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
    return 1;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}

// Browser-affordance policy modes ride the spawn env (Windows has no attach
// mode, so env is the whole story).
static void read_browser_policy_env() {
  char v[16];
  DWORD n = GetEnvironmentVariableA("TINYJS_DOWNLOADS", v, sizeof(v));
  if (n > 0 && n < sizeof(v))
    g_downloads_mode = v;
  n = GetEnvironmentVariableA("TINYJS_POPUPS", v, sizeof(v));
  if (n > 0 && n < sizeof(v))
    g_popup_mode = v;
}

// Built apps get their Start-Menu shortcut on FIRST RUN, not on a first
// toast that may never arrive. That shortcut is what makes an app findable
// in Start/search and is what a taskbar pin resolves through, so tying it to
// notifications left apps that never notify unpinnable. Dev spawns are
// skipped: with no TINYJS_APP_EXE there is no app exe worth pointing at, and
// stamping one anyway is what littered Start with dead launcher shortcuts.
static void ensure_start_menu_shortcut() {
  if (!app_exe_env().empty()) {
    std::wstring aumid = tinyjs_aumid();
    create_start_menu_shortcut(aumid, aumid.substr(7)); // strip "tinyjs."
  }
}

// "mainWindow": false. No webview library instance, no main window, no
// WebView2 environment yet (the first named window creates it, see
// create_sec_controller) — just a hidden host window for the tray, hotkeys,
// clipboard watch, system broadcasts and tiny_dispatch, and our own message
// loop. It's a real top-level window, not HWND_MESSAGE: message-only windows
// miss the broadcasts (theme, power, display changes), and the tray menu
// needs a window it can SetForegroundWindow.
static int run_windowless() {
  // The webview library makes the process per-monitor DPI-aware while
  // building main (win32_edge.hh window_init); with no main that never runs,
  // and there's no manifest to say it instead. Unaware, named windows are
  // bitmap-stretched above 100% scaling and window_scale() reads 1.0.
  webview::detail::enable_dpi_awareness();
  WNDCLASSW wc = {};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"TinyjsHost";
  RegisterClassW(&wc);
  g_host = CreateWindowExW(WS_EX_TOOLWINDOW, L"TinyjsHost",
                           widen(g_app_name).c_str(), WS_OVERLAPPED, 0, 0, 0,
                           0, nullptr, nullptr, GetModuleHandleW(nullptr),
                           nullptr);
  if (!g_host) {
    std::fprintf(stderr, "launcher: cannot create the host window\n");
    return 1;
  }
  g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(g_host, GWLP_WNDPROC,
                                              (LONG_PTR)tiny_wndproc);
  ensure_start_menu_shortcut();
  read_browser_policy_env();
  autodlg_arm();
  // No main window to wear TINYJS_ICON: the named windows get it at open.
  {
    char icon[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TINYJS_ICON", icon, sizeof(icon));
    if (n > 0 && n < sizeof(icon))
      g_icon_default = icon_from_png(icon);
  }
  {
    char act[64];
    DWORD n = GetEnvironmentVariableA("TINYJS_ACTIVATION", act, sizeof(act));
    g_accessory = n > 0 && n < sizeof(act) && strcmp(act, "accessory") == 0;
  }

  send_theme();
  std::thread(pipe_read_loop).detach();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g_tray_added) {
    tray_ensure_icon_struct();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
  }
  // The pipe thread may still be blocked in ReadFile; exit hard.
  ExitProcess(0);
}

static int run(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "--embed-icon") == 0)
    return embed_icon(argv[2], argv[3]);
  if (argc >= 3 && strcmp(argv[1], "--spawn") == 0)
    return spawn_detached();
  if (argc >= 3 && strcmp(argv[1], "--run") == 0)
    return run_hidden();
  if (argc >= 4 && strcmp(argv[1], "--open") == 0)
    return open_mode(argc, argv);
  if (argc == 4 && strcmp(argv[1], "--handoff") == 0)
    return handoff_mode(argv);
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: %s <html-file-or-url> <pipe-name> [title] [WxH] "
                 "[version]\n       %s --embed-icon <exe> <png>\n",
                 argv[0], argv[0]);
    return 1;
  }
  g_target = argv[1];
  std::string pipe_name = argv[2];
  std::string title = argc > 3 ? argv[3] : "tinyjs";
  std::string size_s = argc > 4 ? argv[4] : "960x640";
  if (argc > 5)
    g_app_version = argv[5];
  g_app_name = title;
  int width = 960, height = 640;
  std::sscanf(size_s.c_str(), "%dx%d", &width, &height);
  g_target_is_url = g_target.rfind("http://", 0) == 0 ||
                    g_target.rfind("https://", 0) == 0;

  // OLE (not just COM) for RegisterDragDrop/DoDragDrop; webview's own
  // CoInitializeEx afterwards is a harmless S_FALSE.
  OleInitialize(nullptr);

  // Never pin the app folder: a cwd handle inside it would block the
  // auto-updater's directory swap. All paths we receive are absolute.
  {
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp))
      SetCurrentDirectoryW(tmp);
  }

  // No visible window station = no webview, ever (Session 0 services, some
  // CI/sandbox sessions). Mirrors the mac/linux launchers: fail loudly and
  // BEFORE the pipe connect, so the bridge's "launcher exited before
  // connecting" race reports the death and the failure reads as an
  // environment problem, not an app bug.
  {
    HWINSTA ws = GetProcessWindowStation();
    USEROBJECTFLAGS f = {};
    DWORD n = 0;
    if (ws && GetUserObjectInformationW(ws, UOI_FLAGS, &f, sizeof(f), &n) &&
        !(f.dwFlags & WSF_VISIBLE)) {
      std::fprintf(stderr,
          "tinyjs: cannot create the webview window: this process has no "
          "visible window station (service/CI/sandbox session?).\n"
          "tinyjs: the app code is likely fine; run from an interactive "
          "desktop session.\n");
      return 3;
    }
  }

  // Connect to the backend's named pipe (it listens before spawning us, but
  // retry briefly to be safe).
  for (int i = 0; i < 50; i++) {
    g_pipe = CreateFileW(widen(pipe_name).c_str(), GENERIC_READ | GENERIC_WRITE,
                         0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                         nullptr);
    if (g_pipe != INVALID_HANDLE_VALUE)
      break;
    if (GetLastError() == ERROR_PIPE_BUSY)
      WaitNamedPipeW(widen(pipe_name).c_str(), 1000);
    else
      Sleep(100);
  }
  if (g_pipe == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "launcher: cannot connect to %s\n", pipe_name.c_str());
    return 1;
  }
  // First line: the bridge's token, proving this connection is its launcher
  // and not some other user's read-only one (see pipeToken in bridge.js).
  // Then drop it from our env — nothing we spawn needs it.
  {
    char tok[128];
    DWORD n = GetEnvironmentVariableA("TINYJS_PIPE_TOKEN", tok, sizeof(tok));
    if (n > 0 && n < sizeof(tok)) {
      pipe_write_raw(std::string("HELLO ") + tok + "\n");
      SetEnvironmentVariableA("TINYJS_PIPE_TOKEN", nullptr);
    }
  }

  // tinyjs.json "debug" / "browserAccelerators" ride the spawn env (see the
  // globals' comment). `tinyjs dev` seeds TINYJS_DEBUG=1 so dev always has
  // devtools; packaged apps get nothing unless the manifest says so.
  {
    char v[8];
    DWORD n = GetEnvironmentVariableA("TINYJS_DEBUG", v, sizeof(v));
    if (n > 0 && n < sizeof(v)) {
      g_debug = true;
      g_debug_open = std::string(v) == "open";
    }
    n = GetEnvironmentVariableA("TINYJS_BROWSERACCEL", v, sizeof(v));
    g_browser_accel = n > 0 && v[0] == '1';
  }
  {
    char v[4];
    DWORD n = GetEnvironmentVariableA("TINYJS_NO_MAIN_WINDOW", v, sizeof(v));
    g_windowless = n == 1 && v[0] == '1';
  }
  if (g_windowless)
    return run_windowless();

  g_w = webview_create(g_debug ? 1 : 0, nullptr);
  if (!g_w) {
    std::fprintf(stderr,
                 "launcher: failed to create webview (is the WebView2 runtime "
                 "installed?)\n");
    return 1;
  }

  g_hwnd = (HWND)webview_get_native_handle(g_w,
                                           WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  g_host = g_hwnd;
  g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                                              (LONG_PTR)tiny_wndproc);
  // The webview library created main with (or without) a redirection bitmap
  // from this same env var; menu_allowed needs to know which, since a
  // transparent window has nothing for a GDI menu bar to draw on.
  {
    char t[8];
    DWORD n = GetEnvironmentVariableA("TINYJS_TRANSPARENT", t, sizeof(t));
    g_main_transparent = n > 0 && t[0] == '1';
  }
  apply_relaunch_props(g_hwnd);
  ensure_start_menu_shortcut(); // first run, not first toast (see the helper)

  // Page RPC + injected client library (document-start, every navigation).
  webview_bind(g_w, "__invoke", on_invoke, nullptr);
  webview_init(g_w, "window.__TINY_WIN = 'main';");
  webview_init(g_w, TINY_CLIENT_JS);
  {
    // Optional user-supplied document-start glue (see launcher-macos.cc).
    // Sized dynamically: a bundled inject.ts easily beats a fixed buffer,
    // and a too-small one silently dropped the whole script.
    DWORD need = GetEnvironmentVariableA("TINYJS_INJECT", nullptr, 0);
    if (need > 0) {
      std::vector<char> inj(need + 1);
      DWORD n = GetEnvironmentVariableA("TINYJS_INJECT", inj.data(),
                                        (DWORD)inj.size());
      if (n > 0 && n < inj.size())
        webview_init(g_w, inj.data());
    }
  }

  // Stash the controller + ICoreWebView2 for Reload()/settings/context-menu
  // interception/drag-drop/accelerators.
  g_ctrl = (ICoreWebView2Controller *)webview_get_native_handle(
      g_w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (g_ctrl)
    g_ctrl->get_CoreWebView2(&g_wv2);
  apply_webview_policy(g_wv2);
  if (g_debug_open && g_wv2)
    g_wv2->OpenDevToolsWindow();
  // Browser affordances for wrapped sites: the handlers must be registered
  // BEFORE the first navigation below.
  read_browser_policy_env();
  install_browser_affordances(g_wv2, "main");
  autodlg_arm();
  install_ctx_handler();
  install_drop_target();
  install_accel_handler();
  install_unc_handler();
  // Give the page the keyboard from the start, the way a mac window's webview
  // is first responder the moment it opens. Without it document.hasFocus() is
  // false until the first click in the page — and an app that gates on it
  // (nib ignores menu events unless its window is the focused one) looks
  // broken until you happen to click.
  if (g_ctrl)
    g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

  // Custom User-Agent (TINYJS_UA env; see createApp userAgent).
  apply_user_agent(g_wv2);

  // A main window that starts hidden (accessory, rememberWindows' deferred
  // show) is cloaked until the hide below: webview_set_size's first call
  // SHOWS the window, and with navigate in between it stayed on screen at the
  // default spot long enough to be composed (~20ms, caught by a 2ms poll).
  BOOL start_cloaked = FALSE;
  {
    char v[64];
    DWORD na = GetEnvironmentVariableA("TINYJS_ACTIVATION", v, sizeof(v));
    bool acc = na > 0 && na < sizeof(v) && strcmp(v, "accessory") == 0;
    DWORD nd = GetEnvironmentVariableA("TINYJS_DEFER_SHOW", v, sizeof(v));
    start_cloaked = acc || (nd > 0 && nd < sizeof(v) && strcmp(v, "1") == 0);
    if (start_cloaked)
      DwmSetWindowAttribute(g_hwnd, DWMWA_CLOAK, &start_cloaked,
                            sizeof(start_cloaked));
  }

  webview_set_title(g_w, title.c_str());
  // Size so the PAGE gets exactly the declared size, whatever the frame turns
  // out to be. webview_set_size runs AdjustWindowRect for the window's style,
  // but a frameless main window then has those insets taken straight back off
  // by our WM_NCCALCSIZE — the client swallows the padding and the page comes
  // out bigger than asked (amp declared 320x172 and got 332x178, +12 and +6:
  // the left/right and bottom resize borders). Measuring the real insets works
  // for both cases: they are zero once frameless, so outer == client == asked.
  webview_set_size(g_w, width, height, WEBVIEW_HINT_NONE);
  {
    RECT o, c;
    GetWindowRect(g_hwnd, &o);
    GetClientRect(g_hwnd, &c);
    double sc = window_scale(g_hwnd);
    LONG want_w = (LONG)lround(width * sc) + ((o.right - o.left) - c.right);
    LONG want_h = (LONG)lround(height * sc) + ((o.bottom - o.top) - c.bottom);
    if (want_w != o.right - o.left || want_h != o.bottom - o.top)
      SetWindowPos(g_hwnd, nullptr, 0, 0, want_w, want_h,
                   SWP_NOMOVE | SWP_NOZORDER);
  }

  // Window icon from a png (dev: the project's icon.png via TINYJS_ICON;
  // built apps: dist/icon.png set by the bridge). Kept in g_icon_default so
  // app.icon('') can put it back — the taskbar button is a separate thing the
  // shell owns, see do_appicon.
  {
    char icon[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TINYJS_ICON", icon, sizeof(icon));
    if (n > 0 && n < sizeof(icon)) {
      HICON h = icon_from_png(icon);
      if (h) {
        g_icon_default = h;
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)h);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)h);
      }
    }
  }

  {
    std::string url = g_target_is_url ? g_target : to_file_url(g_target);
    if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
      std::fprintf(stderr, "launcher: navigate %s\n", url.c_str());
    webview_navigate(g_w, url.c_str());
  }

  // Accessory activation (tray-only apps): start hidden, no taskbar button.
  {
    char act[64];
    DWORD n = GetEnvironmentVariableA("TINYJS_ACTIVATION", act, sizeof(act));
    if (n > 0 && strcmp(act, "accessory") == 0) {
      g_accessory = true;
      ShowWindow(g_hwnd, SW_HIDE);
      set_taskbar_button(false);
    }
  }
  // rememberWindows: stay hidden until the bridge has restored main's place
  // (`WINOP show`), but never longer than 3s.
  {
    char v[8];
    DWORD n = GetEnvironmentVariableA("TINYJS_DEFER_SHOW", v, sizeof(v));
    if (!g_accessory && n > 0 && strcmp(v, "1") == 0) {
      g_defer_show = true;
      ShowWindow(g_hwnd, SW_HIDE);
      SetTimer(g_hwnd, TIMER_DEFERSHOW, 3000, nullptr);
    }
  }
  if (start_cloaked) { // hidden now; showing it later needs no uncloak
    BOOL off = FALSE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_CLOAK, &off, sizeof(off));
  }

  send_theme();
  std::thread(pipe_read_loop).detach();

  webview_run(g_w);
  {
    std::lock_guard<std::mutex> lk(g_dispatch_mx); // see tiny_dispatch
    g_stopping = true;
  }
  webview_destroy(g_w);
  if (g_tray_added) {
    tray_ensure_icon_struct();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
  }
  // The pipe thread may still be blocked in ReadFile; exit hard.
  ExitProcess(0);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  int argc = 0;
  LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> args;
  std::vector<char *> argv;
  for (int i = 0; i < argc; i++)
    args.push_back(narrow(wargv[i]));
  for (auto &a : args)
    argv.push_back(&a[0]);
  return run(argc, argv.data());
}
