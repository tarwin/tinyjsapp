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
                               it.checked, !it.disabled};
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
  webview_return(w, req->id.c_str(), 0, json.c_str());
  delete req;
}

// ---------------------------------------------------------------------------
// window ops

static void set_style_bits(LONG bits, bool on) {
  LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
  style = on ? (style | bits) : (style & ~bits);
  SetWindowLongW(g_hwnd, GWL_STYLE, style);
  SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
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

static void do_center() {
  RECT r;
  GetWindowRect(g_hwnd, &r);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
  int w = r.right - r.left, h = r.bottom - r.top;
  int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
  int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
  SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

struct WinopReq {
  std::string win, op;
};

static void do_winop(webview_t, void *arg) {
  WinopReq *req = static_cast<WinopReq *>(arg);
  const std::string &op = req->op;
  auto starts = [&](const char *p) { return op.rfind(p, 0) == 0; };
  if (op == "hide") {
    ShowWindow(g_hwnd, SW_HIDE);
  } else if (starts("show")) {
    bool activate = op != "show 0";
    ShowWindow(g_hwnd, activate ? SW_SHOW : SW_SHOWNA);
    if (activate)
      SetForegroundWindow(g_hwnd);
  } else if (op == "center") {
    do_center();
  } else if (op == "minimize") {
    ShowWindow(g_hwnd, SW_MINIMIZE);
  } else if (op == "restore") {
    ShowWindow(g_hwnd, SW_RESTORE);
  } else if (op == "zoom") {
    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
  } else if (op == "fullscreen") {
    set_fullscreen(!g_fullscreen);
  } else if (starts("fullscreen ")) {
    set_fullscreen(op.substr(11) == "1");
  } else if (starts("ontop ")) {
    SetWindowPos(g_hwnd, op.substr(6) == "1" ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  } else if (starts("resizable ")) {
    set_style_bits(WS_THICKFRAME | WS_MAXIMIZEBOX, op.substr(10) == "1");
  } else if (starts("clickthrough ")) {
    set_click_through(op.substr(13) == "1");
  } else if (starts("level ")) {
    g_level = op.substr(6);
    if (g_level == "floating" || g_level == "overlay") {
      SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else if (g_level == "desktop") {
      SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
      SetWindowPos(g_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else {
      SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE);
      g_level = "normal";
    }
  } else if (starts("pos ")) {
    int x = 0, y = 0;
    std::sscanf(op.c_str() + 4, "%d %d", &x, &y);
    SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
  } else if (starts("hideonclose ")) {
    g_hide_on_close = op.substr(12) == "1";
  }
  // dock/allspaces: no Windows equivalent — ignored.
  delete req;
}

struct ChromeReq {
  std::string win, frame, traffic, transparent, vibrancy, square, first_mouse;
};

static void do_chrome(webview_t, void *arg) {
  ChromeReq *req = static_cast<ChromeReq *>(arg);
  if (!req->frame.empty()) {
    g_frameless = req->frame == "0";
    set_style_bits(WS_CAPTION, !g_frameless);
  }
  if (!req->square.empty()) {
    g_square = req->square == "1";
    if (g_square) {
      // Borderless like macOS: square corners, no titlebar; resize edges kept.
      g_frameless = true;
      set_style_bits(WS_CAPTION, false);
    }
    DWORD pref = g_square ? 1 /* DWMWCP_DONOTROUND */ : 0 /* DEFAULT */;
    DwmSetWindowAttribute(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &pref, sizeof(pref));
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

static void do_clip_write(webview_t, void *arg) {
  ClipWriteReq *req = static_cast<ClipWriteReq *>(arg);
  if (!OpenClipboard(g_hwnd)) {
    delete req;
    return;
  }
  EmptyClipboard();
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
// GET

struct GetReq {
  std::string qid, what;
};

static std::string win_state_json() {
  RECT r;
  GetWindowRect(g_hwnd, &r);
  HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
  LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
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
      g_fullscreen ? "true" : "false", IsIconic(g_hwnd) ? "true" : "false",
      IsWindowVisible(g_hwnd) ? "true" : "false",
      GetForegroundWindow() == g_hwnd ? "true" : "false",
      (ex & WS_EX_TOPMOST) ? "true" : "false",
      (style & WS_THICKFRAME) ? "true" : "false",
      g_click_through ? "true" : "false", g_level.c_str(),
      (g_frameless || g_square) ? "false" : "true",
      (g_frameless || g_square) ? "false" : "true",
      g_square ? "true" : "false", mi.rcMonitor.right - mi.rcMonitor.left,
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
    json = "[\"main\"]";
  } else if (what == "win" || what.rfind("win:", 0) == 0) {
    json = win_state_json();
  } else if (what == "clipboard" || what == "clipboard:count") {
    json = clipboard_json(what == "clipboard:count");
  } else if (what == "mouse" || what.rfind("mouse:", 0) == 0) {
    POINT p;
    GetCursorPos(&p);
    POINT c = p;
    ScreenToClient(g_hwnd, &c);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
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

static void install_ctx_handler() {
  if (!g_wv2)
    return;
  ICoreWebView2_11 *wv11 = nullptr;
  if (SUCCEEDED(g_wv2->QueryInterface(IID_ICoreWebView2_11, (void **)&wv11)) &&
      wv11) {
    EventRegistrationToken tok;
    wv11->add_ContextMenuRequested(new CtxHandler(), &tok);
    wv11->Release();
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
};

static void do_size(webview_t w, void *arg) {
  SizeReq *s = static_cast<SizeReq *>(arg);
  webview_set_size(w, s->width, s->height, WEBVIEW_HINT_NONE);
  delete s;
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
  webview_return(w, req->id.c_str(), req->status == 0 ? 0 : 1,
                 req->json.c_str());
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
        std::string js;
        if (line[4] == ' ') {
          js = wire_unescape(line.substr(5));
        } else {
          size_t sp = line.find(' ', 5);
          if (sp == std::string::npos)
            continue;
          std::string win = line.substr(5, sp - 5);
          if (win != "*" && win != "main")
            continue; // secondary windows not ported
          js = wire_unescape(line.substr(sp + 1));
        }
        webview_dispatch(g_w, do_eval, new std::string(js));
      } else if (line.rfind("TITLE ", 0) == 0) {
        webview_dispatch(g_w, do_title, new std::string(line.substr(6)));
      } else if (line.rfind("SIZE ", 0) == 0) {
        SizeReq *s = new SizeReq{960, 640};
        std::sscanf(line.c_str() + 5, "%d %d", &s->width, &s->height);
        webview_dispatch(g_w, do_size, s);
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
        size_t body = 6;
        if (line[5] == '@') {
          size_t sp = line.find(' ', 6);
          if (sp == std::string::npos)
            continue;
          body = sp + 1; // targeted at a secondary window: apply to main
        }
        webview_dispatch(g_w, do_winop,
                         new WinopReq{"main", line.substr(body)});
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
        size_t body = 7;
        if (line[6] == '@') {
          size_t sp = line.find(' ', 7);
          if (sp == std::string::npos)
            continue;
          body = sp + 1;
        }
        std::vector<std::string> p = split_tabs(line.substr(body));
        ChromeReq *req = new ChromeReq;
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
        got(qid_of(line, 6), "{\"status\":\"unsupported\"}");
      } else if (line.rfind("VOICES ", 0) == 0) {
        got(line.substr(7), "{\"ok\":true,\"voices\":[]}");
      } else if (line.rfind("AUTH ", 0) == 0) {
        got(qid_of(line, 5), "{\"ok\":false,\"error\":\"unsupported on windows\"}");
      } else if (line.rfind("SAY ", 0) == 0 || line.rfind("OSA ", 0) == 0 ||
                 line.rfind("OCR ", 0) == 0 || line.rfind("PDF ", 0) == 0) {
        got_unsupported(qid_of(line, 4));
      } else if (line.rfind("THUMB ", 0) == 0) {
        got_unsupported(qid_of(line, 6));
      } else if (line.rfind("RECORD ", 0) == 0) {
        got_unsupported(qid_of(line, 7));
      } else if (line.rfind("CAPTURE ", 0) == 0 ||
                 line.rfind("WINCTRL ", 0) == 0) {
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
      } else if (line.rfind("WINOPEN ", 0) == 0) {
        std::fprintf(stderr,
                     "tinyjs launcher: secondary windows are not yet "
                     "supported on Windows\n");
      } else if (line == "PRINT") {
        webview_dispatch(g_w, do_eval, new std::string("window.print()"));
      } else if (line == "RELOAD") {
        webview_dispatch(g_w, do_reload, nullptr);
      } else if (line == "QUIT") {
        webview_dispatch(g_w, do_terminate, nullptr);
      }
      // WINCLOSE / DRAGOUT / SHARE / NOWPLAYING / SAYSTOP / QUICKLOOK /
      // BADGE / DOCKICON / HAPTIC: fire-and-forget, no Windows equivalent yet.
    }
  }
  webview_dispatch(g_w, do_terminate, nullptr);
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

static int run(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: %s <html-file-or-url> <pipe-name> [title] [WxH] "
                 "[version]\n",
                 argv[0]);
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

  // Stash the ICoreWebView2 for Reload()/settings/context-menu interception.
  ICoreWebView2Controller *ctrl =
      (ICoreWebView2Controller *)webview_get_native_handle(
          g_w, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (ctrl)
    ctrl->get_CoreWebView2(&g_wv2);
  install_ctx_handler();

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

  {
    std::string url = g_target_is_url ? g_target : to_file_url(g_target);
    if (GetEnvironmentVariableA("TINYJS_LAUNCHER_DEBUG", nullptr, 0))
      std::fprintf(stderr, "launcher: navigate %s\n", url.c_str());
    webview_navigate(g_w, url.c_str());
  }

  // Accessory activation (tray-only apps): start hidden.
  {
    char act[64];
    DWORD n = GetEnvironmentVariableA("TINYJS_ACTIVATION", act, sizeof(act));
    if (n > 0 && strcmp(act, "accessory") == 0)
      ShowWindow(g_hwnd, SW_HIDE);
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
