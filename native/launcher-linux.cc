// tinyjs native launcher — Linux (GTK3 + WebKitGTK 4.1).
//
// Speaks the same newline-delimited wire protocol as launcher.cc (macOS) and
// launcher-win.cc (Windows) over a unix domain socket the backend listens on:
//   launcher <html-file-or-url> <socket> [title] [WxH] [version]
//
// Also: `launcher --open <socket> <app-exe> [url-or-path]` — single-instance /
// deep-link forwarder for built apps (the .desktop Exec handler).
//
// Build: see setup.sh (pkg-config gtk+-3.0 webkit2gtk-4.1 [+appindicator, x11]).

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <libsoup/soup.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#ifdef TINYJS_X11
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#endif
#ifdef TINYJS_APPINDICATOR
#include <libayatana-appindicator/app-indicator.h>
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <functional>

#include "tiny_client.h"  // TINY_CLIENT_JS, generated from runtime/tiny.js

// ---------------------------------------------------------------- globals ---

static std::string g_app_name = "tinyjs";
static std::string g_app_version = "0.0.0";
static std::string g_app_id;          // TINYJS_APP_ID (WM class / notify identity)
static std::string g_target;          // html path or http(s) url
static bool g_target_is_url = false;
static int g_width = 960, g_height = 640;

static int g_sock = -1;
static std::mutex g_write_mutex;

static GtkWindow* g_win = nullptr;    // main window
static WebKitWebView* g_wv = nullptr; // main webview
static GtkWidget* g_vbox = nullptr;
static GtkWidget* g_menubar = nullptr;
static GtkAccelGroup* g_accel = nullptr;

static bool g_hide_on_close = false;
static bool g_accessory = false;
static bool g_quitting = false;

// chrome state (main window), reported by GET win
static bool g_chrome_frame = true, g_chrome_traffic = true,
            g_chrome_transparent = false, g_chrome_square = false,
            g_chrome_first_mouse = false;
static std::string g_chrome_vibrancy;  // "" = none
static std::string g_level = "normal";
static bool g_click_through = false, g_all_spaces = false;

struct SecWin {
  std::string id;
  GtkWindow* win = nullptr;
  WebKitWebView* wv = nullptr;
  bool frame = true, transparent = false, square = false, first_mouse = false;
  std::string vibrancy;
  std::string level = "normal";
  bool click_through = false;
};
static std::map<std::string, SecWin*> g_secwins;

// ------------------------------------------------------------------- utils --

static std::string wire_unescape(const std::string& s) {
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

static std::string wire_escape(const std::string& s) {
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

static std::string json_escape(const std::string& s) {
  std::string out = "\"";
  char buf[8];
  for (unsigned char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c < 0x20) { snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
    else out += (char)c;
  }
  out += "\"";
  return out;
}

static std::vector<std::string> split_tabs(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t i = s.find('\t', start);
    if (i == std::string::npos) { out.push_back(s.substr(start)); break; }
    out.push_back(s.substr(start, i - start));
    start = i + 1;
  }
  return out;
}

static std::string tab_field(const std::vector<std::string>& v, size_t i) {
  return i < v.size() ? v[i] : "";
}

static void pipe_write_line(const std::string& line) {
  std::lock_guard<std::mutex> lock(g_write_mutex);
  if (g_sock < 0) return;
  std::string data = line + "\n";
  const char* p = data.data();
  size_t left = data.size();
  while (left > 0) {
    ssize_t n = write(g_sock, p, left);
    if (n <= 0) return;
    p += n;
    left -= (size_t)n;
  }
}

static void send_got(const std::string& qid, const std::string& json) {
  pipe_write_line("GOT " + qid + " " + json);
}

static void got_unsupported(const std::string& qid) {
  send_got(qid, "{\"ok\":false,\"error\":\"unsupported on linux\"}");
}

// Run a function on the GTK main thread.
static void ui_dispatch(std::function<void()> fn) {
  auto* heap = new std::function<void()>(std::move(fn));
  g_idle_add([](gpointer data) -> gboolean {
    auto* f = (std::function<void()>*)data;
    (*f)();
    delete f;
    return G_SOURCE_REMOVE;
  }, heap);
}

static std::string home_dir() {
  const char* h = getenv("HOME");
  return h ? h : "/tmp";
}

static bool file_exists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}

// -------------------------------------------------------- window registry ---

static WebKitWebView* wv_for(const std::string& winid) {
  if (winid.empty() || winid == "main") return g_wv;
  auto it = g_secwins.find(winid);
  return it == g_secwins.end() ? nullptr : it->second->wv;
}

static GtkWindow* win_for(const std::string& winid) {
  if (winid.empty() || winid == "main") return g_win;
  auto it = g_secwins.find(winid);
  return it == g_secwins.end() ? nullptr : it->second->win;
}

static void eval_in(const std::string& winid, const std::string& js) {
  WebKitWebView* wv = wv_for(winid);
  if (!wv) return;
  webkit_web_view_evaluate_javascript(wv, js.c_str(), -1, nullptr, nullptr,
                                      nullptr, nullptr, nullptr);
}

static void eval_all(const std::string& js) {
  eval_in("main", js);
  for (auto& kv : g_secwins) eval_in(kv.first, js);
}

// Resolve/reject a page call: callid is "<winid>:<seq>".
static void reply_to_call(const std::string& callid, int status, const std::string& json) {
  size_t colon = callid.find(':');
  if (colon == std::string::npos) return;
  std::string winid = callid.substr(0, colon);
  std::string seq = callid.substr(colon + 1);
  for (char c : seq) if (c < '0' || c > '9') return;
  std::string js = "window.__tinyResolve(" + seq + "," +
                   (status == 0 ? "true" : "false") + "," + json_escape(json) + ")";
  eval_in(winid, js);
}

// --------------------------------------------------------- page injection ---

static std::string tiny_shim_js(const std::string& winid) {
  return
    "(() => {\n"
    "  if (window.__tinyShim) return; window.__tinyShim = true;\n"
    "  window.__TINY_WIN = '" + winid + "';\n"
    "  let seq = 0; const pending = {};\n"
    "  window.__invoke = (payload) => new Promise((res, rej) => {\n"
    "    const s = ++seq; pending[s] = { res, rej };\n"
    "    window.webkit.messageHandlers.tiny.postMessage(String(s) + ':' + String(payload));\n"
    "  });\n"
    "  window.__tinyResolve = (s, ok, jsonText) => {\n"
    "    const p = pending[s]; if (!p) return; delete pending[s];\n"
    "    let v = null; try { v = JSON.parse(jsonText); } catch (e) {}\n"
    "    ok ? p.res(v) : p.rej(v);\n"
    "  };\n"
    "})();\n";
}

// A page posted "<seq>:<payload>" from window `winid`.
static void on_script_message(WebKitUserContentManager*, WebKitJavascriptResult* res,
                              gpointer user_data) {
  const char* winid = (const char*)user_data;
  JSCValue* v = webkit_javascript_result_get_js_value(res);
  char* str = jsc_value_to_string(v);
  if (!str) return;
  std::string msg = str;
  g_free(str);
  size_t colon = msg.find(':');
  if (colon == std::string::npos) return;
  std::string seq = msg.substr(0, colon);
  std::string payload = msg.substr(colon + 1);
  pipe_write_line("CALL " + std::string(winid) + ":" + seq +
                  " [" + json_escape(payload) + "]");
}

// Build a UserContentManager with the full injection set for a window.
static WebKitUserContentManager* make_ucm(const std::string& winid) {
  WebKitUserContentManager* ucm = webkit_user_content_manager_new();
  auto add = [&](const std::string& src) {
    WebKitUserScript* s = webkit_user_script_new(
        src.c_str(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, s);
    webkit_user_script_unref(s);
  };
  add(tiny_shim_js(winid));
  add(TINY_CLIENT_JS);
  const char* inject = getenv("TINYJS_INJECT");
  if (inject && *inject) add(inject);
  g_signal_connect_data(ucm, "script-message-received::tiny",
                        G_CALLBACK(on_script_message), g_strdup(winid.c_str()),
                        [](gpointer data, GClosure*) { g_free(data); }, (GConnectFlags)0);
  webkit_user_content_manager_register_script_message_handler(ucm, "tiny");
  return ucm;
}

// ------------------------------------------------- context menu (all wins) ---

struct MenuItemSpec {
  bool separator = false;
  bool submenu = false;
  std::string id, label, key, flags;
  std::vector<MenuItemSpec> children;
};
static std::vector<MenuItemSpec> g_ctx_items;   // custom right-click menu
static bool g_ctx_custom = false;
static bool g_ctx_suppress = false;

// live item registry (menu bar + tray + ctx), id -> widget (may be null for ctx)
struct RegItem {
  GtkWidget* widget = nullptr;   // GtkCheckMenuItem
  std::string label;
  bool checked = false, enabled = true;
  std::string kind;              // "menu" | "tray" | "ctx"
};
static std::map<std::string, RegItem> g_items;

static gboolean on_context_menu(WebKitWebView*, WebKitContextMenu* menu,
                                GdkEvent*, WebKitHitTestResult*, gpointer) {
  if (g_ctx_custom) {
    webkit_context_menu_remove_all(menu);
    std::function<void(WebKitContextMenu*, const std::vector<MenuItemSpec>&)> build =
      [&](WebKitContextMenu* m, const std::vector<MenuItemSpec>& items) {
        for (const auto& it : items) {
          if (it.separator) {
            webkit_context_menu_append(m, webkit_context_menu_item_new_separator());
            continue;
          }
          if (it.submenu) {
            WebKitContextMenu* sub = webkit_context_menu_new();
            build(sub, it.children);
            webkit_context_menu_append(m,
              webkit_context_menu_item_new_with_submenu(it.label.c_str(), sub));
            continue;
          }
          GSimpleAction* act = g_simple_action_new(
              ("tinyctx-" + it.id).c_str(), nullptr);
          bool disabled = it.flags.find('d') != std::string::npos;
          g_simple_action_set_enabled(act, !disabled);
          g_signal_connect_data(act, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
              pipe_write_line("CTX " + std::string((const char*)data));
            }), g_strdup(it.id.c_str()),
            [](gpointer data, GClosure*) { g_free(data); }, (GConnectFlags)0);
          webkit_context_menu_append(m,
            webkit_context_menu_item_new_from_gaction(
              G_ACTION(act), it.label.c_str(), nullptr));
          g_object_unref(act);
        }
      };
    build(menu, g_ctx_items);
    return FALSE;  // show the (replaced) menu
  }
  if (g_ctx_suppress) return TRUE;  // show nothing
  return FALSE;                     // default WebKit menu
}

// ------------------------------------------------------------ drop files ----

// WebKitGTK delivers HTML5 drops to the page itself but real filesystem paths
// only travel in the text/uri-list selection — observe it as it arrives and
// emit DROP alongside (the page still gets its HTML5 event).
static void on_drag_data_received(GtkWidget*, GdkDragContext*, gint, gint,
                                  GtkSelectionData* data, guint, guint, gpointer) {
  GdkAtom target = gtk_selection_data_get_data_type(data);
  char* name = gdk_atom_name(target);
  bool is_uris = name && !strcmp(name, "text/uri-list");
  g_free(name);
  if (!is_uris) return;
  gchar** uris = gtk_selection_data_get_uris(data);
  if (!uris) return;
  std::string json = "[";
  bool any = false;
  for (int i = 0; uris[i]; i++) {
    char* path = g_filename_from_uri(uris[i], nullptr, nullptr);
    if (!path) continue;
    if (any) json += ",";
    json += json_escape(path);
    any = true;
    g_free(path);
  }
  json += "]";
  g_strfreev(uris);
  if (any) pipe_write_line("DROP " + json);
}

// ------------------------------------------------------------- webview ------

static WebKitSettings* make_settings() {
  WebKitSettings* s = webkit_settings_new();
  webkit_settings_set_enable_developer_extras(s, TRUE);
  webkit_settings_set_enable_webgl(s, TRUE);
  webkit_settings_set_javascript_can_access_clipboard(s, TRUE);
  webkit_settings_set_allow_file_access_from_file_urls(s, TRUE);
  webkit_settings_set_allow_universal_access_from_file_urls(s, TRUE);
  webkit_settings_set_enable_media_stream(s, TRUE);
  webkit_settings_set_enable_mediasource(s, TRUE);
  const char* ua = getenv("TINYJS_UA");
  if (ua && *ua) webkit_settings_set_user_agent(s, ua);
  return s;
}

// Enable experimental WebKit features by name (WebGPU parity with the macOS
// launcher, which force-enables the WebKit flag).
static void enable_features(WebKitSettings* s) {
  WebKitFeatureList* list = webkit_settings_get_experimental_features();
  if (!list) return;
  for (gsize i = 0; i < webkit_feature_list_get_length(list); i++) {
    WebKitFeature* f = webkit_feature_list_get(list, i);
    const char* ident = webkit_feature_get_identifier(f);
    if (ident && (!strcmp(ident, "WebGPUEnabled") || !strcmp(ident, "WebGPU"))) {
      webkit_settings_set_feature_enabled(s, f, TRUE);
    }
  }
  webkit_feature_list_unref(list);
}

// tiny-media://proxy/?u=<url> — stream a remote http(s) resource with
// permissive CORS so cross-origin audio is untainted for Web Audio.
static SoupSession* g_media_session = nullptr;

static void media_scheme_cb(WebKitURISchemeRequest* req, gpointer) {
  const char* uri = webkit_uri_scheme_request_get_uri(req);
  std::string upstream;
  if (uri) {
    const char* q = strstr(uri, "u=");
    if (q) {
      char* dec = g_uri_unescape_string(q + 2, nullptr);
      if (dec) { upstream = dec; g_free(dec); }
    }
  }
  if (upstream.rfind("http://", 0) != 0 && upstream.rfind("https://", 0) != 0) {
    GError* err = g_error_new_literal(WEBKIT_NETWORK_ERROR, 1, "bad tiny-media url");
    webkit_uri_scheme_request_finish_error(req, err);
    g_error_free(err);
    return;
  }
  if (!g_media_session) g_media_session = soup_session_new();
  SoupMessage* msg = soup_message_new("GET", upstream.c_str());
  if (!msg) {
    GError* err = g_error_new_literal(WEBKIT_NETWORK_ERROR, 1, "bad tiny-media url");
    webkit_uri_scheme_request_finish_error(req, err);
    g_error_free(err);
    return;
  }
  g_object_ref(req);
  soup_session_send_async(g_media_session, msg, G_PRIORITY_DEFAULT, nullptr,
    [](GObject* src, GAsyncResult* res, gpointer data) {
      WebKitURISchemeRequest* req = (WebKitURISchemeRequest*)data;
      GError* error = nullptr;
      GInputStream* stream = soup_session_send_finish(SOUP_SESSION(src), res, &error);
      if (!stream) {
        webkit_uri_scheme_request_finish_error(req, error);
        g_clear_error(&error);
        g_object_unref(req);
        return;
      }
      SoupMessage* m = soup_session_get_async_result_message(SOUP_SESSION(src), res);
      SoupMessageHeaders* h = m ? soup_message_get_response_headers(m) : nullptr;
      goffset len = h ? soup_message_headers_get_content_length(h) : -1;
      const char* ctype = h ? soup_message_headers_get_one(h, "Content-Type") : nullptr;
      // Live icecast-style streams answer 200 with no length; give the media
      // stack a huge synthetic length so it plays progressively.
      if (len <= 0) len = (goffset)1 << 40;
      WebKitURISchemeResponse* resp = webkit_uri_scheme_response_new(stream, len);
      webkit_uri_scheme_response_set_status(resp, 200, nullptr);
      webkit_uri_scheme_response_set_content_type(resp,
          ctype && *ctype ? ctype : "application/octet-stream");
      SoupMessageHeaders* rh = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
      soup_message_headers_append(rh, "Access-Control-Allow-Origin", "*");
      webkit_uri_scheme_response_set_http_headers(resp, rh);
      webkit_uri_scheme_request_finish_with_response(req, resp);
      g_object_unref(resp);
      g_object_unref(stream);
      g_object_unref(req);
    }, req);
  g_object_unref(msg);
}

// --------------------------------------------------------------- chrome -----

static void apply_rgba_visual(GtkWidget* win) {
  GdkScreen* screen = gtk_widget_get_screen(win);
  GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
  if (visual) gtk_widget_set_visual(win, visual);
}

static void apply_transparent(GtkWindow* win, WebKitWebView* wv, bool on) {
  GdkRGBA clear = {0, 0, 0, 0};
  GdkRGBA white = {1, 1, 1, 1};
  webkit_web_view_set_background_color(wv, on ? &clear : &white);
  gtk_widget_set_app_paintable(GTK_WIDGET(win), on);
}

// fields: frame, traffic, transparent, vibrancy, square, firstMouse ('' = keep)
static void apply_chrome(const std::string& winid, const std::vector<std::string>& f) {
  GtkWindow* win = win_for(winid);
  WebKitWebView* wv = wv_for(winid);
  if (!win || !wv) return;
  bool main_win = (winid.empty() || winid == "main");
  SecWin* sec = nullptr;
  if (!main_win) sec = g_secwins.count(winid) ? g_secwins[winid] : nullptr;

  std::string frame = tab_field(f, 0), traffic = tab_field(f, 1),
              transp = tab_field(f, 2), vib = tab_field(f, 3),
              square = tab_field(f, 4), first = tab_field(f, 5);

  if (!frame.empty()) {
    bool on = frame == "1";
    gtk_window_set_decorated(win, on);
    if (main_win) g_chrome_frame = on; else if (sec) sec->frame = on;
  }
  if (!traffic.empty() && main_win) g_chrome_traffic = traffic == "1";
  if (!transp.empty()) {
    bool on = transp == "1";
    apply_transparent(win, wv, on);
    if (main_win) g_chrome_transparent = on; else if (sec) sec->transparent = on;
  }
  if (!vib.empty()) {
    std::string v = vib == "none" ? "" : vib;
    if (main_win) g_chrome_vibrancy = v; else if (sec) sec->vibrancy = v;
    // no portable blur on Linux — recorded for GET, otherwise a no-op
  }
  if (!square.empty()) {
    bool on = square == "1";
    if (on) gtk_window_set_decorated(win, FALSE);
    else gtk_window_set_decorated(win, main_win ? g_chrome_frame : (sec ? sec->frame : true));
    if (main_win) g_chrome_square = on; else if (sec) sec->square = on;
  }
  if (!first.empty()) {
    bool on = first == "1";
    if (main_win) g_chrome_first_mouse = on; else if (sec) sec->first_mouse = on;
    // GTK already delivers first clicks — stored for GET only
  }
}

// ----------------------------------------------------------- window state ---

// per-window GdkWindowState tracking (minimized/fullscreen for GET win)
static std::map<GtkWindow*, GdkWindowState> g_winstate;

static gboolean on_window_state(GtkWidget* w, GdkEventWindowState* ev, gpointer) {
  g_winstate[GTK_WINDOW(w)] = ev->new_window_state;
  return FALSE;
}

static int menubar_height() {
  if (!g_menubar || !gtk_widget_get_visible(g_menubar)) return 0;
  GtkAllocation a;
  gtk_widget_get_allocation(g_menubar, &a);
  return a.height > 1 ? a.height : 0;
}

// SIZE sets the CONTENT area (the webview box); the main window's menu bar
// rides above it, so add its height back when resizing the outer window.
static void set_content_size(const std::string& winid, int w, int h) {
  GtkWindow* win = win_for(winid);
  if (!win) return;
  int extra = (winid.empty() || winid == "main") ? menubar_height() : 0;
  gtk_window_resize(win, w, h + extra);
}

static void set_level(const std::string& winid, const std::string& level) {
  GtkWindow* win = win_for(winid);
  if (!win) return;
  if (level == "desktop") {
    gtk_window_set_keep_above(win, FALSE);
    gtk_window_set_keep_below(win, TRUE);
  } else if (level == "overlay" || level == "floating") {
    gtk_window_set_keep_below(win, FALSE);
    gtk_window_set_keep_above(win, TRUE);
  } else {
    gtk_window_set_keep_above(win, FALSE);
    gtk_window_set_keep_below(win, FALSE);
  }
  if (winid.empty() || winid == "main") g_level = level;
  else if (g_secwins.count(winid)) g_secwins[winid]->level = level;
}

static void set_click_through(const std::string& winid, bool on) {
  GtkWindow* win = win_for(winid);
  if (!win) return;
  GdkWindow* gdkwin = gtk_widget_get_window(GTK_WIDGET(win));
  if (gdkwin) {
    if (on) {
      cairo_region_t* empty = cairo_region_create();
      gdk_window_input_shape_combine_region(gdkwin, empty, 0, 0);
      cairo_region_destroy(empty);
    } else {
      gdk_window_input_shape_combine_region(gdkwin, nullptr, 0, 0);
    }
  }
  if (winid.empty() || winid == "main") g_click_through = on;
  else if (g_secwins.count(winid)) g_secwins[winid]->click_through = on;
}

static void do_winop(const std::string& winid, const std::string& op) {
  GtkWindow* win = win_for(winid);
  if (!win) return;
  bool main_win = (winid.empty() || winid == "main");

  if (op == "hide") gtk_widget_hide(GTK_WIDGET(win));
  else if (op == "show" || op == "show 1") {
    gtk_widget_show(GTK_WIDGET(win));
    gtk_window_present(win);
  } else if (op == "show 0") {
    // surface without stealing focus
    gtk_window_set_focus_on_map(win, FALSE);
    gtk_widget_show(GTK_WIDGET(win));
    gtk_window_set_focus_on_map(win, TRUE);
  } else if (op == "center") {
    GdkDisplay* d = gdk_display_get_default();
    GdkWindow* gw = gtk_widget_get_window(GTK_WIDGET(win));
    GdkMonitor* m = gw ? gdk_display_get_monitor_at_window(d, gw)
                       : gdk_display_get_primary_monitor(d);
    if (m) {
      GdkRectangle wa;
      gdk_monitor_get_workarea(m, &wa);
      int w, h;
      gtk_window_get_size(win, &w, &h);
      gtk_window_move(win, wa.x + (wa.width - w) / 2, wa.y + (wa.height - h) / 2);
    }
  }
  else if (op == "minimize") gtk_window_iconify(win);
  else if (op == "restore") gtk_window_deiconify(win);
  else if (op == "zoom") {
    GdkWindowState st = g_winstate.count(win) ? g_winstate[win] : (GdkWindowState)0;
    if (st & GDK_WINDOW_STATE_MAXIMIZED) gtk_window_unmaximize(win);
    else gtk_window_maximize(win);
  }
  else if (op == "fullscreen") {
    GdkWindowState st = g_winstate.count(win) ? g_winstate[win] : (GdkWindowState)0;
    if (st & GDK_WINDOW_STATE_FULLSCREEN) gtk_window_unfullscreen(win);
    else gtk_window_fullscreen(win);
  }
  else if (op == "fullscreen 1") gtk_window_fullscreen(win);
  else if (op == "fullscreen 0") gtk_window_unfullscreen(win);
  else if (op == "ontop 1") gtk_window_set_keep_above(win, TRUE);
  else if (op == "ontop 0") gtk_window_set_keep_above(win, FALSE);
  else if (op == "resizable 1") gtk_window_set_resizable(win, TRUE);
  else if (op == "resizable 0") gtk_window_set_resizable(win, FALSE);
  else if (op == "clickthrough 1") set_click_through(winid, true);
  else if (op == "clickthrough 0") set_click_through(winid, false);
  else if (op.rfind("level ", 0) == 0) set_level(winid, op.substr(6));
  else if (op.rfind("pos ", 0) == 0) {
    int x = 0, y = 0;
    if (sscanf(op.c_str() + 4, "%d %d", &x, &y) == 2) gtk_window_move(win, x, y);
  }
  else if (op == "hideonclose 1") { if (main_win) g_hide_on_close = true; }
  else if (op == "hideonclose 0") { if (main_win) g_hide_on_close = false; }
  else if (op == "dock 1") { if (main_win) gtk_window_set_skip_taskbar_hint(win, FALSE); }
  else if (op == "dock 0") { if (main_win) gtk_window_set_skip_taskbar_hint(win, TRUE); }
  else if (op == "allspaces 1") { gtk_window_stick(win); if (main_win) g_all_spaces = true; }
  else if (op == "allspaces 0") { gtk_window_unstick(win); if (main_win) g_all_spaces = false; }
  // unknown verbs: silently ignored
}

// --------------------------------------------------------- menu building ----

// shared block-builder state (menu bar / tray / context menu declarations)
static std::vector<MenuItemSpec> g_build_menus_current;    // items at current level
static std::vector<std::vector<MenuItemSpec>*> g_build_stack;
struct MenuSpec { std::string title; std::vector<MenuItemSpec> items; };
static std::vector<MenuSpec> g_build_menubar;              // MENU sections
static int g_build_mode = 0;   // 0 none, 1 menubar, 2 tray, 3 ctx
struct TraySpec {
  std::string title, icon, tooltip;
  bool template_icon = true, primary = false;
  std::vector<MenuItemSpec> items;
};
static TraySpec g_build_tray;

static std::vector<MenuItemSpec>* build_top() {
  return g_build_stack.empty() ? nullptr : g_build_stack.back();
}

static void build_item_line(const std::string& op, const std::string& rest) {
  std::vector<MenuItemSpec>* level = build_top();
  if (!level) return;
  if (op == "SEP") {
    MenuItemSpec s;
    s.separator = true;
    level->push_back(s);
  } else if (op == "ITEM") {
    auto f = split_tabs(rest);
    MenuItemSpec it;
    it.id = tab_field(f, 0);
    it.label = tab_field(f, 1);
    it.key = tab_field(f, 2);
    it.flags = tab_field(f, 3);
    if (it.id.empty()) it.id = it.label;
    if (it.label.empty()) it.label = it.id;
    level->push_back(it);
  } else if (op == "SUB") {
    auto f = split_tabs(rest);
    MenuItemSpec sub;
    sub.submenu = true;
    sub.id = tab_field(f, 0);
    sub.label = tab_field(f, 1);
    if (sub.label.empty()) sub.label = sub.id;
    level->push_back(sub);
    g_build_stack.push_back(&level->back().children);
  } else if (op == "SUBEND") {
    if (g_build_stack.size() > 1) g_build_stack.pop_back();
  }
}

static void on_menu_item_activate(GtkMenuItem*, gpointer data) {
  const char* payload = (const char*)data;   // "menu\0id" packed as "kind:id"
  std::string s = payload;
  size_t colon = s.find(':');
  std::string kind = s.substr(0, colon), id = s.substr(colon + 1);
  auto it = g_items.find(id);
  if (it != g_items.end() && !it->second.enabled) return;
  if (kind == "tray") pipe_write_line("TRAY " + id);
  else pipe_write_line("MENU " + id);
}

// Build a GtkMenu from item specs; register items under `kind`.
static GtkWidget* build_gtk_menu(const std::vector<MenuItemSpec>& items,
                                 const std::string& kind) {
  GtkWidget* menu = gtk_menu_new();
  for (const auto& it : items) {
    if (it.separator) {
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      continue;
    }
    if (it.submenu) {
      GtkWidget* mi = gtk_menu_item_new_with_label(it.label.c_str());
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), build_gtk_menu(it.children, kind));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      continue;
    }
    bool checked = it.flags.find('c') != std::string::npos;
    bool disabled = it.flags.find('d') != std::string::npos;
    GtkWidget* mi = gtk_check_menu_item_new_with_label(it.label.c_str());
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), checked);
    // hide the check box unless checked (plain items look plain)
    if (!checked) g_object_set(mi, "draw-as-radio", FALSE, NULL);
    gtk_widget_set_sensitive(mi, !disabled);
    if (!it.key.empty() && g_accel && kind == "menu") {
      guint keyval = gdk_keyval_from_name(it.key.c_str());
      if (keyval != GDK_KEY_VoidSymbol) {
        gtk_widget_add_accelerator(mi, "activate", g_accel, keyval,
                                   GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
      }
    }
    g_signal_connect_data(mi, "activate", G_CALLBACK(on_menu_item_activate),
      g_strdup((kind + ":" + it.id).c_str()),
      [](gpointer data, GClosure*) { g_free(data); }, (GConnectFlags)0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    RegItem reg;
    reg.widget = mi;
    reg.label = it.label;
    reg.checked = checked;
    reg.enabled = !disabled;
    reg.kind = kind;
    g_items[it.id] = reg;
  }
  return menu;
}

static void clear_registry_kind(const std::string& kind) {
  for (auto it = g_items.begin(); it != g_items.end();) {
    if (it->second.kind == kind) it = g_items.erase(it);
    else ++it;
  }
}

static void apply_menus() {
  if (!g_menubar) return;
  clear_registry_kind("menu");
  gtk_container_foreach(GTK_CONTAINER(g_menubar),
    [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);
  for (const auto& m : g_build_menubar) {
    GtkWidget* top = gtk_menu_item_new_with_label(m.title.c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), build_gtk_menu(m.items, "menu"));
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menubar), top);
  }
  if (g_build_menubar.empty()) gtk_widget_hide(g_menubar);
  else gtk_widget_show_all(g_menubar);
}

static void menu_update(const std::string& rest) {
  auto f = split_tabs(rest);
  std::string id = tab_field(f, 0), label = tab_field(f, 1),
              checked = tab_field(f, 2), enabled = tab_field(f, 3);
  auto it = g_items.find(id);
  if (it == g_items.end()) return;
  RegItem& reg = it->second;
  if (!label.empty()) {
    reg.label = label;
    if (reg.widget) gtk_menu_item_set_label(GTK_MENU_ITEM(reg.widget), label.c_str());
  }
  if (!checked.empty()) {
    reg.checked = checked == "1";
    if (reg.widget && GTK_IS_CHECK_MENU_ITEM(reg.widget)) {
      g_signal_handlers_block_matched(reg.widget, G_SIGNAL_MATCH_FUNC, 0, 0,
        nullptr, (gpointer)on_menu_item_activate, nullptr);
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(reg.widget), reg.checked);
      g_signal_handlers_unblock_matched(reg.widget, G_SIGNAL_MATCH_FUNC, 0, 0,
        nullptr, (gpointer)on_menu_item_activate, nullptr);
    }
  }
  if (!enabled.empty()) {
    reg.enabled = enabled == "1";
    if (reg.widget) gtk_widget_set_sensitive(reg.widget, reg.enabled);
  }
  // context-menu items live in the stored spec, not widgets
  std::function<void(std::vector<MenuItemSpec>&)> patch =
    [&](std::vector<MenuItemSpec>& items) {
      for (auto& mi : items) {
        if (mi.submenu) { patch(mi.children); continue; }
        if (mi.id != id) continue;
        if (!label.empty()) mi.label = label;
        std::string fl;
        bool c = checked.empty() ? mi.flags.find('c') != std::string::npos : checked == "1";
        bool d = enabled.empty() ? mi.flags.find('d') != std::string::npos : enabled == "0";
        if (c) fl += 'c';
        if (d) fl += 'd';
        mi.flags = fl;
      }
    };
  patch(g_ctx_items);
}

// ------------------------------------------------------------------ tray ----

#ifdef TINYJS_APPINDICATOR
static AppIndicator* g_indicator = nullptr;
static GtkWidget* g_tray_menu = nullptr;
#endif

static void apply_tray() {
#ifdef TINYJS_APPINDICATOR
  clear_registry_kind("tray");
  if (!g_indicator) {
    g_indicator = app_indicator_new(
        (g_app_id.empty() ? "tinyjs-app" : g_app_id).c_str(),
        "application-default-icon", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
  }
  // Icon: a png path (theme-path trick), or the default app icon.
  const std::string& icon = g_build_tray.icon;
  if (!icon.empty() && icon[0] == '/' && file_exists(icon)) {
    char* dir = g_path_get_dirname(icon.c_str());
    char* base = g_path_get_basename(icon.c_str());
    std::string name = base;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    app_indicator_set_icon_theme_path(g_indicator, dir);
    app_indicator_set_icon_full(g_indicator, name.c_str(),
        g_build_tray.tooltip.empty() ? g_app_name.c_str() : g_build_tray.tooltip.c_str());
    g_free(dir);
    g_free(base);
  } else {
    // sf:/emoji:/missing → an icon file the app ships, else a generic glyph
    const char* env_icon = getenv("TINYJS_ICON");
    if (env_icon && *env_icon && file_exists(env_icon)) {
      char* dir = g_path_get_dirname(env_icon);
      char* base = g_path_get_basename(env_icon);
      std::string name = base;
      size_t dot = name.rfind('.');
      if (dot != std::string::npos) name = name.substr(0, dot);
      app_indicator_set_icon_theme_path(g_indicator, dir);
      app_indicator_set_icon_full(g_indicator, name.c_str(), g_app_name.c_str());
      g_free(dir);
      g_free(base);
    } else {
      app_indicator_set_icon_full(g_indicator, "application-default-icon",
                                  g_app_name.c_str());
    }
  }
  if (!g_build_tray.title.empty()) {
    app_indicator_set_label(g_indicator, g_build_tray.title.c_str(), nullptr);
  } else {
    app_indicator_set_label(g_indicator, "", nullptr);
  }
  // AppIndicator/SNI can only open a menu on click (no bare-click events), so
  // an empty menu — or primary-action mode — gets a synthetic first item that
  // emits TRAYCLICK.
  std::vector<MenuItemSpec> items = g_build_tray.items;
  if (items.empty() || g_build_tray.primary) {
    MenuItemSpec open;
    open.id = "\ttrayclick";  // internal marker (tabs can't appear in real ids)
    open.label = g_build_tray.title.empty()
        ? (g_build_tray.tooltip.empty() ? "Open" : g_build_tray.tooltip)
        : g_build_tray.title;
    MenuItemSpec sep;
    sep.separator = true;
    if (!items.empty()) items.insert(items.begin(), sep);
    items.insert(items.begin(), open);
  }
  GtkWidget* menu = gtk_menu_new();
  for (const auto& it : items) {
    if (it.separator) {
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      continue;
    }
    if (it.submenu) {
      GtkWidget* mi = gtk_menu_item_new_with_label(it.label.c_str());
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), build_gtk_menu(it.children, "tray"));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      continue;
    }
    if (it.id == "\ttrayclick") {
      GtkWidget* mi = gtk_menu_item_new_with_label(it.label.c_str());
      g_signal_connect(mi, "activate",
        G_CALLBACK(+[](GtkMenuItem*, gpointer) { pipe_write_line("TRAYCLICK"); }),
        nullptr);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      continue;
    }
    bool checked = it.flags.find('c') != std::string::npos;
    bool disabled = it.flags.find('d') != std::string::npos;
    GtkWidget* mi = gtk_check_menu_item_new_with_label(it.label.c_str());
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), checked);
    gtk_widget_set_sensitive(mi, !disabled);
    g_signal_connect_data(mi, "activate", G_CALLBACK(on_menu_item_activate),
      g_strdup(("tray:" + it.id).c_str()),
      [](gpointer data, GClosure*) { g_free(data); }, (GConnectFlags)0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    RegItem reg;
    reg.widget = mi;
    reg.label = it.label;
    reg.checked = checked;
    reg.enabled = !disabled;
    reg.kind = "tray";
    g_items[it.id] = reg;
  }
  gtk_widget_show_all(menu);
  app_indicator_set_menu(g_indicator, GTK_MENU(menu));
  if (g_tray_menu) gtk_widget_destroy(g_tray_menu);
  g_tray_menu = menu;
  app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
#endif
}

static void remove_tray() {
#ifdef TINYJS_APPINDICATOR
  clear_registry_kind("tray");
  if (g_indicator) app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_PASSIVE);
#endif
}

// --------------------------------------------------------------- dialogs ----

static void do_dialog(const std::string& callid, const std::string& body) {
  auto f = split_tabs(body);
  std::string op = tab_field(f, 0);
  auto label_or = [](const std::string& s, const char* dflt) {
    return s.empty() ? std::string(dflt) : s;
  };

  if (op == "open" || op == "openmulti" || op == "dir" || op == "save") {
    GtkFileChooserAction action =
        op == "dir" ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
      : op == "save" ? GTK_FILE_CHOOSER_ACTION_SAVE
      : GTK_FILE_CHOOSER_ACTION_OPEN;
    GtkFileChooserNative* dlg = gtk_file_chooser_native_new(
        g_app_name.c_str(), g_win, action, nullptr, nullptr);
    if (op == "openmulti") {
      gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
    }
    if (op == "save") {
      gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    }
    gint res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg));
    if (res != GTK_RESPONSE_ACCEPT) {
      reply_to_call(callid, 0, "null");
    } else if (op == "openmulti") {
      GSList* list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
      std::string json = "[";
      bool any = false;
      for (GSList* l = list; l; l = l->next) {
        if (any) json += ",";
        json += json_escape((char*)l->data);
        any = true;
        g_free(l->data);
      }
      json += "]";
      g_slist_free(list);
      reply_to_call(callid, 0, json);
    } else {
      char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
      reply_to_call(callid, 0, path ? json_escape(path) : "null");
      g_free(path);
    }
    g_object_unref(dlg);
    return;
  }

  if (op == "alert" || op == "confirm") {
    std::string message = label_or(tab_field(f, 1), g_app_name.c_str());
    std::string detail = tab_field(f, 2);
    std::string ok = label_or(tab_field(f, 3), "OK");
    std::string cancel = label_or(tab_field(f, 4), "Cancel");
    GtkWidget* dlg = gtk_message_dialog_new(g_win,
        GTK_DIALOG_MODAL,
        op == "alert" ? GTK_MESSAGE_INFO : GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE, "%s", message.c_str());
    if (!detail.empty()) {
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s",
                                               detail.c_str());
    }
    if (op == "confirm") {
      gtk_dialog_add_button(GTK_DIALOG(dlg), cancel.c_str(), GTK_RESPONSE_CANCEL);
    }
    gtk_dialog_add_button(GTK_DIALOG(dlg), ok.c_str(), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (op == "alert") reply_to_call(callid, 0, "true");
    else reply_to_call(callid, 0, res == GTK_RESPONSE_OK ? "true" : "false");
    return;
  }

  if (op == "prompt") {
    std::string message = label_or(tab_field(f, 1), g_app_name.c_str());
    std::string dflt = tab_field(f, 2);
    std::string ok = label_or(tab_field(f, 3), "OK");
    std::string cancel = label_or(tab_field(f, 4), "Cancel");
    GtkWidget* dlg = gtk_dialog_new_with_buttons(g_app_name.c_str(), g_win,
        GTK_DIALOG_MODAL, cancel.c_str(), GTK_RESPONSE_CANCEL,
        ok.c_str(), GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* label = gtk_label_new(message.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), dflt.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 8);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);
    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    std::string text = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_widget_destroy(dlg);
    reply_to_call(callid, 0, res == GTK_RESPONSE_OK ? json_escape(text) : "null");
    return;
  }

  reply_to_call(callid, 1, json_escape("unknown dialog op: " + op));
}

// ------------------------------------------------------------- clipboard ----

static int g_clip_count = 0;          // our own monotonically-increasing counter
static int g_clip_self_count = -1;    // count value produced by our own write
static bool g_clip_watching = false;
static std::string g_clip_image_tmp;  // materialized clipboard png

struct ClipData {
  std::string text, html, color;
  GdkPixbuf* image = nullptr;
  std::vector<std::string> uris;
};
static ClipData* g_clip_data = nullptr;

static void clip_get_func(GtkClipboard*, GtkSelectionData* sel, guint, gpointer data) {
  ClipData* d = (ClipData*)data;
  GdkAtom target = gtk_selection_data_get_target(sel);
  char* name = gdk_atom_name(target);
  std::string t = name ? name : "";
  g_free(name);
  if (t == "text/html" && !d->html.empty()) {
    gtk_selection_data_set(sel, target, 8, (const guchar*)d->html.data(),
                           d->html.size());
  } else if (t == "text/uri-list" && !d->uris.empty()) {
    std::vector<char*> arr;
    for (auto& u : d->uris) arr.push_back((char*)u.c_str());
    arr.push_back(nullptr);
    gtk_selection_data_set_uris(sel, arr.data());
  } else if ((t == "image/png" || t.rfind("image/", 0) == 0) && d->image) {
    gtk_selection_data_set_pixbuf(sel, d->image);
  } else if (!d->text.empty() || !d->color.empty()) {
    const std::string& s = d->text.empty() ? d->color : d->text;
    gtk_selection_data_set_text(sel, s.c_str(), s.size());
  }
}

static void clip_clear_func(GtkClipboard*, gpointer data) {
  ClipData* d = (ClipData*)data;
  if (d == g_clip_data) g_clip_data = nullptr;
  if (d->image) g_object_unref(d->image);
  delete d;
}

static void do_clipwrite(const std::string& rest) {
  auto f = split_tabs(rest);
  ClipData* d = new ClipData();
  d->text = wire_unescape(tab_field(f, 0));
  d->html = wire_unescape(tab_field(f, 1));
  std::string image = wire_unescape(tab_field(f, 2));
  d->color = wire_unescape(tab_field(f, 3));
  for (size_t i = 4; i < f.size(); i++) {
    std::string p = wire_unescape(f[i]);
    if (p.empty()) continue;
    char* uri = g_filename_to_uri(p.c_str(), nullptr, nullptr);
    if (uri) { d->uris.push_back(uri); g_free(uri); }
  }
  if (!image.empty()) {
    if (image.rfind("data:", 0) == 0) {
      size_t comma = image.find(',');
      if (comma != std::string::npos) image = image.substr(comma + 1);
    }
    if (image[0] == '/' || image.rfind("~/", 0) == 0) {
      std::string p = image[0] == '~' ? home_dir() + image.substr(1) : image;
      d->image = gdk_pixbuf_new_from_file(p.c_str(), nullptr);
    } else {
      gsize len = 0;
      guchar* bytes = g_base64_decode(image.c_str(), &len);
      if (bytes) {
        GInputStream* ms = g_memory_input_stream_new_from_data(bytes, len, g_free);
        d->image = gdk_pixbuf_new_from_stream(ms, nullptr, nullptr);
        g_object_unref(ms);
      }
    }
  }

  std::vector<GtkTargetEntry> targets;
  auto addT = [&](const char* name) {
    targets.push_back({(gchar*)name, 0, (guint)targets.size()});
  };
  if (!d->text.empty() || (!d->color.empty() && d->text.empty())) {
    addT("UTF8_STRING"); addT("text/plain;charset=utf-8"); addT("text/plain");
  }
  if (!d->html.empty()) addT("text/html");
  if (d->image) addT("image/png");
  if (!d->uris.empty()) addT("text/uri-list");
  if (targets.empty()) { clip_clear_func(nullptr, d); return; }

  GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  g_clip_data = d;
  gtk_clipboard_set_with_data(cb, targets.data(), targets.size(),
                              clip_get_func, clip_clear_func, d);
  gtk_clipboard_set_can_store(cb, nullptr, 0);
  g_clip_self_count = g_clip_count + 1;  // the owner-change about to fire is ours
}

static void on_clip_owner_change(GtkClipboard*, GdkEvent*, gpointer) {
  g_clip_count++;
  if (g_clip_watching) {
    bool self = g_clip_count == g_clip_self_count;
    pipe_write_line("CLIPCHANGE " + std::to_string(g_clip_count) + (self ? " 1" : " 0"));
  }
}

static std::string clipboard_json(bool count_only) {
  if (count_only) {
    return "{\"changeCount\":" + std::to_string(g_clip_count) + "}";
  }
  GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  std::string kind = "empty";
  std::string text_json = "null", html_json = "null", paths_json = "[]",
              image_json = "null", size_json = "null";

  gchar** uris = gtk_clipboard_wait_for_uris(cb);
  if (uris && uris[0]) {
    kind = "files";
    std::string arr = "[";
    bool any = false;
    for (int i = 0; uris[i]; i++) {
      char* p = g_filename_from_uri(uris[i], nullptr, nullptr);
      if (!p) continue;
      if (any) arr += ",";
      arr += json_escape(p);
      any = true;
      g_free(p);
    }
    arr += "]";
    paths_json = arr;
  }
  if (uris) g_strfreev(uris);

  gchar* text = gtk_clipboard_wait_for_text(cb);
  if (text) {
    text_json = json_escape(text);
    if (kind == "empty") kind = "text";
    g_free(text);
  }

  if (kind == "empty" || kind == "text") {
    GdkPixbuf* pix = gtk_clipboard_wait_for_image(cb);
    if (pix) {
      kind = "image";
      if (g_clip_image_tmp.empty()) {
        g_clip_image_tmp = std::string(g_get_tmp_dir()) + "/tinyjs-clip-" +
                           std::to_string(getpid()) + ".png";
      }
      if (gdk_pixbuf_save(pix, g_clip_image_tmp.c_str(), "png", nullptr, NULL)) {
        image_json = json_escape(g_clip_image_tmp);
        size_json = "{\"width\":" + std::to_string(gdk_pixbuf_get_width(pix)) +
                    ",\"height\":" + std::to_string(gdk_pixbuf_get_height(pix)) + "}";
      }
      g_object_unref(pix);
    }
  }

  GdkAtom html_atom = gdk_atom_intern("text/html", FALSE);
  GtkSelectionData* sel = gtk_clipboard_wait_for_contents(cb, html_atom);
  if (sel) {
    gint len = gtk_selection_data_get_length(sel);
    const guchar* raw = gtk_selection_data_get_data(sel);
    if (len > 0 && raw) {
      std::string html((const char*)raw, (size_t)len);
      // some sources hand over UTF-16LE — crude but effective detection
      if (len > 1 && raw[1] == 0) {
        std::string narrow;
        for (int i = 0; i + 1 < len; i += 2) if (raw[i]) narrow += (char)raw[i];
        html = narrow;
      }
      html_json = json_escape(html);
    }
    gtk_selection_data_free(sel);
  }

  return "{\"kind\":" + json_escape(kind) +
         ",\"changeCount\":" + std::to_string(g_clip_count) +
         ",\"text\":" + text_json + ",\"html\":" + html_json +
         ",\"paths\":" + paths_json + ",\"image\":" + image_json +
         ",\"imageSize\":" + size_json +
         ",\"color\":null,\"concealed\":false,\"sourceApp\":null,\"sourceURL\":null}";
}

// -------------------------------------------------------------- GET / win ---

static std::string win_state_json(const std::string& winid) {
  GtkWindow* win = win_for(winid);
  if (!win) return "null";
  bool main_win = (winid.empty() || winid == "main");
  SecWin* sec = main_win ? nullptr : g_secwins[winid];

  int x = 0, y = 0, w = 0, h = 0;
  gtk_window_get_position(win, &x, &y);
  gtk_window_get_size(win, &w, &h);
  if (main_win) h -= menubar_height();
  GdkWindowState st = g_winstate.count(win) ? g_winstate[win] : (GdkWindowState)0;
  bool fullscreen = st & GDK_WINDOW_STATE_FULLSCREEN;
  bool minimized = st & GDK_WINDOW_STATE_ICONIFIED;
  bool ontop = st & GDK_WINDOW_STATE_ABOVE;
  bool visible = gtk_widget_get_visible(GTK_WIDGET(win));
  bool focused = gtk_window_is_active(win);
  bool resizable = gtk_window_get_resizable(win);

  GdkDisplay* d = gdk_display_get_default();
  GdkWindow* gw = gtk_widget_get_window(GTK_WIDGET(win));
  GdkMonitor* m = gw ? gdk_display_get_monitor_at_window(d, gw)
                     : gdk_display_get_primary_monitor(d);
  GdkRectangle geo = {0, 0, 0, 0};
  int scale = 1;
  if (m) { gdk_monitor_get_geometry(m, &geo); scale = gdk_monitor_get_scale_factor(m); }

  bool frame = main_win ? g_chrome_frame : (sec ? sec->frame : true);
  bool traffic = main_win ? g_chrome_traffic : true;
  bool transparent = main_win ? g_chrome_transparent : (sec ? sec->transparent : false);
  std::string vib = main_win ? g_chrome_vibrancy : (sec ? sec->vibrancy : "");
  bool square = main_win ? g_chrome_square : (sec ? sec->square : false);
  bool first = main_win ? g_chrome_first_mouse : (sec ? sec->first_mouse : false);
  std::string level = main_win ? g_level : (sec ? sec->level : "normal");
  bool clickthrough = main_win ? g_click_through : (sec ? sec->click_through : false);

  auto b = [](bool v) { return v ? "true" : "false"; };
  return std::string("{") +
    "\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) +
    ",\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) +
    ",\"fullscreen\":" + b(fullscreen) + ",\"minimized\":" + b(minimized) +
    ",\"visible\":" + b(visible) + ",\"focused\":" + b(focused) +
    ",\"alwaysOnTop\":" + b(ontop) + ",\"resizable\":" + b(resizable) +
    ",\"clickThrough\":" + b(clickthrough) + ",\"level\":" + json_escape(level) +
    ",\"allSpaces\":" + b(main_win ? g_all_spaces : (st & GDK_WINDOW_STATE_STICKY)) +
    ",\"chrome\":{\"frame\":" + b(frame) + ",\"trafficLights\":" + b(traffic) +
    ",\"transparent\":" + b(transparent) +
    ",\"vibrancy\":" + (vib.empty() ? "null" : json_escape(vib)) +
    ",\"squareCorners\":" + b(square) + ",\"acceptsFirstMouse\":" + b(first) + "}" +
    ",\"screen\":{\"width\":" + std::to_string(geo.width) +
    ",\"height\":" + std::to_string(geo.height) +
    ",\"scale\":" + std::to_string(scale) + "}}";
}

static std::string mouse_json(const std::string& winid) {
  GdkDisplay* d = gdk_display_get_default();
  GdkSeat* seat = gdk_display_get_default_seat(d);
  GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
  int gx = 0, gy = 0;
  if (pointer) gdk_device_get_position(pointer, nullptr, &gx, &gy);

  std::string winpart = "null";
  WebKitWebView* wv = wv_for(winid);
  if (wv) {
    GdkWindow* gw = gtk_widget_get_window(GTK_WIDGET(wv));
    if (gw) {
      int ox = 0, oy = 0;
      gdk_window_get_origin(gw, &ox, &oy);
      int rx = gx - ox, ry = gy - oy;
      int w = gdk_window_get_width(gw), h = gdk_window_get_height(gw);
      bool inside = rx >= 0 && ry >= 0 && rx < w && ry < h;
      winpart = "{\"x\":" + std::to_string(rx) + ",\"y\":" + std::to_string(ry) +
                ",\"inside\":" + (inside ? "true" : "false") + "}";
    }
  }

  GdkMonitor* m = gdk_display_get_monitor_at_point(d, gx, gy);
  GdkRectangle geo = {0, 0, 0, 0};
  int scale = 1;
  if (m) { gdk_monitor_get_geometry(m, &geo); scale = gdk_monitor_get_scale_factor(m); }

  return "{\"x\":" + std::to_string(gx) + ",\"y\":" + std::to_string(gy) +
         ",\"window\":" + winpart +
         ",\"screen\":{\"x\":" + std::to_string(geo.x) +
         ",\"y\":" + std::to_string(geo.y) +
         ",\"width\":" + std::to_string(geo.width) +
         ",\"height\":" + std::to_string(geo.height) +
         ",\"scale\":" + std::to_string(scale) + "}}";
}

static std::string screens_json() {
  GdkDisplay* d = gdk_display_get_default();
  int n = gdk_display_get_n_monitors(d);
  std::string out = "[";
  for (int i = 0; i < n; i++) {
    GdkMonitor* m = gdk_display_get_monitor(d, i);
    GdkRectangle geo, wa;
    gdk_monitor_get_geometry(m, &geo);
    gdk_monitor_get_workarea(m, &wa);
    const char* model = gdk_monitor_get_model(m);
    if (i) out += ",";
    out += "{\"id\":" + std::to_string(i) +
           ",\"name\":" + (model ? json_escape(model) : "null") +
           ",\"x\":" + std::to_string(geo.x) + ",\"y\":" + std::to_string(geo.y) +
           ",\"width\":" + std::to_string(geo.width) +
           ",\"height\":" + std::to_string(geo.height) +
           ",\"visible\":{\"x\":" + std::to_string(wa.x) +
           ",\"y\":" + std::to_string(wa.y) +
           ",\"width\":" + std::to_string(wa.width) +
           ",\"height\":" + std::to_string(wa.height) + "}" +
           ",\"scale\":" + std::to_string(gdk_monitor_get_scale_factor(m)) +
           ",\"primary\":" + (gdk_monitor_is_primary(m) ? "true" : "false") + "}";
  }
  out += "]";
  return out;
}

static std::string battery_json() {
  GDir* dir = g_dir_open("/sys/class/power_supply", 0, nullptr);
  if (!dir) return "null";
  std::string bat;
  const char* name;
  while ((name = g_dir_read_name(dir))) {
    if (g_str_has_prefix(name, "BAT")) { bat = std::string("/sys/class/power_supply/") + name; break; }
  }
  g_dir_close(dir);
  if (bat.empty()) return "null";
  auto readf = [](const std::string& p) -> std::string {
    gchar* data = nullptr;
    if (!g_file_get_contents(p.c_str(), &data, nullptr, nullptr)) return "";
    std::string s = data;
    g_free(data);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
  };
  std::string cap = readf(bat + "/capacity");
  std::string status = readf(bat + "/status");
  bool charging = status == "Charging";
  bool plugged = charging || status == "Full" || status == "Not charging";
  return "{\"percent\":" + (cap.empty() ? "null" : cap) +
         ",\"charging\":" + (charging ? "true" : "false") +
         ",\"plugged\":" + (plugged ? "true" : "false") +
         ",\"minutesRemaining\":null}";
}

static std::string idle_json();  // fwd (D-Bus)

static void answer_get(const std::string& qid, const std::string& what) {
  if (what == "windows") {
    std::string out = "[\"main\"";
    for (auto& kv : g_secwins) out += "," + json_escape(kv.first);
    out += "]";
    send_got(qid, out);
  } else if (what == "win" || what.rfind("win:", 0) == 0) {
    send_got(qid, win_state_json(what == "win" ? "main" : what.substr(4)));
  } else if (what == "mouse" || what.rfind("mouse:", 0) == 0) {
    send_got(qid, mouse_json(what == "mouse" ? "main" : what.substr(6)));
  } else if (what == "screens") {
    send_got(qid, screens_json());
  } else if (what == "clipboard") {
    send_got(qid, clipboard_json(false));
  } else if (what == "clipboard:count") {
    send_got(qid, clipboard_json(true));
  } else if (what == "idle") {
    send_got(qid, idle_json());
  } else if (what == "battery") {
    send_got(qid, battery_json());
  } else if (what == "frontmost") {
    send_got(qid, "null");
  } else if (what == "traypos") {
    send_got(qid, "null");  // SNI does not expose icon geometry
  } else if (what.rfind("item:", 0) == 0) {
    std::string id = what.substr(5);
    auto it = g_items.find(id);
    if (it == g_items.end()) { send_got(qid, "{\"exists\":false}"); return; }
    send_got(qid, "{\"exists\":true,\"label\":" + json_escape(it->second.label) +
                  ",\"checked\":" + (it->second.checked ? "true" : "false") +
                  ",\"enabled\":" + (it->second.enabled ? "true" : "false") + "}");
  } else {
    send_got(qid, "null");
  }
}

// ------------------------------------------------------------------ D-Bus ---

static GDBusConnection* session_bus() {
  static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  return bus;
}

static GDBusConnection* system_bus() {
  static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
  return bus;
}

// seconds since last user input: GNOME Mutter IdleMonitor (ms); fallback 0
static std::string idle_json() {
  double seconds = 0;
  GDBusConnection* bus = session_bus();
  if (bus) {
    GVariant* r = g_dbus_connection_call_sync(bus,
        "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core",
        "org.gnome.Mutter.IdleMonitor", "GetIdletime", nullptr,
        G_VARIANT_TYPE("(t)"), G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr);
    if (r) {
      guint64 ms = 0;
      g_variant_get(r, "(t)", &ms);
      seconds = ms / 1000.0;
      g_variant_unref(r);
    }
  }
  char buf[64];
  snprintf(buf, sizeof buf, "{\"seconds\":%.1f}", seconds);
  return buf;
}

// --- notifications: org.freedesktop.Notifications ---------------------------

static std::map<guint32, std::string> g_notif_ids;  // dbus id -> app notify id
static std::map<guint32, std::vector<std::string>> g_notif_actions;
static bool g_notif_signals_wired = false;

static void wire_notification_signals() {
  if (g_notif_signals_wired) return;
  GDBusConnection* bus = session_bus();
  if (!bus) return;
  g_notif_signals_wired = true;
  g_dbus_connection_signal_subscribe(bus, "org.freedesktop.Notifications",
      "org.freedesktop.Notifications", "ActionInvoked",
      "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
         GVariant* params, gpointer) {
        guint32 nid = 0;
        const gchar* action = nullptr;
        g_variant_get(params, "(u&s)", &nid, &action);
        auto it = g_notif_ids.find(nid);
        if (it == g_notif_ids.end() || !action) return;
        if (!strcmp(action, "default")) {
          pipe_write_line("NOTIFYCLICK " + it->second);
        } else {
          pipe_write_line("NOTIFYACTION " + it->second + "\t" + action + "\t");
        }
      }, nullptr, nullptr);
  g_dbus_connection_signal_subscribe(bus, "org.freedesktop.Notifications",
      "org.freedesktop.Notifications", "NotificationClosed",
      "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
         GVariant* params, gpointer) {
        guint32 nid = 0, reason = 0;
        g_variant_get(params, "(uu)", &nid, &reason);
        g_notif_ids.erase(nid);
        g_notif_actions.erase(nid);
      }, nullptr, nullptr);
}

// crude extraction of "id" and "title" string fields from the actions JSON
// (avoids a JSON parser dependency; the bridge emits compact well-formed JSON)
static std::vector<std::pair<std::string, std::string>> parse_actions(const std::string& json) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t pos = 0;
  auto find_str = [&](const std::string& key, size_t from, size_t to) -> std::string {
    std::string needle = "\"" + key + "\":";
    size_t k = json.find(needle, from);
    if (k == std::string::npos || k > to) return "";
    size_t q1 = json.find('"', k + needle.size());
    if (q1 == std::string::npos) return "";
    std::string val;
    for (size_t i = q1 + 1; i < json.size(); i++) {
      if (json[i] == '\\' && i + 1 < json.size()) { val += json[++i]; continue; }
      if (json[i] == '"') break;
      val += json[i];
    }
    return val;
  };
  while ((pos = json.find('{', pos)) != std::string::npos) {
    size_t end = json.find('}', pos);
    if (end == std::string::npos) break;
    std::string id = find_str("id", pos, end);
    std::string title = find_str("title", pos, end);
    if (!id.empty()) out.push_back({id, title.empty() ? id : title});
    pos = end + 1;
  }
  return out;
}

static void do_notify(const std::string& rest) {
  wire_notification_signals();
  auto f = split_tabs(rest);
  std::string nid = tab_field(f, 0);
  std::string title = tab_field(f, 1);
  std::string body = tab_field(f, 2);
  std::string subtitle = tab_field(f, 3);
  bool sound = tab_field(f, 4) == "1";
  std::string actions_json = wire_unescape(tab_field(f, 5));
  if (!subtitle.empty()) body = subtitle + "\n" + body;

  GVariantBuilder actions;
  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  g_variant_builder_add(&actions, "s", "default");
  g_variant_builder_add(&actions, "s", "Open");
  for (auto& a : parse_actions(actions_json)) {
    g_variant_builder_add(&actions, "s", a.first.c_str());
    g_variant_builder_add(&actions, "s", a.second.c_str());
  }

  GVariantBuilder hints;
  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  if (!g_app_id.empty()) {
    g_variant_builder_add(&hints, "{sv}", "desktop-entry",
                          g_variant_new_string(g_app_id.c_str()));
  }
  if (!sound) {
    g_variant_builder_add(&hints, "{sv}", "suppress-sound",
                          g_variant_new_boolean(TRUE));
  }

  const char* env_icon = getenv("TINYJS_ICON");
  GDBusConnection* bus = session_bus();
  if (!bus) return;
  GVariant* r = g_dbus_connection_call_sync(bus,
      "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "Notify",
      g_variant_new("(susssasa{sv}i)",
                    g_app_name.c_str(), (guint32)0,
                    env_icon && *env_icon ? env_icon : "",
                    title.c_str(), body.c_str(), &actions, &hints, -1),
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
  if (r) {
    guint32 dbus_id = 0;
    g_variant_get(r, "(u)", &dbus_id);
    if (!nid.empty()) g_notif_ids[dbus_id] = nid;
    g_variant_unref(r);
  }
}

// --- secrets: org.freedesktop.secrets (Secret Service, plain session) -------

static std::string g_secret_session;

static bool secret_open_session(GDBusConnection* bus) {
  if (!g_secret_session.empty()) return true;
  GVariant* r = g_dbus_connection_call_sync(bus,
      "org.freedesktop.secrets", "/org/freedesktop/secrets",
      "org.freedesktop.Secret.Service", "OpenSession",
      g_variant_new("(sv)", "plain", g_variant_new_string("")),
      G_VARIANT_TYPE("(vo)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (!r) return false;
  GVariant* out = nullptr;
  const gchar* path = nullptr;
  g_variant_get(r, "(v&o)", &out, &path);
  if (path) g_secret_session = path;
  if (out) g_variant_unref(out);
  g_variant_unref(r);
  return !g_secret_session.empty();
}

static GVariant* secret_attrs(const std::string& service, const std::string& account) {
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{ss}"));
  g_variant_builder_add(&b, "{ss}", "service", service.c_str());
  g_variant_builder_add(&b, "{ss}", "account", account.c_str());
  return g_variant_builder_end(&b);
}

static std::vector<std::string> secret_search(GDBusConnection* bus,
    const std::string& service, const std::string& account) {
  std::vector<std::string> out;
  GVariant* r = g_dbus_connection_call_sync(bus,
      "org.freedesktop.secrets", "/org/freedesktop/secrets",
      "org.freedesktop.Secret.Service", "SearchItems",
      g_variant_new("(@a{ss})", secret_attrs(service, account)),
      G_VARIANT_TYPE("(aoao)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (!r) return out;
  GVariantIter *unlocked = nullptr, *locked = nullptr;
  g_variant_get(r, "(aoao)", &unlocked, &locked);
  const gchar* path;
  while (unlocked && g_variant_iter_next(unlocked, "&o", &path)) out.push_back(path);
  if (unlocked) g_variant_iter_free(unlocked);
  if (locked) g_variant_iter_free(locked);
  g_variant_unref(r);
  return out;
}

static void do_secret(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  std::string op = tab_field(f, 0);
  std::string service = wire_unescape(tab_field(f, 1));
  std::string account = wire_unescape(tab_field(f, 2));
  GDBusConnection* bus = session_bus();
  if (!bus || !secret_open_session(bus)) {
    send_got(qid, "{\"ok\":false,\"error\":\"no secret service\"}");
    return;
  }

  if (op == "get") {
    auto items = secret_search(bus, service, account);
    if (items.empty()) { send_got(qid, "{\"ok\":true,\"value\":null}"); return; }
    GVariantBuilder paths;
    g_variant_builder_init(&paths, G_VARIANT_TYPE("ao"));
    g_variant_builder_add(&paths, "o", items[0].c_str());
    GError* err = nullptr;
    GVariant* r = g_dbus_connection_call_sync(bus,
        "org.freedesktop.secrets", "/org/freedesktop/secrets",
        "org.freedesktop.Secret.Service", "GetSecrets",
        g_variant_new("(aoo)", &paths, g_secret_session.c_str()),
        G_VARIANT_TYPE("(a{o(oayays)})"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    if (!r) {
      send_got(qid, "{\"ok\":false,\"error\":" +
               json_escape(err ? err->message : "keychain error") + "}");
      g_clear_error(&err);
      return;
    }
    std::string value;
    GVariantIter* iter = nullptr;
    g_variant_get(r, "(a{o(oayays)})", &iter);
    const gchar* ipath = nullptr;
    GVariant* secret = nullptr;
    while (iter && g_variant_iter_next(iter, "{&o@(oayays)}", &ipath, &secret)) {
      const gchar* spath = nullptr;
      GVariantIter *params = nullptr, *val = nullptr;
      const gchar* ctype = nullptr;
      g_variant_get(secret, "(&oayay&s)", &spath, &params, &val, &ctype);
      guchar byte;
      while (val && g_variant_iter_next(val, "y", &byte)) value += (char)byte;
      if (params) g_variant_iter_free(params);
      if (val) g_variant_iter_free(val);
      g_variant_unref(secret);
    }
    if (iter) g_variant_iter_free(iter);
    g_variant_unref(r);
    send_got(qid, "{\"ok\":true,\"value\":" + json_escape(value) + "}");
    return;
  }

  if (op == "set") {
    std::string value = wire_unescape(tab_field(f, 3));
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Label",
        g_variant_new_string((service + "/" + account).c_str()));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Attributes",
        secret_attrs(service, account));
    GVariantBuilder val;
    g_variant_builder_init(&val, G_VARIANT_TYPE("ay"));
    for (char c : value) g_variant_builder_add(&val, "y", (guchar)c);
    GVariantBuilder params;
    g_variant_builder_init(&params, G_VARIANT_TYPE("ay"));
    GError* err = nullptr;
    GVariant* r = g_dbus_connection_call_sync(bus,
        "org.freedesktop.secrets", "/org/freedesktop/secrets/aliases/default",
        "org.freedesktop.Secret.Collection", "CreateItem",
        g_variant_new("(a{sv}(oayays)b)", &props, g_secret_session.c_str(),
                      &params, &val, "text/plain", TRUE),
        G_VARIANT_TYPE("(oo)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    if (!r) {
      send_got(qid, "{\"ok\":false,\"error\":" +
               json_escape(err ? err->message : "keychain error") + "}");
      g_clear_error(&err);
      return;
    }
    g_variant_unref(r);
    send_got(qid, "{\"ok\":true}");
    return;
  }

  if (op == "del") {
    auto items = secret_search(bus, service, account);
    for (auto& item : items) {
      GVariant* r = g_dbus_connection_call_sync(bus,
          "org.freedesktop.secrets", item.c_str(),
          "org.freedesktop.Secret.Item", "Delete", nullptr,
          G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
      if (r) g_variant_unref(r);
    }
    send_got(qid, "{\"ok\":true}");
    return;
  }

  send_got(qid, "{\"ok\":false,\"error\":\"bad secret op\"}");
}

// --- power: login1 sleep inhibitor (+ ScreenSaver for display) ---------------

static int g_inhibit_fd = -1;
static guint32 g_screensaver_cookie = 0;

static void do_power(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  bool on = tab_field(f, 0).rfind("on", 0) == 0;
  bool display = tab_field(f, 1) == "1";
  std::string reason = wire_unescape(tab_field(f, 2));
  if (reason.empty()) reason = g_app_name;

  // release anything held (each call replaces the previous assertion)
  if (g_inhibit_fd >= 0) { close(g_inhibit_fd); g_inhibit_fd = -1; }
  if (g_screensaver_cookie) {
    GDBusConnection* bus = session_bus();
    if (bus) {
      GVariant* r = g_dbus_connection_call_sync(bus,
          "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
          "org.freedesktop.ScreenSaver", "UnInhibit",
          g_variant_new("(u)", g_screensaver_cookie), nullptr,
          G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
      if (r) g_variant_unref(r);
    }
    g_screensaver_cookie = 0;
  }
  if (!on) { send_got(qid, "{\"ok\":true,\"active\":false}"); return; }

  bool ok = false;
  GDBusConnection* sys = system_bus();
  if (sys) {
    GUnixFDList* fds = nullptr;
    GVariant* r = g_dbus_connection_call_with_unix_fd_list_sync(sys,
        "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "Inhibit",
        g_variant_new("(ssss)", "sleep:idle", g_app_name.c_str(),
                      reason.c_str(), "block"),
        G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
        &fds, nullptr, nullptr);
    if (r && fds) {
      gint32 idx = 0;
      g_variant_get(r, "(h)", &idx);
      g_inhibit_fd = g_unix_fd_list_get(fds, idx, nullptr);
      ok = g_inhibit_fd >= 0;
    }
    if (r) g_variant_unref(r);
    if (fds) g_object_unref(fds);
  }
  if (display) {
    GDBusConnection* bus = session_bus();
    if (bus) {
      GVariant* r = g_dbus_connection_call_sync(bus,
          "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
          "org.freedesktop.ScreenSaver", "Inhibit",
          g_variant_new("(ss)", g_app_name.c_str(), reason.c_str()),
          G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
      if (r) {
        g_variant_get(r, "(u)", &g_screensaver_cookie);
        g_variant_unref(r);
        ok = true;
      }
    }
  }
  send_got(qid, ok ? "{\"ok\":true,\"active\":true}"
                   : "{\"ok\":false,\"active\":false}");
}

// --- theme + sleep/wake observers --------------------------------------------

static void send_theme() {
  bool dark = false;
  GDBusConnection* bus = session_bus();
  if (bus) {
    GVariant* r = g_dbus_connection_call_sync(bus,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "Read",
        g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    if (r) {
      GVariant* v = nullptr;
      g_variant_get(r, "(v)", &v);
      GVariant* inner = v && g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)
          ? g_variant_get_variant(v) : nullptr;
      GVariant* u = inner ? inner : v;
      if (u && g_variant_is_of_type(u, G_VARIANT_TYPE_UINT32)) {
        dark = g_variant_get_uint32(u) == 1;
      }
      if (inner) g_variant_unref(inner);
      if (v) g_variant_unref(v);
      g_variant_unref(r);
    }
  } else {
    gboolean prefer_dark = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 &prefer_dark, NULL);
    dark = prefer_dark;
  }
  pipe_write_line(dark ? "SYS theme dark" : "SYS theme light");
}

static void install_system_observers() {
  GDBusConnection* bus = session_bus();
  if (bus) {
    g_dbus_connection_signal_subscribe(bus,
        "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Settings",
        "SettingChanged", "/org/freedesktop/portal/desktop", nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
           GVariant* params, gpointer) {
          const gchar *ns = nullptr, *key = nullptr;
          GVariant* val = nullptr;
          g_variant_get(params, "(&s&sv)", &ns, &key, &val);
          if (ns && key && !strcmp(ns, "org.freedesktop.appearance") &&
              !strcmp(key, "color-scheme") &&
              g_variant_is_of_type(val, G_VARIANT_TYPE_UINT32)) {
            bool dark = g_variant_get_uint32(val) == 1;
            pipe_write_line(dark ? "SYS theme dark" : "SYS theme light");
          }
          if (val) g_variant_unref(val);
        }, nullptr, nullptr);
  }
  GDBusConnection* sys = system_bus();
  if (sys) {
    g_dbus_connection_signal_subscribe(sys,
        "org.freedesktop.login1", "org.freedesktop.login1.Manager",
        "PrepareForSleep", "/org/freedesktop/login1", nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
           GVariant* params, gpointer) {
          gboolean going = FALSE;
          g_variant_get(params, "(b)", &going);
          pipe_write_line(going ? "SYS sleep" : "SYS wake");
        }, nullptr, nullptr);
  }
}

// --- Now Playing: MPRIS (org.mpris.MediaPlayer2) -----------------------------

// crude scalar-field extraction from the compact JSON the bridge sends
static std::string json_find_str(const std::string& j, const std::string& key) {
  std::string needle = "\"" + key + "\":";
  size_t k = j.find(needle);
  if (k == std::string::npos) return "";
  size_t q1 = j.find('"', k + needle.size());
  size_t colon_end = k + needle.size();
  // only accept a string value (skip if the value is a number/bool)
  size_t first_non_ws = j.find_first_not_of(" \t", colon_end);
  if (first_non_ws == std::string::npos || j[first_non_ws] != '"') return "";
  std::string val;
  for (size_t i = q1 + 1; i < j.size(); i++) {
    if (j[i] == '\\' && i + 1 < j.size()) { val += j[++i]; continue; }
    if (j[i] == '"') break;
    val += j[i];
  }
  return val;
}

static double json_find_num(const std::string& j, const std::string& key, double dflt) {
  std::string needle = "\"" + key + "\":";
  size_t k = j.find(needle);
  if (k == std::string::npos) return dflt;
  return atof(j.c_str() + k + needle.size());
}

static bool json_find_bool(const std::string& j, const std::string& key, bool dflt) {
  std::string needle = "\"" + key + "\":";
  size_t k = j.find(needle);
  if (k == std::string::npos) return dflt;
  return j.compare(k + needle.size(), 4, "true") == 0;
}

struct NowPlaying {
  std::string title, artist, album;
  double duration = 0, elapsed = 0;
  bool playing = false;
};
static NowPlaying g_np;
static guint g_mpris_owner = 0;
static guint g_mpris_reg_root = 0, g_mpris_reg_player = 0;

static const char* MPRIS_XML =
  "<node>"
  " <interface name='org.mpris.MediaPlayer2'>"
  "  <method name='Raise'/><method name='Quit'/>"
  "  <property name='CanQuit' type='b' access='read'/>"
  "  <property name='CanRaise' type='b' access='read'/>"
  "  <property name='HasTrackList' type='b' access='read'/>"
  "  <property name='Identity' type='s' access='read'/>"
  "  <property name='SupportedUriSchemes' type='as' access='read'/>"
  "  <property name='SupportedMimeTypes' type='as' access='read'/>"
  " </interface>"
  " <interface name='org.mpris.MediaPlayer2.Player'>"
  "  <method name='Next'/><method name='Previous'/><method name='Pause'/>"
  "  <method name='PlayPause'/><method name='Stop'/><method name='Play'/>"
  "  <method name='Seek'><arg name='Offset' type='x' direction='in'/></method>"
  "  <method name='SetPosition'>"
  "   <arg name='TrackId' type='o' direction='in'/>"
  "   <arg name='Position' type='x' direction='in'/></method>"
  "  <method name='OpenUri'><arg name='Uri' type='s' direction='in'/></method>"
  "  <property name='PlaybackStatus' type='s' access='read'/>"
  "  <property name='Rate' type='d' access='readwrite'/>"
  "  <property name='Metadata' type='a{sv}' access='read'/>"
  "  <property name='Volume' type='d' access='readwrite'/>"
  "  <property name='Position' type='x' access='read'/>"
  "  <property name='MinimumRate' type='d' access='read'/>"
  "  <property name='MaximumRate' type='d' access='read'/>"
  "  <property name='CanGoNext' type='b' access='read'/>"
  "  <property name='CanGoPrevious' type='b' access='read'/>"
  "  <property name='CanPlay' type='b' access='read'/>"
  "  <property name='CanPause' type='b' access='read'/>"
  "  <property name='CanSeek' type='b' access='read'/>"
  "  <property name='CanControl' type='b' access='read'/>"
  " </interface>"
  "</node>";

static GVariant* mpris_metadata() {
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&b, "{sv}", "mpris:trackid",
      g_variant_new_object_path("/org/tinyjs/track/0"));
  if (!g_np.title.empty()) {
    g_variant_builder_add(&b, "{sv}", "xesam:title",
        g_variant_new_string(g_np.title.c_str()));
  }
  if (!g_np.artist.empty()) {
    GVariantBuilder artists;
    g_variant_builder_init(&artists, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&artists, "s", g_np.artist.c_str());
    g_variant_builder_add(&b, "{sv}", "xesam:artist", g_variant_builder_end(&artists));
  }
  if (!g_np.album.empty()) {
    g_variant_builder_add(&b, "{sv}", "xesam:album",
        g_variant_new_string(g_np.album.c_str()));
  }
  if (g_np.duration > 0) {
    g_variant_builder_add(&b, "{sv}", "mpris:length",
        g_variant_new_int64((gint64)(g_np.duration * 1e6)));
  }
  return g_variant_builder_end(&b);
}

static void mpris_method_call(GDBusConnection*, const gchar*, const gchar*,
                              const gchar* iface, const gchar* method,
                              GVariant* params, GDBusMethodInvocation* inv, gpointer) {
  if (!strcmp(iface, "org.mpris.MediaPlayer2.Player")) {
    if (!strcmp(method, "Play")) pipe_write_line("MEDIAKEY play");
    else if (!strcmp(method, "Pause")) pipe_write_line("MEDIAKEY pause");
    else if (!strcmp(method, "PlayPause")) pipe_write_line("MEDIAKEY toggle");
    else if (!strcmp(method, "Stop")) pipe_write_line("MEDIAKEY pause");
    else if (!strcmp(method, "Next")) pipe_write_line("MEDIAKEY next");
    else if (!strcmp(method, "Previous")) pipe_write_line("MEDIAKEY previous");
    else if (!strcmp(method, "Seek")) {
      gint64 offset_us = 0;
      g_variant_get(params, "(x)", &offset_us);
      char buf[64];
      snprintf(buf, sizeof buf, "MEDIAKEY seek\t%.3f",
               g_np.elapsed + offset_us / 1e6);
      pipe_write_line(buf);
    } else if (!strcmp(method, "SetPosition")) {
      const gchar* track = nullptr;
      gint64 pos_us = 0;
      g_variant_get(params, "(&ox)", &track, &pos_us);
      char buf[64];
      snprintf(buf, sizeof buf, "MEDIAKEY seek\t%.3f", pos_us / 1e6);
      pipe_write_line(buf);
    }
  } else if (!strcmp(method, "Raise")) {
    gtk_window_present(g_win);
  }
  g_dbus_method_invocation_return_value(inv, nullptr);
}

static GVariant* mpris_get_property(GDBusConnection*, const gchar*, const gchar*,
                                    const gchar* iface, const gchar* prop,
                                    GError**, gpointer) {
  if (!strcmp(iface, "org.mpris.MediaPlayer2")) {
    if (!strcmp(prop, "Identity")) return g_variant_new_string(g_app_name.c_str());
    if (!strcmp(prop, "CanQuit") || !strcmp(prop, "HasTrackList"))
      return g_variant_new_boolean(FALSE);
    if (!strcmp(prop, "CanRaise")) return g_variant_new_boolean(TRUE);
    if (!strcmp(prop, "SupportedUriSchemes") || !strcmp(prop, "SupportedMimeTypes"))
      return g_variant_new_strv(nullptr, 0);
  } else {
    if (!strcmp(prop, "PlaybackStatus"))
      return g_variant_new_string(g_np.playing ? "Playing" : "Paused");
    if (!strcmp(prop, "Metadata")) return mpris_metadata();
    if (!strcmp(prop, "Position")) return g_variant_new_int64((gint64)(g_np.elapsed * 1e6));
    if (!strcmp(prop, "Rate") || !strcmp(prop, "MinimumRate") ||
        !strcmp(prop, "MaximumRate") || !strcmp(prop, "Volume"))
      return g_variant_new_double(1.0);
    if (!strcmp(prop, "CanGoNext") || !strcmp(prop, "CanGoPrevious") ||
        !strcmp(prop, "CanPlay") || !strcmp(prop, "CanPause") ||
        !strcmp(prop, "CanSeek") || !strcmp(prop, "CanControl"))
      return g_variant_new_boolean(TRUE);
  }
  return nullptr;
}

static gboolean mpris_set_property(GDBusConnection*, const gchar*, const gchar*,
                                   const gchar*, const gchar*, GVariant*,
                                   GError**, gpointer) {
  return TRUE;  // Rate/Volume writes accepted and ignored
}

static void mpris_emit_changed() {
  GDBusConnection* bus = session_bus();
  if (!bus || !g_mpris_owner) return;
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&props, "{sv}", "PlaybackStatus",
      g_variant_new_string(g_np.playing ? "Playing" : "Paused"));
  g_variant_builder_add(&props, "{sv}", "Metadata", mpris_metadata());
  g_dbus_connection_emit_signal(bus, nullptr, "/org/mpris/MediaPlayer2",
      "org.freedesktop.DBus.Properties", "PropertiesChanged",
      g_variant_new("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &props, nullptr),
      nullptr);
}

static void do_nowplaying(const std::string& rest) {
  GDBusConnection* bus = session_bus();
  if (!bus) return;
  if (rest == "clear" || rest.empty()) {
    if (g_mpris_owner) {
      g_bus_unown_name(g_mpris_owner);
      g_mpris_owner = 0;
      if (g_mpris_reg_root) g_dbus_connection_unregister_object(bus, g_mpris_reg_root);
      if (g_mpris_reg_player) g_dbus_connection_unregister_object(bus, g_mpris_reg_player);
      g_mpris_reg_root = g_mpris_reg_player = 0;
    }
    g_np = NowPlaying();
    return;
  }
  std::string json = wire_unescape(rest);
  g_np.title = json_find_str(json, "title");
  g_np.artist = json_find_str(json, "artist");
  g_np.album = json_find_str(json, "album");
  g_np.duration = json_find_num(json, "duration", 0);
  g_np.elapsed = json_find_num(json, "elapsed", 0);
  g_np.playing = json_find_bool(json, "playing", true);

  if (!g_mpris_owner) {
    static GDBusNodeInfo* node = nullptr;
    if (!node) node = g_dbus_node_info_new_for_xml(MPRIS_XML, nullptr);
    if (!node) return;
    static const GDBusInterfaceVTable vtable = {
      mpris_method_call, mpris_get_property, mpris_set_property, {nullptr}
    };
    g_mpris_reg_root = g_dbus_connection_register_object(bus,
        "/org/mpris/MediaPlayer2", node->interfaces[0], &vtable,
        nullptr, nullptr, nullptr);
    g_mpris_reg_player = g_dbus_connection_register_object(bus,
        "/org/mpris/MediaPlayer2", node->interfaces[1], &vtable,
        nullptr, nullptr, nullptr);
    std::string safe;
    for (char c : (g_app_id.empty() ? g_app_name : g_app_id)) {
      safe += (g_ascii_isalnum(c) ? c : '_');
    }
    g_mpris_owner = g_bus_own_name_on_connection(bus,
        ("org.mpris.MediaPlayer2." + safe).c_str(),
        G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr, nullptr, nullptr);
  }
  mpris_emit_changed();
}

// --- audioTap: system-output PCM via the PipeWire/Pulse monitor --------------

static GPid g_tap_pid = 0;
static guint g_tap_timer = 0;
static guint g_tap_watch = 0;
static int g_tap_fd = -1;
static std::mutex g_tap_mutex;
static std::string g_tap_buf;        // raw s16le interleaved, filled by reader
static int g_tap_rate = 44100, g_tap_channels = 2, g_tap_interval = 80;

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64(const unsigned char* data, size_t len) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    unsigned v = data[i] << 16;
    if (i + 1 < len) v |= data[i + 1] << 8;
    if (i + 2 < len) v |= data[i + 2];
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += i + 1 < len ? B64[(v >> 6) & 63] : '=';
    out += i + 2 < len ? B64[v & 63] : '=';
  }
  return out;
}

static gboolean tap_tick(gpointer) {
  // one chunk per interval; silence keeps the cadence when no data arrived
  size_t want = (size_t)g_tap_rate * g_tap_interval / 1000 * g_tap_channels * 2;
  std::string chunk;
  {
    std::lock_guard<std::mutex> lock(g_tap_mutex);
    if (g_tap_buf.size() >= want) {
      // keep only the freshest interval's worth (drop backlog)
      if (g_tap_buf.size() > want * 4) {
        g_tap_buf.erase(0, g_tap_buf.size() - want);
      }
      chunk = g_tap_buf.substr(0, want);
      g_tap_buf.erase(0, want);
    }
  }
  if (chunk.empty()) chunk.assign(want, '\0');
  int frames = (int)(chunk.size() / (g_tap_channels * 2));
  char head[128];
  snprintf(head, sizeof head, "\t%d\t%d\t%d\t%lld", g_tap_rate, g_tap_channels,
           frames, (long long)(g_get_monotonic_time() / 1000));
  pipe_write_line("AUDIOTAP " +
                  base64((const unsigned char*)chunk.data(), chunk.size()) + head);
  return G_SOURCE_CONTINUE;
}

static void tap_stop() {
  if (g_tap_timer) { g_source_remove(g_tap_timer); g_tap_timer = 0; }
  if (g_tap_watch) { g_source_remove(g_tap_watch); g_tap_watch = 0; }
  if (g_tap_fd >= 0) { close(g_tap_fd); g_tap_fd = -1; }
  if (g_tap_pid) {
    kill(g_tap_pid, SIGTERM);
    g_spawn_close_pid(g_tap_pid);
    g_tap_pid = 0;
  }
  std::lock_guard<std::mutex> lock(g_tap_mutex);
  g_tap_buf.clear();
}

static gboolean tap_readable(gint fd, GIOCondition cond, gpointer) {
  if (cond & (G_IO_HUP | G_IO_ERR)) { g_tap_watch = 0; return G_SOURCE_REMOVE; }
  char buf[16384];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n <= 0) { g_tap_watch = 0; return G_SOURCE_REMOVE; }
  std::lock_guard<std::mutex> lock(g_tap_mutex);
  g_tap_buf.append(buf, (size_t)n);
  if (g_tap_buf.size() > (size_t)g_tap_rate * g_tap_channels * 2 * 4) {
    g_tap_buf.erase(0, g_tap_buf.size() / 2);  // hard cap ~4s
  }
  return G_SOURCE_CONTINUE;
}

static void do_audiotap(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  int interval = atoi(tab_field(f, 2).c_str());
  if (interval < 20) interval = 20;
  if (interval > 500) interval = 500;
  tap_stop();

  // parec (PulseAudio compat, present with pipewire-pulse) or pw-cat --raw
  std::string rate_s = std::to_string(g_tap_rate);
  std::vector<std::string> cmd;
  char* exe = g_find_program_in_path("parec");
  if (exe) {
    cmd = {exe, "-d", "@DEFAULT_MONITOR@", "--format=s16le",
           "--rate=" + rate_s, "--channels=2", "--raw"};
    g_free(exe);
  } else if ((exe = g_find_program_in_path("pw-cat"))) {
    cmd = {exe, "--record", "--raw", "--format", "s16", "--rate", rate_s,
           "--channels", "2", "-P", "{ stream.capture.sink=true }", "-"};
    g_free(exe);
  } else {
    send_got(qid, "{\"ok\":false,\"code\":\"unsupported\","
                  "\"message\":\"no parec or pw-cat on PATH\"}");
    return;
  }

  std::vector<const gchar*> argv;
  for (auto& a : cmd) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  gint out_fd = -1;
  GError* err = nullptr;
  if (!g_spawn_async_with_pipes(nullptr, (gchar**)argv.data(), nullptr,
        (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
        nullptr, nullptr, &g_tap_pid, nullptr, &out_fd, nullptr, &err)) {
    g_clear_error(&err);
    send_got(qid, "{\"ok\":false,\"code\":\"failed\"}");
    return;
  }
  g_tap_fd = out_fd;
  g_tap_interval = interval;
  g_tap_watch = g_unix_fd_add(out_fd, (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
                              tap_readable, nullptr);
  g_tap_timer = g_timeout_add(interval, tap_tick, nullptr);
  send_got(qid, "{\"ok\":true,\"sampleRate\":" + std::to_string(g_tap_rate) +
                ",\"channels\":" + std::to_string(g_tap_channels) + "}");
}

// --- keystroke / hotkeys (X11) ------------------------------------------------

struct KeyCombo { unsigned mods = 0; unsigned long keysym = 0; bool ok = false; };

#ifdef TINYJS_X11
static KeyCombo parse_combo(const std::string& combo) {
  KeyCombo out;
  std::string key;
  size_t start = 0;
  std::vector<std::string> parts;
  for (;;) {
    size_t i = combo.find('+', start);
    if (i == std::string::npos) { parts.push_back(combo.substr(start)); break; }
    parts.push_back(combo.substr(start, i - start));
    start = i + 1;
  }
  for (auto& raw : parts) {
    std::string p;
    for (char c : raw) p += g_ascii_tolower(c);
    if (p == "cmd" || p == "command" || p == "meta" || p == "ctrl" || p == "control") {
      out.mods |= ControlMask;
    } else if (p == "alt" || p == "opt" || p == "option") {
      out.mods |= Mod1Mask;
    } else if (p == "shift") {
      out.mods |= ShiftMask;
    } else if (p == "win" || p == "super") {
      out.mods |= Mod4Mask;
    } else {
      key = p;
    }
  }
  if (key.empty()) return out;
  static const std::map<std::string, unsigned long> named = {
    {"enter", XK_Return}, {"return", XK_Return}, {"tab", XK_Tab},
    {"space", XK_space}, {"esc", XK_Escape}, {"escape", XK_Escape},
    {"delete", XK_BackSpace}, {"backspace", XK_BackSpace},
    {"forwarddelete", XK_Delete}, {"up", XK_Up}, {"down", XK_Down},
    {"left", XK_Left}, {"right", XK_Right}, {"home", XK_Home}, {"end", XK_End},
    {"pageup", XK_Page_Up}, {"pagedown", XK_Page_Down},
  };
  auto it = named.find(key);
  if (it != named.end()) out.keysym = it->second;
  else out.keysym = XStringToKeysym(key.c_str());
  out.ok = out.keysym != 0 && out.keysym != NoSymbol;
  return out;
}

static Display* xtest_display() {
  static Display* dpy = XOpenDisplay(nullptr);
  return dpy;
}

static bool do_keystroke(const std::string& combo) {
  Display* dpy = xtest_display();
  if (!dpy) return false;
  int ev, err, maj, min;
  if (!XTestQueryExtension(dpy, &ev, &err, &maj, &min)) return false;
  KeyCombo c = parse_combo(combo);
  if (!c.ok) return false;
  KeyCode key = XKeysymToKeycode(dpy, c.keysym);
  if (!key) return false;
  auto mod_key = [&](unsigned mask) -> KeyCode {
    if (mask == ControlMask) return XKeysymToKeycode(dpy, XK_Control_L);
    if (mask == Mod1Mask) return XKeysymToKeycode(dpy, XK_Alt_L);
    if (mask == ShiftMask) return XKeysymToKeycode(dpy, XK_Shift_L);
    if (mask == Mod4Mask) return XKeysymToKeycode(dpy, XK_Super_L);
    return 0;
  };
  unsigned masks[] = {ControlMask, Mod1Mask, ShiftMask, Mod4Mask};
  for (unsigned m : masks) {
    if (c.mods & m) XTestFakeKeyEvent(dpy, mod_key(m), True, 0);
  }
  XTestFakeKeyEvent(dpy, key, True, 0);
  XTestFakeKeyEvent(dpy, key, False, 0);
  for (unsigned m : masks) {
    if (c.mods & m) XTestFakeKeyEvent(dpy, mod_key(m), False, 0);
  }
  XFlush(dpy);
  return true;
}

// global hotkeys via XGrabKey on the root window (X11/XWayland sessions)
struct Hotkey { KeyCode code; unsigned mods; };
static std::map<std::string, Hotkey> g_hotkeys;

static GdkFilterReturn hotkey_filter(GdkXEvent* xev, GdkEvent*, gpointer) {
  XEvent* e = (XEvent*)xev;
  if (e->type != KeyPress) return GDK_FILTER_CONTINUE;
  unsigned mods = e->xkey.state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
  for (auto& kv : g_hotkeys) {
    if (kv.second.code == e->xkey.keycode && kv.second.mods == mods) {
      pipe_write_line("HOTKEY " + kv.first);
      return GDK_FILTER_REMOVE;
    }
  }
  return GDK_FILTER_CONTINUE;
}

static bool g_hotkey_filter_installed = false;

static void grab_key(Display* dpy, Window root, KeyCode code, unsigned mods, bool grab) {
  unsigned ignorable[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
  for (unsigned extra : ignorable) {
    if (grab) {
      XGrabKey(dpy, code, mods | extra, root, False, GrabModeAsync, GrabModeAsync);
    } else {
      XUngrabKey(dpy, code, mods | extra, root);
    }
  }
}

static bool on_x11() {
  GdkDisplay* gd = gdk_display_get_default();
#ifdef GDK_WINDOWING_X11
  return GDK_IS_X11_DISPLAY(gd);
#else
  (void)gd;
  return false;
#endif
}

static void x11_hotkey_register(const std::string& id, const std::string& combo) {
  GdkDisplay* gd = gdk_display_get_default();
#ifdef GDK_WINDOWING_X11
  if (!GDK_IS_X11_DISPLAY(gd)) return;
  Display* dpy = GDK_DISPLAY_XDISPLAY(gd);
  KeyCombo c = parse_combo(combo);
  if (!c.ok) return;
  KeyCode code = XKeysymToKeycode(dpy, c.keysym);
  if (!code) return;
  Window root = DefaultRootWindow(dpy);
  auto it = g_hotkeys.find(id);
  if (it != g_hotkeys.end()) {
    grab_key(dpy, root, it->second.code, it->second.mods, false);
    g_hotkeys.erase(it);
  }
  grab_key(dpy, root, code, c.mods, true);
  XFlush(dpy);
  g_hotkeys[id] = {code, c.mods};
  if (!g_hotkey_filter_installed) {
    g_hotkey_filter_installed = true;
    GdkScreen* screen = gdk_screen_get_default();
    GdkWindow* rootwin = gdk_screen_get_root_window(screen);
    XSelectInput(dpy, root, KeyPressMask);
    gdk_window_add_filter(rootwin, hotkey_filter, nullptr);
  }
#endif
}

static void x11_hotkey_unregister(const std::string& id) {
  GdkDisplay* gd = gdk_display_get_default();
#ifdef GDK_WINDOWING_X11
  if (!GDK_IS_X11_DISPLAY(gd)) return;
  auto it = g_hotkeys.find(id);
  if (it == g_hotkeys.end()) return;
  Display* dpy = GDK_DISPLAY_XDISPLAY(gd);
  grab_key(dpy, DefaultRootWindow(dpy), it->second.code, it->second.mods, false);
  XFlush(dpy);
  g_hotkeys.erase(it);
#endif
}
#else
static bool do_keystroke(const std::string&) { return false; }
static bool on_x11() { return false; }
static void x11_hotkey_register(const std::string&, const std::string&) {}
static void x11_hotkey_unregister(const std::string&) {}
#endif

// --- Wayland global hotkeys: org.freedesktop.portal.GlobalShortcuts ----------
// The portal is dialog-driven (the user approves/rebinds shortcuts once, by
// Wayland's design), and binds the whole set at once — so we accumulate the
// app's shortcuts and (re)bind after the session is ready. Presses arrive as
// Activated signals → HOTKEY <id>.

static std::map<std::string, std::string> g_portal_shortcuts;  // id -> combo
static std::string g_gs_session;      // portal session handle (empty until ready)
static bool g_gs_creating = false;
static bool g_gs_activated_wired = false;

// "cmd+shift+k" -> the portal trigger syntax "CTRL+SHIFT+k"
static std::string portal_trigger(const std::string& combo) {
  std::string out, key;
  size_t start = 0;
  std::vector<std::string> parts;
  for (;;) {
    size_t i = combo.find('+', start);
    if (i == std::string::npos) { parts.push_back(combo.substr(start)); break; }
    parts.push_back(combo.substr(start, i - start));
    start = i + 1;
  }
  for (auto& raw : parts) {
    std::string p;
    for (char c : raw) p += g_ascii_tolower(c);
    const char* mod = nullptr;
    if (p == "cmd" || p == "command" || p == "meta" || p == "ctrl" || p == "control") mod = "CTRL";
    else if (p == "alt" || p == "opt" || p == "option") mod = "ALT";
    else if (p == "shift") mod = "SHIFT";
    else if (p == "win" || p == "super") mod = "LOGO";
    if (mod) { out += out.empty() ? "" : "+"; out += mod; }
    else key = raw;  // keep the key's original case
  }
  if (!key.empty()) { out += out.empty() ? "" : "+"; out += key; }
  return out;
}

static void gs_bind_shortcuts();

static void gs_on_activated(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                            const gchar*, GVariant* params, gpointer) {
  const gchar* session = nullptr;
  const gchar* shortcut_id = nullptr;
  g_variant_get(params, "(&o&s@a{sv})", &session, &shortcut_id, nullptr);
  if (shortcut_id) pipe_write_line(std::string("HOTKEY ") + shortcut_id);
}

static void gs_wire_activated(GDBusConnection* bus) {
  if (g_gs_activated_wired) return;
  g_gs_activated_wired = true;
  g_dbus_connection_signal_subscribe(bus, "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.GlobalShortcuts", "Activated",
      "/org/freedesktop/portal/desktop", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      gs_on_activated, nullptr, nullptr);
}

// subscribe to a portal Request's Response, invoking cb(results) once (then
// unsubscribing itself). cb runs only on a success (code 0) response.
struct PortalWait {
  GDBusConnection* bus;
  guint sub;
  std::function<void(GVariant*)> cb;
};

static void portal_await_response(GDBusConnection* bus, const std::string& request_path,
                                  std::function<void(GVariant*)> cb) {
  auto* w = new PortalWait{bus, 0, std::move(cb)};
  w->sub = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.portal.Desktop",
      "org.freedesktop.portal.Request", "Response", request_path.c_str(), nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
         GVariant* params, gpointer data) {
        auto* w = (PortalWait*)data;
        guint32 code = 0;
        GVariant* results = nullptr;
        g_variant_get(params, "(u@a{sv})", &code, &results);
        if (code == 0) w->cb(results);
        if (results) g_variant_unref(results);
        g_dbus_connection_signal_unsubscribe(w->bus, w->sub);  // one-shot
      }, w, [](gpointer data) { delete (PortalWait*)data; });
}

static std::string portal_sender_token(GDBusConnection* bus, const char* prefix,
                                       std::string& out_token) {
  const char* unique = g_dbus_connection_get_unique_name(bus);
  std::string sender = unique ? unique + 1 : "";
  for (auto& c : sender) if (c == '.') c = '_';
  out_token = std::string(prefix) + std::to_string(g_get_monotonic_time() % 1000000);
  return "/org/freedesktop/portal/desktop/request/" + sender + "/" + out_token;
}

static void gs_bind_shortcuts() {
  GDBusConnection* bus = session_bus();
  if (!bus || g_gs_session.empty()) return;
  GVariantBuilder shortcuts;
  g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
  for (auto& kv : g_portal_shortcuts) {
    GVariantBuilder meta;
    g_variant_builder_init(&meta, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&meta, "{sv}", "description",
        g_variant_new_string((g_app_name + ": " + kv.first).c_str()));
    std::string trig = portal_trigger(kv.second);
    if (!trig.empty()) {
      g_variant_builder_add(&meta, "{sv}", "preferred_trigger",
          g_variant_new_string(trig.c_str()));
    }
    g_variant_builder_add(&shortcuts, "(sa{sv})", kv.first.c_str(), &meta);
  }
  std::string token;
  std::string req = portal_sender_token(bus, "tjbind", token);
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
  g_dbus_connection_call(bus, "org.freedesktop.portal.Desktop",
      "/org/freedesktop/portal/desktop", "org.freedesktop.portal.GlobalShortcuts",
      "BindShortcuts",
      g_variant_new("(oa(sa{sv})sa{sv})", g_gs_session.c_str(), &shortcuts, "", &opts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, nullptr, nullptr);
}

static void gs_create_session() {
  if (g_gs_creating || !g_gs_session.empty()) return;
  GDBusConnection* bus = session_bus();
  if (!bus) return;
  g_gs_creating = true;
  gs_wire_activated(bus);
  std::string session_token = "tjgs" + std::to_string(g_get_monotonic_time() % 1000000);
  std::string handle_token;
  std::string req = portal_sender_token(bus, "tjgscreate", handle_token);
  portal_await_response(bus, req, [](GVariant* results) {
    const gchar* handle = nullptr;
    if (results && g_variant_lookup(results, "session_handle", "&s", &handle) && handle) {
      g_gs_session = handle;
    }
    g_gs_creating = false;
    if (!g_gs_session.empty()) gs_bind_shortcuts();
  });
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(handle_token.c_str()));
  g_variant_builder_add(&opts, "{sv}", "session_handle_token",
      g_variant_new_string(session_token.c_str()));
  g_dbus_connection_call(bus, "org.freedesktop.portal.Desktop",
      "/org/freedesktop/portal/desktop", "org.freedesktop.portal.GlobalShortcuts",
      "CreateSession", g_variant_new("(a{sv})", &opts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, nullptr, nullptr);
}

static void hotkey_register(const std::string& id, const std::string& combo) {
  if (on_x11()) { x11_hotkey_register(id, combo); return; }
  g_portal_shortcuts[id] = combo;
  if (g_gs_session.empty()) gs_create_session();  // binds once ready
  else gs_bind_shortcuts();
}

static void hotkey_unregister(const std::string& id) {
  if (on_x11()) { x11_hotkey_unregister(id); return; }
  g_portal_shortcuts.erase(id);
  if (!g_gs_session.empty()) gs_bind_shortcuts();
}

// --- shell / login / sound / capture / pdf / thumb / say ---------------------

static void do_shell(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  std::string op = tab_field(f, 0);
  std::string target = wire_unescape(tab_field(f, 1));
  auto ok = [&]() { send_got(qid, "{\"ok\":true,\"error\":null}"); };
  auto fail = [&](const std::string& e) {
    send_got(qid, "{\"ok\":false,\"error\":" + json_escape(e) + "}");
  };

  if (op == "open") {
    std::string uri = target;
    if (!uri.empty() && uri[0] == '/') {
      if (!file_exists(uri)) { fail("no such file"); return; }
      char* u = g_filename_to_uri(uri.c_str(), nullptr, nullptr);
      if (!u) { fail("bad path"); return; }
      uri = u;
      g_free(u);
    }
    GError* err = nullptr;
    if (g_app_info_launch_default_for_uri(uri.c_str(), nullptr, &err)) ok();
    else {
      fail(err ? err->message : "no application registered for URL");
      g_clear_error(&err);
    }
    return;
  }

  if (op == "reveal") {
    if (!file_exists(target)) { fail("no such file"); return; }
    char* uri = g_filename_to_uri(target.c_str(), nullptr, nullptr);
    GDBusConnection* bus = session_bus();
    bool done = false;
    if (bus && uri) {
      GVariantBuilder uris;
      g_variant_builder_init(&uris, G_VARIANT_TYPE("as"));
      g_variant_builder_add(&uris, "s", uri);
      GVariant* r = g_dbus_connection_call_sync(bus,
          "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
          "org.freedesktop.FileManager1", "ShowItems",
          g_variant_new("(ass)", &uris, ""), nullptr,
          G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
      if (r) { g_variant_unref(r); done = true; }
    }
    if (uri) g_free(uri);
    if (!done) {
      // fall back to opening the parent directory
      char* dir = g_path_get_dirname(target.c_str());
      char* duri = g_filename_to_uri(dir, nullptr, nullptr);
      done = duri && g_app_info_launch_default_for_uri(duri, nullptr, nullptr);
      g_free(dir);
      if (duri) g_free(duri);
    }
    done ? ok() : fail("could not reveal");
    return;
  }

  if (op == "trash") {
    GFile* file = g_file_new_for_path(target.c_str());
    GError* err = nullptr;
    bool done = g_file_trash(file, nullptr, &err);
    g_object_unref(file);
    if (done) ok();
    else {
      fail(err ? err->message : "could not trash");
      g_clear_error(&err);
    }
    return;
  }

  fail("unknown shell op");
}

// launch-at-login: an autostart .desktop for the app the backend runs as.
// The launcher's parent IS the backend (the built app binary in packaged
// apps; tjs in dev — which we refuse, matching Windows).
static std::string parent_exe() {
  char buf[4096];
  std::string link = "/proc/" + std::to_string(getppid()) + "/exe";
  ssize_t n = readlink(link.c_str(), buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  return buf;
}

static std::string autostart_path() {
  const char* cfg = getenv("XDG_CONFIG_HOME");
  std::string base = cfg && *cfg ? cfg : home_dir() + "/.config";
  return base + "/autostart/" + (g_app_id.empty() ? "tinyjs-app" : g_app_id) + ".desktop";
}

static void do_login(const std::string& qid, const std::string& rest) {
  std::string op = rest.substr(0, rest.find('\t'));
  std::string exe = parent_exe();
  std::string base = exe.substr(exe.rfind('/') + 1);
  if (exe.empty() || base == "tjs") {
    send_got(qid, "{\"status\":\"unsupported\",\"ok\":false,\"error\":null}");
    return;
  }
  std::string path = autostart_path();
  if (op == "get") {
    send_got(qid, std::string("{\"status\":\"") +
             (file_exists(path) ? "enabled" : "disabled") + "\",\"ok\":true,\"error\":null}");
    return;
  }
  if (op.rfind("set ", 0) == 0) {
    bool enable = op.substr(4, 1) == "1";
    if (!enable) {
      unlink(path.c_str());
      send_got(qid, "{\"status\":\"disabled\",\"ok\":true,\"error\":null}");
      return;
    }
    char* dir = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    std::string desktop = "[Desktop Entry]\nType=Application\nName=" + g_app_name +
        "\nExec=\"" + exe + "\"\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
    bool ok = g_file_set_contents(path.c_str(), desktop.c_str(), desktop.size(), nullptr);
    send_got(qid, ok ? "{\"status\":\"enabled\",\"ok\":true,\"error\":null}"
                     : "{\"status\":\"disabled\",\"ok\":false,\"error\":\"write failed\"}");
    return;
  }
  send_got(qid, "{\"status\":\"unsupported\",\"ok\":false,\"error\":null}");
}

static void do_sound(const std::string& qid, const std::string& rest) {
  std::string target = wire_unescape(rest);
  if (target.empty()) {
    gdk_display_beep(gdk_display_get_default());
    send_got(qid, "{\"ok\":true}");
    return;
  }
  std::string path = target;
  if (!file_exists(path)) {
    // a system sound name — try the freedesktop sound theme
    for (const char* ext : {".oga", ".ogg", ".wav"}) {
      std::string cand = "/usr/share/sounds/freedesktop/stereo/" + target + ext;
      if (file_exists(cand)) { path = cand; break; }
    }
  }
  if (!file_exists(path)) { send_got(qid, "{\"ok\":false}"); return; }
  for (const char* player : {"paplay", "pw-play", "aplay"}) {
    char* exe_path = g_find_program_in_path(player);
    if (!exe_path) continue;
    std::string cmd = std::string(exe_path);
    g_free(exe_path);
    const gchar* argv[] = {cmd.c_str(), path.c_str(), nullptr};
    GError* err = nullptr;
    if (g_spawn_async(nullptr, (gchar**)argv, nullptr,
                      (GSpawnFlags)(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                      nullptr, nullptr, nullptr, &err)) {
      send_got(qid, "{\"ok\":true}");
      return;
    }
    g_clear_error(&err);
  }
  send_got(qid, "{\"ok\":false}");
}

static void do_capture(const std::string& qid, const std::string&) {
  // X11/XWayland: grab the root window. Pure Wayland: the root pixmap is
  // black/unavailable — report unsupported (the Screenshot portal needs a
  // user dialog; see TODO-linux.md).
  GdkWindow* root = gdk_screen_get_root_window(gdk_screen_get_default());
  int w = gdk_window_get_width(root), h = gdk_window_get_height(root);
  GdkPixbuf* pix = gdk_pixbuf_get_from_window(root, 0, 0, w, h);
  if (!pix) {
    send_got(qid, "{\"ok\":false,\"error\":\"screen capture needs an X11 session (Wayland: unsupported)\"}");
    return;
  }
  std::string path = std::string(g_get_tmp_dir()) + "/tinyjs-capture-" +
                     std::to_string(getpid()) + "-" +
                     std::to_string(g_get_monotonic_time()) + ".png";
  bool ok = gdk_pixbuf_save(pix, path.c_str(), "png", nullptr, NULL);
  int pw = gdk_pixbuf_get_width(pix), ph = gdk_pixbuf_get_height(pix);
  g_object_unref(pix);
  if (!ok) { send_got(qid, "{\"ok\":false,\"error\":\"could not save capture\"}"); return; }
  send_got(qid, "{\"ok\":true,\"path\":" + json_escape(path) +
                ",\"width\":" + std::to_string(pw) +
                ",\"height\":" + std::to_string(ph) + "}");
}

static void do_pdf(const std::string& qid, const std::string& rest) {
  std::string path = wire_unescape(rest);
  if (path.empty()) { send_got(qid, "{\"ok\":false,\"error\":\"no path\"}"); return; }
  WebKitPrintOperation* op = webkit_print_operation_new(g_wv);
  GtkPrintSettings* settings = gtk_print_settings_new();
  gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_PRINTER, "Print to File");
  gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT, "pdf");
  char* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
  if (uri) {
    gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);
    g_free(uri);
  }
  webkit_print_operation_set_print_settings(op, settings);
  g_object_unref(settings);
  // "finished" fires even after "failed" — judge success by the output file.
  struct Ctx { std::string qid, path; };
  Ctx* ctx = new Ctx{qid, path};
  g_signal_connect_data(op, "finished",
    G_CALLBACK(+[](WebKitPrintOperation*, gpointer data) {
      Ctx* c = (Ctx*)data;
      if (file_exists(c->path)) {
        send_got(c->qid, "{\"ok\":true,\"path\":" + json_escape(c->path) + ",\"error\":null}");
      } else {
        send_got(c->qid, "{\"ok\":false,\"error\":\"pdf failed\"}");
      }
      delete c;
    }), ctx, nullptr, (GConnectFlags)0);
  webkit_print_operation_print(op);
  g_object_unref(op);
}

static void do_thumb(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  std::string path = wire_unescape(tab_field(f, 0));
  int size = atoi(tab_field(f, 1).c_str());
  if (size <= 0) size = 256;
  if (!file_exists(path)) { send_got(qid, "{\"ok\":false,\"error\":\"no such file\"}"); return; }
  GdkPixbuf* pix = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
  if (!pix) { send_got(qid, "{\"ok\":false,\"error\":\"no thumbnail\"}"); return; }
  int w = gdk_pixbuf_get_width(pix), h = gdk_pixbuf_get_height(pix);
  double s = (double)size * 2 / (w > h ? w : h);  // rendered @2x like macOS
  if (s > 1) s = 1;
  int tw = (int)(w * s), th = (int)(h * s);
  GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pix, tw > 0 ? tw : 1,
                                              th > 0 ? th : 1, GDK_INTERP_BILINEAR);
  g_object_unref(pix);
  std::string out = std::string(g_get_tmp_dir()) + "/tinyjs-thumb-" +
                    std::to_string(getpid()) + "-" +
                    std::to_string(g_get_monotonic_time()) + ".png";
  bool ok = scaled && gdk_pixbuf_save(scaled, out.c_str(), "png", nullptr, NULL);
  if (scaled) g_object_unref(scaled);
  if (!ok) { send_got(qid, "{\"ok\":false,\"error\":\"no thumbnail\"}"); return; }
  send_got(qid, "{\"ok\":true,\"path\":" + json_escape(out) +
                ",\"width\":" + std::to_string(tw) +
                ",\"height\":" + std::to_string(th) + "}");
}

// say/voices via speech-dispatcher's spd-say when installed
static GPid g_say_pid = 0;

static void do_say(const std::string& qid, const std::string& rest) {
  auto f = split_tabs(rest);
  std::string text = wire_unescape(tab_field(f, 0));
  std::string voice = wire_unescape(tab_field(f, 1));
  double rate = atof(tab_field(f, 2).c_str());
  char* exe = g_find_program_in_path("spd-say");
  if (!exe) { send_got(qid, "{\"ok\":false}"); return; }
  // spd-say rate: -100..100; tinyjs rate: 0..1 with ~0.5 normal
  int spd_rate = (int)((rate <= 0 ? 0.5 : rate) * 200 - 100);
  std::string rate_s = std::to_string(spd_rate);
  std::vector<const gchar*> argv = {exe, "-w", "-r", rate_s.c_str()};
  if (!voice.empty()) { argv.push_back("-l"); argv.push_back(voice.c_str()); }
  argv.push_back(text.c_str());
  argv.push_back(nullptr);
  GPid pid = 0;
  GError* err = nullptr;
  if (!g_spawn_async(nullptr, (gchar**)argv.data(), nullptr,
                     (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD |
                                   G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                     nullptr, nullptr, &pid, &err)) {
    g_clear_error(&err);
    g_free(exe);
    send_got(qid, "{\"ok\":false}");
    return;
  }
  g_free(exe);
  g_say_pid = pid;
  char* qid_heap = g_strdup(qid.c_str());
  g_child_watch_add(pid, [](GPid pid, gint status, gpointer data) {
    char* q = (char*)data;
    if (g_say_pid == pid) g_say_pid = 0;
    send_got(q, status == 0 ? "{\"ok\":true}" : "{\"ok\":false}");
    g_free(q);
    g_spawn_close_pid(pid);
  }, qid_heap);
}

static void do_saystop() {
  if (g_say_pid) kill(g_say_pid, SIGTERM);
  char* exe = g_find_program_in_path("spd-say");
  if (exe) {
    const gchar* argv[] = {exe, "-S", nullptr};  // stop all speech-dispatcher output
    g_spawn_async(nullptr, (gchar**)argv, nullptr,
                  (GSpawnFlags)(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                  nullptr, nullptr, nullptr, nullptr);
    g_free(exe);
  }
}

// pick a color via the Screenshot portal (works on X11 + Wayland)
static void do_pickcolor(const std::string& qid) {
  GDBusConnection* bus = session_bus();
  if (!bus) { got_unsupported(qid); return; }
  std::string token = "tinyjs" + std::to_string(g_get_monotonic_time() % 1000000);
  const char* unique = g_dbus_connection_get_unique_name(bus);
  std::string sender = unique ? unique + 1 : "";  // strip ':'
  for (auto& c : sender) if (c == '.') c = '_';
  std::string request_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

  char* qid_heap = g_strdup(qid.c_str());
  guint sub = g_dbus_connection_signal_subscribe(bus,
      "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
      "Response", request_path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection* bus2, const gchar*, const gchar*, const gchar*, const gchar*,
         GVariant* params, gpointer data) {
        char* q = (char*)data;
        guint32 code = 0;
        GVariant* results = nullptr;
        g_variant_get(params, "(u@a{sv})", &code, &results);
        if (code != 0) {
          send_got(q, "{\"ok\":true,\"color\":null}");  // user cancelled
        } else {
          GVariant* color = results ? g_variant_lookup_value(results, "color",
                                        G_VARIANT_TYPE("(ddd)")) : nullptr;
          if (color) {
            double r = 0, g = 0, b = 0;
            g_variant_get(color, "(ddd)", &r, &g, &b);
            char hex[16];
            snprintf(hex, sizeof hex, "#%02x%02x%02x",
                     (int)(r * 255 + 0.5), (int)(g * 255 + 0.5), (int)(b * 255 + 0.5));
            send_got(q, std::string("{\"ok\":true,\"color\":\"") + hex + "\"}");
            g_variant_unref(color);
          } else {
            send_got(q, "{\"ok\":true,\"color\":null}");
          }
        }
        if (results) g_variant_unref(results);
        g_free(q);
        // one-shot: unsubscribe ourselves
        guint* subp = (guint*)g_object_get_data(G_OBJECT(bus2), "tinyjs-pickcolor-sub");
        if (subp) {
          g_dbus_connection_signal_unsubscribe(bus2, *subp);
          g_free(subp);
          g_object_set_data(G_OBJECT(bus2), "tinyjs-pickcolor-sub", nullptr);
        }
      }, qid_heap, nullptr);
  guint* subp = g_new(guint, 1);
  *subp = sub;
  g_object_set_data(G_OBJECT(bus), "tinyjs-pickcolor-sub", subp);

  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(token.c_str()));
  GVariant* r = g_dbus_connection_call_sync(bus,
      "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.Screenshot", "PickColor",
      g_variant_new("(sa{sv})", "", &opts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (!r) {
    g_dbus_connection_signal_unsubscribe(bus, sub);
    g_object_set_data(G_OBJECT(bus), "tinyjs-pickcolor-sub", nullptr);
    g_free(subp);
    got_unsupported(qid);
    return;
  }
  g_variant_unref(r);
}

// ------------------------------------------------------- secondary windows --

static void load_target_into(WebKitWebView* wv, const std::string& target) {
  if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
    webkit_web_view_load_uri(wv, target.c_str());
    return;
  }
  char* uri = g_filename_to_uri(target.c_str(), nullptr, nullptr);
  if (uri) {
    webkit_web_view_load_uri(wv, uri);
    g_free(uri);
  }
}

static WebKitWebView* make_webview(const std::string& winid) {
  WebKitUserContentManager* ucm = make_ucm(winid);
  WebKitSettings* settings = make_settings();
  enable_features(settings);
  WebKitWebsitePolicies* policies = webkit_website_policies_new_with_policies(
      "autoplay", WEBKIT_AUTOPLAY_ALLOW, NULL);
  WebKitWebView* wv = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
      "user-content-manager", ucm,
      "settings", settings,
      "website-policies", policies,
      NULL));
  g_object_unref(ucm);
  g_object_unref(settings);
  g_object_unref(policies);
  g_signal_connect(wv, "context-menu", G_CALLBACK(on_context_menu), nullptr);
  g_signal_connect_after(wv, "drag-data-received",
                         G_CALLBACK(on_drag_data_received), nullptr);
  return wv;
}

static gboolean on_secwin_delete(GtkWidget* w, GdkEvent*, gpointer data) {
  (void)w;
  (void)data;
  return FALSE;  // destroy handler does the cleanup
}

static void on_secwin_destroy(GtkWidget*, gpointer data) {
  char* id = (char*)data;
  auto it = g_secwins.find(id);
  if (it != g_secwins.end()) {
    pipe_write_line(std::string("WINCLOSED ") + id);
    delete it->second;
    g_secwins.erase(it);
  }
}

static void do_winopen(const std::string& rest) {
  auto f = split_tabs(rest);
  std::string id = tab_field(f, 0);
  if (id.empty() || id == "main") return;
  auto existing = g_secwins.find(id);
  if (existing != g_secwins.end()) {
    gtk_window_present(existing->second->win);
    return;
  }
  std::string page = tab_field(f, 1);
  std::string title = tab_field(f, 2);
  if (title.empty()) title = id;
  int w = 600, h = 400;
  sscanf(tab_field(f, 3).c_str(), "%dx%d", &w, &h);

  SecWin* sec = new SecWin();
  sec->id = id;
  sec->win = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_title(sec->win, title.c_str());
  gtk_window_set_default_size(sec->win, w, h);
  apply_rgba_visual(GTK_WIDGET(sec->win));
  const char* icon = getenv("TINYJS_ICON");
  if (icon && *icon) gtk_window_set_icon_from_file(sec->win, icon, nullptr);
  sec->wv = make_webview(id);
  gtk_container_add(GTK_CONTAINER(sec->win), GTK_WIDGET(sec->wv));
  g_signal_connect(sec->win, "window-state-event", G_CALLBACK(on_window_state), nullptr);
  g_signal_connect(sec->win, "delete-event", G_CALLBACK(on_secwin_delete), nullptr);
  g_signal_connect_data(sec->win, "destroy", G_CALLBACK(on_secwin_destroy),
    g_strdup(id.c_str()), [](gpointer d, GClosure*) { g_free(d); }, (GConnectFlags)0);
  g_secwins[id] = sec;

  // chrome + position BEFORE showing (no flash / no jump)
  std::vector<std::string> chrome = {tab_field(f, 4), tab_field(f, 5), tab_field(f, 6),
                                     tab_field(f, 7), tab_field(f, 8), tab_field(f, 9)};
  apply_chrome(id, chrome);
  std::string xs = tab_field(f, 10), ys = tab_field(f, 11);
  bool has_pos = !xs.empty() && !ys.empty();
  if (has_pos) gtk_window_move(sec->win, atoi(xs.c_str()), atoi(ys.c_str()));
  else gtk_window_set_position(sec->win, GTK_WIN_POS_CENTER);

  load_target_into(sec->wv, page);
  gtk_widget_show_all(GTK_WIDGET(sec->win));
}

static void do_winclose(const std::string& id) {
  auto it = g_secwins.find(id);
  if (it == g_secwins.end()) return;
  gtk_widget_destroy(GTK_WIDGET(it->second->win));
}

// -------------------------------------------------------------- read loop ---

static void handle_line(const std::string& line);

static void reader_thread() {
  std::string buf;
  char chunk[4096];
  for (;;) {
    ssize_t n = read(g_sock, chunk, sizeof chunk);
    if (n <= 0) break;
    buf.append(chunk, (size_t)n);
    size_t i;
    while ((i = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, i);
      buf.erase(0, i + 1);
      if (!line.empty()) {
        ui_dispatch([line]() { handle_line(line); });
      }
    }
  }
  ui_dispatch([]() {
    g_quitting = true;
    gtk_main_quit();
  });
}

// peel "@<winid>" from an op like "EVAL@side js…"; returns {winid, rest}
static bool peel_target(const std::string& line, const std::string& op,
                        std::string& winid, std::string& rest) {
  if (line.rfind(op + " ", 0) == 0) {
    winid = "main";
    rest = line.substr(op.size() + 1);
    return true;
  }
  if (line.rfind(op + "@", 0) == 0) {
    size_t sp = line.find(' ', op.size() + 1);
    if (sp == std::string::npos) {
      winid = line.substr(op.size() + 1);
      rest = "";
    } else {
      winid = line.substr(op.size() + 1, sp - op.size() - 1);
      rest = line.substr(sp + 1);
    }
    return true;
  }
  if (line == op) {
    winid = "main";
    rest = "";
    return true;
  }
  return false;
}

static void handle_line(const std::string& line) {
  std::string winid, rest;

  // block-builder context (menu / tray / ctx declarations)
  if (g_build_mode != 0) {
    if (line.rfind("ITEM ", 0) == 0) { build_item_line("ITEM", line.substr(5)); return; }
    if (line.rfind("SUB ", 0) == 0) { build_item_line("SUB", line.substr(4)); return; }
    if (line == "SUBEND") { build_item_line("SUBEND", ""); return; }
    if (line == "SEP") { build_item_line("SEP", ""); return; }
    if (g_build_mode == 1) {
      if (line.rfind("MENU ", 0) == 0) {
        g_build_menubar.push_back({line.substr(5), {}});
        g_build_stack.clear();
        g_build_stack.push_back(&g_build_menubar.back().items);
        return;
      }
      if (line == "MENUEND") {
        g_build_mode = 0;
        g_build_stack.clear();
        apply_menus();
        return;
      }
    }
    if (g_build_mode == 2 && line == "TRAYEND") {
      g_build_mode = 0;
      g_build_stack.clear();
      apply_tray();
      return;
    }
    if (g_build_mode == 3 && line == "CTXEND") {
      g_build_mode = 0;
      g_build_stack.clear();
      g_ctx_custom = true;
      return;
    }
    // fall through: a non-block line ends nothing; process it normally
  }

  if (line.rfind("RET ", 0) == 0) {
    size_t sp1 = line.find(' ', 4);
    if (sp1 == std::string::npos) return;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return;
    std::string callid = line.substr(4, sp1 - 4);
    int status = atoi(line.substr(sp1 + 1, sp2 - sp1 - 1).c_str());
    reply_to_call(callid, status, line.substr(sp2 + 1));
    return;
  }

  if (line.rfind("EVAL@* ", 0) == 0) { eval_all(wire_unescape(line.substr(7))); return; }
  if (peel_target(line, "EVAL", winid, rest)) { eval_in(winid, wire_unescape(rest)); return; }

  if (peel_target(line, "TITLE", winid, rest)) {
    GtkWindow* win = win_for(winid);
    if (win) gtk_window_set_title(win, rest.c_str());
    return;
  }

  if (peel_target(line, "SIZE", winid, rest)) {
    int w = 0, h = 0;
    if (sscanf(rest.c_str(), "%d %d", &w, &h) == 2) set_content_size(winid, w, h);
    return;
  }

  if (line.rfind("DLG ", 0) == 0) {
    size_t sp = line.find(' ', 4);
    if (sp == std::string::npos) return;
    do_dialog(line.substr(4, sp - 4), line.substr(sp + 1));
    return;
  }

  if (line == "QUIT") { g_quitting = true; gtk_main_quit(); return; }
  if (line == "RELOAD") { webkit_web_view_reload_bypass_cache(g_wv); return; }
  if (line == "PRINT") {
    WebKitPrintOperation* op = webkit_print_operation_new(g_wv);
    webkit_print_operation_run_dialog(op, g_win);
    g_object_unref(op);
    return;
  }

  if (line == "MENUBEGIN") {
    g_build_mode = 1;
    g_build_menubar.clear();
    g_build_stack.clear();
    return;
  }
  if (line.rfind("MENUUPD ", 0) == 0) { menu_update(line.substr(8)); return; }

  if (line.rfind("TRAYBEGIN", 0) == 0) {
    g_build_mode = 2;
    g_build_tray = TraySpec();
    auto f = split_tabs(line.size() > 10 ? line.substr(10) : "");
    g_build_tray.title = tab_field(f, 0);
    g_build_tray.icon = tab_field(f, 1);
    g_build_tray.template_icon = tab_field(f, 2) != "0";
    g_build_tray.tooltip = tab_field(f, 3);
    g_build_tray.primary = tab_field(f, 4) == "1";
    g_build_stack.clear();
    g_build_stack.push_back(&g_build_tray.items);
    return;
  }
  if (line == "TRAYREMOVE") { remove_tray(); return; }

  if (line == "CTXBEGIN") {
    g_build_mode = 3;
    g_ctx_items.clear();
    clear_registry_kind("ctx");
    g_build_stack.clear();
    g_build_stack.push_back(&g_ctx_items);
    return;
  }
  if (line == "CTXCLEAR") {
    g_ctx_custom = false;
    g_ctx_items.clear();
    return;
  }
  if (line.rfind("CTXSUPPRESS", 0) == 0) {
    g_ctx_suppress = line.size() > 12 && line.substr(12) == "1";
    return;
  }

  if (peel_target(line, "WINOP", winid, rest)) { do_winop(winid, rest); return; }
  if (peel_target(line, "CHROME", winid, rest)) { apply_chrome(winid, split_tabs(rest)); return; }

  if (line == "DRAGWIN") {
    GdkDisplay* d = gdk_display_get_default();
    GdkSeat* seat = gdk_display_get_default_seat(d);
    GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
    int x = 0, y = 0;
    if (pointer) gdk_device_get_position(pointer, nullptr, &x, &y);
    gtk_window_begin_move_drag(g_win, 1, x, y, GDK_CURRENT_TIME);
    return;
  }

  if (line.rfind("WINOPEN ", 0) == 0) { do_winopen(line.substr(8)); return; }
  if (line.rfind("WINCLOSE ", 0) == 0) { do_winclose(line.substr(9)); return; }

  if (line.rfind("CLIPWRITE ", 0) == 0) { do_clipwrite(line.substr(10)); return; }
  if (line.rfind("CLIPWATCH ", 0) == 0) {
    g_clip_watching = atoi(line.substr(10).c_str()) > 0;
    return;
  }

  if (line.rfind("GET ", 0) == 0) {
    size_t sp = line.find(' ', 4);
    if (sp == std::string::npos) return;
    answer_get(line.substr(4, sp - 4), line.substr(sp + 1));
    return;
  }

  if (line.rfind("NOTIFY ", 0) == 0) { do_notify(line.substr(7)); return; }

  auto qid_op = [&](const char* op, std::string& qid, std::string& body) -> bool {
    std::string prefix = std::string(op) + " ";
    if (line.rfind(prefix, 0) != 0) return false;
    size_t sp = line.find(' ', prefix.size());
    if (sp == std::string::npos) {
      qid = line.substr(prefix.size());
      body = "";
    } else {
      qid = line.substr(prefix.size(), sp - prefix.size());
      body = line.substr(sp + 1);
    }
    return true;
  };

  std::string qid, body;
  if (qid_op("KEYSTROKE", qid, body)) {
    bool ok = do_keystroke(body);
    send_got(qid, std::string("{\"ok\":") + (ok ? "true" : "false") +
                  ",\"trusted\":" + (ok ? "true" : "false") + "}");
    return;
  }
  if (qid_op("PERMCHK", qid, body) || qid_op("PERMREQ", qid, body)) {
    static const char* known[] = {"accessibility", "screen", "notifications",
                                  "microphone", "camera"};
    bool is_known = false;
    for (const char* k : known) if (body == k) is_known = true;
    send_got(qid, is_known ? "{\"status\":\"granted\"}"
                           : "{\"status\":\"unsupported\"}");
    return;
  }
  if (qid_op("SHELL", qid, body)) { do_shell(qid, body); return; }
  if (qid_op("LOGIN", qid, body)) { do_login(qid, body); return; }
  if (qid_op("POWER", qid, body)) { do_power(qid, body); return; }
  if (qid_op("SOUND", qid, body)) { do_sound(qid, body); return; }
  if (qid_op("SECRET", qid, body)) { do_secret(qid, body); return; }
  if (qid_op("CAPTURE", qid, body)) { do_capture(qid, body); return; }
  if (qid_op("PDF", qid, body)) { do_pdf(qid, body); return; }
  if (qid_op("THUMB", qid, body)) { do_thumb(qid, body); return; }
  if (qid_op("AUTH", qid, body)) { send_got(qid, "{\"ok\":false}"); return; }
  if (qid_op("SAY", qid, body)) { do_say(qid, body); return; }
  if (line == "SAYSTOP") { do_saystop(); return; }
  if (qid_op("VOICES", qid, body)) { send_got(qid, "{\"ok\":true,\"voices\":[]}"); return; }
  if (qid_op("PICKCOLOR", qid, body)) { do_pickcolor(qid); return; }
  if (qid_op("OCR", qid, body)) { got_unsupported(qid); return; }
  if (qid_op("OSA", qid, body)) { got_unsupported(qid); return; }
  if (qid_op("SPOTLIGHT", qid, body)) { send_got(qid, "{\"ok\":true,\"paths\":[]}"); return; }
  if (qid_op("RECORD", qid, body)) { got_unsupported(qid); return; }
  if (qid_op("WINCTRL", qid, body)) { got_unsupported(qid); return; }
  if (line.rfind("AI available ", 0) == 0) {
    send_got(line.substr(13), "{\"status\":\"unsupported\"}");
    return;
  }
  if (line.rfind("AI generate ", 0) == 0) {
    std::string q = line.substr(12);
    size_t sp = q.find(' ');
    if (sp != std::string::npos) q = q.substr(0, sp);
    size_t tab = q.find('\t');
    if (tab != std::string::npos) q = q.substr(0, tab);
    send_got(q, "{\"ok\":false,\"error\":\"not built in\"}");
    return;
  }
  if (line == "AUDIOTAP STOP") { tap_stop(); return; }
  if (qid_op("AUDIOTAP", qid, body)) { do_audiotap(qid, body); return; }
  if (line.rfind("NOWPLAYING", 0) == 0) {
    do_nowplaying(line.size() > 11 ? line.substr(11) : "");
    return;
  }

  if (line.rfind("HKREG ", 0) == 0) {
    auto f = split_tabs(line.substr(6));
    hotkey_register(tab_field(f, 0), tab_field(f, 1));
    return;
  }
  if (line.rfind("HKUNREG ", 0) == 0) { hotkey_unregister(line.substr(8)); return; }

  if (line.rfind("BOUNCE", 0) == 0) {
    gtk_window_set_urgency_hint(g_win, TRUE);
    return;
  }
  if (line.rfind("DOCKICON ", 0) == 0) {
    std::string path = wire_unescape(line.substr(9));
    if (path.empty()) {
      const char* icon = getenv("TINYJS_ICON");
      if (icon && *icon) gtk_window_set_icon_from_file(g_win, icon, nullptr);
    } else if (file_exists(path)) {
      gtk_window_set_icon_from_file(g_win, path.c_str(), nullptr);
    }
    return;
  }
  // BADGE, SHARE, QUICKLOOK, HAPTIC: silent no-ops (fire-and-forget)
}

// ------------------------------------------------------- main window close --

static gboolean on_main_delete(GtkWidget*, GdkEvent*, gpointer) {
  if (g_hide_on_close) {
    gtk_widget_hide(GTK_WIDGET(g_win));
    return TRUE;
  }
  return FALSE;
}

static void on_main_destroy(GtkWidget*, gpointer) {
  if (!g_quitting) {
    g_quitting = true;
    gtk_main_quit();
  }
}

// ------------------------------------------------------------ --open mode ---

// launcher --open <socket> <app-exe> [url-or-path]
// Forward a deep link / file open to the running app over its instance socket,
// starting the app first when needed. The registered .desktop Exec handler.
static int open_mode(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: launcher --open <socket> <app-exe> [arg]\n");
    return 1;
  }
  std::string sock_path = argv[2];
  std::string app_exe = argv[3];
  std::string arg = argc > 4 ? argv[4] : "";

  std::string json;
  if (arg.empty()) {
    json = "{\"activate\":true}";
  } else if (arg.find("://") != std::string::npos && arg.rfind("file://", 0) != 0 &&
             !file_exists(arg)) {
    json = "{\"url\":" + json_escape(arg) + "}";
  } else {
    std::string path = arg;
    if (arg.rfind("file://", 0) == 0) {
      char* p = g_filename_from_uri(arg.c_str(), nullptr, nullptr);
      if (p) { path = p; g_free(p); }
    }
    json = "{\"paths\":[" + json_escape(path) + "]}";
  }

  auto try_send = [&]() -> bool {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof addr) != 0) {
      close(fd);
      return false;
    }
    std::string line = json + "\n";
    write(fd, line.data(), line.size());
    close(fd);
    return true;
  };

  if (try_send()) return 0;
  // app not running: start it, then retry for a few seconds
  const gchar* spawn_argv[] = {app_exe.c_str(), nullptr};
  g_spawn_async(nullptr, (gchar**)spawn_argv, nullptr,
                (GSpawnFlags)(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                nullptr, nullptr, nullptr, nullptr);
  for (int i = 0; i < 100; i++) {
    g_usleep(100000);  // 100ms
    if (try_send()) return 0;
  }
  return 1;
}

// ------------------------------------------------------------------- main ---

int main(int argc, char** argv) {
  if (argc >= 2 && !strcmp(argv[1], "--open")) return open_mode(argc, argv);

  if (argc < 3) {
    fprintf(stderr,
            "usage: launcher <html-file-or-url> <socket> [title] [WxH] [version]\n");
    return 1;
  }
  g_target = argv[1];
  g_target_is_url = g_target.rfind("http://", 0) == 0 || g_target.rfind("https://", 0) == 0;
  std::string sock_path = argv[2];
  if (argc > 3) g_app_name = argv[3];
  if (argc > 4) sscanf(argv[4], "%dx%d", &g_width, &g_height);
  if (argc > 5) g_app_version = argv[5];
  const char* app_id = getenv("TINYJS_APP_ID");
  if (app_id && *app_id) g_app_id = app_id;
  const char* activation = getenv("TINYJS_ACTIVATION");
  g_accessory = activation && !strcmp(activation, "accessory");

  // WM_CLASS ↔ .desktop matching (StartupWMClass); must precede gtk_init
  g_set_prgname((g_app_id.empty() ? g_app_name : g_app_id).c_str());

  // connect to the backend
  g_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (g_sock < 0) { perror("socket"); return 1; }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path.c_str(), sizeof addr.sun_path - 1);
  if (connect(g_sock, (struct sockaddr*)&addr, sizeof addr) != 0) {
    perror("connect");
    return 1;
  }

  gtk_init(&argc, &argv);

  // tiny-media:// proxy scheme must be registered on the default context
  // before any webview exists
  webkit_web_context_register_uri_scheme(webkit_web_context_get_default(),
      "tiny-media", media_scheme_cb, nullptr, nullptr);

  g_win = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_title(g_win, g_app_name.c_str());
  gtk_window_set_default_size(g_win, g_width, g_height);
  gtk_window_set_position(g_win, GTK_WIN_POS_CENTER);
  apply_rgba_visual(GTK_WIDGET(g_win));
  const char* icon = getenv("TINYJS_ICON");
  if (icon && *icon) gtk_window_set_icon_from_file(g_win, icon, nullptr);

  g_accel = gtk_accel_group_new();
  gtk_window_add_accel_group(g_win, g_accel);

  g_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(g_win), g_vbox);
  g_menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(g_vbox), g_menubar, FALSE, FALSE, 0);

  g_wv = make_webview("main");
  gtk_box_pack_start(GTK_BOX(g_vbox), GTK_WIDGET(g_wv), TRUE, TRUE, 0);

  g_signal_connect(g_win, "window-state-event", G_CALLBACK(on_window_state), nullptr);
  g_signal_connect(g_win, "delete-event", G_CALLBACK(on_main_delete), nullptr);
  g_signal_connect(g_win, "destroy", G_CALLBACK(on_main_destroy), nullptr);

  GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  g_signal_connect(cb, "owner-change", G_CALLBACK(on_clip_owner_change), nullptr);

  load_target_into(g_wv, g_target);

  if (g_accessory) {
    gtk_window_set_skip_taskbar_hint(g_win, TRUE);
    // window stays hidden; only WINOP show reveals it
    gtk_widget_show_all(g_vbox);
    gtk_widget_hide(g_menubar);
  } else {
    gtk_widget_show_all(GTK_WIDGET(g_win));
    gtk_widget_hide(g_menubar);  // shown only when the app declares menus
  }

  install_system_observers();
  send_theme();

  std::thread reader(reader_thread);
  reader.detach();

  gtk_main();

  if (g_inhibit_fd >= 0) close(g_inhibit_fd);
  tap_stop();
  remove_tray();
  _exit(0);
}
