// tinyjs window launcher — Windows.
//
// The same dumb window process as native/launcher.cc (macOS), speaking the
// identical newline-delimited wire protocol, but over a named pipe
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
// MENU*/MENUUPD (Win32 menu bar), TRAY* (Shell_NotifyIcon), WINOP (hide/show/
// center/minimize/restore/fullscreen/zoom/ontop/resizable/pos/hideonclose/
// clickthrough/level), CHROME (frame/squareCorners), GET (win/mouse/screens/
// clipboard/battery/idle/frontmost/item/traypos/windows), CLIPWRITE/CLIPWATCH,
// HKREG/HKUNREG (RegisterHotKey), KEYSTROKE (SendInput; cmd ≡ ctrl), SHELL
// (open/reveal/trash), SECRET (Credential Manager), POWER
// (SetThreadExecutionState), SOUND (PlaySound/MessageBeep), NOTIFY (tray
// balloon), BOUNCE (FlashWindowEx), CTX* (custom right-click menu via
// WebView2 ContextMenuRequested), SYS theme/sleep/wake events, DRAGWIN.
//
// macOS-only ops (vibrancy, dock, spaces, Quick Look, OCR, AppleScript, …)
// answer their query with ok:false/'unsupported' (never hang a promise) or
// are ignored when fire-and-forget. Multi-window (WINOPEN) is not yet ported.

#include "webview/webview.h"
#include "tiny_client.h" // generated from runtime/tiny.js (gen-client)

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
#include <gdiplus.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
static bool g_accessory = false;       // tray-only app: no taskbar button
static std::string g_target;           // html path or http(s) url
static bool g_target_is_url = false;
static std::string g_app_name = "tinyjs";
static std::string g_app_version = "0.0.0";

static bool g_hide_on_close = false;
static bool g_frameless = false;
static bool g_square = false;
static bool g_click_through = false;
static std::string g_level = "normal";
static bool g_fullscreen = false;
static WINDOWPLACEMENT g_fs_placement = {sizeof(WINDOWPLACEMENT)};
static LONG g_fs_style = 0, g_fs_exstyle = 0;

// menu / tray / ctx registries (string ids <-> Win32 command ints)
struct ItemReg {
  UINT cmd = 0;
  HMENU parent = nullptr;
  std::string id, label, kind; // kind: menu | tray | ctx
  bool checked = false, enabled = true;
  std::string key; // menu accelerator char ('' = none); fired via Ctrl+<key>
};
static std::map<UINT, ItemReg *> g_cmd_reg;
static std::map<std::string, ItemReg *> g_id_reg;
static UINT g_next_cmd = 1000;
static HMENU g_menu_bar = nullptr;
static HMENU g_tray_menu = nullptr;
static bool g_tray_primary = false;
static bool g_tray_added = false;
static HICON g_tray_icon = nullptr;
static HMENU g_ctx_menu = nullptr;
static bool g_ctx_suppress = false;
static std::string g_last_notif_id;

static DWORD g_clip_last_seq = 0;
static DWORD g_clip_self_seq = 0;
static std::map<int, std::string> g_hotkeys; // atom id -> string id
static int g_next_hotkey = 1;
static bool g_theme_dark = false;
static bool g_asleep = false;

#define WM_TINY_TRAY (WM_APP + 2)
#define TIMER_CLIPWATCH 0x7101

// multi-window plumbing (defined in the multi-window section below)
struct TinyWin;
static std::map<std::string, TinyWin *> g_windows;
static TinyWin *win_for_id(const std::string &id);
static HWND hwnd_for_win(const std::string &id);
static void route_ret(webview_t w, const std::string &composite, int status,
                      const std::string &json);
static void secwin_eval(const std::string &id, const std::string &js);

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
  bool separator = false;
  bool checked = false;
  bool disabled = false;
  std::vector<MenuItemSpec> submenu;
};
struct MenuSpec {
  std::string title;
  std::vector<MenuItemSpec> items;
};
struct TraySpec {
  std::string title, icon, tooltip;
  bool template_icon = true;
  bool primary = false;
  bool remove = false;
  std::vector<MenuItemSpec> items;
};

static void clear_registry(const std::string &kind) {
  for (auto it = g_cmd_reg.begin(); it != g_cmd_reg.end();) {
    if (it->second->kind == kind) {
      g_id_reg.erase(it->second->id);
      delete it->second;
      it = g_cmd_reg.erase(it);
    } else {
      ++it;
    }
  }
}

static std::string display_key(const std::string &key) {
  if (key.empty())
    return "";
  std::string k = key;
  k[0] = (char)toupper((unsigned char)k[0]);
  return "\tCtrl+" + k;
}

static void build_menu_items(HMENU menu, const std::vector<MenuItemSpec> &items,
                             const std::string &kind) {
  for (const auto &it : items) {
    if (it.separator) {
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      continue;
    }
    if (!it.submenu.empty()) {
      HMENU sub = CreatePopupMenu();
      build_menu_items(sub, it.submenu, kind);
      AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub,
                  widen(it.label.empty() ? it.id : it.label).c_str());
      continue;
    }
    UINT cmd = g_next_cmd++;
    ItemReg *reg = new ItemReg{cmd, menu, it.id,
                               it.label.empty() ? it.id : it.label, kind,
                               it.checked, !it.disabled, it.key};
    g_cmd_reg[cmd] = reg;
    if (!it.id.empty())
      g_id_reg[it.id] = reg;
    UINT flags = MF_STRING;
    if (it.checked)
      flags |= MF_CHECKED;
    if (it.disabled)
      flags |= MF_GRAYED;
    AppendMenuW(menu, flags, cmd,
                widen(reg->label + display_key(it.key)).c_str());
  }
}

static void apply_menus(webview_t, void *arg) {
  std::vector<MenuSpec> *menus = static_cast<std::vector<MenuSpec> *>(arg);
  clear_registry("menu");
  HMENU bar = nullptr;
  if (!menus->empty()) {
    bar = CreateMenu();
    for (const auto &m : *menus) {
      HMENU popup = CreatePopupMenu();
      build_menu_items(popup, m.items, "menu");
      AppendMenuW(bar, MF_POPUP, (UINT_PTR)popup, widen(m.title).c_str());
    }
  }
  HMENU old = GetMenu(g_hwnd);
  SetMenu(g_hwnd, bar);
  if (old)
    DestroyMenu(old);
  g_menu_bar = bar;
  DrawMenuBar(g_hwnd);
  delete menus;
}

struct MenuUpdReq {
  std::string id, label, checked, enabled;
};

static void do_menu_update(webview_t, void *arg) {
  MenuUpdReq *req = static_cast<MenuUpdReq *>(arg);
  auto it = g_id_reg.find(req->id);
  if (it != g_id_reg.end()) {
    ItemReg *reg = it->second;
    if (!req->label.empty()) {
      reg->label = req->label;
      ModifyMenuW(reg->parent, reg->cmd, MF_BYCOMMAND | MF_STRING, reg->cmd,
                  widen(reg->label).c_str());
    }
    if (!req->checked.empty()) {
      reg->checked = req->checked == "1";
      CheckMenuItem(reg->parent, reg->cmd,
                    MF_BYCOMMAND | (reg->checked ? MF_CHECKED : MF_UNCHECKED));
    }
    if (!req->enabled.empty()) {
      reg->enabled = req->enabled == "1";
      EnableMenuItem(reg->parent, reg->cmd,
                     MF_BYCOMMAND | (reg->enabled ? MF_ENABLED : MF_GRAYED));
    }
    if (reg->kind == "menu")
      DrawMenuBar(g_hwnd);
  }
  delete req;
}

// ---------------------------------------------------------------------------
// tray

static NOTIFYICONDATAW g_nid = {};

static void tray_ensure_icon_struct() {
  memset(&g_nid, 0, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_hwnd;
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
  if (!spec->icon.empty() && spec->icon.rfind("sf:", 0) != 0)
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
  SetForegroundWindow(g_hwnd);
  UINT cmd = (UINT)TrackPopupMenu(menu,
                                  TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_hwnd, nullptr);
  PostMessageW(g_hwnd, WM_NULL, 0, 0);
  if (cmd) {
    auto it = g_cmd_reg.find(cmd);
    if (it != g_cmd_reg.end() && it->second->enabled)
      pipe_write_line(std::string(event_prefix) + " " + it->second->id);
  }
}

// NOTIFY — a balloon on the tray icon (created invisible-ish on demand when
// the app has no tray). Windows' toast API needs an AppUserModelID +
// registration; the balloon path works for unpackaged dev apps.
struct NotifReq {
  std::string id, title, body, subtitle;
  bool sound = false;
};

static void do_notify(webview_t, void *arg) {
  NotifReq *req = static_cast<NotifReq *>(arg);
  if (!g_tray_added) {
    tray_ensure_icon_struct();
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TINY_TRAY;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy(g_nid.szTip, widen(g_app_name).c_str(), 127);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_tray_added = true;
  }
  g_last_notif_id = req->id;
  tray_ensure_icon_struct();
  g_nid.uFlags = NIF_INFO;
  std::string body = req->body;
  if (!req->subtitle.empty())
    body = req->subtitle + "\n" + body;
  wcsncpy(g_nid.szInfoTitle, widen(req->title).c_str(), 63);
  wcsncpy(g_nid.szInfo, widen(body).c_str(), 255);
  g_nid.dwInfoFlags = NIIF_INFO | (req->sound ? 0 : NIIF_NOSOUND);
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
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

static std::string run_prompt(const std::string &message,
                              const std::string &defval) {
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
  dlg_item(t, BS_DEFPUSHBUTTON | WS_TABSTOP, 128, 56, 50, 14, IDOK, 0x0080,
           L"OK");
  dlg_item(t, BS_PUSHBUTTON | WS_TABSTOP, 182, 56, 50, 14, IDCANCEL, 0x0080,
           L"Cancel");
  PromptState st;
  st.message = widen(message);
  st.value = widen(defval);
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
                          (LPCDLGTEMPLATEW)t.data(), g_hwnd, prompt_proc,
                          (LPARAM)&st);
  return st.ok ? json_escape(narrow(st.value)) : "null";
}

static std::string run_file_dialog(const std::string &op) {
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
  hr = dlg->Show(g_hwnd);
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
    int r = MessageBoxW(g_hwnd, widen(text).c_str(), widen(g_app_name).c_str(),
                        flags);
    json = req->op == "alert" ? "true" : (r == IDOK ? "true" : "false");
  } else if (req->op == "prompt") {
    json = run_prompt(a(0).empty() ? g_app_name : a(0), a(1));
  } else {
    json = run_file_dialog(req->op);
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

struct WinopReq {
  std::string win, op;
};

static void do_winop(webview_t, void *arg) {
  WinopReq *req = static_cast<WinopReq *>(arg);
  const std::string &op = req->op;
  HWND hwnd = hwnd_for_win(req->win);
  bool main = hwnd == g_hwnd;
  if (!hwnd) {
    delete req;
    return;
  }
  auto starts = [&](const char *p) { return op.rfind(p, 0) == 0; };
  if (op == "hide") {
    ShowWindow(hwnd, SW_HIDE);
  } else if (starts("show")) {
    bool activate = op != "show 0";
    ShowWindow(hwnd, activate ? SW_SHOW : SW_SHOWNA);
    if (activate)
      SetForegroundWindow(hwnd);
  } else if (op == "center") {
    do_center(hwnd);
  } else if (op == "minimize") {
    ShowWindow(hwnd, SW_MINIMIZE);
  } else if (op == "restore") {
    ShowWindow(hwnd, SW_RESTORE);
  } else if (op == "zoom") {
    ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
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
    set_style_bits(hwnd, WS_THICKFRAME | WS_MAXIMIZEBOX, op.substr(10) == "1");
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
    std::sscanf(op.c_str() + 4, "%d %d", &x, &y);
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  } else if (starts("hideonclose ") && main) {
    g_hide_on_close = op.substr(12) == "1";
  } else if (starts("dock ") && main) {
    // setDockVisible maps to the taskbar button: hidden = tool window (the
    // tray-only app look). Must hide while flipping the style or the shell
    // ignores it.
    bool show = op.substr(5) == "1";
    bool visible = IsWindowVisible(g_hwnd);
    if (visible)
      ShowWindow(g_hwnd, SW_HIDE);
    LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    ex = show ? (ex & ~WS_EX_TOOLWINDOW) : (ex | WS_EX_TOOLWINDOW);
    SetWindowLongW(g_hwnd, GWL_EXSTYLE, ex);
    if (visible)
      ShowWindow(g_hwnd, SW_SHOWNA);
  }
  // allspaces: no Windows equivalent — ignored.
  delete req;
}

struct ChromeReq {
  std::string win, frame, traffic, transparent, vibrancy, square, first_mouse;
};

static ICoreWebView2Controller *ctrl_for_win(const std::string &id);

static void do_chrome(webview_t, void *arg) {
  ChromeReq *req = static_cast<ChromeReq *>(arg);
  HWND hwnd = hwnd_for_win(req->win);
  bool main = hwnd == g_hwnd;
  if (!hwnd) {
    delete req;
    return;
  }
  if (!req->frame.empty()) {
    bool frameless = req->frame == "0";
    if (main)
      g_frameless = frameless;
    set_style_bits(hwnd, WS_CAPTION, !frameless);
  }
  if (!req->square.empty()) {
    bool square = req->square == "1";
    if (main)
      g_square = square;
    if (square) {
      // Borderless like macOS: square corners, no titlebar; resize edges kept.
      if (main)
        g_frameless = true;
      set_style_bits(hwnd, WS_CAPTION, false);
    }
    DWORD pref = square ? 1 /* DWMWCP_DONOTROUND */ : 0 /* DEFAULT */;
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &pref, sizeof(pref));
  }
  if (!req->transparent.empty()) {
    // Page background becomes see-through where the page itself is
    // transparent (pair with a DWM backdrop or a frameless window).
    ICoreWebView2Controller *ctrl = ctrl_for_win(req->win);
    ICoreWebView2Controller2 *c2 = nullptr;
    if (ctrl &&
        SUCCEEDED(ctrl->QueryInterface(IID_ICoreWebView2Controller2,
                                       (void **)&c2)) &&
        c2) {
      COREWEBVIEW2_COLOR clear = {0, 0, 0, 0}, opaque = {255, 255, 255, 255};
      c2->put_DefaultBackgroundColor(req->transparent == "1" ? clear : opaque);
      c2->Release();
    }
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
  delete req;
}

static void do_dragwin(webview_t, void *) {
  ReleaseCapture();
  SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

// ---------------------------------------------------------------------------
// clipboard

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
  if (!OpenClipboard(g_hwnd))
    return "{\"kind\":\"empty\",\"changeCount\":" + std::to_string(seq) + "}";
  std::string kind = "empty", text = "null", html = "null", paths = "null",
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
        wchar_t tmp[MAX_PATH], file[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        GetTempFileNameW(tmp, L"tjc", 0, file);
        std::wstring png = std::wstring(file) + L".png";
        if (bmp->Save(png.c_str(), &g_png_clsid, nullptr) == Gdiplus::Ok) {
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
  if (!OpenClipboard(g_hwnd)) {
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
    SetTimer(g_hwnd, TIMER_CLIPWATCH, (UINT)*ms, nullptr);
  } else {
    KillTimer(g_hwnd, TIMER_CLIPWATCH);
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
      {"delete", VK_DELETE}, {"backspace", VK_BACK}, {"up", VK_UP},
      {"down", VK_DOWN},     {"left", VK_LEFT},     {"right", VK_RIGHT},
      {"home", VK_HOME},     {"end", VK_END},       {"pageup", VK_PRIOR},
      {"pagedown", VK_NEXT}, {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD}};
  auto it = named.find(k);
  if (it == named.end())
    return false;
  vk = it->second;
  return true;
}

// combo 'cmd+shift+k' -> modifier vks + key vk. cmd maps to Ctrl on Windows.
static bool parse_combo(const std::string &combo, std::vector<WORD> &mods,
                        WORD &key, UINT *hotkey_mods) {
  mods.clear();
  key = 0;
  if (hotkey_mods)
    *hotkey_mods = 0;
  size_t start = 0;
  std::vector<std::string> toks;
  for (;;) {
    size_t plus = combo.find('+', start);
    if (plus == std::string::npos) {
      toks.push_back(combo.substr(start));
      break;
    }
    toks.push_back(combo.substr(start, plus - start));
    start = plus + 1;
  }
  for (size_t i = 0; i < toks.size(); i++) {
    std::string t = toks[i];
    for (auto &c : t)
      c = (char)tolower((unsigned char)c);
    bool last = i + 1 == toks.size();
    if (!last || toks.size() == 1) {
      if (t == "cmd" || t == "meta" || t == "command" || t == "ctrl" ||
          t == "control") {
        mods.push_back(VK_CONTROL);
        if (hotkey_mods) *hotkey_mods |= MOD_CONTROL;
        continue;
      }
      if (t == "alt" || t == "opt" || t == "option") {
        mods.push_back(VK_MENU);
        if (hotkey_mods) *hotkey_mods |= MOD_ALT;
        continue;
      }
      if (t == "shift") {
        mods.push_back(VK_SHIFT);
        if (hotkey_mods) *hotkey_mods |= MOD_SHIFT;
        continue;
      }
      if (t == "win" || t == "super") {
        mods.push_back(VK_LWIN);
        if (hotkey_mods) *hotkey_mods |= MOD_WIN;
        continue;
      }
    }
    if (!parse_key_token(t, key))
      return false;
  }
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

struct HotkeyReq {
  std::string id, combo; // empty combo = unregister
};

static void do_hotkey(webview_t, void *arg) {
  HotkeyReq *req = static_cast<HotkeyReq *>(arg);
  // unregister an existing binding for this id either way
  for (auto it = g_hotkeys.begin(); it != g_hotkeys.end();) {
    if (it->second == req->id) {
      UnregisterHotKey(g_hwnd, it->first);
      it = g_hotkeys.erase(it);
    } else {
      ++it;
    }
  }
  if (!req->combo.empty()) {
    std::vector<WORD> mods;
    WORD key = 0;
    UINT hmods = 0;
    if (parse_combo(req->combo, mods, key, &hmods)) {
      int id = g_next_hotkey++;
      if (RegisterHotKey(g_hwnd, id, hmods | MOD_NOREPEAT, key))
        g_hotkeys[id] = req->id;
    }
  }
  delete req;
}

// ---------------------------------------------------------------------------
// GOT-answering ops

struct QReq {
  std::string qid, rest;
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
      fo.hwnd = g_hwnd;
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
               : "{\"ok\":false,\"error\":\"credential write failed\"}";
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
  ICoreWebView2_7 *wv7 = nullptr;
  if (g_wv2 &&
      SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_7, (void **)&wv7)) &&
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
  g_voice->Speak(widen(text).c_str(),
                 SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
  // Resolve when playback finishes (mirrors macOS `say`); a worker waits so
  // the UI thread stays free. Interrupted (purged by a newer Speak) still
  // resolves — WaitUntilDone returns once this utterance leaves the queue.
  ISpVoice *v = g_voice;
  v->AddRef();
  std::thread([v, qid]() {
    v->WaitUntilDone(INFINITE);
    got(qid, "{\"ok\":true}");
    v->Release();
  }).detach();
  delete req;
}

static void do_saystop(webview_t, void *) {
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

static void install_drop_target() {
  ICoreWebView2Controller4 *c4 = nullptr;
  if (g_ctrl &&
      SUCCEEDED(g_ctrl->QueryInterface(IID_ICoreWebView2Controller4,
                                       (void **)&c4)) &&
      c4) {
    c4->put_AllowExternalDrop(FALSE);
    c4->Release();
  }
  RegisterDragDrop(g_hwnd, new DropTarget());
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
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD keys) override {
    if (esc) return DRAGDROP_S_CANCEL;
    if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
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
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (const auto &p : paths) {
      PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(widen(p).c_str());
      if (pidl)
        pidls.push_back(pidl);
    }
    if (!pidls.empty()) {
      IDataObject *data = nullptr;
      if (SUCCEEDED(SHCreateDataObject(nullptr, (UINT)pidls.size(),
                                       (PCIDLIST_ABSOLUTE_ARRAY)pidls.data(),
                                       nullptr, IID_PPV_ARGS(&data))) &&
          data) {
        DropSource *src = new DropSource();
        DWORD effect = 0;
        HRESULT hr = DoDragDrop(data, src,
                                DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
        if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
          std::fprintf(stderr, "launcher: DoDragDrop hr=0x%lx effect=%lu\n",
                       (unsigned long)hr, (unsigned long)effect);
        src->Release();
        data->Release();
      }
    }
    for (auto pidl : pidls)
      ILFree(pidl);
    OleUninitialize();
  }).detach();
}

// ---------------------------------------------------------------------------
// menu accelerators — WebView2 owns the keyboard, so Ctrl+<key> combos are
// caught in its AcceleratorKeyPressed event and routed to the menu registry.

struct AccelHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
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
    if (!(GetKeyState(VK_CONTROL) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000))
      return S_OK;
    UINT vk = 0;
    args->get_VirtualKey(&vk);
    char c = 0;
    if (vk >= 'A' && vk <= 'Z') c = (char)tolower((int)vk);
    else if (vk >= '0' && vk <= '9') c = (char)vk;
    if (!c)
      return S_OK;
    for (auto &kv : g_cmd_reg) {
      ItemReg *reg = kv.second;
      if (reg->kind == "menu" && reg->enabled && reg->key.size() == 1 &&
          tolower((unsigned char)reg->key[0]) == c) {
        args->put_Handled(TRUE);
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
  LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
  bool frameless = main ? (g_frameless || g_square) : !(style & WS_CAPTION);
  char buf[640];
  std::snprintf(
      buf, sizeof(buf),
      "{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
      "\"fullscreen\":%s,\"minimized\":%s,\"visible\":%s,\"focused\":%s,"
      "\"alwaysOnTop\":%s,\"resizable\":%s,"
      "\"clickThrough\":%s,\"level\":\"%s\",\"allSpaces\":false,"
      "\"chrome\":{\"frame\":%s,\"trafficLights\":%s,\"transparent\":false,"
      "\"vibrancy\":null,\"squareCorners\":%s,\"acceptsFirstMouse\":true},"
      "\"screen\":{\"width\":%ld,\"height\":%ld,\"scale\":%.2f}}",
      r.left, r.top, r.right - r.left, r.bottom - r.top,
      (main && g_fullscreen) ? "true" : "false",
      IsIconic(hwnd) ? "true" : "false",
      IsWindowVisible(hwnd) ? "true" : "false",
      GetForegroundWindow() == hwnd ? "true" : "false",
      (ex & WS_EX_TOPMOST) ? "true" : "false",
      (style & WS_THICKFRAME) ? "true" : "false",
      (main && g_click_through) ? "true" : "false",
      main ? g_level.c_str() : "normal",
      frameless ? "false" : "true", frameless ? "false" : "true",
      (main && g_square) ? "true" : "false",
      mi.rcMonitor.right - mi.rcMonitor.left,
      mi.rcMonitor.bottom - mi.rcMonitor.top, monitor_scale(mon));
  return buf;
}

static BOOL CALLBACK enum_mon_proc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
  std::vector<HMONITOR> *v = (std::vector<HMONITOR> *)lp;
  v->push_back(mon);
  return TRUE;
}

static std::string screens_json() {
  std::vector<HMONITOR> mons;
  EnumDisplayMonitors(nullptr, nullptr, enum_mon_proc, (LPARAM)&mons);
  std::string json = "[";
  for (size_t i = 0; i < mons.size(); i++) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mons[i], (MONITORINFO *)&mi);
    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "{\"id\":%zu,\"name\":%s,"
        "\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
        "\"visible\":{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld},"
        "\"scale\":%.2f,\"primary\":%s}",
        i, json_escape(narrow(mi.szDevice)).c_str(), mi.rcMonitor.left,
        mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcWork.left, mi.rcWork.top,
        mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top,
        monitor_scale(mons[i]),
        (mi.dwFlags & MONITORINFOF_PRIMARY) ? "true" : "false");
    if (i)
      json += ",";
    json += buf;
  }
  json += "]";
  return json;
}

static void do_get(webview_t, void *arg) {
  GetReq *req = static_cast<GetReq *>(arg);
  std::string json = "null";
  const std::string &what = req->what;
  if (what == "windows") {
    json = "[\"main\"";
    for (auto &kv : g_windows)
      json += "," + json_escape(kv.first);
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
      target = g_hwnd;
    POINT p;
    GetCursorPos(&p);
    POINT c = p;
    ScreenToClient(target, &c);
    RECT cr;
    GetClientRect(target, &cr);
    bool inside = c.x >= 0 && c.y >= 0 && c.x < cr.right && c.y < cr.bottom;
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    char buf[384];
    std::snprintf(
        buf, sizeof(buf),
        "{\"x\":%ld,\"y\":%ld,\"window\":{\"x\":%ld,\"y\":%ld,\"inside\":%s},"
        "\"screen\":{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld,"
        "\"scale\":%.2f}}",
        p.x, p.y, c.x, c.y, inside ? "true" : "false", mi.rcMonitor.left,
        mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
        mi.rcMonitor.bottom - mi.rcMonitor.top, monitor_scale(mon));
    json = buf;
  } else if (what == "screens") {
    json = screens_json();
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
      nii.hWnd = g_hwnd;
      nii.uID = 1;
      RECT r;
      if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &r))) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld}",
                      r.left, r.top, r.right - r.left, r.bottom - r.top);
        json = buf;
      }
    }
  } else if (what.rfind("item:", 0) == 0) {
    auto it = g_id_reg.find(what.substr(5));
    if (it != g_id_reg.end()) {
      ItemReg *reg = it->second;
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
  std::string url;                     // navigated once the controller exists
  std::vector<std::string> pending_js; // eval'd once the controller exists
};

static TinyWin *win_for_id(const std::string &id) {
  auto it = g_windows.find(id);
  return it == g_windows.end() ? nullptr : it->second;
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

static ICoreWebView2Controller *ctrl_for_win(const std::string &id) {
  if (id.empty() || id == "main")
    return g_ctrl;
  TinyWin *tw = win_for_id(id);
  return tw ? tw->ctrl : nullptr;
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
  size_t c = composite.find(':');
  if (c == std::string::npos) {
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

static std::string sec_shim_js(const std::string &winid) {
  return "(() => {"
         "if (window.__tinyShim) return; window.__tinyShim = true;"
         "window.__TINY_WIN = '" + winid + "';"
         "let seq = 0; const pending = {};"
         "window.__invoke = (payload) => new Promise((res, rej) => {"
         "  const s = ++seq; pending[s] = { res, rej };"
         "  window.chrome.webview.postMessage(String(s) + ':' + String(payload));"
         "});"
         "window.__tinyResolve = (s, ok, jsonText) => {"
         "  const p = pending[s]; if (!p) return; delete pending[s];"
         "  let v = null; try { v = JSON.parse(jsonText); } catch (e) {}"
         "  ok ? p.res(v) : p.rej(v);"
         "};"
         "})();";
}

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
    pipe_write_line("CALL " + winid + ":" + body.substr(0, c) + " [" +
                    json_escape(body.substr(c + 1)) + "]");
    return S_OK;
  }
};

struct SecCtrlHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
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
        riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Controller *ctrl) override {
    TinyWin *tw = win_for_id(winid);
    if (!tw || FAILED(hr) || !ctrl)
      return S_OK;
    ctrl->AddRef();
    tw->ctrl = ctrl;
    ctrl->get_CoreWebView2(&tw->wv);
    RECT rc;
    GetClientRect(tw->hwnd, &rc);
    ctrl->put_Bounds(rc);
    if (tw->wv) {
      tw->wv->AddScriptToExecuteOnDocumentCreated(
          widen(sec_shim_js(winid)).c_str(), nullptr);
      tw->wv->AddScriptToExecuteOnDocumentCreated(widen(TINY_CLIENT_JS).c_str(),
                                                  nullptr);
      char inj[8192];
      DWORD n = GetEnvironmentVariableA("TINYJS_INJECT", inj, sizeof(inj));
      if (n > 0 && n < sizeof(inj))
        tw->wv->AddScriptToExecuteOnDocumentCreated(widen(inj).c_str(),
                                                    nullptr);
      SecMsgHandler *mh = new SecMsgHandler();
      mh->winid = winid;
      EventRegistrationToken tok;
      tw->wv->add_WebMessageReceived(mh, &tok);
      mh->Release();
      tw->wv->Navigate(widen(tw->url).c_str());
      for (auto &js : tw->pending_js)
        tw->wv->ExecuteScript(widen(js).c_str(), nullptr);
      tw->pending_js.clear();
    }
    return S_OK;
  }
};

static LRESULT CALLBACK secwin_proc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp) {
  switch (msg) {
  case WM_SIZE: {
    TinyWin *tw = win_for_id(id_for_hwnd(hwnd));
    if (tw && tw->ctrl) {
      RECT rc;
      GetClientRect(hwnd, &rc);
      tw->ctrl->put_Bounds(rc);
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
      g_windows.erase(id);
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

struct WinOpenReq {
  std::string id, page, title;
  int width = 600, height = 400;
  std::string frame, traffic, transparent, vibrancy, square, first_mouse;
  bool hasPos = false;
  int x = 0, y = 0;
};

static void do_winopen(webview_t, void *arg) {
  WinOpenReq *wr = static_cast<WinOpenReq *>(arg);
  if (TinyWin *ex = win_for_id(wr->id)) {
    ShowWindow(ex->hwnd, SW_SHOW);
    SetForegroundWindow(ex->hwnd);
    delete wr;
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = secwin_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TinyjsSecondary";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
  }
  bool frameless = wr->frame == "0" || wr->square == "1";
  DWORD style = frameless ? (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                             WS_MAXIMIZEBOX | WS_SYSMENU)
                          : WS_OVERLAPPEDWINDOW;
  RECT rc = {0, 0, wr->width, wr->height};
  AdjustWindowRect(&rc, style, FALSE);
  HWND hwnd = CreateWindowExW(
      0, L"TinyjsSecondary", widen(wr->title.empty() ? wr->id : wr->title).c_str(),
      style, wr->hasPos ? wr->x : CW_USEDEFAULT,
      wr->hasPos ? wr->y : CW_USEDEFAULT, rc.right - rc.left,
      rc.bottom - rc.top, nullptr, nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (!hwnd) {
    delete wr;
    return;
  }
  if (wr->square == "1") {
    DWORD pref = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));
  }
  TinyWin *tw = new TinyWin();
  tw->hwnd = hwnd;
  bool is_url = wr->page.rfind("http://", 0) == 0 ||
                wr->page.rfind("https://", 0) == 0;
  tw->url = is_url ? wr->page : to_file_url(wr->page);
  g_windows[wr->id] = tw;
  ShowWindow(hwnd, SW_SHOW);
  // Reuse the main webview's environment for the new controller.
  ICoreWebView2_2 *wv22 = nullptr;
  if (g_wv2 &&
      SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_2, (void **)&wv22)) &&
      wv22) {
    ICoreWebView2Environment *env = nullptr;
    if (SUCCEEDED(wv22->get_Environment(&env)) && env) {
      SecCtrlHandler *ch = new SecCtrlHandler();
      ch->winid = wr->id;
      env->CreateCoreWebView2Controller(hwnd, ch);
      ch->Release();
      env->Release();
    }
    wv22->Release();
  }
  delete wr;
}

static void do_winclose(webview_t, void *arg) {
  std::string *id = static_cast<std::string *>(arg);
  HWND h = hwnd_for_win(*id);
  if (h && h != g_hwnd)
    PostMessageW(h, WM_CLOSE, 0, 0);
  delete id;
}

struct EvalReq {
  std::string win, js;
};

static void do_eval_win(webview_t w, void *arg) {
  EvalReq *er = static_cast<EvalReq *>(arg);
  if (er->win == "main") {
    webview_eval(w, er->js.c_str());
  } else if (er->win == "*") {
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
  case WM_CLOSE:
    if (g_hide_on_close) {
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    }
    break;
  case WM_COMMAND: {
    auto it = g_cmd_reg.find((UINT)LOWORD(wp));
    if (it != g_cmd_reg.end() && it->second->kind == "menu") {
      if (it->second->enabled)
        pipe_write_line("MENU " + it->second->id);
      return 0;
    }
    break;
  }
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

static void do_terminate(webview_t w, void *) { webview_terminate(w); }

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

static void do_size(webview_t w, void *arg) {
  SizeReq *s = static_cast<SizeReq *>(arg);
  if (s->win == "main") {
    webview_set_size(w, s->width, s->height, WEBVIEW_HINT_NONE);
  } else {
    HWND h = hwnd_for_win(s->win);
    if (h) {
      RECT rc = {0, 0, s->width, s->height};
      AdjustWindowRect(&rc, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE);
      SetWindowPos(h, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOMOVE | SWP_NOZORDER);
    }
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
  else
    webview_eval(w, "location.reload()");
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

static void do_bounce(webview_t, void *arg) {
  int *critical = static_cast<int *>(arg);
  FLASHWINFO fi = {sizeof(fi)};
  fi.hwnd = g_hwnd;
  fi.dwFlags = FLASHW_ALL | (*critical ? FLASHW_TIMER : FLASHW_TIMERNOFG);
  fi.uCount = *critical ? 20 : 3;
  FlashWindowEx(&fi);
  delete critical;
}

// ---------------------------------------------------------------------------
// pipe read loop (background thread; UI work hops via webview_dispatch)

static void pipe_read_loop() {
  std::string buf;
  char chunk[4096];
  std::vector<MenuSpec> pending_menus;
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
        webview_dispatch(g_w, do_reply, rr);
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
        webview_dispatch(g_w, do_eval_win, er);
      } else if (line.rfind("TITLE@", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_title_win,
                         new EvalReq{line.substr(6, sp - 6),
                                     line.substr(sp + 1)});
      } else if (line.rfind("TITLE ", 0) == 0) {
        webview_dispatch(g_w, do_title, new std::string(line.substr(6)));
      } else if (line.rfind("SIZE@", 0) == 0) {
        size_t sp = line.find(' ', 5);
        if (sp == std::string::npos)
          continue;
        SizeReq *s = new SizeReq{600, 400};
        s->win = line.substr(5, sp - 5);
        std::sscanf(line.c_str() + sp + 1, "%d %d", &s->width, &s->height);
        webview_dispatch(g_w, do_size, s);
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
        flush_root();
        pending_menus.push_back(MenuSpec{line.substr(5), {}});
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
        build_stack.back().push_back(it);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line == "SEP") {
        MenuItemSpec sep;
        sep.separator = true;
        build_stack.back().push_back(sep);
      } else if ((in_menu_block || in_tray_block || in_ctx_block) &&
                 line.rfind("SUB ", 0) == 0) {
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
        webview_dispatch(g_w, apply_tray, new TraySpec(pending_tray));
      } else if (line == "TRAYREMOVE") {
        TraySpec *rm = new TraySpec{};
        rm->remove = true;
        webview_dispatch(g_w, apply_tray, rm);
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
        webview_dispatch(g_w, do_winop, new WinopReq{win, line.substr(body)});
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
        g_ctx_suppress = line.substr(12) == "1";
        webview_dispatch(g_w, [](webview_t, void *arg) {
          apply_ctx_suppress_fallback(arg != nullptr);
        }, g_ctx_suppress ? (void *)1 : nullptr);
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
        webview_dispatch(g_w, do_notify, req);
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
      } else if (line.rfind("KEYSTROKE ", 0) == 0) {
        size_t sp = line.find(' ', 10);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_keystroke,
                         new KeystrokeReq{line.substr(10, sp - 10),
                                          line.substr(sp + 1)});
      } else if (line.rfind("PERMCHK ", 0) == 0 ||
                 line.rfind("PERMREQ ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_perm,
                         new QReq{line.substr(8, sp - 8), line.substr(sp + 1)});
      } else if (line.rfind("SHELL ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_shell,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("POWER ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_power,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("SOUND", 0) == 0 &&
                 (line.size() == 5 || line[5] == ' ')) {
        std::string rest = line.size() > 6 ? line.substr(6) : "";
        size_t sp = rest.find(' ');
        std::string qid = sp == std::string::npos ? rest : rest.substr(0, sp);
        std::string target = sp == std::string::npos ? "" : rest.substr(sp + 1);
        webview_dispatch(g_w, do_sound, new QReq{qid, target});
      } else if (line.rfind("SECRET ", 0) == 0) {
        size_t sp = line.find(' ', 7);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_secret,
                         new QReq{line.substr(7, sp - 7), line.substr(sp + 1)});
      } else if (line.rfind("BOUNCE", 0) == 0) {
        webview_dispatch(g_w, do_bounce,
                         new int(line.size() > 7 ? std::atoi(line.c_str() + 7)
                                                 : 0));
      } else if (line.rfind("LOGIN ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos) {
          got(line.substr(6), "{\"status\":\"unsupported\"}");
          continue;
        }
        webview_dispatch(g_w, do_login,
                         new QReq{line.substr(6, sp - 6), line.substr(sp + 1)});
      } else if (line.rfind("VOICES ", 0) == 0) {
        webview_dispatch(g_w, do_voices, new QReq{line.substr(7), ""});
      } else if (line == "SAYSTOP") {
        webview_dispatch(g_w, do_saystop, nullptr);
      } else if (line.rfind("SAY ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_say,
                         new QReq{line.substr(4, sp - 4), line.substr(sp + 1)});
      } else if (line.rfind("PDF ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_pdf,
                         new QReq{line.substr(4, sp - 4), line.substr(sp + 1)});
      } else if (line.rfind("CAPTURE ", 0) == 0) {
        size_t sp = line.find(' ', 8);
        std::string qid = sp == std::string::npos ? line.substr(8)
                                                  : line.substr(8, sp - 8);
        std::string rest = sp == std::string::npos ? "0" : line.substr(sp + 1);
        webview_dispatch(g_w, do_capture, new QReq{qid, rest});
      } else if (line.rfind("THUMB ", 0) == 0) {
        size_t sp = line.find(' ', 6);
        if (sp == std::string::npos)
          continue;
        webview_dispatch(g_w, do_thumb,
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
        webview_dispatch(g_w, do_dragout, req);
      } else if (line.rfind("AUTH ", 0) == 0) {
        got(qid_of(line, 5), "{\"ok\":false,\"error\":\"unsupported on windows\"}");
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
        // nothing to stop
      } else if (line.rfind("AUDIOTAP ", 0) == 0) {
        got(qid_of(line, 9),
            "{\"ok\":false,\"code\":\"unsupported\","
            "\"message\":\"audioTap is not supported on windows\"}");
      } else if (line == "PRINT") {
        webview_dispatch(g_w, do_eval, new std::string("window.print()"));
      } else if (line == "RELOAD") {
        webview_dispatch(g_w, do_reload, nullptr);
      } else if (line == "QUIT") {
        webview_dispatch(g_w, do_terminate, nullptr);
      }
      // WINCLOSE / SHARE / NOWPLAYING / QUICKLOOK / BADGE / DOCKICON /
      // HAPTIC: fire-and-forget, no Windows equivalent yet.
    }
  }
  webview_dispatch(g_w, do_terminate, nullptr);
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
  if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
    std::fprintf(stderr, "launcher: CALL %s %s\n", id, req);
  pipe_write_line(std::string("CALL ") + id + " " + req);
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

  auto try_send = [&]() -> bool {
    HANDLE h = CreateFileW(widen(pipe).c_str(), GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
      return false;
    DWORD n = 0;
    WriteFile(h, json.data(), (DWORD)json.size(), &n, nullptr);
    CloseHandle(h);
    return true;
  };
  if (try_send())
    return 0;
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
    if (try_send())
      return 0;
  }
  return 1;
}

static int run(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "--embed-icon") == 0)
    return embed_icon(argv[2], argv[3]);
  if (argc >= 4 && strcmp(argv[1], "--open") == 0)
    return open_mode(argc, argv);
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

  g_w = webview_create(1 /* debug: enables devtools */, nullptr);
  if (!g_w) {
    std::fprintf(stderr,
                 "launcher: failed to create webview (is the WebView2 runtime "
                 "installed?)\n");
    return 1;
  }

  g_hwnd = (HWND)webview_get_native_handle(g_w,
                                           WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
  g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                                              (LONG_PTR)tiny_wndproc);

  // Page RPC + injected client library (document-start, every navigation).
  webview_bind(g_w, "__invoke", on_invoke, nullptr);
  webview_init(g_w, "window.__TINY_WIN = 'main';");
  webview_init(g_w, TINY_CLIENT_JS);
  {
    // Optional user-supplied document-start glue (see launcher.cc).
    char inj[8192];
    DWORD n = GetEnvironmentVariableA("TINYJS_INJECT", inj, sizeof(inj));
    if (n > 0 && n < sizeof(inj))
      webview_init(g_w, inj);
  }

  // Stash the controller + ICoreWebView2 for Reload()/settings/context-menu
  // interception/drag-drop/accelerators.
  g_ctrl = (ICoreWebView2Controller *)webview_get_native_handle(
      g_w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (g_ctrl)
    g_ctrl->get_CoreWebView2(&g_wv2);
  install_ctx_handler();
  install_drop_target();
  install_accel_handler();

  // Custom User-Agent (TINYJS_UA env; see createApp userAgent).
  {
    char ua[2048];
    DWORD n = GetEnvironmentVariableA("TINYJS_UA", ua, sizeof(ua));
    if (n > 0 && n < sizeof(ua) && g_wv2) {
      ICoreWebView2Settings *settings = nullptr;
      if (SUCCEEDED(g_wv2->get_Settings(&settings)) && settings) {
        ICoreWebView2Settings2 *s2 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings2,
                                               (void **)&s2)) &&
            s2) {
          s2->put_UserAgent(widen(ua).c_str());
          s2->Release();
        }
        settings->Release();
      }
    }
  }

  webview_set_title(g_w, title.c_str());
  webview_set_size(g_w, width, height, WEBVIEW_HINT_NONE);

  // Window/taskbar icon from a png (dev: the project's icon.png via
  // TINYJS_ICON; built apps: dist/icon.png set by the bridge).
  {
    char icon[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TINYJS_ICON", icon, sizeof(icon));
    if (n > 0 && n < sizeof(icon)) {
      HICON h = icon_from_png(icon);
      if (h) {
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
      SetWindowLongW(g_hwnd, GWL_EXSTYLE,
                     GetWindowLongW(g_hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    }
  }

  send_theme();
  std::thread(pipe_read_loop).detach();

  webview_run(g_w);
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
