# win.open({ show: false }) — open a window hidden (#102)

Planned 2026-11-01. Status: implemented on `feat/win-open-hidden`; macOS + Windows verified,
Linux written but not yet compiled or run (TODO-verify.md). Windows needed a
fix: the WebView2 controller stayed IsVisible = FALSE after the first show.

## Why

There is no way to open a secondary window without it appearing. Apps
that want to fill a window before the user sees it (fetch data, lay out,
then reveal) fake it with `size: '1x1'` and a later `setSize()`. That
breaks under `"rememberWindows": true`: a window opened without x/y gets
its SAVED rect (bridge.js `win.open` → `savedPlacement`), so the 1x1
becomes the remembered size and the window shows at once, then jumps when
the app's delayed `setPosition`/`setSize` lands (#102). Restore
overriding `size` is correct and stays. The missing piece is a real
"hidden until I say so".

## API

```js
await tiny.win.open('editor', { page: 'editor.html', show: false });
// later — from the bridge, the opener, or the window's own page:
tiny.win.show();                    // in editor.html
app.window('editor').show();        // backend
```

- `show?: boolean`, default `true`. `false`: the window is created,
  placed (explicit x/y, or the remembered placement), its page loads, and
  it stays off screen until `show()`.
- Not shown automatically after a timeout. Main's deferred show has a 3s
  fallback because main has no app code to show it; a secondary window
  was opened by app code that can show it. Mention in the docs that
  forgetting `show()` leaves an invisible window (and `windows()` still
  lists it).
- `win.open` on an id that already exists keeps focusing/showing it as
  today, whatever `show` says. That's the existing "exists: just focus it"
  path, and `show: false` there would mean nothing useful.
- `parent` + `show: false`: the relation is set up when the window is
  first shown (see the macOS gotcha below), not at open.

## Wire

Append one field to WINOPEN: field 16 = `'0'` when `show === false`, else
empty. All three launchers parse fields by index with size checks, so an
older launcher just ignores it (a stale launcher shows the window, which
is today's behaviour).

## Per platform

### bridge.js
- `win.open`: accept `show`, send field 16.
- rememberWindows restore keeps working unchanged: it sets x/y/size on
  WINOPEN and `applyPlacementState` sends maximize/fullscreen after. CHECK
  that maximize/fullscreen applied to a hidden window doesn't show it
  (AppKit `zoom:` / `toggleFullScreen:` on an ordered-out window,
  `ShowWindow(SW_MAXIMIZE)` definitely shows on Windows, GTK maximize
  before map is fine). If they do show it, defer the state until the
  first `show()` (keep a per-window pending-state slot in the bridge).
- `savePlacement` already skips `!st.visible`, so a hidden window never
  overwrites its saved rect. Good, nothing to do.
- `rescueNote(id, 'pos')` (offscreen rescue) on a hidden window: CHECK it
  doesn't show it; if it acts on map, it should wait for the show.

### macOS (launcher-macos.cc `do_winopen`)
- Skip `makeKeyAndOrderFront` / `panel_front` when hidden.
- GOTCHA: `[parent addChildWindow:child ordered:]` orders the child IN.
  When hidden, remember the parent id on the TinyWindow slot and attach
  it in the secondary-window "show" winop (line ~1896) right after
  ordering front. Attach only once (after a later hide/show the relation
  is still there).
- CHECK WKWebView in a never-ordered-in window: page load, JS timers,
  `requestAnimationFrame` (occlusion throttling stops rAF; timers should
  run). Fine for "load data then show". Document rAF if it's paused.

### Windows (launcher-win.cc `do_winopen`, ~line 6305)
- Skip `ShowWindow(hwnd, SW_SHOW)` when hidden. Everything before it
  (frameless re-calc, `render_menu`, `fit_client`) already runs on a
  hidden HWND.
- WebView2: the controller is created after ShowWindow. Make sure
  `create_sec_controller` doesn't depend on the window being visible, and
  that `put_IsVisible` follows the first show (a hidden-at-creation
  controller can stay `IsVisible = FALSE` → blank page after show).
- WINOPEN's parent field: owner windows are set at create time, which
  doesn't show anything, so no deferral needed here.

### Linux (launcher-linux.cc `do_winopen`, ~line 7111)
- Skip `gtk_widget_show_all` + `present_now` when hidden, but still
  `gtk_widget_show` the CHILDREN (the webview) so that the later
  `gtk_widget_show(win)` in the "show" winop maps everything. Simplest:
  `gtk_widget_show_all` then `gtk_widget_hide(win)` before it's ever
  mapped, or show the children only.
- CHECK WebKitGTK loads and runs JS in an unmapped toplevel (it should,
  the view is realized on map but loading isn't tied to it). If it stalls
  until realize, `gtk_widget_realize(win)` without mapping.
- Wayland: no position anyway. X11: set position before the first map as
  today.

## Types + docs
- `TinyOpenWindowOptions.show?: boolean` in template/types/tiny.d.ts
  (and the d.ts copies the release flow syncs).
- docs/docs page for windows: `show: false` + the "load then show" recipe,
  and a note that `size: '1x1'` as a hiding trick fights rememberWindows.
- Regenerate docs/docs/api-map.md with `scripts/api-map/run.sh`.
- CHANGELOG entry crediting #102.

## Verify (TODO-verify.md ticks per OS, after watching it work)
1. `show: false`, no rememberWindows: nothing appears; page's
   `setTimeout(() => tiny.win.show(), 2000)` shows it at its place.
2. Same with rememberWindows and a saved placement: appears ONCE at the
   remembered rect, no jump (the #102 repro).
3. `parent: true` + `show: false` (macOS especially): stays hidden while
   the parent is shown/moved; after `show()` it's a child (moves with
   parent, minimizes with it).
4. Saved maximized/fullscreen + `show: false`: stays hidden, comes up
   maximized/fullscreen on show.
5. Backend `app.window(id).show()` works as well as the page's own.
6. Page in the hidden window runs timers and its RPC calls answer.
7. Old launcher + new bridge: window shows (field ignored), no error.
