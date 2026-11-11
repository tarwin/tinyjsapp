# Cross-platform checks still owed

Things implemented on one machine that nobody has yet watched happen on
another. macOS is the primary dev box, so Windows and Linux entries here are
usually "written, compiled at best, never seen".

Tick a box only after seeing it with your own eyes on that OS — a
fire-and-forget wire op that reaches a shell which ignores it looks exactly
like one that was never implemented.

## #114 — tray `titleColor` / `titleFont` (macOS only, 2026-11-11)

Two new TRAYBEGIN fields (5: colour `#rrggbbaa` or `#light,#dark`, 6:
`monospacedDigit` | `monospaced`), applied as the status button's
attributedTitle; `debug:tray` reports the drawn font and the colour resolved
under Aqua and DarkAqua. Windows and Linux parse TRAYBEGIN by index with size
checks, so they should just ignore the extra fields — nobody has watched it.

- [x] **macOS** (27.2, dev run, 2026-11-11): a backend that set 8 specs in turn
  read back the right colour, light and dark (`#f00` → `#ff0000`, the pair →
  `#b00020` / `#ff8a80`) and the right font (`monospaced` →
  `.AppleSystemUIFontMonospaced-Regular`). Screenshots on a dark menu bar
  show the dark half of the pair. With `monospacedDigit`, "11:11" is 170px,
  as wide as plain "88:88", against 152px plain. A plain `set()` after a
  styled one goes back to the system colour; `'red'` and `'serif'` log a
  warning and fall back. Not yet seen: a LIGHT menu bar.
- [ ] **Windows** — a spec with both fields still shows the icon, tooltip
  and menu as before (no garbled tooltip).
- [ ] **Linux** — same, and the label (where the panel shows one) is unchanged.

## #113 — non-resizable window remembered as maximized (macOS fix, 2026-11-10)

`-[NSWindow isZoomed]` is YES for any window without the Resizable bit, so after
`setResizable(false)` every move reported `maximized: true`, rememberWindows saved
it, and the next launch zoomed the window. `emit_winstate` now ANDs in the
Resizable bit. Self-checking app: `test/remember-nonresizable/` (run `tinyjs dev`
twice; README there). Windows (`IsZoomed`) and Linux (GTK maximized state) read
the real state, so they shouldn't have had the bug, but nobody has watched it there.

- [x] **macOS** (27.2, dev run, 2026-11-10): both runs PASS, `window-state.json`
  keeps `maximized: false`. The same app on a launcher built WITHOUT the fix
  reports `maximized:true` after the move and comes back zoomed on run 2.
  User dragged kid by hand, quit, reran: came back where it was left, not
  zoomed. Not yet seen: the reporter's rw1 sample, a built .app.
- [ ] **Windows** — both runs PASS, no FAIL lines while dragging kid.
- [ ] **Linux** — same (X11; Wayland can't check the position).

## #107 — `tiny.embedURL` (`feat/embed-url`, 2026-11-07)

Self-driving app: `test/embed-url/app` (README there). Headless, any OS:
`bin/tjs run test/embed-url/check.js`. Look for: the right-hand player shows the
video and plays when pressed, the left-hand direct iframe's result (may differ per
engine), `lsof`/`ss`/`netstat` shows the listener on 127.0.0.1 only, fullscreen.

- [x] **macOS** (27.2, dev run, 2026-11-07): direct iframe shows Error 153, the
  embedURL frame shows the player, listener on 127.0.0.1 only, gone when the launcher
  exits. Muted `autoplay=1` played: captions advanced between screenshots 12 s apart.
  Built, signed .app, by hand: play, sound and volume, fullscreen in and out, and the
  "Watch on YouTube" link all behaved as expected. After Cmd+Q no process or listener
  is left. Not seen: Vimeo.
- [x] **Windows** (11 26200, dev run, 2026-11-07): the embedURL frame shows the
  player with `document.referrer` `http://127.0.0.1:<port>/`, listener on 127.0.0.1
  only, gone when the window closes. A muted click sent over CDP played: currentTime
  0.8 → 4.8 → 8.8 s. `document.fullscreenEnabled` and the autoplay policy are true in
  the loopback and player frames. The direct iframe doesn't get 153: it lands on
  `chrome-error://chromewebdata/` (a grey cloud), cause not found. `check.js` passes
  with an absolute backslash path (README).
  Built app, started with no std handles: backend, launcher and a 127.0.0.1 listener
  all ran and exited on close, but DevTools are off there, so the player wasn't seen.
  Not seen: pressing play by hand, sound, entering fullscreen, the player in a built
  app, autoplay under the default policy (that run had it relaxed).
- [x] **Linux** (Ubuntu 24.04 arm64, WebKitGTK 2.52.6, tjs 26.11.0, dev run under
  XWayland, 2026-11-07): direct iframe shows Error 153, the embedURL frame shows the
  player, listener on 127.0.0.1 only, gone when the launcher exits. Muted `autoplay=1`
  played: captions showing, 44% of the panel changed between `xwd` captures 12 s apart,
  and the app had a running PipeWire stream. `check.js` passes. YouTube's `enablejsapi`
  handshake: nothing back from `iframe.contentWindow`, `onReady` from
  `iframe.contentWindow.frames[0]`. Not seen: pressing play by hand, sound, fullscreen,
  a built app, `YT.Player` itself, Vimeo.

## #102 — `win.open({ show: false })` (`feat/win-open-hidden`, 2026-11-01)

WINOPEN gained field 16: `"0"` = open hidden. Plan + gotchas:
TODO-win-open-hidden.md. Self-checking app: `test/win-open-hidden/` (run `tinyjs dev`
twice; PASS/FAIL lines in the console — README there). The bridge holds a remembered maximize/fullscreen
until the window's first `show()` (applying it would show the window).

- [x] **macOS** (27.0.1, dev run, states read back via getState):
  hidden at an explicit x/y while its page ran JS + an RPC; `tiny.win.show()`
  from the page brought it up at the same spot; `parent: true` attached on
  that show (moving main carried it). With rememberWindows: opened hidden at
  the remembered rect and appeared there, no jump (the #102 repro). Saved
  maximized: hidden at the normal rect, maximized on show.
- [x] **Windows** (11 arm64 / Parallels, 200%, dev run, 2026-11-01): the
  predicted blank page was REAL — the controller created on the never-shown
  HWND stayed `IsVisible = FALSE`, so after show() the frame was black with
  a white box and the page still said `visibilityState: 'hidden'` (seen via
  CDP + a screen grab; getState's `visible` read true, so the old checks
  passed). Fixed: secwin_proc's WM_WINDOWPOSCHANGED + SWP_SHOWWINDOW does
  `put_IsVisible(TRUE)`, and controller creation does it if the HWND is
  already up. The test now has the page report its own visibilityState.
  After the fix both runs PASS; kid paints (#f66 sampled), its owner is
  main; run 2 opened hidden at 300,200 and maximized on show. Not seen:
  backend `app.window(id).show()` (same WINOP, untested) and dragging.
  *Seen 2026-11-04 (same VM, tjs 26.11.0):* backend show() shows it and the
  page reports `visible`; real mouse drags by the title bar and a
  `data-tiny-drag` strip land exactly and kid stays painted; dragging main
  leaves kid in place and above main; both test runs still PASS. Noticed: after
  a later `hide()` the page still says `visibilityState: 'visible'` (only the
  never-shown case reports hidden).
- [x] **Linux** — *(2026-11-01, Ubuntu 24.04 arm64, GNOME 46, WebKitGTK
  2.52.6; test/win-open-hidden both runs, X11 via GDK_BACKEND=x11 and native
  Wayland, kid's map state + geometry polled from outside with xwininfo
  every 50 ms.)* X11: all checks PASS — hidden while its page ran JS + an
  RPC (unmapped WebKitGTK loads fine, no realize needed), shown at the
  placed spot, the page sees `visibilityState: 'visible'` after show (the
  check the Windows fix added; re-run after rebasing onto it), run 2 hidden
  AT the remembered 300,200, `parent` sets WM_TRANSIENT_FOR main. **Found broken first:** run 2's remembered maximize
  never happened — the bridge sends `show` then `zoom` back to back, and
  mutter drops a `_NET_WM_STATE` request for a window it hasn't taken on
  yet (GDK already counts it mapped, so it isn't GDK's withdrawn path).
  The launcher now holds a zoom/fullscreen that arrives between a show and
  its map-event until that map-event; after, the poll saw kid go from
  unmapped straight to viewable maximized, never at 400x300. Wayland: hidden,
  JS while hidden, show and the remembered maximize PASS; the two position
  checks FAIL as expected (no client positions on Wayland). Not checked:
  old launcher + new bridge (item 7 in the plan). (Polling tip: GNOME 46
  draws X11 frames from a separate mutter-x11-frames client that copies
  WM_NAME, so `xwininfo -name` can hit the frame, not the client.)*

## Menu item icons — `icon` on `TinyMenuItem` (2026-10-29)

`ITEM` gained a 5th field and `SUB` a 3rd: the icon, a png path (bridge.js
resolves project-relative ones against cwd) or `sf:<name>`. Same builders
for menu bar / right-click / tray on each OS, so one path per OS covers
all three menus. Kitchen sink: the right-click menu (Overview / GPU /
Notification / Print) and Tools ▸ Menus ▸ "Add a Demo menu" (Say hello,
A submenu) carry icons — sf: on macOS, emoji drawn to png elsewhere.

- [ ] **macOS** — compiled, and a dev run with sf:, relative png, wide png,
  missing file, unknown symbol and an icon'd submenu: every call succeeded,
  `menu.update` relabel + `menu.get` read back right. macOS 27 HIDES menu
  item images unless the item sets `preferredImageVisibility = .visible`
  (new in the 27 SDK; `show_menu_item_image` sets it by key). Without it
  nothing showed, even a plain AppKit menu built for a macOS 14 target.
  SEEN on 27.0.1: right-click menu with sf:bell + a png, both drawn.
  Kitchen sink's icons confirmed by hand on macOS 27 (2026-10-29).
  Still to look at: the menu bar and tray menus; on macOS 14/15/26 the
  icons show with no opt-in (selector absent). SF Symbols follow
  the text color (and turn white on highlight), pngs at 16pt keep their
  aspect, a missing file / unknown symbol is a plain label, ⌘J still
  shows beside an icon'd item, checkmark + icon on one item looks sane.
- [ ] **Windows** — never compiled. `menu_item_bitmap` (GDI+ → 32bpp
  PARGB DIB at SM_CYSMICON, cached per path) + `MIIM_BITMAP`. Check:
  it builds; alpha edges are clean (no black fringe = premultiply right);
  icons in menu bar, right-click and tray; a submenu parent's icon
  (set by position); `sf:` is a plain label; a CHECKED item with an icon
  (Win32 draws the bitmap in the check column — tick or framed icon?);
  `menu.update` relabel KEEPS the icon (switched ModifyMenuW →
  SetMenuItemInfoW MIIM_STRING for that); 150%/200% scaling looks sharp.
  *2026-10-29, Windows 11 26200 arm64, 200%, dev:* builds clean. SEEN in
  the tray + right-click menus (screenshots), menu bar by the maintainer
  and by reading back each item's `hbmpItem`: relative, absolute, a 4:1
  png (aspect kept, it spills into the left gutter but not onto the
  label), half-alpha + anti-aliased edges blend clean, no fringe;
  disabled greys its icon; missing file / `sf:` / none are label-only and
  aligned; submenu parent icons show in the menu bar AND the context menu;
  relabel kept the same bitmap handle; clicks still arrive (`TRAY abs`);
  32px icons crisp at 200%. **FAILS: a CHECKED item with an icon shows
  NO tick** — the bitmap takes the check column, no frame either
  (readback: `MFS_CHECKED` + bitmap). Needs a decision (e.g. drop the
  bitmap while checked, or draw the tick into it). 150% not tried.
  FIXED same day: a checked item now gets an icon variant with a tick
  badge (accent disc, white tick, menu-coloured ring) bottom-right,
  cached per path; `menu.update` swaps it both ways. SEEN in tray and
  right-click menus — built checked, checked→unchecked and
  unchecked→checked; bar readback shows the swapped handle + MFS_CHECKED.
  `emoji:` (Direct2D + DirectWrite colour font, DLLs loaded at runtime):
  SEEN in colour in the tray menu — 👋, checked 📁 with the badge, 👍🏽,
  🏳️‍🌈 (ZWJ), ★ in text colour, disabled 🔔 greyed, submenu parent 🧮 and
  its 🖨️ (VS16); bar readback has bitmaps on all of them.
- [ ] **`emoji:` on macOS** — NSImage drawingHandler at 16pt, system font
  13pt → Apple Color Emoji fallback. *(2026-10-29, macOS 27.0.1, 2x, dev,
  right-click menu, screenshot.)* Builds clean. SEEN in colour, centred,
  sharp: 👋 (with ⌘K), checked 📁 (✓ in its own column), 👍🏽, 🏳️‍🌈 (ZWJ),
  submenu parent 🧮, beside a 32px png at the same size. FIXED: ★ drew
  BLACK (no foreground colour set) — near-invisible on the dark menu; now
  labelColor, seen white. A disabled 🔔 is NOT greyed (as on Linux).
  Still to see: menu bar + tray, the submenu child 🖨️, light mode, the
  highlighted row, macOS 14–26.
- [x] **`emoji:` on Linux** — Pango "emoji" family into a 16×scale cairo
  surface (menu bar + right-click). *(2026-10-29, arm64 Ubuntu 24.04, dev,
  XWayland, Noto Color Emoji installed; xwd of both popups.)* Builds clean.
  SEEN in colour in the menu bar AND right-click menu: 👋 (with Ctrl+K
  text), checked 📁 (✓ in the check column), 👍🏽, 🏳️‍🌈 (ZWJ), ★ in text
  colour, submenu parent 🧮. A disabled item's icon is NOT greyed (GTK
  leaves images on insensitive items alone — pngs too), unlike Windows.
  `GDK_SCALE=2` (same day, bar + right-click): 👋 is 28×27 device px vs
  14×14 at 1x, its 2×2 blocks mostly non-uniform — drawn at 2x, not
  upscaled; sharp, about the size of a 32px png beside it.
- [ ] **`emoji:` on Linux, still to see** — 22.04 CI build, fallback
  without a colour emoji font.
- [x] **Linux** — menu bar + right-click: label moved into a box behind a GtkImage
  (`menu_item_set_icon`). *(2026-10-29, arm64 Ubuntu 24.04 / GTK 3.24.41,
  dev, XWayland via `GDK_BACKEND=x11`; popups photographed with xwd, keys
  via the launcher's XTest `keystroke`.)* Builds with no warnings. Menu bar
  at 1x: project-relative png drawn at 16px (32px source, smooth edge), a
  64x16 png keeps its aspect (64px wide), sf:, a missing file and a
  non-png file are plain labels, a disabled item's icon draws, an icon'd
  submenu parent AND its icon'd child both draw. `checked:true` + icon:
  the ✓ sits in the check column left of the icon, looks fine. Ctrl+K
  text shows on the icon'd item and the accelerator fires (`menu.on` got
  `red`) before and after a relabel; `menu.update` relabel keeps icon and
  shortcut text, `menu.get` reads the new label back; unchecking drops
  the ✓ and keeps the icon. At `GDK_SCALE=2` the png loads at 32 device
  px (1:1 with the source, sharp) and the wide one at 128. Tray: its
  dbusmenu `GetLayout` carries
  labels and toggle props, no icon props (and the relabel reached it).
  Not checked: CI's 22.04 build (the calls are GTK 3.10-era, so expected
  fine), x86_64, a native-Wayland run, and a scale change while running
  (the icon's scale is fixed when the menu is built).
- [x] **Linux right-click is now our GtkMenu** (same day, same rig) — a
  WebKitContextMenuItem takes no image, so `on_context_menu` pops a
  `build_gtk_menu(…, "ctx")` at the pointer and returns TRUE. Seen via
  Shift+F10: icons, ✓ on a `checked` item, an icon'd submenu, a disabled
  item; a click reaches `menu.onContext` (not `menu.on`); `menu.update`
  relabel/uncheck shows on the next right-click; `menu.get` answers for a
  context-only item BEFORE its first right-click (spec fallback) and after
  an update. Stock roles: Copy then Paste in a textarea → `hellohello`,
  Undo → `hello`; Paste greys out off an editable, Undo with nothing to
  undo. `setContext(null)` → WebKit's default menu (Back…Inspect Element)
  is back. Known: Copy stays ENABLED with no selection — the menu bar's
  stock Copy does the same (WebKit's can-execute answer); WebKit's own
  stock Copy used to grey out there. Stock labels are ours now
  (untranslated, as the menu bar's already were).
- [x] **Linux right-click, real mouse** (same day; XTest button events from
  a 15-line C tool, since this VM has no xdotool) — right-click at root
  (897,595) opened the menu window at (887,585), i.e. the visible menu at
  the pointer after GTK's 10px shadow margin; at `GDK_SCALE=2` likewise
  (12px margin). A left-click on an item sent its id to `menu.onContext`
  and closed the menu.
- [ ] **Linux right-click, still to see** — a SECONDARY window's right-click
  (stock items act on that window's webview); native Wayland (popup at
  pointer from a key event); kitchen-sink's emoji icons.

## Backend `app.dialog` (2026-10-25)

All seven `tiny.dialog` methods are available from the backend, using the
same `DIALOG_OPS` encoding and the existing `DLG q<id>` / `GOT` replies.
Linux's PDF consent panel now carries an explicit Downloads hint rather
than choosing a folder from the reply id. `test/dialogs/README.md` has the
headless contract checks and the cross-platform native fixture.

- [x] **Linux x86_64, X11/Xvfb, dev and built executable** — real GTK3
  panels driven with XTest. `mainWindow: false`: alert → true; confirm
  accepted/cancelled → true/false; prompt accepted empty string/cancelled
  → ''/null; all four pickers cancel → null; openFile/openFiles/pickFolder
  and saveFile return the selected absolute paths (openFiles an array).
  saveFile does not create the chosen file. No app windows or WebKit
  processes while the backend panel is open. The named page opened after
  the panels is denied `dialog.confirm` while the backend has used it
  under the same manifest. *(2026-10-25, GTK 3.24.49, WebKitGTK 2.50.4)*
- [x] **Linux desktop, X11/Wayland** — focus/activation from a real tray
  action with a compositor, including accessory mode. Xvfb focus was set
  by the test driver and does not verify desktop focus-stealing rules.
  *(2026-10-28, Ubuntu 24.04 arm64, GNOME 46, AppIndicator tray,
  test/dialogs/tray, user at the desktop.)* Wayland: panels came up in
  front and took keys; confirm → `true`, save cancel → `null`, no app
  windows. X11 (`GDK_BACKEND=x11`) FAILED on v0.67.0: Mutter's
  focus-stealing prevention showed "is ready" and placed the panel beside
  the focused half-screen window; one confirm was left stranded. Fixed by
  presenting the panels with a server timestamp (`present_dialog_x11` in
  launcher-linux.cc); re-run with an app in front: panels raised and took
  keys, confirm cancel → `false`, save cancel → `null`, no app windows. An
  accepted desktop save wasn't repeated here — the path/no-file result is
  the Xvfb box above.
- [x] **macOS Apple Silicon, dev and built .app, native fixture** — user
  reported `PASS windowless native dialogs` and `PASS page gate` in both
  runs, including the optional file/folder selections. Dev also confirmed
  saveFile did not create the selected file. macOS version not recorded.
- [x] **macOS Apple Silicon, built .app, real tray** — user reported
  working display/focus and `test/dialogs/tray` logs for confirm cancel
  then accept (false/true), save cancel then accept (null/absolute path),
  and another save cancellation. Each completed action reported no app
  windows. macOS version not recorded.
- [x] **macOS Apple Silicon, dev, real tray** — user confirmed the same
  tray/focus checks also worked in dev. Re-checked by the maintainer after
  the merge (v0.67.0): a tray-only app with no main window, dialogs opened
  from the tray menu, worked as expected. *(2026-10-28, macOS 27.0.1.)*
- [x] **macOS, built .app, save side effect** — user checked the accepted
  save path with `ls`; it reported no such file. The save panel returned
  the selected path without creating the file.
- [x] **Windows, dev and built executable, native fixture** —
  `PASS windowless native dialogs` and `PASS page gate` in both, with
  `TINYJS_DIALOG_FIXTURES` (file, files, folder and save selections
  return the absolute `C:\…` paths). Driven by messaging the launcher's
  own `#32770` panels (WM_COMMAND / WM_SETTEXT on the name box), no
  global keys. Every panel had the foreground on arrival; custom labels
  read `Done` (the lone MB_OK button, which carries IDCANCEL's id) and
  `OK,Stop`; the alert detail is flattened (`First Second`); save
  pre-filled `report.json` and created no file; no panel after the PASS.
  Headless `test/dialogs/check.js` passes too. *(2026-10-28, Windows 11
  26200 arm64 / Parallels, x64 launcher under emulation.)*
- [x] **Windows, real tray** — test/dialogs/tray with a real mouse click
  on the tray icon while another app is foreground: the panel must come
  up in front. A posted tray click has no foreground rights, so it can't
  answer this one. Maintainer ran it by hand: all steps worked as
  expected (dev vs built not recorded). *(2026-10-28, Windows 11 26200
  arm64 / Parallels.)*

## `tiny.capabilities` — can()/get()/list() per tiny.* path (`feat/capabilities-can`, 2026-10-24)

tiny.js reads each function's wire names from its own source at boot and
asks `client.capabilities` (always allowed, like client.hello) for a
verdict per name: the "api" gate for the calling origin first, then the
platform table. Same tiny.js and bridge on every OS, but the origin stamp
and the platform table differ per launcher. Headless:
`bin/tjs run test/capabilities/check.js`. End to end: test/capabilities/README.md.

- [x] **macOS 27.0.1, dev** — wrapped site (wrapper preset) 95 paths,
  gate reasons with `needs` (clip.read, fetch, media.proxy, event.drop,
  event.open-url, secrets.get); own file:// page all 169.
  *(2026-10-24)*
- [x] **Windows** — same run; expect `win.setAllSpaces` and `proxyURL`
  `{ why: 'platform' }` on the own page, and the site's verdicts to match
  macOS (the origin rides the LAST CALL element — a site passing extra
  `__invoke` arguments must not change its answer).
  - Own page 146 paths, `proxyURL` / `win.setAllSpaces` /
    `macos.ai.generate` `why: 'platform'`, the rest ok. Site 89 paths,
    gate + `needs` exactly as on macOS (clip.read, fetch, media.proxy,
    event.drop, event.open-url, macos.ai.generate, secrets.get);
    notify, clipboard.write, menu.on ok. `before` false, `fired` = count.
  - Hostile page over CDP: from the site, raw `__invoke` of
    `client.capabilities` with trailing `'file:///C:/x.html'`,
    `'x', 'file://'`, `null`, `{ origin: 'file://' }` → identical to the
    plain call; `clip.read` with a forged file:// still denied. Control:
    the same probe on the own page reads all ok, so it can see a change.
  - Headless check.js: all ok, but only with `TINY_JS` set and the
    script path absolute — `new URL('.', import.meta.url).pathname` is
    `/C:/…` on Windows (ENOENT), and a relative script path breaks tjs's
    relative imports. Harness issue, not tiny.js.
  *(2026-10-28, Windows 11 26200 arm64 / Parallels, dev.)*
- [x] **Linux, Wayland** — `win.setPosition`, `app.captureScreen`,
  `app.keystroke` read `why: 'session'`; on X11 they're ok. Ubuntu arm64,
  GNOME Wayland, 2026-10-25, test/capabilities/app: the own page reads all
  three `session`; under XWayland (`GDK_BACKEND=x11`) all three ok — a real
  X11 login not tried. The wrapped site reads `gate` for captureScreen and
  keystroke on both (the gate is checked before the platform).
- [x] **Windows, window-mode popup** (the WebView2 popup whose calls never
  return, TODO-site-wrapper.md) — `await tiny.capabilities.ready` resolves
  after ~3s with `can()` false everywhere, instead of hanging.
  Verified 2026-10-25 with a CROSS-origin popup (opener :8769, popup :8770):
  ready at 3008ms, `list()` empty, `get('notify')` → pending, while
  setTitle / system.capabilities / refresh all stayed hung (4s races).
  Control on main's tiny.js: the same popup never got past `await ready`
  (25s). Caveat: a SAME-origin popup reuses the initial about:blank's
  Window, so it keeps that bootstrap's `tiny` and its answer (9 paths,
  gated as origin "null") — ready is instant and can() claims those 9
  although every call still hangs.
- [x] **Linux, first page** — `ready` arrives quickly with the probes warmed
  at startup; two windows opening together spawn ONE portal probe (watch
  `ps` / strace for gdbus). Ubuntu arm64, GNOME Wayland, 2026-10-25: a PATH
  shim logging gdbus saw 2 spawns (one portal, one Unity) for the two
  windows of test/capabilities/app, against 4 with main's bridge.js; `ready`
  in 4–7 ms on both pages.
- [x] **Linux, Wayland: levels + all spaces are stubs** — from the GTK3
  source (gdkwindow-wayland.c: keep_above/below, stick/unstick are empty).
  Ubuntu arm64, GNOME Wayland, 2026-10-25, `WAYLAND_DEBUG=1` with a title
  change as a marker between calls: `setLevel('floating')`,
  `setAlwaysOnTop(true)` and `setAllSpaces(true)` send nothing beyond the
  marker's own repaint. If a compositor honours any of the three, flip its
  key back.
- [x] **Linux, Wayland: `setClickThrough` reaches the compositor** — it did
  NOT before 2026-10-25: GTK rewrites a client-decorated window's input
  shape (the CSD shadow cut-out) on every size-allocate, so the launcher's
  empty region never went out — every `set_input_region` carried the full
  window. Now reapplied after GTK's and committed via a relayout: the log
  shows an empty `set_input_region` + commit right after
  `setClickThrough(true)`, and the full region back after `(false)`.
- [x] **Linux, Wayland: click-through, by hand** — Ubuntu arm64, GNOME
  Wayland, 2026-10-25, a self-timed page over another window: 20 clicks
  landed before, 0 with click-through on (the window blurred once, and the
  window behind took the click — seen by hand), 30 after turning it off;
  `getState().clickThrough` read true / false.

## #89 — macOS: `chrome.panel` non-activating panels (2026-10-23)

TinyPanel (NSPanel + NonactivatingPanel, the bit kept through every
styleMask rewrite); the main window gets it via the vendored webview's
`NSWindow_class_name()` hook, set from TINYJS_PANEL / TinyjsPanel before
`webview_create`. Test app: an accessory app, frameless panel main window,
setAllSpaces + floating, shown from a hotkey / tray click.

- [x] **macOS 27.0.1, built app, over a full-screen app** — shows on the
  full-screen Space (no Space switch) with the keyboard; typing, ⌘V, ⌘X,
  ⌘Z, ⌘A reach the page; blur on clicking the app behind, focus again on
  re-show; Esc → hide leaves the full-screen app with the keyboard.
  *(2026-10-23)*
- [x] **secondary panel via win.open** (+ squareCorners) — focused after
  open and after hide/show while the frontmost app stays the previous one.
  *(2026-10-23, macOS 27.0.1)*
- [x] **⌘C** in a panel page — `copy` fires, and the copied text pastes
  into a second field. *(2026-10-23, macOS 27.0.1)*
- [x] **`tinyjs dev`** — TINYJS_PANEL path: hotkey shows it with focus, ⌘V
  pastes. *(2026-10-23, macOS 27.0.1)*
- [ ] **macOS 14** — oldest supported OS.
- [x] **Windows** — the #89 question itself, no panel involved: an
  accessory/tray app with setLevel('floating'), a full-screen app in front
  (borderless full screen, e.g. a browser in F11), `show()` from the tray
  click or a hotkey → the window appears over it AND takes the keyboard.
  Expected to work already (SetForegroundWindow, foreground rights from
  the user's input); exclusive-full-screen games can't be drawn over.
  Also: `chrome.panel: true` opens an ordinary window (main and win.open).
  *(2026-10-23, Windows 11 arm64, dev: accessory + frameless + `panel` +
  floating + setAllSpaces, summoned by a global hotkey over (a) a
  borderless full-screen WinForms window that held the foreground and (b)
  Edge `--start-fullscreen`. Each time the window came up topmost AND
  foreground, typed keys reached the page and none leaked into the
  full-screen app, and hide handed the foreground back to it. A `win.open`
  with `chrome: { panel: true, squareCorners: true }` opened as an ordinary
  secondary window and took typing (the trailing WINOPEN field is
  ignored). Tray-click path not driven — posted clicks carry no
  foreground rights here, so the hotkey stood in. **Found and fixed on the
  way:** an accessory main's FIRST `show()` reported no `focused: true`
  and its blur no `focused: false` — startup leaves the hidden main its
  thread's active window, so the show sent no WM_ACTIVATE and the dedupe
  then swallowed the blur, which is the event a hide-on-blur palette (the
  `--menubar --panel` wrapper template) waits for. `show`/`hide` now emit
  WINSTATE themselves; three show/blur/hide cycles reported correctly
  after.)*
- [x] **Linux X11** — same check (present_with_time; the WM's
  focus-stealing prevention decides). Same `chrome.panel` ignored check.
  *(2026-10-23, Ubuntu 24.04 arm64, GNOME 46, XWayland via GDK_BACKEND=x11,
  hotkeys + typing injected with XTest; victim = a full-screen tinyjs app
  with a focused input. Found broken first: only the FIRST hotkey show took
  the keyboard — every show after a hide, and a fresh win.open, came up on
  top marked DEMANDS_ATTENTION while the typing kept going to the
  full-screen app. `show`/`win.open`/re-open/restore/MPRIS Raise now map
  the window, then present_with_time(server time) (`present_now`). After:
  4/4 main summons and 4/4 hide→re-open of a win.open window take the
  keyboard, a new win.open does too, the typed text lands in the palette,
  Esc→hide hands it back to the full-screen app, which stays full screen.
  `chrome.panel` (main via TINYJS_PANEL, win.open's trailing field): an
  ordinary _NET_WM_WINDOW_TYPE_NORMAL window. Also found: any window
  leveled WHILE MAPPED (setLevel or setAlwaysOnTop, main or win.open) came
  back ordinary after hide→show — the WM drops _NET_WM_STATE on withdraw;
  only a pre-first-show level survived, as GTK's initial hint. Now
  `set_stacking` re-applies it on every map-event: floating, ontop and
  desktop all survive hide→show, 'normal' clears, and the palette's
  win.open window stays ABOVE through 3 re-opens.)*
- [x] **Linux Wayland** — same check; the compositor may refuse focus
  without an activation token. Record what happens rather than expect
  it to work. *(2026-10-23, Ubuntu 24.04 arm64, GNOME 46 Wayland: no
  GlobalShortcuts portal and no XTest into Wayland clients, so shown from
  a backend timer — the hardest case, no user input at all. Mutter GAVE
  focus every time: show, show after hide, a new win.open (onWindowState
  focused:true in the palette, focused:false in the victim, which stayed
  full screen). Real keyboard, same timer path: typed text landed in the
  palette on both shows and in the win.open window, Esc→hide returned the
  keyboard to the full-screen app each time (victim `aaabbbccc`, palette
  `onetwo`, sec `three`). The hotkey-triggered path wants a portal
  (KDE / GNOME 48+).)*

## #90 — macOS: the 'location' permission, for wifi ssid (2026-10-23)

`permissions.check/request('location')` (CoreLocation) plus
`"permissions": { "location" }` → NSLocationUsageDescription +
NSLocationWhenInUseUsageDescription, and the location entitlement when
signed. macOS only; Windows/Linux already answer `'unsupported'` for an
unknown name.

- [x] **macOS 27, built ad-hoc app** — `check` → `'undetermined'`;
  `request` shows the "would like to use your current location" prompt
  naming the app with the tinyjs.json reason. *(2026-10-23)*
- [x] **macOS, after Allow** — `request` resolves `'granted'`, `wifi()`
  has a non-null `ssid`/`bssid`; the app is listed (on) under Location
  Services. On relaunch the first `check` already says `'granted'`.
  *(2026-10-23, macOS 27.0.1)*
- [x] **macOS, after Don't Allow** — `'denied'`, listed (off) under
  Location Services; on relaunch `check` → `'denied'` and `request` answers
  at once with no prompt. *(2026-10-23, macOS 27.0.1 — the relaunch caught
  a fresh CLLocationManager reading NotDetermined before locationd's first
  callback; queries now wait for it.)*
- [x] **macOS, signed (hardened runtime)** — same as Allow; the entitlement
  is in `codesign -d --entitlements -`. *(2026-10-23, macOS 27.0.1,
  Developer ID)*
- [ ] **macOS 14** — the oldest supported OS prompts and reads ssid.
- [x] **`tinyjs dev`** — `request` resolves `'undetermined'` promptly (no
  hang) and stderr names the tinyjs.json key. *(2026-10-23, macOS 27)*

## Linux: globalHotkeys capability, and file types join the existing type (2026-10-21)

Bridge only. `bin/tjs run test/linux-mime/check.js` covers the globs2
lookup on any OS.

Found while verifying on Linux: the first built app joined
`application/x-art.tarwin.kitchen-sink-md` instead of `text/markdown` — a
leftover package from ANOTHER app built by an older tinyjs ties
text/markdown's weight in `~/.local/share/mime/globs2`, which is read
first. `systemMimeForExt` now ignores every app-invented
`application/x-<reverse.dns.id>-<ext>` type; linux-mime/check.js gained
the case (fails on the old code).

- [ ] **Linux, Wayland with the GlobalShortcuts portal** (KDE, GNOME 48+) —
  `capabilities().globalHotkeys` true; `requirements(['globalHotkeys'])` ok.
- [x] **Linux, Wayland without it** (e.g. an older GNOME) — false, and the
  requirement explains and names `xdg-desktop-portal`. *(2026-10-21, Ubuntu 24.04 arm64, GNOME 46)*
- [x] **Linux** — a built app with `"fileExtensions": ["md", "zzqx"]`:
  the .desktop `MimeType=` lists `text/markdown` and
  `application/x-<id>-zzqx`; `~/.local/share/mime/packages/<id>.xml`
  only defines the zzqx one; `xdg-mime query filetype notes.md` still says
  text/markdown; the app is in a .md file's Open With. An install made by
  an older tinyjs: after one launch its old package no longer defines md.
  *(Linux: all of it, after the fix above — a planted old-style package
  defining md + zzqx was rewritten to zzqx only; `gio mime text/markdown`
  lists the app; a .zzqx file types as the app's own. 2026-10-21, Ubuntu 24.04 arm64, GNOME 46)*
- [x] **Linux** — `setAsDefaultHandler('md')` → 'ok', and
  `xdg-mime query default text/markdown` names the app (mimeapps.list got
  `text/markdown=`, not another app's type). *(2026-10-21, Ubuntu 24.04 arm64, GNOME 46)*

## Hotkey / keystroke key names: one set everywhere (2026-10-21)

`canonical_key` in native/combo.h folds spellings; each launcher's table
gained the names it lacked. Windows and Linux edited without compiling.
Note: on macOS a SYNTHESISED F-key or arrow (app.keystroke) doesn't trigger
a registered hotkey — true of f12/left before this change too — so check
those with real key presses.

- [x] **macOS** — `cmd+shift+home`, `cmd+shift+f13` and `ctrl+alt+-`
  register (`true`; home/f13 were unknown keys before); `ctrl+alt+-`
  fired from a synthesised keystroke. *(2026-10-21)*
- [ ] **macOS** — press ⌘⇧Home and ⌘⇧F13 for real: the hotkeys fire.
- [x] **Windows** — `register('m','ctrl+alt+minus')`, `'ctrl+alt+quote'`,
  `'ctrl+alt+/'` → true and fire on real presses; `app.keystroke('delete')`
  in a text field deletes BACKWARD now (forwarddelete deletes forward).
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  all three → true and each fired once, pressed with `app.keystroke` —
  SendInput, which RegisterHotKey can't tell from a real press, unlike
  macOS. In an input holding `abc|def`: delete → `abdef`, forwarddelete →
  `abef`.)*
- [x] **Linux, X11** — `'ctrl+alt+quote'` → true and fires (was refused).
  *(2026-10-21, XWayland via GDK_BACKEND=x11, keys injected with XTest;
  ctrl+alt+minus too)*

## hotkey.register() answers true/false (2026-10-21)

New wire shape `HKREG <qid> <id>\t<combo>` → `GOT <qid> {"ok":…}` in all
three launchers; the bridge falls back to `true` after 3 s if no answer
comes. Windows and Linux edited without compiling here.

- [x] **macOS** — `test/hotkey-combos/`: `registered` is
  `{"sk": true, "bad": false}`. *(2026-10-21)*
- [x] **Windows** — same test; also register a combo another app holds
  (e.g. one PowerToys uses): `false`, logged "the system refused it".
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  `{"sk": false, "bad": false}`. `sk` is false correctly: Win+K belongs to
  the shell (Cast), logged "the system refused it". So that is the held-combo
  case, and so is a second tinyjs app registering `ctrl+alt+shift+f9` while
  the first holds it: `false`, and `ctrl+alt+shift+f10` → `true` in the
  same app.)*
- [x] **Linux, X11** — same test; a combo another client grabbed (e.g.
  register it in two tinyjs apps): the second gets `false` (the new
  error trap around XGrabKey). *(2026-10-21, XWayland: a small XGrabKey
  client held ctrl+alt+quote → `false`, logged "the system refused it
  (another app may hold it)"; ctrl+alt+minus in the same run `true`)*
- [ ] **Linux, Wayland** — `register()` → `true` and the portal dialog
  still appears; `'hyper+j'` → `false`. *(GNOME 46, no portal: `{"sk":
  true, "bad": false}`, hyper+j logged — the dialog part needs KDE /
  GNOME 48+)*

## Page events through the "api" gate (2026-10-21)

Bridge only, but each launcher's origin stamp (file:// especially) and its
`location.origin` / `location.protocol` must agree for the guard to deliver.
`bin/tjs run test/event-gate/check.js` covers the decisions headless.

- [x] **macOS** — `test/event-gate/` README run: site `["menu","theme"]`,
  own page all six; the original bridge gave the site all six. *(2026-10-21)*
- [x] **Windows** — same run. Also check an app with NO `"api"` block
  still gets every event in every window (the broadcast path).
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  site `["menu","theme"]`, own all six. The same app with the `"api"`
  block removed: site and own both heard all six.)*
- [x] **Linux** — same two runs. *(2026-10-21, Wayland: site
  `["menu","theme"]`, own all six; no "api" block: both pages all six)*

## Windows: userAgent on popups and secondary windows (2026-10-21)

`apply_user_agent` in launcher-win.cc, now called for every webview. Edited
without compiling here.

- [x] **Windows** — compiles; with `"userAgent": "TinyTest/1.0"` and
  `"popups": "window"`, a page that `window.open`s a page printing
  `navigator.userAgent` shows `TinyTest/1.0` in the popup (and in a
  `win.open` secondary), as in the main window.
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated;
  `navigator.userAgent` read in every webview over CDP.)* Main and secondary
  said TinyTest/1.0, but **the popup still had the stock Edge UA: FIXED.**
  WebView2 seems to reset the UA when `put_NewWindow` adopts the webview,
  so it is set again right after. With the fix the popup's
  `navigator.userAgent` and the User-Agent header its server logged are both
  `TinyTest/1.0`.

## macOS: rel=noopener popups reach onWindowOpen (2026-10-21)

- [x] **macOS** — `test/popup-noopener/`: both urls in `asked` and
  `events`; the launcher built from HEAD missed the link. *(2026-10-21)*
- [x] **Windows** — same test, to confirm it already asks for the link (the
  bug was a macOS-only code path). *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  both urls twice in `asked`, both `-> deny` in `events`.)*
- [x] **Linux** — same test. *(2026-10-21: both urls twice in `asked`,
  both `-> deny` in `events`)*

## Linux Wayland: keystroke / paste / permissions answer honestly (2026-10-21)

Bridge only (runtime/bridge.js), keyed on the same ON_X11 test as
`capabilities()`.

- [x] **Linux, Wayland** — `app.keystroke('cmd+v')` → `{ ok: false,
  trusted: false }`; `permissions.check('accessibility')` and
  `check('screen')` → `'unsupported'`; `check('camera')` still `'granted'`.
  *(2026-10-21, Ubuntu 24.04 arm64, GNOME 46)*
- [x] **Linux, X11** — unchanged: keystroke `ok: true`, all five `'granted'`.
  *(2026-10-21, XWayland via GDK_BACKEND=x11, WAYLAND_DISPLAY unset;
  globalHotkeys true)*

## Windows: clipboard temp files, empty paths, interrupted say() (2026-10-21)

Edited without compiling here.

- [x] **Windows** — compiles. Copy an image, `clipboard.read()` three
  times: one `tinyjs-clip-<pid>-<n>.png` in %TEMP% and no `tjc*.tmp`;
  copy another image and read: the old png is gone. Copy text only:
  `paths` is `[]`. `captureScreen()` leaves no empty `tjp*.tmp`.
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated;
  images set from PowerShell): three reads → the same
  `tinyjs-clip-3552-39.png`, the only matching file; a second image →
  `-48.png`, and `-39` is gone; text → `paths: []`; captureScreen → only
  its own `tjp65D.tmp.png`, no empty placeholder. Note: the last clip png
  stays after the app quits (one per process; it's removed only when
  another image is read).)*
- [x] **Windows** — `say('long sentence…')` then `say('hi')`: the first
  resolves `false`, the second `true`; `say(…)` then `stopSpeaking()`:
  `false`. *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated: false / true /
  false.)*

## Update swap: stage beside the app, then rename (2026-10-21)

`swapBundle` (runtime/update.js). `bin/tjs run test/update-swap/check.js`
forces each failure on macOS/Linux. Owed: a real update end to end.

- [ ] **macOS** — a real published update installs and relaunches;
  nothing named `.update-new` / `.update-backup` is left beside the app.
- [x] **Linux** — same, from a tmpfs `/tmp` (the copy path). *(2026-10-21:
  built 0.1.0 → localhost manifest → `install()` → relaunched as 0.2.0
  with `TMPDIR=/dev/shm`; only `updapp/` in its folder, no
  `tinyjs-update-*` left in /dev/shm)*

## Native 'gain' filter defaults to 1, not 0 (2026-10-21)

The row the bridge sends (`audioFilterRow`) — `bin/tjs run
test/audio-filter-rows/check.js` covers it on any OS. What's owed is
hearing it: `filters([{ type: 'gain', gainR: 0.4 }])` should pan left at
full level instead of going silent.

- [ ] **macOS** — packaged app with System Audio Recording granted.
- [x] **Linux** — native chain; measure with the null-sink rig in
  CLAUDE.md (left ≈ full, right ≈ 0.4). *(2026-10-21: chain output routed
  to a null sink, rig checked with a left-only tone first; unfiltered
  L/R 0.2828/0.2828, with the filter 0.2828/0.1131 — R/L 0.400)*

## Page events: a throwing listener doesn't stop the rest (2026-10-21)

`window.__emit` in runtime/tiny.js (compiled into every launcher) now
catches per listener and reports with `reportError`.

- [x] **macOS** — two `tiny.api.on('ping')` listeners, the first throws,
  backend pushes twice: both run both times, two `error` events reported.
  The old launcher ran only the first. *(2026-10-21)*
- [x] **Windows** — `test/page-events/` (WebView2 has `reportError`).
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  `{"got": ["first 1","second 1","first 2","second 2"], "reported": 2}`.)*
- [x] **Linux** — `test/page-events/`; on a WebKitGTK without `reportError` the
  fallback rethrows on a timer, so the console still shows it. *(2026-10-21:
  launcher compiles; four `got`, `reported: 2`)*

## Hotkey / keystroke combos: one parser, never a bare key (2026-10-21)

`native/combo.h` replaces each launcher's own combo parsing (hotkeys and
keystrokes). `c++ -std=c++17 native/combo_test.cc -o /tmp/ct && /tmp/ct`
covers the words on any OS; `test/hotkey-combos/` drives the launcher wiring.
Windows and Linux were edited without being compiled here.

- [x] **macOS** — rebuilt; a self-driving app registered `super+k` and
  `hyper+j`, sent ⌘K with `app.keystroke`: `super+k` fired (the old
  launcher, built from HEAD, fired nothing — it had registered a bare K),
  and `hyper+j` logged `not registered: needs exactly one key plus
  modifiers`. *(2026-10-21)*
- [x] **Windows** — compiles (CI or setup.ps1); `app.hotkey.register('k',
  'k+ctrl+shift')` fires on Ctrl+Shift+K (modifiers after the key used to
  be refused); `'hyper+k'` and `'a+b'` log "not registered" and K / B stay
  free; a combo another app holds logs "the system refused it".
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated;
  `combo_test.cc` passes with MinGW g++): `k+ctrl+shift` → true and fired
  on a synthesised Ctrl+Shift+K. `hyper+k` / `a+b` → false, logged "needs
  exactly one known key plus modifiers", and typing k then b into the
  focused input inserted both. Win+K and a combo held by a second tinyjs
  app → false, logged "the system refused it".)*
- [x] **Linux, X11** — compiles; `'super+k'` fires on Super+K; `'hyper+k'`
  logs "not registered" and a bare K typed in another app is NOT swallowed
  (it used to be grabbed). *(2026-10-21, XWayland + XTest: super+k and
  k+ctrl+shift fire; after hyper+k was refused another client could
  XGrabKey a bare K, so nothing holds it)*
- [ ] **Linux, Wayland** — `'hyper+k'` is never offered to the portal (logs
  "not registered"); a valid combo still shows the portal dialog.
  *(GNOME 46: the refusal logs; the dialog needs a portal — KDE / GNOME 48+)*

## shell.open from a wrapped site: web and mail links only (2026-10-21)

`shellOpenAllowed` (runtime/bridge.js) refuses any other target when the
call's origin isn't the app's own. The check is in the bridge, so it's the
same code everywhere; what differs per OS is the origin the launcher stamps.
`bin/tjs run test/shell-open-gate/check.js` covers the decision; the
hostile-page run in that folder's README covers the stamp.

- [x] **macOS** — hostile-page run: all three targets refused with the
  gate's message; with the check bypassed they reached the OS ("no such
  file", "no application registered for URL"). *(2026-10-21)*
- [x] **Windows** — same run; the wrapped origin must be refused (it's the
  ShellExecuteW case that matters most). *(2026-10-21, Windows 11 26200 arm64 / Parallels, x64 WinLibs launcher built from 5541ea5, emulated:
  all three targets read the gate's message for `http://127.0.0.1:8766`,
  so the origin the launcher stamps is the one the gate sees. The check
  scripts for shell-open-gate, event-gate, api-gate, audio-filter-rows and
  linux-mime pass here too, run from their own folders.)*
- [x] **Linux** — same run. *(2026-10-21: all three targets read the
  gate's message, origin `http://127.0.0.1:8766`)*

## #82 — Windows on ARM: native arm64 builds (`feat/82-windows-arm64`, 2026-10-20)

Release CI builds `tjs.exe` from source on `windows-11-arm` (MSVC + vcpkg,
as txiki's own x64 release) and the launcher with llvm-mingw; its verify
step asserts both are ARM64 (PE machine 0xAA64) and its smoke tests run
them natively. A `release-test/**` branch runs the whole release without
publishing. `python3 test/update-win-arch/check.py` (macOS/Linux) covers
which download an app picks; the env-var ARM detection below is the part
nothing has seen on a real machine.

- [x] **CI** — a `release-test/**` dry run: arm64 leg green, verify step
  prints `machine 0xAA64` for both binaries, GUI smoke passes.
  *(2026-10-21, run 39558361061 on windows-11-arm (Cobalt 100): all 31 of
  txiki's test-wasm*.js pass against the arm64 tjs.exe; tjs.exe and
  launcher-win.exe both 0xAA64; tjs.exe imports only system DLLs (no
  VCRUNTIME/MSVCP); GUI smoke page passed in a real window; both zips in
  checksums.txt; "Create GitHub release" skipped. It took two WAMR patches
  (native/patches/): without them every WASI call crashed with 0xC0000005,
  which txiki's own x64 build, emulated on the same runner, did not.)*
- [x] **Windows on ARM, install** — `irm …/install.ps1 | iex` (or
  `TINYJS_INSTALL_ZIP` with the CI zip) installs arm64: Task Manager's
  Details tab shows `tjs.exe` as ARM64, not x64. From the usual (ARM64)
  PowerShell, and from an x64 shell too if one is to hand: the installer
  reads the machine's arch from the registry so that one should pick arm64
  as well.
  *(2026-10-21, Windows 11 26200 arm64 / Parallels, run 39558361061's
  zips. `docs/install.ps1` with `TINYJS_INSTALL_ZIP` + a scratch
  `TINYJS_HOME`: installed, `tinyjs version` ran; `bin\tjs.exe` and
  `native\launcher-win.exe` PE 0xAA64, and a running `tjs.exe` reports
  process machine 0xAA64 (GetProcessInformation(ProcessMachineTypeInfo),
  what Task Manager's Architecture column reads). The local-zip path skips
  arch selection, so its three lines were run on their own: ARM64 PS 5.1,
  x86 PS 5.1 (SysWOW64) and an x64 pwsh 7.4.6 (emulated) all pick
  `tinyjs-windows-arm64.zip`. Inbox Windows has no x64 PowerShell, only
  x86, so pwsh came from the portable zip. Under x64 emulation
  `PROCESSOR_ARCHITEW6432` is UNSET (it is set only for x86): the registry
  read is what catches it there.)*
- [x] **Windows on ARM, app** — `tinyjs new` + `build` + run: the app's
  exe and launcher.exe show as ARM64; `tiny.system.architecture()` says
  arm64; window, tray, dialogs, audio as usual (`test/appsurface.html`).
  *(2026-10-21: built exe + launcher.exe PE 0xAA64 and running as 0xAA64.
  **`architecture()` and `info().arch` said `x86_64`: FIXED.** bridge.js
  read `navigator.platform`, which is "Win32" on every Windows CPU, and fell
  back to x86_64; it now reads the env vars the way `doctor` does. After the
  fix the arm64 build says arm64, and so does an x64 build of the same app
  (PE 0x8664, emulated), since `tiny.system` describes the machine. A
  self-driving probe page in a built app: tray icon up (`position()` a real
  32x48 rect) and removed, Web Audio `running` with the clock advancing
  0.64 s in 0.6 s, `dialog.alert` shown and answered (harness gotcha: MB_OK's
  lone button has id IDCANCEL, so WM_COMMAND IDOK does nothing; send 2),
  quit cleanly. appsurface's decorations (badge/progress/flash) weren't
  watched; nothing in this branch touches them.)*
- [x] **Windows on ARM, publish + update** — publish 0.2.0 with the arm64
  tinyjs then the x64 one in the same project: one manifest, `win.arm64`,
  `win.x86_64`, and `win.url` = the x64 zip. Then from a localhost
  manifest: an arm64 0.1.0 updates to the arm64 0.2.0; an **x64** 0.1.0
  updates to the **arm64** 0.2.0 (this is the PROCESSOR_IDENTIFIER /
  PROCESSOR_ARCHITEW6432 detection — log both from the x64 app if it
  doesn't); an arm64 app against an x64-only manifest sees no update.
  *(2026-10-21: arm64 publish, then x64 publish ("kept from an earlier
  publish of this version"): one manifest with `win.arm64` →
  `-win-arm64.zip`, `win.x86_64` and `win.url` → `-win.zip`. Each case ran
  a fresh copy of the 0.1.0 dist against a localhost manifest, with the
  server log as the record of which zip was fetched. arm64 → GET
  `-win-arm64.zip`, relaunched (`--tinyjs-relaunched`) as 0.2.0, 0xAA64.
  **x64 → GET `-win-arm64.zip`**, relaunched as 0.2.0 with the exe and
  launcher now 0xAA64. The x64 app saw PA=AMD64, `PROCESSOR_ARCHITEW6432`
  unset, PI="ARMv8 (64-bit) Family 8 …", so the `PROCESSOR_IDENTIFIER`
  test is the one doing the work. arm64 vs a manifest with `win.arm64`
  removed → `available: false`, only the manifest fetched.)*
- [x] **Windows on ARM, source checkout** — `setup.ps1` downloads
  `tjs-windows-arm64.zip` and, with llvm-mingw installed, builds an ARM64
  launcher; without it, prints the emulated-launcher note.
  *(2026-10-21, two fresh clones, `-SkipPath`. (1) As is, no llvm-mingw:
  the latest release (v0.61.0) has no arm64 tjs → "using the x64 one" note,
  txiki x64 download, the llvm-mingw note, WinLibs g++ → tjs.exe and
  launcher both 0x8664. (2) No release carries `tjs-windows-arm64.zip`
  until this ships, so the download URL was pointed at localhost serving
  run 39558361061's zip (the only edit), with CI's pinned llvm-mingw
  20260922 (sha256 checked) on PATH, not CXX: tjs.exe byte-identical to
  CI's, launcher built by `aarch64-w64-mingw32-clang++`, both 0xAA64, and
  `tinyjs dev` from that checkout ran the probe app natively. Not seen:
  the real github.com download (first release), and the winget
  package-folder search branch.)*
- [x] **Windows x64** — nothing regressed: `publish` still names the zip
  `-win.zip` and fills `win.url` plus `win.x86_64`; an existing x64 app
  updates from it.
  *(2026-10-21, x64 tinyjs emulated on the ARM box, not x64 hardware.
  The publish above: `-win.zip`, `win.url` + `win.x86_64`. A 0.1.0 built
  with the **released v0.61.0** x64 tinyjs (which predates the split and
  reads `win.url`) against the merged manifest → GET `-win.zip`, relaunched
  as 0.2.0, still 0x8664. An x64 0.1.0 from this branch vs an x64-only
  manifest → `-win.zip`, 0.2.0. Real x64 hardware never sees the ARM env
  values, so it takes the plain x86_64 path: the same path, minus the
  emulation.)*

## PR #63 — signed update manifests (`feat/signed-updates`, 2026-10-20)

`update.publicKey` (one key or a list) + `tinyjs publish` signing from
`TINYJS_UPDATE_PRIVATE_KEY_FILE`; the hosted manifest keeps its plain fields
plus a `signed` block, so keyless apps update from the same url. Guide:
docs/signed-updates.md. `python3 -u test/signed-updates/check.py` runs the
crypto + 73 runtime checks + CLI publish/sign checks everywhere, but its
packaged update run is macOS-only and SKIPs elsewhere, so Windows and Linux
need a real update watched by hand (localhost manifest, as for `extras/`).

- [x] **macOS** — `check.py` PASS: signed publish → packaged check/install
  → swap → relaunch 2.0.0; tamper, unsigned-at-install and checksum
  rejection leave the bundle untouched; a keyless app updates from the
  same signed manifest and sees nothing in a `--signed-only` one.
  *(2026-10-20, macOS 27.0 arm64.)*
- [x] **Windows** — `check.py` PASS (txiki has Ed25519 there), then by
  hand: an app with two keys in `publicKey`, `publish` with the second
  key's private file, real update 0.1.0 → 0.2.0 from a localhost manifest
  (check → install → relaunch shows 0.2.0). Edit the `win` url inside
  `signed.payload` → `check()` errors "signature verification failed";
  edit the plain `win` url instead → still installs the signed one.
  `--expires` in the past (hand-signed) → "expired". Same manifest, app
  without a key → updates; `--signed-only` → no update.
  *(2026-10-20, Windows 11 26200 arm64: `check.py` PASS (txiki 26.6.0),
  packaged run SKIPped as designed. By hand, scripted: `publicKey` =
  [A, B], published with B's private file; installed 0.1.0 copy outside
  `dist/`. Payload's `win.url` edited → "update manifest signature
  verification failed", exe untouched. Payload re-signed with B and
  `expires` 2026-01-01 → "update manifest expired … needs to re-sign it",
  exe untouched. Plain `win` url → `127.0.0.1:1` + zeroed sha256 → installed
  anyway; relaunched exe in the same folder reported 0.2.0. Keyless 0.1.0
  build: `{signed}` only → no update; the full signed manifest → updated
  and relaunched as 0.2.0.)*
- [x] **Linux** — the same, with the per-arch `linux` block.
  *(2026-10-20, Ubuntu 24.04 arm64: `check.py` PASS, packaged part
  SKIPped as designed. Then a Linux port of that packaged fixture (scratch
  script, not committed): app pinning two keys, `publish` signed with the
  second; each case ran from a fresh copy of the 1.0.0 `dist/` against a
  localhost manifest. `linux.arm64` url edited inside `signed.payload` →
  "signature verification failed"; `signed` dropped at install →
  "signed Ed25519 … required"; hand-signed `expires` 2026-01-01 →
  "expired … needs to re-sign"; signed with a wrong sha256 → "checksum
  mismatch". Folder byte-identical after every rejection. Plain
  `linux.arm64` url/sha256 edited → still installed the signed tarball;
  untouched manifest → check → install → relaunched as 2.0.0. Keyless
  build: `--signed-only` → no update, same signed manifest → 2.0.0. No
  private-key bytes in the installed folder, no `tinyjs-update-*` left.)*

## `extras/` — README, licence beside the app in the download (`feat/extras`, 2026-10-20)

`test/extras/check.py` (README there) — builds and publishes only, no window
needed. Done on all three OSes, including a real auto-update on Windows and
Linux; nothing owed.

- [x] **macOS** — `check.py` PASS: dmg root, not in the .app / update zip,
  no-`--dmg` note, lists, errors; the .app in the dmg verifies ad-hoc and
  signed with a Developer ID. *(2026-10-20, macOS 27.0, arm64.)*
- [x] **macOS, notarize** — `tinyjs notarize --dmg` with a Developer ID:
  the rebuilt dmg still has the extras beside the stapled app.
  *(2026-10-20, macOS 27.0 arm64: throwaway app with root + `macos/`
  README, LICENSE, `docs/`, `install.sh`. Apple: Accepted. Mounted dmg:
  `macos/` README won, `docs/` whole, `install.sh` still 755; `stapler
  validate` OK, `codesign --verify --deep --strict` OK, `spctl`
  accepted as Notarized Developer ID.)*
- [x] **Windows** — `check.py` PASS; then open the publish zip in Explorer
  and see `README.txt` beside `<name>.exe`; run an auto-update and see the
  new version's README replace the old one. *(2026-10-20, Windows 11
  26200: `check.py` PASS except the symlink case, SKIPped — creating a
  symlink needs Developer Mode or admin. Junctions need neither, so they
  were tried by hand: `extras/` itself, a top-level folder, one nested in a
  folder, one in `extras/windows/`, and a list entry reached through one
  all fail the build ("symlinks are not supported"; libuv's lstat reports
  a junction as a link) and nothing lands in `dist/`. Publish zip listed:
  `upapp/README.txt` beside `upapp/upapp.exe`, `docs/` whole. Real update
  0.1.0 → 0.2.0 from a localhost manifest: check → install → relaunch
  reported 0.2.0, and `README.txt` read the v0.2.0 text.)*
- [x] **Windows: README's "updates replace them" is not what happens.**
  Resolved as docs (2026-10-20): README and skill/references/release.md now
  describe Windows separately (overwrite by name; dropped or renamed extras
  and user files stay). Pruning the previous version's extras stays an
  option if renaming extras between versions turns out to matter.
  The same update left `OLD-ONLY-IN-V1.txt` (dropped from v0.2.0's
  extras), `docs/a.txt` (v0.2.0 ships `docs/b.txt`, so `docs/` now holds
  both) and a user's `USER-SAVED.txt`. `winSwapDir` overlays file by file
  and prunes only `WIN_OWNED_DIRS` (`executables`, `resources`), on purpose:
  the Shelf's marker lives in that folder. Same name → replaced; dropped
  or renamed → kept forever. Either the README's Windows/Linux bullet gets
  split per OS, or the updater learns which files were the previous
  version's extras (a list written at build time) and prunes those.
  Linux not checked — it may really swap the folder.
  Linux checked (2026-10-20): it does — the README is right for Linux, so
  the fix is the Windows half only.
- [x] **Linux** — `check.py` PASS (22.04 container is enough); extract the
  tarball and run the shipped `install.sh` directly. *(2026-10-20, Ubuntu
  24.04 arm64 host: `check.py` all PASS. Publish tarball: one `upapp/`
  with `launcher`, the backend, `README.txt`, `docs/`, `install.sh` 755;
  extracted, `install.sh` ran both by absolute path and as `./install.sh`.
  Real update 0.1.0 → 0.2.0 from a localhost manifest, installed folder
  holding v1's extras + a user's `USER-SAVED.txt`: relaunched as 0.2.0, and
  the folder is exactly v0.2.0's — new `README.txt`, `docs/b.txt` only,
  `OLD-ONLY-IN-V1.txt` and `USER-SAVED.txt` gone, `install.sh` still 755.
  Same with `TMPDIR=/dev/shm` (tmpfs, so the cross-filesystem copyTree
  path instead of rename): identical result, exec bits kept, no
  `tinyjs-update-*` left behind.)*

## #70 — multi-screen: one Windows DIP space, screen-relative placement, `rememberWindows` (`feat/screens-70`, 2026-10-20)

Plan and research: TODO-screens.md. Probe: `test/screens/` (README there) —
`SCREENS_OUT=… tinyjs dev` runs every screen through the three coordinate
spaces and the helpers; `SCREENS_PHASE=restore` checks rememberWindows after
a restart; `SCREENS_PHASE=watch` records `screens-changed` while you plug and
unplug. Linux launcher compiles (Ubuntu 22.04 container) but has never run.

- [x] **macOS, 2x built-in + 1x virtual display** (CGVirtualDisplay helper,
  1920x1080 at 1x, right of the panel). Probe `ok: true` on both screens:
  global/local-pt/local-px round-trip and set(get()) per screen, screenAt /
  screenFor, foregroundScreen, matchScreen (exact, replug, stranger),
  `win.open({ screen })`, `setFullscreen(true, { screen })` on the 1x
  screen, `setSize({ units: 'px' })` exact (601x401) on 1x and snapped to
  whole points (602x402) on 2x. rememberWindows: restored to the virtual
  screen; after unplugging, the laptop-only layout's own placement; with
  that layout deleted, the cursor's screen at the same local spot; replugged
  as a NEW id (6 → 9) with the same uid → back on it. `screens-changed`
  reached backend (`onSystem('screens')`) and page (`[1, 8]` then `[1]`).
  *(2026-10-20, macOS 27.0.1, M-series.)*
- [x] **macOS, deferred show** — with rememberWindows the main window never
  appears at the default spot before jumping (watch it, or screen-record a
  launch). *(2026-10-20: CGWindowList polled every 5ms through a restore
  launch — the first on-screen frame is already at the restored spot,
  growing 492x376 → 500x382 over ~0.1s, which is AppKit's open animation;
  nothing at the default centred position.)*
- [x] **macOS re-check after the Windows/Linux fixes (`aceea9e`)** — probe
  `ok: true` and restore exact on THREE screens: 2x built-in, a real 2x
  external (YOGA Pad Pro, 1080x675) and the 1x virtual display. Kitchen
  sink `feat/screens` (examples repo) driven by an injected script through
  the Screens card: send-to-screen on all three, pixel-exact 640x400 px at
  (32, 32) px on 1x, matchScreen with a wrong uid, fullscreen on the next
  screen and back, foregroundScreen — no page errors. On the 1080x675
  external a 1100x720 window is shrunk and nudged up by AppKit to fit
  (local y 31, not 40): OS behaviour, not ours. Found + fixed: macOS posts
  its screen-parameters notification on fullscreen enter/exit, so
  `screens-changed` fired twice with an unchanged list; the bridge now only
  emits when screens() actually changed (unplug/replug still gives exactly
  2 events). That dedupe is bridge-side, so it applies on every OS —
  worth one plug/unplug re-check on Windows and Linux.
  *(2026-10-20, macOS 27.0.1.)*
  - [x] **Linux** — the dedupe as committed SWALLOWED real changes: it
    compared against screensCache, which every app.screens() / screenAt()
    / screenFor() / foregroundScreen() / matchScreen() refreshes, so a
    query landing between the change and its `SYS screens` (the Linux
    launcher waits 500ms for the burst to settle) made the new list look
    old. Headless rig (two virtual monitors), unplug + replug Meta-1 with a
    backend polling app.screens() every 200ms (kitchen-sink's Desktop tab
    polls twice a second): 0 events. Now compared against the last list
    ANNOUNCED (seeded at boot). After: plain 2 + 2 (`[[0], [0, 1]]`),
    polling 2 + 2, fullscreen in/out alone 0 — and 0 for fullscreen on the
    real one-screen desktop, Wayland and X11. *(2026-10-20.)*
  - [x] **Windows** — reproduced, then fixed. One display in the VM today
    (no second Parallels display attached), so the real change is a
    RESCALE: the watch probe (scratch copy + a 200ms backend app.screens()
    poller + an optional fullscreen in/out) while DisplayConfigSetDeviceInfo
    takes the display 200% → 175% → 200% (GetDpiForMonitor read back
    168 / 192 each time). Old bridge (`06e9ccc`) + poller: 0 onSystem, 0
    page events — swallowed on Windows too. Fixed bridge: 2 + 2 without the
    poller, 2 + 2 with it (77 polls); fullscreen in/out + poller alone: 0.
    *(2026-10-20, Windows 11 26200 arm64 / Parallels.)*
  - [ ] **Windows, plug/unplug** — the same with a second display removed
    and re-added (Win+P "PC screen only" → "Extend", as above). Needs a
    second Parallels display attached.
  - [ ] **macOS** — re-run the plug/unplug and fullscreen checks on the
    fixed dedupe, once with something polling screens() (the kitchen-sink
    Desktop tab open is enough).

Windows runs: Windows 11 arm64 in Parallels, launcher rebuilt from the branch
by `tinyjs dev`. Second screen = an external on the Mac passed through as a
second "Parallels Vu" (both 2x native); scales set per monitor and monitors
moved with DisplayConfigSetDeviceInfo / SetDisplayConfig (what Settings
does), so every layout below is a real Windows one. A cable pull does NOT
reach the guest (Parallels keeps the display) — "unplug" here is Win+P
"PC screen only" → "Extend" (SetDisplayConfig TOPOLOGY_INTERNAL/EXTEND),
which removes and re-adds the monitor with the same broadcasts.
*(2026-10-20.)* Scratch harness (dpi.ps1, topo.ps1, a 2ms window poller,
a broadcast spy) was not committed.

- [x] **Windows, mixed DPI (100% + 125%), laptop RIGHT of the external** —
  Lionel's setup. `screens()` reports the laptop at `x` = the external's
  width (not width/1.25). Probe `ok: true`. Then with the 125% screen LEFT
  of and ABOVE the primary, and with 150%.
  - Primary 3456x2168 at 100%, second 2160x1350 px: 125% right → `x: 3456`
    (not 2765), 1728x1080; left → `x: -1728`; above → `y: -1080`; 150%
    right → `x: 3456`, 1440x900. Probe `ok: true` in all four, every case
    round-trips on both screens (local px within the ±1 a fractional scale
    allows). Also both at 2x, right and left: `ok: true`.
- [x] **Windows, a window straddling two monitors** — `setPosition(get())`
  does not move it (both directions pick the monitor holding most of it);
  dragging it across emits one onState with the new `screenId`.
  - FIXED on the way: a window coming FROM the 150% screen to a spot mostly
    on the 100% one landed 100 DIPs off on the wrong screen
    (`setPosition(3156, 542)` → `3256, 509`, screen 1). It kept its 1.5x
    physical size, sat 50/50 physically, so Windows never rescaled it and
    getState read it through the 150% monitor. `match_monitor_dpi` now hops
    it inside the target first (WM_DPICHANGED fires synchronously; the
    webview's handler resizes), for `pos` and `spos`. After: all four
    from×to combinations land exactly, size stays 400x300 DIP.
  - set(get()) ×3 stable mostly-on-0 and mostly-on-1 (100% + 150%).
  - Slide across (40 SetWindowPos steps from another process — the same
    WM_MOVE path a drag takes, but not a mouse drag): one onState 0 → 1;
    the other way one 1 → 0.
  - Sizes across 100% ↔ 150% (fixed after the first pass): secondaries
    opened or moved either way read exactly 600x400; Tiny Deck (has a menu
    bar) hopped 4× at 1100x720, 800x500, 640x400, 500x320, exact every
    time. Kitchen-sink's Screens card driven over CDP: place per screen,
    px placement 640x400 px at (32,32) px, matchScreen with a wrong uid,
    foregroundScreen, fullscreen on the other screen, rememberWindows
    reopening on screen 1.
  - amp at 100/150: docked group moves in lockstep across and back (CDP
    drag), renders right on 150%, quits with exit 0 (crashed before; see
    TODO-screens.md). Real-mouse drag across mixed DPI NOT done — see the
    WebView2 screenX note there.
- [ ] **Windows, uid** — stable across a reboot; two identical monitors get
  different uids; note what happens when one moves to another port.
  - Two identical panels (both "Parallels Vu"): different uids,
    `\?\DISPLAY#PRL5000#3&21de92e8&0&UID0#{e6f0…}` vs `…&UID1#{…}` —
    the PnP instance part tells them apart. Survived scale changes, moves
    and a topology off/on (rememberWindows matched it back). Reboot and
    port move not done (a VM has no ports; a reboot ends the session).
- [x] **Windows, `screens-changed`** — plug/unplug, change a monitor's
  scale, move the taskbar (SPI_SETWORKAREA). Does a scale change alone send
  WM_DISPLAYCHANGE? If not, it needs WM_DPICHANGED/WM_SETTINGCHANGE wiring.
  - **Scale: yes, it does** — 200% → 225% → 200% sent WM_SETTINGCHANGE
    0x9F, WM_DISPLAYCHANGE, WM_SETTINGCHANGE 0x2F each way; one
    `onSystem('screens')` + one page event per change, the list carrying
    2.25 / 1536x964 then 2 / 1728x1084. No extra wiring needed.
  - **Work area:** taskbar auto-hide on/off → one event each (visible
    height 1084 / 1036). A bare SPI_SETWORKAREA is reverted by Explorer
    within 300ms, so set+revert reads as ONE settled event.
  - **Unplug/replug** (Win+P, see above): remove → one event, `[0]`;
    re-add → TWO events 650ms apart, both `[0, 1]` — Windows sends two
    WM_DISPLAYCHANGE ~930ms apart on re-add, past the 500ms debounce.
    Harmless (same list twice); a bridge-side "same list as last time"
    skip would make it one.
- [x] **Windows, `setFullscreen(true, { screen })`** — main lands on the
  chosen monitor; a secondary maximizes there.
  - Main: probe's fullscreenOn → screen 1 in every layout above. A
    secondary opened on screen 0 maximized on screen 1 (1440 wide, its work
    area) and one opened on 1 maximized on 0; onState maximized
    false → true.
- [x] **Windows, rememberWindows** — restore phase, incl. a maximized window
  (normal rect kept), and no flash before the deferred show.
  - One screen: back at local 150,120, 500x350. Maximized: saved
    `maximized: true` with the normal rect 200,150 520x360; restored
    maximized, un-maximizing landed on 200,150 520x360. (`getState()` has
    no `maximized`; only onState does.)
  - Two screens: back on screen 1 (150% too). Win+P single → that layout's
    own placement; with it deleted → the cursor's screen, same local spot
    (150,120, 500x350); extend again → back on screen 1 by uid.
  - FIXED: **flash.** `webview_set_size`'s first call SHOWS main, and the
    defer hide came after navigate, so main sat visible at the default spot
    for ~20ms — a 2ms poll caught it in 2 of 5 restores (vis=1 at
    442,442, hidden, then shown placed). Main now starts DWM-cloaked when
    it will start hidden (deferred show or accessory) and is uncloaked
    after the hide. After: 0 of 5 visible before the placed frame; a plain
    app (no rememberWindows) shows exactly as before.
  - FIXED (guard): once, after a Win+P detach with main on the vanishing
    150% screen, the single-screen layout saved main as `116x0` at 0,0 and
    restored it as a 116x1 window. Not reproduced in four more detaches
    (equal and mixed DPI, main on the vanishing screen — all saved
    500x350). The bridge now neither saves nor restores an empty content
    box; with a planted 116x0 entry: before 116x1, after the default
    400x300.
- [x] **Linux X11, two monitors** — probe `ok: true`; foregroundScreen via
  _NET_ACTIVE_WINDOW; uid looks like `DP-1/<mfr>/<model>`; `internal` true
  for eDP. First run FAILED every round-trip: `getState()` read the client
  origin (gdk_window_get_origin) while `pos`/`spos` move the mutter frame,
  so a decorated window read 37px (the title bar) below where it was put
  and `set(get())` marched it down — `win.open({ screen, x, y })` and
  rememberWindows too. Pre-dates #70 (8a4e43f only fixed undecorated). Now
  reads gdk_window_get_root_origin (frame top-left; equal to the origin
  when undecorated). After: probe `ok: true`, `fails: []` on both screens;
  restore twice → screen 1, local 150,120, 500x350, no drift; same with
  `"chrome": { "frame": false }` (probe ok, restore exact).
  foregroundScreen → screen 1 (the probe's own window was there);
  uid `Meta-0//Meta-0` (Xwayland: no EDID, model = connector); `primary`
  true on screen 0. *(2026-10-20, arm64 VM; rig: headless gnome-shell 46
  with `--virtual-monitor 1920x1080 --virtual-monitor 1280x720` in its own
  dbus-run-session, GSETTINGS_BACKEND=memory, a stub
  `org.freedesktop.systemd1` on that bus so its Xwayland starts, app on
  `DISPLAY=:2 GDK_BACKEND=x11`. Not run: real Xorg, a real eDP panel /
  `internal: true`, real EDID manufacturer/model.)*
  Re-run on the VM's real GNOME desktop with a second (Parallels) screen
  attached, after the Windows fixes were pulled: X11 via the session's own
  Xwayland + mutter — probe `ok: true`, `fails: []`; restore twice →
  screen 1 (`Virtual-2//Virtual-2`), local 150,120, 500x350. *(2026-10-20.)*
- [x] **Linux Wayland** — `setFullscreen(true, { screen })` uses that output
  (the one placement op Wayland honours); foregroundScreen is null; spos /
  pos are no-ops; rememberWindows restores size only; uid from
  xdg_output.name on GNOME and KDE. GNOME only: fullscreen on screen 1 →
  `screenId: 1`; foregroundScreen null; every position op left the window
  at 0,0 (`canPosition: false`); restore gave 500x350 at 0,0; uid
  `Meta-0/MetaVendor/MetaVirtualMonitor` (rig) and
  `Virtual-1/unknown/unknown` (VM desktop). `primary` is false on every
  screen (GDK Wayland has no primary). *(2026-10-20, same rig on
  `WAYLAND_DISPLAY`, plus the VM's real one-screen GNOME desktop. KDE not
  run — none here.)*
  Re-run on the real two-screen desktop (Wayland): same — fullscreen on
  screen 1 → `screenId: 1`, foregroundScreen null, positions no-ops,
  restore 500x350 at 0,0. *(2026-10-20.)*
- [x] **Linux, fractional scaling** — what `scale` and `pixels` report at
  125% on GNOME and KDE (expected: an integer, rounding differs), and what
  WebKitGTK's `devicePixelRatio` says in the same setup. GNOME 46 with
  scale-monitor-framebuffer, 1920x1080 at 125%: Wayland client →
  logical 1536x864, `scale: 2`, `pixels` 3072x1728 (GTK's buffer, not the
  panel's 1920x1080), devicePixelRatio 2; X11 client (Xwayland, upscaled
  by mutter) → 1536x864, `scale: 1`, `pixels` 1536x864, devicePixelRatio
  1. The scale change, and the change back, each sent ONE
  `onSystem('screens')` and ONE page `onScreensChanged` (Wayland and X11).
  *(2026-10-20, same rig, keyfile gsettings backend in the scratchpad. KDE
  not run.)*
- **kitchen-sink 0.20.0 (examples `feat/screens`) on Linux** — a page
  driver clicked the Screens card and cookie demo (scratch copy, own app
  id). Three fixes came out of it:
  - [x] **Linux: restored main lost the menu bar's height every launch**
    (600 → 574 → 548). The bridge's restore setSize ran before the first
    menu bar; apply_menus' one-time repayment aims at the tinyjs.json size
    and its born-size guard skipped the now-resized window. A setSize before
    the first bar now becomes the repayment target. After: 900x600 twice.
    *(2026-10-20, X11 on the real two-screen desktop.)*
  - [x] **bridge: a maximized window with no normal rect** (born maximized,
    or mutter auto-maximizing a 1100x720 that doesn't fit a 768-tall
    screen) restored on whatever screen the WM chose — zoom() only. It now
    moves onto `stateScreen` first. Planted entry, maximized on screen 1 →
    back maximized on screen 1. *(Linux X11, 2026-10-20.)*
  - [x] **bridge: a move <500ms before `quit()` was never saved** (the
    debounce timer died with the process). `app.quit()` (all quit paths:
    page `tiny.quit()`, closing main) now flushes pending saves, ≤1s.
    Moved to screen 1 300ms before quit → saved and restored there.
    *(Linux X11, 2026-10-20.)*
  - [ ] **macOS + Windows: re-check the two bridge fixes** — shared code:
    maximize on screen 2, quit; relaunch lands maximized on screen 2 (also
    with a window that was never un-maximized); move then `tiny.quit()`
    within 300ms → relaunch at the new spot; a plain quit isn't slower.
    - [x] Windows (2026-10-20, 200% + 200%): moved to screen 1 (300,200)
      450x320 and `app.quit()` 250ms later → saved and relaunched there
      (×2); quit → exit 68/101ms vs 65/91ms with nothing pending.
      Maximized on screen 1 → relaunch maximized on screen 1, un-maximize
      back to its normal rect there. Planted never-un-maximized entry (no
      rect, stateScreen screen 1) → maximized on screen 1. Planted with
      the normal rect on screen 0 and stateScreen screen 1 → maximized on
      screen 1, but un-maximizing then lands at 40,40 on screen 1, not the
      saved rect on screen 0: the pre-zoom setPosition replaces the normal
      rect. Probably fine (it's where the user last saw it). Note too that
      a save after un-maximizing drops `stateScreen` (entry rebuilt without
      it) — harmless, since it's only read when `maximized` is set.
  - Everything else as expected: map click → setPosition(40, 40, { screen })
    (X11: work-area clamped to 66,40 on the dock screen, 40,40 on the
    other), pixel-exact 640x400 px at 32,32 px, fullscreen on the next
    screen, matchScreen with a wrong uid → same screen, foregroundScreen
    (X11 an id, Wayland null), cookies set/remove/get, no page errors on
    X11 or Wayland.
- [ ] **Linux, KDE Plasma (Wayland)** — the two boxes above on KDE: uid
  from xdg_output.name, fullscreen-on-output, and `scale` / `pixels` /
  devicePixelRatio at 125% (KWin may hand GTK3 a different integer).

## #78 — NVIDIA + Wayland: launcher sets `__NV_DISABLE_EXPLICIT_SYNC=1` (2026-10-19)

The proprietary NVIDIA driver's explicit-sync path got the launcher's
Wayland connection killed (GDK `Error 71` → `_exit(1)`) before the page
loaded; stock MiniBrowser died the same way. The reporter confirmed that
`__NV_DISABLE_EXPLICIT_SYNC=1` alone fixes it, with GPU compositing and
fullscreen video still working. The launcher now sets it before `gtk_init`
when `WAYLAND_DISPLAY` is set and `/proc/driver/nvidia/version` exists, and
leaves it alone if the user has already set it. No NVIDIA machine here.

- [ ] **Linux, NVIDIA + Wayland** — an app launches with no env vars, and
  `video.webkitEnterFullscreen()` shows the picture. (Reporter's box:
  CachyOS, KDE 6.7.5, RTX 4060, driver 615.78.08, WebKitGTK 2.52.6.)
- [x] **Linux, non-NVIDIA (VM, Wayland)** — the gate stays closed and the
  app runs as before; a self-quitting page exits 0, and a SIGABRT'd
  launcher now prints `tinyjs: webview launcher killed by SIGABRT` and
  exits 1 (it used to exit 0). *(2026-10-19, arm64 VM.)*

## #46 — `app.cookies`: the webview's cookie jar (`feat/cookies-46`, 2026-10-19)

Backend-only `app.cookies.get/set/remove` over each engine's own store
(`COOKIES` wire op): WKHTTPCookieStore, WebKitCookieManager, WebView2's
ICoreWebView2CookieManager. Probe + expected values: `test/cookies/README.md`.

- [x] **macOS** — every row of the probe table as expected; server.py saw
  `injected=v1; sess=a b=c; sid=s3cret`. *(2026-10-19, macOS 27.)*
- [x] **Windows** — same probe; every row as expected, server.py saw
  `sid=s3cret; injected=v1; sess=a b=c`. A url-only set comes back
  host-only (`127.0.0.1`, no dot) and `removeExact` found it. Unset
  SameSite reads `lax` here (Chromium's default; Linux `none`, macOS
  `null`) — `vis` in `fromPage`. *(2026-10-19, Windows 11 arm64, launcher
  rebuilt from the branch by `tinyjs dev`.)*
- [x] **Linux** — same probe; every row as expected, server.py saw
  `sid=s3cret; injected=v1; sess=a b=c`. Unset SameSite reads `none`
  here (macOS: `null`). *(2026-10-19, ubuntu:22.04 container on arm64,
  Xvfb + dbus-run-session, WebKitGTK 2.50.4 — a real launcher and
  WebKitGTK, but not a desktop session; x86_64 and pre-2.42 WebKitGTK
  not run.)*
  Re-run on the VM's real Wayland desktop (WebKitGTK 2.52.6, arm64, clean
  jar): every row as expected, server.py saw `sid=s3cret; injected=v1;
  sess=a b=c`, `all` = 2. But unset SameSite (`vis`) reads `lax` here, not
  `none` as in the 2.50.4 container — it varies by WebKitGTK version, so
  don't document a fixed value. *(2026-10-19.)*
- SameSite follow-up (same day): `set()` now defaults `sameSite` to
  `'lax'`, and macOS reads no-policy as `'none'` instead of `null` (what
  WebKit enforces, and what `set()` needs to keep such a cookie working).
  Probe: `session` should be `[["a b=c", null, "lax"]]` everywhere.
  - [x] **macOS** — `session` `lax`; `vis` reads `none`; every other row
    unchanged. *(2026-10-19.)*
  - [x] **Windows** — `session` `lax` (expected unchanged: `vis` `lax`).
    *(2026-10-19, Windows 11 arm64, clean jar: `session`
    `[["a b=c", null, "lax"]]`, `vis` `lax`, every other row unchanged;
    server.py saw `sid=s3cret; injected=v1; sess=a b=c`.)*
  - [x] **Linux** — `session` `lax` on WebKitGTK 2.50 AND 2.52 (`vis`
    still varies by version; that's documented, not a bug). *(2026-10-19,
    arm64: 2.52.6 on the VM's Wayland desktop — `vis` `lax`; 2.50.4 in an
    ubuntu:22.04 container, Xvfb + dbus-run-session, `setup.sh` from a copy
    of the branch — `vis` `none`. Both: `session` `["a b=c", null,
    "lax"]`, every other row unchanged, server.py saw `sid=s3cret;
    injected=v1; sess=a b=c`.)*

## `app.paths()` creates data/cache/logs; build names a busy dist/ (#71, #72, 2026-10-19)

`makePathDirs` in bridge.js; the `exists('dist')` check after `rmTree('dist')`
in cli.js's `cmdBuild`.

- [x] **macOS** — fresh app id: all three dirs exist once `app.paths()`
  resolves, and `writeFile(paths.logs + '/file.log')` succeeds. A dist/
  file that can't be deleted (read-only subdir) stops the build with
  "couldn't clear dist/ — dist/locked/MyApp.exe is in use", exit 1.
  *(2026-10-19, scratch app.)*
- [x] **Windows paths** — same check, from `tinyjs dev` and from the
  built exe: `%LOCALAPPDATA%\<id>\Cache` and `\Logs` both appear, and the
  page's `tiny.app.paths()` does the same. *(2026-10-19, fresh ids: dev
  page, dev backend, built exe page — all three dirs exist, `Logs\file.log`
  written. Control run with main's bridge.js: no Cache/Logs, ENOENT.)*
- [x] **Windows busy dist/** — with the built app running, `tinyjs build`
  and `tinyjs publish` both print the "is in use … still running?" message
  naming `dist/<name>.exe` (or `launcher.exe`), not `EEXIST`. Quit the app
  and the next build succeeds. *(2026-10-19: both named
  `dist/launcher.exe`, exit 1; rebuild after quitting succeeded.)*
- [x] **Linux paths** — `~/.cache/<id>` and `~/.local/state/<id>` appear.
  *(2026-10-19, arm64 VM, fresh ids: dev page, dev backend, built app page —
  all three dirs exist, `file.log` written into `paths.logs`. Control run
  with main's bridge.js: data + cache already existed (something else makes
  them on Linux), `~/.local/state/<id>` didn't, write failed ENOENT.)*

## macOS — Dock reopening after hide-on-close (2026-10-16)

With `setHideOnClose(true)`, the red button hid the main window, but a Dock
click did not bring it back. The reopen delegate now shows the existing
main window when hide-on-close is enabled and no windows are visible.

- [x] **macOS** — reproduced the failure before the fix. In a
  `setHideOnClose(true)` app, repeated red-button close → Dock click
  restores the window with its unsaved input intact, in dev and packaged
  `.app` runs. *(2026-10-16, macOS 26.4.1, manual checks by the PR #73
  author. 2026-10-19: scripted before/after — `performClose:` then a
  LaunchServices reopen — restores on the PR launcher only; plus a real
  Dock click, hand-checked.)*
- [x] **macOS visible-window reopen (dev and packaged)** — switching away
  while the window is visible, then clicking its Dock icon focuses the same
  window with its input intact, without creating another window.
- [x] **macOS minimized-window reopen (dev and packaged)** — yellow-button
  minimize → Dock click restores the window with its input intact.
- [x] **macOS windowless reopen (dev and packaged)** — in a
  `"mainWindow": false` tray app, Dock clicks leave the app without a main
  window, and opening/closing a named window from the tray still works.
  *(2026-10-17, macOS 26.4.1, manual checks of all three cases above.)*

## `tinyjs update-project` + doctor's stale-files note (`feat/update-project`, 2026-10-16)

`cmdUpdateProject()` / `projectToolFiles()` in cli.js. Per OS, in a fresh
`tinyjs new` project: (a) `tinyjs update-project` says already up to date,
and doctor has no "not from this tinyjs" note; (b) append to
`.claude/skills/tinyjs/SKILL.md`, delete a file under
`.agents/skills/tinyjs/references/`, add a stray file to the skill folder,
overwrite `types/tiny.d.ts`: doctor names all three, `update-project` names
every file it updated, added or removed, the stray file is gone, the
deleted one is back, and doctor is clean again. Put the stray file INSIDE
`references/` (next to real files): only it may go — txiki's `tjs.remove`
deletes a non-empty folder even without `recursive`, which once took all of
`references/` with it; (c) move both skill folders away: `update-project` adds both;
(d) delete `types/tjs.d.ts`: it is not recreated; (e) outside a project,
and with an extra argument: an error, exit 1; (f) SYMLINKS, hostile-clone
style, with a "victim" folder of real files outside the project that must
come through untouched: `.claude/skills/tinyjs` and `.claude` linking to it
→ skipped by name; `types/tiny.d.ts` linking to a victim file → skipped; a
link INSIDE the skill (a references/ file, and references/ itself) →
removed as a link and replaced by a real file, victim untouched; no skill
plus a dangling `.claude/skills/tinyjs` link → skipped, nothing created at
its target. Windows: the same with `mklink /D` symlinks AND `mklink /J`
junctions (does readDir/lstat report a junction as a link?); (g) LOCKED
files, the Windows run's findings now fixed: hold a file to update and a
stray to remove open with no sharing, plus a shell cwd inside a stray
folder — each failure named under "could not update", every other change
still made and listed, exit 1 (never 0xC0000005); a rerun after releasing
them finishes the job, emptied folders included, and doctor is clean. Windows: the folder replace
(rmTree + copyTree) with Explorer or an editor holding the skill folder open.

- [x] **macOS** — (a)–(e), plus a Vite-style layout (old `src/` and
  `backend/` tiny.d.ts + tjs.d.ts: all four named and updated).
  File-by-file version: stray `references/my-notes.md` and
  `extra/deep/old.md` removed by name, `references/` kept, both skill
  folders `diff -rq` identical to `skill/`. (f) all symlink cases, victim
  intact each time; a FIFO in the skill folder is removed without being
  read. *(2026-10-16)*
- [x] **Windows** — second pass on 3e98a17 *(2026-10-16)*. (f) junctions
  and symlinks pass; (g) passes (the no-sharing lock too, after 0172809).
  - (f) **symlinks**, on 4dd12a7 from an elevated shell (`mklink /D` and
    file `mklink`). tjs's lstat and readDir report both kinds as
    `isSymbolicLink`. Every case on a fresh project, victim folder
    byte-identical after each:
    - Skipped by name, exit 0: `.claude/skills/tinyjs` → victim;
      `.claude` → victim, absolute and relative (`..\victim`);
      `types/tiny.d.ts` file link → a victim file, left as a link.
    - Removed as links and replaced with real files: `references/api.md`
      file link → victim (now a file matching the install);
      `references/` → victim\sub (now a real folder, 6 files).
    - Extra links removed, the victim's files untouched:
      `references/extra` dir link and `references/notes.md` file link.
    - Dangling `.claude/skills/tinyjs` with no skill folders: skipped,
      target not created, and `.agents` not added (as with the junction).
      A dangling `.claude` link instead: skipped, target not created,
      `.agents` added.
    - Doctor skips linked items: clean, exit 0.
  - (f) **junctions** (`mklink /J`; tjs's lstat AND readDir both report
    one as `isSymbolicLink`). Every case on a fresh project, with the
    victim folder hashed before and after: byte-identical every time.
    `.claude/skills/tinyjs` → victim and `.claude` → victim: "skipped …
    symbolic link", exit 0. `references/` → victim\sub: "removed
    references (not a regular file)", a real `references/` re-added (6
    files). A junction named `references/api.md` was replaced by a real
    file. An extra `references/extra` junction → victim was removed as a
    link, and the victim's contents were never touched. No skill folders
    plus a dangling `.claude/skills/tinyjs` junction: skipped, target not
    created. Side effect: `.agents` isn't added either (the dangling link
    counts as a skill item). Doctor skips linked items silently.
  - (f) **symlinks**: not possible in the first, unelevated session ("You
    do not have sufficient privilege"); done since, see above.
  - (g) locks held during the run:
    - **Read-shared** (`FileShare.Read` on a file to update,
      `ReadWrite` on a stray to remove; how editors and Office hold
      files): both named under "could not update", tiny.d.ts still
      updated, exit 1. After release: both done, doctor clean.
    - **No sharing at all** (`FileShare.None`): the whole folder is
      skipped. `filesUnder` reads every file up front, so one unreadable
      file gives `.claude/skills/tinyjs/: could not read it: EBUSY`, and
      nothing else in that folder is synced. The other items (.agents,
      types) were still updated and named, exit 1 every time (3 runs),
      no 0xC0000005. Fix: catch the read per file inside `filesUnder`.
      **Fixed after this run** (an unreadable file is its own failure,
      the rest of the folder syncs; macOS `chmod 000` stand-in passes):
      rerun this case. **Rerun on 0172809, Windows ✓ (2026-10-16):** a
      fresh project, edited SKILL.md held with `FileShare.None`, plus a
      stray, an edited and a deleted reference in the same folder. 3 runs:
      only `SKILL.md: EBUSY` listed under "could not update"; stray
      removed, api.md updated, recipes.md re-added, tiny.d.ts updated;
      exit 1 each time, never 0xC0000005. After release: SKILL.md updated,
      exit 0, then "already up to date", doctor clean, skill folder
      hash-identical to `skill/`.
    - **Shell cwd inside a stray `extra/deep`**: named
      (`extra/deep/: EBUSY`), exit 1. After release: both emptied
      folders removed, then "already up to date".
  First pass, below: *(2026-10-16, Windows 11 arm64, fresh
  `tinyjs new` project in a temp dir.)* (a) "already up to date", no
  doctor note. (b) doctor named `.claude/skills/tinyjs/`,
  `.agents/skills/tinyjs/`, `types/tiny.d.ts`; update-project listed
  updated SKILL.md, removed `references/my-notes.md`, added
  `references/recipes.md`, updated tiny.d.ts; `references/` kept; both
  skill folders hash-identical to `skill/` (7 files); doctor clean. (c)
  both added back. (d) tjs.d.ts not recreated. (e) both errors, exit 1.
  **Locked files** (held open from PowerShell during the run):
  - A file to update held with no sharing, or a stray to remove held
    without delete sharing (how most Windows apps open files): the run
    stops at that file with only `Error: EBUSY: resource busy or locked`
    and exit 1. It doesn't name the file, and it doesn't list changes
    already made before it. Fine after the lock is released. One of four
    such runs exited `0xC0000005` (access violation) instead of 1. It
    didn't recur in 3 more runs or in a bare tjs probe (an uncaught EBUSY
    from writeFile exits 1 every time).
  - A shell whose cwd is inside a stray subfolder: the file goes, but
    the emptied folders stay (the removal error is swallowed), and later
    runs say "already up to date", because filesUnder only sees files.
    Harmless, but not "identical to skill/".
  Suggested fix: catch per file, name it, keep going, print what did
  change, exit 1 if anything failed.
- [x] **Linux** — (f) and (g) at 3e98a17, (a)–(e) rerun there and still
  passing. (f) victim folder md5-checked after every case: skill folder as
  a link, `.claude` as a link, `types/tiny.d.ts` as a link, and a dangling
  skill link were each "skipped …: it is, or is inside, a symbolic link",
  exit 0, victim intact, nothing created at the dangling target. A link
  for `references/api.md` and for `.agents/…/references/` itself:
  "removed … (not a regular file)", then real files added, both skill
  folders `diff -rq` identical, victim intact. A FIFO in the skill: removed
  without blocking. (g) no Windows locks here, so chmod stood in: SKILL.md
  444 in a 555 skill folder, plus a stray `extra/deep/old.md` in a 555
  folder. Both named under "could not update 2 item(s)" with EACCES,
  tiny.d.ts still updated and listed, exit 1. After restoring permissions
  the rerun updated SKILL.md and removed old.md, `deep/` and `extra/`;
  identical, doctor clean. *(2026-10-16, arm64 GNOME VM)*
  Earlier run: (a)–(e) in a fresh `tinyjs new` project from the
  checkout. (a) "already up to date", doctor clean. (b) appended SKILL.md,
  deleted `references/recipes.md`, stray `references/my-notes.md`,
  overwrote `types/tiny.d.ts`: doctor's NOTE named both skill folders +
  tiny.d.ts (exit 0); `update-project` listed updated SKILL.md, added
  recipes.md, removed my-notes.md, updated tiny.d.ts; `references/` kept,
  both skill folders `diff -rq` identical to `skill/`, doctor clean.
  (c) both folders moved away → both `added`, identical. (d) `tjs.d.ts`
  deleted → "already up to date", not recreated. (e) no tinyjs.json and an
  extra argument → errors, exit 1. Plus `extra/deep/old.md` in the skill:
  removed by name, folder identical after. *(2026-10-16, arm64 GNOME VM)*

## `tinyjs doctor` (#61, `feat/61-doctor`, 2026-10-16)

`cmdDoctor()` in cli.js, read-only (stat/readFile only). Per OS, from the
checkout and from a release install: (a) `tinyjs doctor` in an empty folder
and in a project folder — no false `FAIL`, exit 0; (b) the PATH line names
the right install, including a second install added later on PATH; (c) a
project with a missing `name`, `"minTinyjsVersion": "99.0.0"` (release
install) and `frontend.build: "pnpm run build"` with pnpm off PATH — each a
`FAIL`, exit 1. Windows: (d) `OK: WebView2 Runtime <version>`; the PATH line
recognises `tinyjs.cmd` (PATHEXT) as this install; `node`/`npm` are found
as `.exe`/`.cmd`. Linux: (d) `OK: WebKitGTK 4.1` with the runtime package
installed (read from `/etc/ld.so.cache`), and the `DISPLAY` warning over
plain ssh.

- [x] **macOS** — (a), (b), (c) plus stale, missing and non-executable
  launcher, a bad `TINYJS_LAUNCHER`, invalid / non-object `tinyjs.json`,
  a name only in the `macos` block, and `package.json` without
  `node_modules`. *(2026-10-16, Apple M3 Max, fake release install in a
  temp dir.)*
- [x] **Windows** — (a), (b), (c), (d). *(2026-10-16, Windows 11 arm64,
  PowerShell; the checkout, plus a fake release install in a temp dir
  (checkout's cli.js/bin/native/runtime, `VERSION` v0.57.5). The real
  v0.51.1 install in `%LOCALAPPDATA%\tinyjs` sat second on PATH, untouched.)*
  (a) empty folder and a `{ name, title }` project: exit 0 from both
  installs, no FAIL; the project got `OK: project "good"` plus the
  `$schema` NOTE. (b) fake first → `OK: this install` + `WARN: other
  installs later on PATH: …\AppData\Local\tinyjs\…`; checkout first →
  `WARN: tinyjs on PATH is …\tinyjsapp\tinyjs, a different install`; no
  tinyjs on PATH → the NOTE. (c) no `name` + `minTinyjsVersion: "99.0.0"`
  + `frontend.build: "pnpm run build"` (no pnpm here): three FAILs, exit 1
  (the checkout skips the version check, as designed: two FAILs). (d) `OK:
  WebView2 Runtime 154.0.4258.62`; `OK: on PATH: node, npm`; with the
  extensionless `tinyjs` deleted from the fake install, its `tinyjs.cmd`
  alone read as this install. **Found and fixed:** `findOnPath` tried the
  bare name before PATHEXT, so on Windows the extensionless bash `tinyjs`
  (which cmd and PowerShell can't run) counted as a hit. The "other
  installs" WARN listed one install twice (`…\tinyjs\tinyjs,
  …\tinyjs\tinyjs.CMD`), and the PATH line named the bash script. It now
  looks up PATHEXT extensions only, lowercased. Re-run after the fix, with
  the bash `tinyjs` left in place everywhere: each install listed once as
  `…\tinyjs.cmd` (`WARN: other tinyjs installs later on PATH:
  …\AppData\Local\tinyjs\tinyjs.cmd`; `…\tinyjsapp\tinyjs.cmd, a different
  install`). Every verdict unchanged; node/npm still found; the pnpm FAIL
  still fires.
- [x] **Linux** — (a) empty folder and project folder, from the checkout
  and a fake release install (`VERSION` v0.57.5 in a temp dir): no FAIL,
  exit 0. (b) PATH: the `~/.local/bin` symlink reads as this install; a
  second install later on PATH warns by path; another install first warns
  "a different install"; none on PATH is a NOTE. (c) no `name`,
  `minTinyjsVersion: "99.0.0"` (release only — the checkout is `dev` and
  rightly skips it), `pnpm run build` without pnpm: 3 FAILs, exit 1; stub
  `node`/`pnpm` on PATH → `OK: on PATH: node, pnpm` and the pnpm FAIL goes.
  (d) `OK: WebKitGTK 4.1`; `env -u DISPLAY -u WAYLAND_DISPLAY` → the
  desktop-session WARN, still exit 0. Cosmetic: the header reads `on linux,
  unknown` on this arm64 VM — libuv's cpus[0].model is "unknown" when
  /proc/cpuinfo has no model name (normal on ARM); printing the arch there
  would say more. *(2026-10-16, Linux arm64, Ubuntu GNOME VM)*
- [x] **Coloured labels** — in a terminal `OK:` green, `WARN:` yellow,
  `FAIL:` bold red, `NOTE:` dim, verdict line red/green; `tinyjs doctor |
  more` (or `> out.txt`) and `NO_COLOR=1` stay plain, with no stray escape
  codes. Windows: PowerShell in Windows Terminal AND the old conhost window
  (libuv should translate the codes there). macOS ✓ *(2026-10-16, via
  `script`)*.
  - [x] Windows — colours seen by the user in both Windows Terminal and
    the old Command Prompt (conhost) window *(2026-10-16)*. Plain
    output checked headlessly: `> file`, `| more` and `NO_COLOR=1` (also
    redirected) have no ESC byte, all 7 labels intact. NO_COLOR in a real
    terminal rests on the code (`tint` checks it before any colour).
  - [x] Linux — under a pty (`script -qec "tinyjs doctor"`), bytes
    checked: `OK` `ESC[32m`, `WARN` `ESC[33m`, `FAIL` `ESC[1;31m`, `NOTE`
    `ESC[2m`, verdict `ESC[1;31m` (1 problem) / `ESC[32m` (clean), each
    reset with `ESC[0m`. Zero ESC bytes with `NO_COLOR=1` or `TERM=dumb` in
    the pty, piped, and redirected to a file (all 9 labels intact).
    *(2026-10-16, arm64 GNOME VM)*
- [x] **Header shows the architecture** (follow-up to the Linux note
  above): `on linux arm64` with no `unknown` on the arm64 VM, and
  `on windows arm64, <cpu>` on Windows 11 arm64. macOS: `on macos arm64,
  Apple M3 Max` ✓ *(2026-10-16)*. Windows ✓ *(2026-10-16, after a fix)*:
  as first pushed it printed `on windows x86_64` on this arm64 machine,
  because bin\tjs.exe is x64 and Windows on ARM tells an emulated x64
  process `PROCESSOR_ARCHITECTURE=AMD64`. `PROCESSOR_IDENTIFIER` isn't
  rewritten (`ARMv8 (64-bit) Family 8 …` inside tjs), so doctor checks it
  too (and `PROCESSOR_ARCHITEW6432`). Now `on windows arm64, Apple
  Silicon` (the VM's CPU model). An x64 PC, simulated by env only (AMD64 +
  `Intel64 Family 6 …`), still reads `x86_64`, as does WOW64 x86 + AMD64.
  Not tried on real x64 hardware.
  Linux ✓ *(2026-10-16, arm64 GNOME VM)*: `tinyjs doctor` header reads
  `tinyjs dev (txiki.js 26.6.0) on linux arm64` — no `unknown`.

## `tinyjs update` retries, then falls back to curl (`fix/67-update-fetch`, 2026-10-15)

`fetchLatestVersion()` in cli.js: two native HEAD attempts (8 s each,
redirects followed by hand, http(s) only), then `curl -I -L`; a failure
names every attempt's error. The CLI runs on bare tjs, so bridge.js's fetch
repair shim never covered it. Per OS, from the checkout:
(a) `bin/tjs run test/update-fetch/check.js` — every case `ok`, tarpit
included (~32 s; it bounds the worst case); (b) 20× `update --check` against
real GitHub from a throwaway install (cli.js + a `VERSION` of an older tag):
20/20; (c) Windows: the curl it finds is `C:\Windows\System32\curl.exe`, and
case (a)'s "no curl on PATH" still prints a clean error.

- [x] **macOS** — (a) all six cases pass, tarpit gave up at 32.0 s; (b)
  20/20; the daily notice still prints and caches, and stays silent on
  failure. *(2026-10-15, macOS arm64)*
- [x] **Windows** — (a), (b), (c). *(2026-10-15, Windows 11 arm64.)* (a)
  `bin\tjs.exe run test/update-fetch/check.js`: all six cases `ok`; tarpit
  gave up at 32.2 s (fetch 8 s ×2, then curl's 15 s); no-curl case printed
  `curl: ENOENT: no such file or directory` in the clean error, exit 1. (b)
  throwaway install (cli.js + `VERSION` v0.56.0), 20× `update --check`
  against real GitHub from PowerShell: 20/20 "v0.57.2 is available",
  ~0.2 s each (native fetch, no fallback needed here). (c) the curl
  `tjs.spawn(['curl', …])` really runs, read off the child's pid, is
  `C:\WINDOWS\system32\curl.exe`. Note: from a Git Bash shell PATH puts
  Git's `/mingw64/bin/curl` first, so that's the curl there — not checked.
- [ ] **Windows, the reporter's network (#67)** — the real test, since the
  failure never reproduced here: ask for 30× `tinyjs update --check` on the
  build with the fix.
- [x] **Linux** — (a) all six cases pass, tarpit gave up at 32.1 s (36 s
  total); (b) 20/20 against real GitHub from a throwaway install
  (`VERSION` v0.57.1 → "v0.57.2 is available", exit 0). *(2026-10-15,
  Linux arm64, Ubuntu GNOME VM)*

## `win.open('main')` shows main, throws when windowless (`fix/win-open-main`, 2026-10-15)

`win.open('main', opts)` (backend `app.win.open` and page `tiny.win.open`)
now runs `app.show()`, which shows and focuses the main window and ignores
the opts. Under `mainWindow: false` it throws `there is no 'main' window …`.
Before this, macOS focused main, Linux ignored the call, Windows built a
half-wired secondary keyed "main", and windowless apps got silence. The
change is in the bridge, plus a Windows `do_winopen` guard that drops an
empty or "main" id the way macOS and Linux already did (only a raw
`WINOPEN main` sent around the bridge can reach it).

Per OS, with a scratch app: (a) main window: `app.win.hide({ app: false })`,
then `app.win.open('main', { page: 'nope.html', size: '10x10' })`; main
comes back visible + focused at its old size, `app.windows()` is still
`["main"]`, and no second window appears. (b) the same from a page
(`tiny.win.open('main')` from a secondary window) resolves and brings main
up. (c) `"mainWindow": false`: the call throws (page: rejects) with the
message, and `win.open('editor', …)` afterwards still opens.

- [x] **macOS** — (a) and (c) via `tinyjs dev` scratch apps: hidden main
  went `visible:false,focused:false` to `visible:true,focused:true` at
  960×640, windows `["main"]`; windowless threw, then `["editor"]`.
  *(2026-10-15)*
- [x] **macOS (b)** — main hidden, then a secondary page's
  `await tiny.win.open('main', { size: '10x10' })` resolved; main went
  `visible:false,focused:false` to `true,true`, still 960×640, windows
  `["main","sec"]`. *(2026-10-15)*
- [x] **Windows** — (a), (b), (c). Rebuild the launcher first (guard in
  `do_winopen`). Also confirm no stray "main"-titled window ever appears.
  *(2026-10-15, Windows 11 arm64, `tinyjs dev` rebuilt the launcher;
  scratch apps on a timeline, and every top-level window the launcher owned
  logged via EnumWindows every 200 ms.) (a) main went `visible:false,
  focused:false` to `true,true` (foreground), still 960×640, windows
  `["main"]`. (b) with `sec` open, the page's `tiny.win.open('main', {
  size: '10x10' })` resolved `true`; main came back focused at 960×640,
  windows `["main","sec"]`. (c) `app.win.open('main')` threw the message,
  `win.open('editor', …)` then opened (`["editor"]`), and the editor page's
  `tiny.win.open('main')` rejected with the same message. No window other
  than OpenMain / OpenMainSec / NoWinEditor (and the windowless app's
  hidden TinyjsHost) appeared at any point. Launcher guard on its own:
  with the bridge's `'main'` check removed for one run, the raw
  `WINOPEN main` was dropped (no new window, main stayed hidden); bridge
  restored after.*
- [x] **Linux** — (a), (b), (c) on the arm64 GNOME VM, Wayland and X11
  (`windowPlacement: true`), `tinyjs dev` scratch apps. (a) hidden main went
  `visible:false,focused:false` to `true,true` at 960×640, windows
  `["main"]`, no second window. (b) secondary page's `tiny.win.open('main',
  { size: '10x10' })` resolved, main back `true,true` at 960×640, windows
  `["main","sec"]`. (c) threw the "no 'main' window" message, then
  `["editor"]`. One X11 (b) run read `focused:false` at +1.5s; two reruns
  read `true` at +1.5s and +4.5s — XWayland focus lag, not the call.
  *(2026-10-15)*

## Backend `app` speaks the page's names; old ones warn (unreleased, 2026-10-14)

`app.menu.*`, `app.win.*` / `app.win.open`, `app.window(id).menu.*`,
`app.audio.filters/filter/balance/clear`, `app.notify(title, body, opts)`;
the old names log one deprecation line each. Behaviour changes riding along:
`app.window('main').close()` quits (was a no-op), `app.mouseTracking.start()`
throws on failure (was `{ ok: false }`), `audio.filter(i, patch)` merges
onto the slot (backend and `pageChain`), `promptMissing` uses `dialog.*`
(was calling non-existent `win.alert`/`win.confirm`). All bridge/tiny.js,
no launcher change — but the Wayland failure path and the dialog path were
never exercised.

- [x] **macOS** — scratch app driving every new and old name: new names
  work, each old name warned exactly once (`setMenu` called twice → one
  line), page calls never warned, `app.window('main').close()` quit, wire
  showed `filter(0, { gain: -3 })` → `AUDIOFILTERSET 0 60 1.1 -3` after
  `filters([{ freq: 60, q: 1.1, gain: 4 }])`. *(2026-10-14)*
- [x] **Linux (Wayland)** — `app.mouseTracking.start()` with the portal
  denied throws an Error with `.code === 'denied'`; `tiny.app.mouseTracking`
  still does too. `tiny.system.promptMissing()` with something actually
  missing shows its confirm dialog instead of rejecting. Seen on the arm64
  GNOME VM: Cancel on the ScreenCast dialog → backend and page both threw
  `screen share not granted` with `.code 'denied'`; Share (before the token
  was cleared) → backend resolved `{ ok: true }`. `promptMissing()` with
  plocate absent put up the confirm, resolved `{ missing: [spotlight.index,
  windowPosition], copied: true }`. *(2026-10-14)*
- [x] **Windows** — `app.window('main').close()` quits; `app.win.menu.set`
  draws main's own bar and `app.win.menu.reset()` puts the app bar back.
  *(2026-10-14, Windows 11 arm64, `tinyjs dev` (rebuilt the launcher for
  the new tiny.js); scratch app on a timeline, main's real bar read with
  GetMenu/GetMenuStringW every 250 ms. `app.menu.set` → `AppBar`;
  `app.win.menu.set` → `MainOwn`; `app.win.menu.reset()` → `AppBar` again;
  `app.window('main').close()` → window gone, dev exited 0, no
  launcher-win/tjs left running. Two runs, same result.)*
- [x] **macOS, second pass** — `app.win.close('second')` closed only that
  window, `app.win.close()` quit; backend `system.*` / `theme.get` /
  `info()` / `paths()` answered (old `app.paths.cache` read warned once);
  page `unwatch()` left the backend's `onClipboardChange` firing (one
  `CLIPWATCH 500` on the wire, no `0`); `filter(0, { type })` and
  `{ gainR }` resent `AUDIOFILTERS`; `clear()` sent `AUDIOBALANCE 0` first.
  *(2026-10-14)*
- [x] **Windows** — `app.macos.quickLook()` / `selectedText()`
  throw; `tiny.macos.ai.availability()` and `app.macos.ai.availability()`
  both resolve `'unsupported'`. *(2026-10-14, same run as above: backend
  threw "app.macos.X is macOS-only (this is windows) — guard with
  app.system.isMacOS()"; page `tiny.macos.quickLook()` / `selectedText()`
  rejected with the `tiny.` wording; both `ai.availability()` resolved
  `'unsupported'`. The page rejections are plain strings, not Errors,
  because that is how WebView2's `__invoke` binding rejects every page call
  — it was like that before this branch.)*
- [x] **Linux** — the same: quickLook / selectedText threw "macOS-only (this
  is linux) — guard with app.system.isMacOS()", both `ai.availability()`s
  resolved `'unsupported'`; `app.system.info()` → `{ os: 'linux', arch:
  'arm64', session: 'wayland', desktop: 'ubuntu:GNOME' }`. *(2026-10-14)*

## `dialog.saveFile({ name })` pre-fills the file name (#59, 2026-10-14)

The bridge now forwards `name` (basename only; any folder part is dropped,
since Windows' SetFileName would navigate to it) as the save arm's existing
second arg, the one the #36 backend panel already used. Linux's "start in
Downloads" moved from "has a name" to "is the backend's q<digits> panel", so
a page's named panel keeps GTK's usual folder.

- [x] **macOS** — `{ types: ['docx'], name: 'mydocument' }` shows
  `mydocument.docx`; `{ name: '../../etc/evil.txt' }` shows `evil.txt` in
  the last-used folder. *(seen by hand 2026-10-14)*
- [x] **Windows** — same two calls; the name is pre-filled, `.docx` is added
  on save, and the folder is NOT `../../etc`. *(2026-10-14, Windows 11
  arm64, `tinyjs dev` with a rebuilt launcher; the dialog's file-name field
  and address bar read via Win32, Save pressed with BM_CLICK on its own
  button.)* `{ types: ['docx'], name: 'mydocument' }`: field
  `mydocument.docx`, page got `…\Downloads\mydocument.docx`.
  `'../../etc/evil.txt'` → field `evil.txt`, and `'..\..\Windows\win.txt'`
  → `win.txt`; both in the last-used folder (Downloads), not steered.
  Without a name the Windows field is EMPTY (macOS says "Untitled"), so
  Save does nothing until one is typed — as before; the tiny.js comment
  that promised 'Untitled' now says so.
- [x] **Linux** — same two calls, plus a wrapped-page `printToPDF` outside
  the write roots still opens its panel in Downloads (#36 unchanged).
  *(2026-10-14, Ubuntu 24.04 arm64, `GDK_BACKEND=x11`, launcher rebuilt by
  `tinyjs dev`; a wrapped `http://127.0.0.1:8859` page with `dialog.*`,
  `win.*`, `store.*`, `app.paths` granted; panels photographed with xwd and
  seen by hand, Save clicked by hand. `{ types: ['docx'], name:
  'mydocument' }`: name `mydocument`, filter `*.docx`, opens in GTK's
  usual folder (dev's cwd), NOT Downloads; returned `…/mydocument.docx`.
  `{ name: '../../etc/evil.txt' }`: shows `evil.txt` in that same folder;
  returned `<cwd>/evil.txt`. `printToPDF('~/probe59-outside.pdf')`: panel
  opens in `~/Downloads` with `probe59-outside.pdf` filled in; Save wrote
  a real PDF there. Driving note: the launcher's XTest `keystroke('return')`
  can't answer these panels while a Wayland-native window (the terminal)
  holds focus — `_NET_ACTIVE_WINDOW` is 0x0 and mutter marks the modal
  DEMANDS_ATTENTION, so the key goes to the terminal. The #36 run got away
  with it; don't rely on it, and don't send keys you wouldn't want typed
  into the terminal.)*

## Windows accessory / `presence('menubar')` kept the tool-window caption (#56, 2026-10-14)

The taskbar button is now dropped by giving main a never-shown OWNER
(`set_taskbar_button`) instead of `WS_EX_TOOLWINDOW`, so the title bar stays
normal. With no button to minimize to, minimize HIDES main (title-bar button
via WM_SYSCOMMAND, and `win.minimize()`); `show` un-minimizes a window that
got minimized some other way. Written on macOS.

- [x] **Windows: compiles** — MinGW g++ 16.1, the setup.ps1 command; no new
  warnings under `-Wall`.
- [x] **Windows: accessory title bar** — `"activation": "accessory"`, show
  from the tray: icon, minimize, maximize and a normal close are all there;
  no taskbar button. ~~Not in Alt+Tab~~ — **it IS in Alt+Tab while shown**
  (seen by hand 2026-10-14): Alt+Tab lists the last visible popup of an
  invisible root owner, so the owner trick only drops the taskbar button.
  Only WS_EX_TOOLWINDOW keeps a window out of Alt+Tab, and that is #56's
  caption. Accepted: a showing accessory window can be Alt+Tabbed to;
  hidden, it isn't listed. **SEEN** (screenshot): full caption
  with icon + min/max/close, no taskbar button; Win32: owned (GW_OWNER =
  TinyjsTaskbarOwner), no WS_EX_TOOLWINDOW, MINIMIZEBOX/MAXIMIZEBOX kept.
  *(2026-10-14, Windows 11 arm64 26200 at 200%, `tinyjs dev` of a probe app driven from Python: tray clicks posted as the shell callback to main — with a main window the tray icon lives on main, not a TinyjsHost — and state read via Win32; screenshots.)*
- [x] **Windows: minimize hides** — the title-bar button and `win.minimize()`
  both hide; the tray's show brings it back restored, not as a caption strip.
  **SEEN:** WM_SYSCOMMAND SC_MINIMIZE (what the button sends) → hidden, not
  iconic; tray click → visible, 800x600 client, same position. Same for
  `app.minimize()`.
- [x] **Windows: Win+M then show** — whatever Win+M leaves, showing from the
  tray brings the window back restored. **Stand-in SEEN:** minimized from
  outside with ShowWindow(SW_MINIMIZE) (no WM_SYSCOMMAND, so not
  intercepted) it sits as a 314x50 caption strip at the bottom-left; a tray
  click brings it back restored (not iconic, 800x600 client). Real Win+M,
  by hand: it leaves the window alone (no state change logged) — Win+M
  minimizes taskbar-button windows, as it skipped the tool window before;
  the tray click then works as usual. The title-bar minimize by hand: hidden,
  and the tray brings it back at the same rect (twice).
- [x] **Windows: presence round-trip** — `presence('menubar')` drops the
  button with the caption unchanged; `presence('normal')` brings the button
  back, and minimize goes to the taskbar again. Re-run the app-surface page.
  **SEEN** in the probe and in `test/appsurface.html` (all results ok;
  screenshots in each hold): menubar → no button, owner set, caption
  unchanged; normal → button back, owner cleared, the title-bar minimize
  goes to the taskbar (iconic at -32000) and `show` restores it.

## Windows `setResizable(false)` grew the page +10x+10 (#57, 2026-10-14)

Flipping WS_THICKFRAME held the OUTER rect, so the thick-vs-thin border
difference went to the client. The op now goes through
`set_style_bits_keep_client`, like `chrome.frame` does. Written on macOS.

- [x] **Windows: compiles**
- [x] **Windows: page keeps its size** — declare 400x300, call
  `setResizable(false)` then `(true)`; `innerWidth`/`innerHeight` stay
  400x300 both times, and the edges stop/start resizing. **SEEN:**
  false/true twice: `innerWidth`×`innerHeight` 400x300 every time, client
  800x600 (200%), the outer rect shrinking 826x671 → 806x651 and back
  instead; THICKFRAME + MAXIMIZEBOX cleared and restored. *(2026-10-14, Windows 11 arm64 26200 at 200%, `tinyjs dev` of a probe app driven from Python: tray clicks posted as the shell callback to main — with a main window the tray icon lives on main, not a TinyjsHost — and state read via Win32; screenshots.)*

## Windows first load still blank, intermittently (#32 reopened, 2026-10-14)

Reported on v0.52.0 with `preact-ts` (issuecomment-6170185946): the trace
ends at `NAVQ n1 main http://127.0.0.1:5173/` → `NAVR n1 allow` with no
`NAV` after it, so the re-issue went out (or was due to) and no
NavigationStarting ever followed: WebView2 swallowed it even after the
cancel's completion. Fix: every re-issue arms a 1s verify; if nothing at all
has started in that window by then, it is sent again (4 tries total). A
retry re-stamps the one-use allow-once marker. It never adds a second use:
if the swallowed attempt does start late, that start is asked like any
other navigation (see the last item). Written on macOS; compiled and
verified on Windows the same day.

- [x] **Windows: compiles** — locally via `setup.ps1` (MinGW g++ 16.1),
  clean. *(2026-10-14; the first tagged CI build is still to come.)*
- [x] **Windows: dev server first load** — `preact-ts` and `react-ts`,
  `TINYJS_DEBUG=1 tinyjs dev` launched ~10 times: every run reaches
  `NAV … commit` and paints. *(2026-10-14: 10/10 each, every run a cold
  start with no browser process of ours left over. CDP read
  `http://127.0.0.1:5173/` with a rendered root every time. None of those
  runs needed the retry, so it was forced with a scratch build that drops
  the first re-issue (`TINYJS_TEST_SWALLOW`). The verify fired at exactly
  +1000 ms, the retry started without a second `NAVQ`, and the page
  painted. Re-run after the fix below, with the same result.)*
- [x] **Windows: no double ask** — wrapped site (`"url"`) first load,
  reload, and a link: exactly one `NAVQ` per navigation, as before.
  *(2026-10-14, real launcher, CDP-driven: `n1` first load, `n2`
  `location.reload()` (fresh document), `n3` link to `two.html`, each with
  `allow`/`start`/`finish`.)*
  Re-run after the fix below: `n1`/`n2`/`n3` again, and 5/5 cold
  dev-server starts each for `preact-ts` and `react-ts`.
- [x] **Windows: a retry no longer leaves a spare use behind** — the
  first cut gave each retry an extra marker use, so the dropped attempt
  could also pass if it started late. When it never started (the usual
  case), the spare use stayed live for the full 10 s, and a same-url
  navigation then passed with **no `NAVQ`**: with the swallow build, a
  `location.reload()` 93 ms after the retried load. A shorter spare
  lifetime would not help, because the url and the timing look exactly
  like a late start. So retries no longer add uses. *(2026-10-14, Windows,
  swallow build: the same reload now asks `NAVQ n2`, as the no-retry
  control does. Late-start variant (the dropped attempt fires 500 ms after
  the retry has loaded): it gets one ordinary `NAVQ n2` → `allow` →
  `start`/`finish`, and the page stays loaded. That is one extra ask in
  a rare case, instead of a skipped one.)*

## Bundled data resources (`resources/`)

Reproducible checks: `python3 -u test/resources/check.py` and
`tsc --noEmit --strict --skipLibCheck --lib ES2022,DOM test/resources/types.ts`.

Reworked 2026-10-13 from #49's project-relative list to the `resources/`
folder (whole folder by default, list narrows it). The 2026-10-11 runs below
were of the old layout; the CLI paths changed, so each OS needs a re-run.

- [x] macOS 27.0 arm64, 2026-10-13: `check.py` PASS on the folder layout —
  dev, bare executable and relocated .app backend/frontend reads, whole
  folder by default, list narrowing + per-OS override, list without folder,
  folder symlink, URL wrapper, null without resources, rebuild cleanup,
  publish archive, signature.
- [~] Windows 11 arm64 (26200), 2026-10-13, Python 3.13: `check.py` PASS
  with two fixture names swapped — dev and portable backend/frontend RPC,
  relocated dist in a `moved build ü` folder, binary bytes, path rejection,
  platform override, rebuild cleanup, publish zip bytes, URL wrapper, whole
  folder, null. Symlinks SKIP (no Developer Mode); junctions checked by
  hand instead — `resources/link` and `resources/` itself as `mklink /J`
  both refused "symlinks are not supported". The swaps, and why the stock
  script can't pass here yet:
  - project folder `project with spaces ü` → ASCII: **tjs.exe's argv is
    ANSI** (upstream txiki `main`), so `tjs run <path with ü>` gets `�` and
    ENOENTs. `tinyjs dev` passes an absolute entry path, and `tinyjs.cmd`
    passes `%~dp0cli.js` — so a user whose profile is `C:\Users\Jürgen`
    can't run the CLI at all. Pre-existing, not resources-specific. Fix
    candidates: an `activeCodePage` UTF-8 manifest stamped onto tjs.exe at
    setup (compiled apps inherit it), or upstream `wmain`.
  - fixture `данные` → `données`: **Windows' bsdtar 3.8.8 segfaults
    creating any archive holding a name outside the ANSI codepage**
    (0xC0000005; `ü` is fine), and skips such entries extracting (exit 1).
    So `publish` crashes and the updater would refuse those files.
    Pre-existing; resources make non-ASCII file names likelier.
  Fixed on the way: `publish` and the updater now call
  `%SystemRoot%\System32\tar.exe` — from Git Bash, PATH found GNU tar,
  which ignores `-a` and wrote a POSIX tar named `-win.zip`. check.py now
  re-execs in UTF-8 mode on Windows and compares paths separator-blind
  (packaged roots are `C:\…\dir/resources`, like the other `app.paths`).
- [x] Linux x86_64, 2026-10-13: `check.py` PASS on the folder layout in an
  Ubuntu 22.04 amd64 container (Rosetta, on the arm64 VM) under Xvfb +
  `dbus-run-session`, command in `test/resources/README.md`, against a
  fresh clone of `dad7102` built by its own `setup.sh`. The first attempt
  "failed" on the last launch with an empty log: that was the host's
  `pgrep -x launcher-linux` cleanup loop (other checks running at the same
  time), which sees and kills processes inside docker containers. The re-run,
  with nothing else killing launchers, passed every step.
- [x] Linux arm64 + Wayland, 2026-10-13: `check.py` PASS on Ubuntu 24.04
  aarch64, GNOME Wayland session (GDK's default backend, no `GDK_BACKEND`).

## Bundled executables (`executables/<os>/`, #8)

Reproducible checks: `python3 -u test/executables/check.py` and
`tsc --noEmit --strict --skipLibCheck --lib ES2022,DOM test/executables/types.ts`
(README in that folder). Built 2026-10-14 on macOS; the Windows/Linux paths
(`dist/executables/`, PE/ELF checks, dev copies on Windows instead of links,
the Linux publish `chmod -R +x`) were written there and never run.

- [x] macOS 27.0 arm64, 2026-10-14: `check.py` PASS (root + per-OS/arch
  overrides + `"executables"` lists), ad-hoc and with
  `TINYJS_TEST_SIGN_IDENTITY` (Developer ID): Helpers signed with the
  hardened runtime, a bun-compiled sidecar dies without `allow-jit` and runs
  with the documented entitlements, bundle verifies.
- [x] macOS notarization of an app with a sidecar (`tinyjs notarize`) —
  Apple accepts it and the stapled app spawns the sidecar. *2026-10-14,
  macOS 27.0 arm64: an app with a universal C sidecar and a `bun build
  --compile` sidecar (JIT, `executableEntitlements` with allow-jit & co.)
  — `Accepted`, notary log `issues: None`, both Helpers in the ticket.
  A ditto-zipped copy given a quarantine xattr: `spctl -a -t exec` says
  "accepted, source=Notarized Developer ID"; `open` showed the "downloaded
  from the Internet… Apple checked it" prompt, ran translocated
  (AppTranslocation path in app.paths.executables) and both sidecars
  printed. Not seen on a second Mac — same Gatekeeper path, though.*
- [x] macOS `--universal` / `--arch x86_64` builds skip / pick
  `macos-x86_64/` and the slice warning holds. *2026-10-14, same app:
  default build ships the root arm64 `jit`; `--universal` keeps it and
  warns "no x86_64 slice — spawning it fails on Intel Macs"; `--arch
  x86_64` picks `macos-x86_64/jit` (lipo: x86_64), the bundle verifies, and
  under Rosetta both sidecars print x86_64 (`hello x86_64`, `jit x64`). The
  Intel spawn failure itself can't happen on this Mac — not seen.*
- [x] Windows 11 arm64 (26200), 2026-10-14, Python 3.13: `check.py` PASS —
  dev copies, PE check, overrides, lists, `dist/executables/`, publish zip,
  spawn from a relocated dist, null cases. Symlinks SKIP (no Developer
  Mode); junctions by hand instead: one inside `executables/windows/`,
  `windows/` itself and `executables/` itself all fail the build with
  "symlinks are not supported". Two test fixes on the way: the build's CPU
  folder is `tjs.exe`'s (x64 here, emulated — `windows-x86_64/` is right;
  the script had taken arm64 from Python), and `launcher.exe` outlives the
  backend by a moment, keeping the dist locked, so `launch()` now waits
  until the folder can be renamed.
  Sidecar with a `.dll` beside it (MinGW-built `dllcar.exe` importing
  `helper.dll`, both in `executables/windows/`): runs in dev and from a
  relocated dist; with the DLL deleted it dies 0xC0000135, so the pass is
  real.
- [x] Windows: auto-update installs a new sidecar. 2026-10-14, a local
  manifest server, 1.0.0 → 1.0.1 where 1.0.1 swaps `helper.dll` (42 → 43),
  adds `new.exe`, drops `gone.exe`; 1.0.0 keeps a sidecar running (exe +
  DLL loaded) through the install. The relaunched 1.0.1 spawned the sidecar
  and got "dll says 43"; `new.exe` arrived. Two leftovers, both from the
  Windows file-by-file swap (not executables-specific — `resources/` has
  the same shape):
  - **A file dropped from the new version stayed** (`gone.exe` was still in
    the install). The walk only adds and replaces; macOS replaces the whole
    .app and Linux the whole folder, so only Windows kept stale files.
    **Fixed same day:** `winSwapDir` now prunes `executables/` and
    `resources/` first (stale files renamed aside through the same journal,
    dropped folders removed after). Re-run: `gone.exe`, `resources/sub/…`
    and, in a second run, a whole dropped `resources/` are gone; the Shelf
    marker and a stray top-level file stay. Rollback: with the app exe held
    open without delete-sharing the swap fails "current version is intact"
    and the tree (gone.exe included) is identical to before.
  - **An update that adds the first sidecar**: 1.0.0 with no
    `executables/` (`app.paths.executables` null) → 1.0.1 adding `new.exe` +
    `helper.dll`; the relaunched 1.0.1 spawned `new.exe` → "dll says 43".
    Its forced rollback left an EMPTY `executables/` behind (the walk made
    the folder, rollback never removed it — harmless, null is baked in at
    build, but not identical). Fixed: the walk journals folders it creates
    and rollback removes them; both scenarios re-run, trees identical.
  - The running sidecar's `dllcar.exe.update-old` / `helper.dll.update-old`
    stay in `executables/` until the next update's sweep (as the top-level
    exes always have), visible to an app that lists the folder.
- [x] Linux x86_64 (22.04 container) and arm64: `check.py` PASS — ELF and
  `#!` scripts, exec bit in `dist/` and the publish tarball. *2026-10-14:
  arm64 on the Ubuntu 24.04 VM under GNOME Wayland (launcher current);
  x86_64 in an `ubuntu:22.04` amd64 container (Rosetta) from a fresh clone of
  `fe7bf71` built by its own `setup.sh` (`file`: both x86-64), under Xvfb +
  `dbus-run-session`, same command as the resources README. check.py had no
  `#!` case on Linux (only macOS's rejection), so one was added: a
  `#!/bin/sh` script checked in mode 644 gets the build's "isn't executable"
  note, is executable in `dist/executables/`, runs, and keeps +x in the
  publish tarball. Proven able to fail: with both build chmods
  (`copyExecutables`, `stageExecutables`) commented out it asserts — either
  one alone is enough to pass.*

## App assets (`assets/`), help book, Help menu

- [x] macOS 27.0 arm64, 2026-10-13, Developer ID build of a `tinyjs new`
  app with #47's sample book: `assets/icon.png` → AppIcon.icns; Credits.rtf
  in Resources; book copied to Resources, `CFBundleHelpBookFolder` /
  `CFBundleHelpBookName` (= the book's `CFBundleIdentifier`) and
  `CFBundleShortVersionString` in Info.plist; `hiutil` rebuilt the index;
  `codesign --verify --deep --strict` OK; Help menu appears last in the bar
  (screenshot). Unused `assets/logo.svg` noted by the build.
- [x] macOS 27.0, 2026-10-13, hand-tested in the built app: "<App> Help" and
  ⌘? open the book in Help Viewer; the Help search field finds the book's
  pages; About shows the Credits.rtf text; a `{ role: 'help', items }` item
  sits below "<App> Help".
- [x] macOS 2026-10-13: in `tinyjs dev` (no book) a `{ role: 'help' }`
  declared FIRST still lands last in the bar, after Develop (screenshot).
- [x] macOS 2026-10-13, hand-tested: a `{ role: 'help' }` item's click
  reaches `tiny.menu.on`; `standard: false` leaves just the search field and
  the app's items (no "<App> Help"); About shows `assets/Credits.html` with a
  working link and "Version x.y.z" from `CFBundleShortVersionString`;
  `resources/hello.txt` read from the .app (backend and page agree on the
  root, which followed the .app to a new folder).
- [x] macOS 2026-10-13: `tinyjs notarize` of a Developer ID app carrying a
  help book AND `resources/` — Accepted, stapled, `spctl` accepts it
  (Notarized Developer ID).
- [x] macOS 2026-10-13: Help Viewer caches the book — an edited page in a
  rebuild at the SAME version (even from a different .app path) showed the
  old copy; bumping `version` showed the edit.
- [x] Windows 11 arm64, 2026-10-13, dev AND built: a menu declared File,
  `{ role: 'help', items }`, View reads `File | Help | View` (GetMenu, and
  screenshot); WM_COMMAND on both Help items reached the backend's `onMenu`
  (`site`, `issue`). A solid-magenta `assets/icon.png` is the window's big
  and small icon (WM_GETICON — what the taskbar shows) in dev and built,
  and `helpcheck.exe` + `launcher.exe` carry it (ExtractIconEx, 32/16 px).
  No `assets/`, green root `icon.png`: green in dev, built window and exe.
- [x] Linux arm64, 2026-10-13, dev AND built, under XWayland
  (`GDK_BACKEND=x11`, so xwd and XTest work): a menu declared File,
  `{ role: 'help', items }`, View reads `File | Help | View` (xwd of the
  window); the Help popup lists Website and "Report Issue  Shift+Ctrl+H".
  Both items reached the backend's `onMenu`: `issue` via its accelerator,
  `site` via XTest F10 → Right → Down → Return. A solid-magenta
  `assets/icon.png` is the window's `_NET_WM_ICON` in dev and built, is
  copied to `dist/icon.png`, and the first-run `.desktop` has
  `Icon=<dist>/icon.png` (and `StartupWMClass` = id). No `assets/`, green
  root `icon.png`: green `_NET_WM_ICON` in dev and built, green `dist/icon.png`.
  **Found and fixed on the way:** every Linux menu bar and tray item is a
  `GtkCheckMenuItem`, and GTK's class handler toggles it on activate. So
  "Report Issue" showed a ✓ after its accelerator fired, and a `checked`
  item lost its ✓ on click, whatever the app had set. `on_menu_item_activate`
  now puts the widget back to the registry's `checked`. Re-checked with an
  xwd of the popup: no ✓ on a plain item after activation, a `checked:true`
  item keeps its ✓ after activation, and an item the app unticks via
  `updateMenuItem` in `onMenu` stays unticked through a second press.
- [x] macOS 2026-10-13: vanilla `tinyjs new` and `tinyjs wrap` scaffold
  `assets/icon.png` and build from it; a project with `icon.png` and
  `Credits.html` at the root (no `assets/`) still builds with both.
- [x] `tinyjs new --template …` (vite) scaffolds `assets/icon.png`; `wrap
  --force` on a pre-assets wrap keeps its root icon; both on Windows/Linux.
  Windows 2026-10-13: `new vt --template vanilla-ts --no-install` →
  `vt/assets/icon.png`; `wrap` of a local page → `assets/icon.png`; with
  that moved to a (green) root `icon.png` and `assets/` deleted, `wrap
  --force` against a page with no favicon left `icon.png` byte-identical
  and made no `assets/`. Linux arm64 2026-10-13: the same three
  (`new vt --template vanilla-ts --no-install` → `vt/assets/icon.png`, run
  in the arm64 node container since this VM has no host Node; `wrap
  http://127.0.0.1:8765/` → `assets/icon.png`; root green `icon.png` and no
  `assets/`, then `wrap --force` → `icon.png` md5 unchanged, no `assets/`).
  Also seen on Linux (container, no TTY): `tinyjs new … --template react-ts`
  or `vanilla-ts` WITH install finishes its work and prints "created …",
  then **tjs segfaults on exit (139)**. With `--no-install` it exits 0. Not
  investigated; with a TTY, `new` stops at its prompts instead.

## How to run the app-surface checks

```sh
cd <any tinyjs app dir>
TINYJS_HTML=/abs/path/to/tinyjs/test/appsurface.html tinyjs dev
```

Self-driving: it steps through badge → progress → attention → icon →
presence, holding each state for a few seconds, prints `SURFACE RESULTS`, and
quits after ~14s. It also prints what `capabilities()` claims for this
machine, so a blank Dock next to `badge=true` is a real bug and next to
`badge=false` is honest degradation.

On macOS run the **dev checkout's** `./tinyjs` — a `tinyjs` on PATH may be an
installed release whose client predates these calls, in which case the page
throws on its first line and the window just flashes.

~~Two checks in that page are structurally unable to pass on Windows~~ — **fixed
in the page, 2026-07-28.** It used to call `attention()` while the page was the
foreground window (where `FLASHW_TIMERNOFG` is a documented no-op), and to call
`presence('normal')` ~0.2s before quitting, so a restored taskbar button and a
departed app looked identical. The page now minimizes itself before
`attention()`, holds ~6s so several flash cycles are observable, restores, and
holds ~4s after `presence('normal')`. It runs ~27s rather than ~14s as a
result. Both were then verified on Windows — see the Windows notes below.

## app surface — badge / attention / icon / progress / presence

Added 2026-07-25 (the dock→intent-verb rename + `progress`).

| check | macOS | Windows | Linux |
| --- | --- | --- | --- |
| `badge('3')` shows a count | ✅ seen | ✅ seen — red disc, white '3' (2026-07-28) | ✅ seen — Tarwin, 2026-08-01 |
| `badge('NEW')` (non-numeric) | ✅ arbitrary text | ✅ collapses to a bullet (2026-07-28) | 🔶 hides: dbus shows count-visible=false (2026-07-28) |
| `attention()` | ✅ bounces | ✅ flashes the taskbar button | ✅ X11 urgency bit / ❌ Wayland |
| `icon(png)` replaces the icon | ✅ seen | ✅ seen (fixed 2026-07-25) | ✅ X11 (fixed 2026-07-26) / ❌ Wayland |
| `icon(ico)` replaces the icon | n/a | ✅ seen | n/a |
| `icon` reaches the taskbar button | ✅ Dock icon | ❌ **title bar + Alt-Tab only** | ⬜ dock uses the .desktop icon |
| `icon('')` restores | ✅ seen | ✅ back to icon.png (fixed 2026-07-25) | ✅ byte-identical restore |
| `progress(0..1)` draws a bar | ✅ seen | ✅ seen — 45% then 90% | ✅ seen — Tarwin, 2026-08-01 |
| `progress` + `icon` compose | ✅ seen — the macOS-specific risk | n/a | n/a |
| `badge` + `progress` compose | n/a | ✅ seen together (2026-07-28) | ✅ one signal carries both — seen 2026-08-01 |
| `progress(null)` clears | ✅ seen | ✅ bar goes, button stays | 🔶 signal correct, clearing unseen |
| `presence('menubar')` hides it | ✅ seen | ✅ button vanishes | ✅ X11 skip-taskbar / ❌ Wayland |
| `presence('normal')` restores | ✅ seen | ✅ button comes back | ✅ X11 / ❌ Wayland |

Linux column checked 2026-07-26 on Ubuntu 24.04 aarch64, GNOME 46, in **both**
a native Wayland session and XWayland (`GDK_BACKEND=x11`), against a launcher
rebuilt from source. Nothing was watched with eyes — GNOME's screenshot and
`Shell.Introspect` D-Bus APIs are both locked to portal callers on this box —
so each row was settled at the protocol layer instead: `xprop` for the X11
properties, `WAYLAND_DEBUG=1` for Wayland traffic, `dbus-monitor` for the
LauncherEntry signal. 🔶 means the wire side is proven and only the pixels are
unconfirmed.

**Three of the five verbs are X11-only, and now say so.** Under a Wayland
session `app.icon`, `app.attention` and `app.presence` emitted **zero bytes**
of Wayland protocol — GTK3's Wayland backend has nowhere to put a window icon,
an urgency bit or a skip-taskbar hint, so all three were silent no-ops while
`capabilities()` claimed `icon=true presence=true` (and `attention` wasn't in
the table at all, which the "absent = true" rule reads as supported). This is
the same trap as the Windows `nowPlaying`/`haptic` finding. Fixed by gating all
three on `ON_X11`; the page now prints `icon=false presence=false` on Wayland
and `true` on XWayland.

**`app.icon` never worked on Linux at all, on either session** — fixed
2026-07-26. `gtk_window_set_icon_from_file()` hands GDK the image at its
natural size, and GDK only publishes `_NET_WM_ICON` — the property shells
actually read — while it fits X11's per-request limit. Measured by bisecting
sizes against a plain GTK3 control program: **256×256 lands, 512×512 is
dropped**, leaving only the legacy `WM_HINTS` icon pixmap that nothing reads.
Every tinyjs app ships a 1024×1024 `icon.png`, so this silently swallowed both
`app.icon()` *and the startup window icon* for every app ever shipped. The
launcher now scales to an icon list (256/128/64/48/32, never upscaling).
Verified by checksumming the property: baseline → a red test icon changes it,
and `icon('')` restores the original **byte-identically**.

**`attention()` latched forever** — fixed alongside. Nothing ever cleared the
urgency hint, and Mutter does not clear it on focus (measured: still set while
`_NET_WM_STATE_FOCUSED`), so one `attention()` left the window demanding
attention for the rest of the run, unlike the transient macOS bounce and
Windows flash. A `focus-in-event` handler now drops it; verified across a
minimize/restore cycle (0 → 1 → 0).

**The `.desktop` id question is closed — the ids match.** The bridge writes
`~/.local/share/applications/<app id>.desktop` and the launcher addresses
`application://<app id>.desktop`; a built test app produced
`app.tinyjs.surfacetest.desktop` on disk and that exact string on the bus.
But note **`tinyjs dev` registers no `.desktop` at all** (registration is gated
on `bundlePath()`), so in dev the badge/progress signal is addressed to an
entry the shell has never heard of. Badge/progress can only be seen from a
**built** app — don't read a blank dock in dev as a bug.

**Capability detection for badge/progress is now a probe, not a guess.** A dock
implementing LauncherEntry takes the `com.canonical.Unity` bus name (it is what
libunity itself looks for), and it is owned on this box — so `capabilities()`
asks the bus via the existing `busNameOwned()` helper instead of regexing
`XDG_CURRENT_DESKTOP`, which was wrong in both directions: false for plain
GNOME with Dash-to-Dock added, true for an Ubuntu session with the dock
removed. The old guess survives only as the fallback where there is no `gdbus`.

**Ubuntu Dock really does draw them — Tarwin, 2026-08-01.** The badge and the
progress bar both appear on the dock icon, so the LauncherEntry signal this
whole column was settled at the protocol layer actually lands somewhere. The
🔶 rows above are now ✅. Still unwatched, and both are narrow: `progress(null)`
*clearing* the bar (only the outgoing signal is proven), and `badge('NEW')`
hiding rather than drawing something wrong — dbus says `count-visible=false`,
which is the right instruction, but nobody has looked at the icon while it is
in force.

Windows column checked 2026-07-25 on Windows 11 Pro 26200, launcher rebuilt
from current source first — the checked-in `launcher-win.exe` was three days
stale and predated the intent-verb rename, so testing it would have proved
nothing. `tinyjs build` now runs the same `ensureLauncherFresh()` guard `dev`
had (2026-07-26), so a dev checkout can no longer ship a stale `launcher.exe`
— or, worse, silently skip the app icon, since `--embed-icon` is executed *by*
that binary and a copy predating the flag just exits non-zero. That failure is
no longer swallowed: the build warns and passes the launcher's own reason
through. `capabilities()` reported `badge=false icon=true presence=true
progress=true`, and no badge was drawn — honest degradation, as intended.

**Re-run 2026-07-28 against a launcher rebuilt from current source** (the local
`launcher-win.exe` — a gitignored build artifact, so a fresh clone has none and
a stale one is invisible to `git status` — was two days older than
`launcher-win.cc`. Rebuild FIRST, always: `powershell -ExecutionPolicy Bypass
-File setup.ps1 -SkipPath`).
`badge` now draws, so the table above and `capabilities()` both changed; the
rest of this section still holds.

**`app.badge` works on Windows — seen, not merely compiled.** A red disc with a
white `3` in the corner of the taskbar button. None of the three predicted
failure modes happened: not invisible (the alpha fix in `badge_icon()` does its
job), not a black square, and `SetOverlayIcon` DID get a taskbar button from a
plain `tinyjs dev` spawn. `badge('NEW')` collapses to a centred bullet exactly
as documented (16px fits 1–2 glyphs), `badge` and `progress` compose on the same
button, and clearing both returns the strip to a pixel-identical baseline.
`capabilities().badge` is therefore now `true` on Windows.

Photographed rather than eyeballed, since the decorations live outside the app
window: a page holding each state ~5s, the taskbar strip captured every 500ms,
and frames diffed. **Diff against a frame from INSIDE the app's run, not one
from before launch** — Windows 11 centres taskbar buttons, so a new button
shifts the whole strip, and a diff-vs-before-launch is ~33k changed pixels of
layout shift that drowns a 770-pixel badge completely. That mistake made the
first pass read as "nothing drew".

**`attention()` and `presence()` re-verified with the amended page.** With the
window minimized, consecutive-frame diffs oscillate between ~0 and ~4500 changed
pixels for about 4.7s — the flash — and the button then settles into a held red
attention highlight (mean brightness 188.6 vs 198.2 idle) rather than reverting,
which a single frame could never have shown. Restoring the window clears it.
`presence('menubar')` takes the button away (the strip reads the same brightness
as no-button-at-all) and `presence('normal')` brings it back and HOLDS for 6.5s
before quit.

**Windows `presence` was the highest-risk item — it is fine.** The handler
matches `presence ` and parses `op.substr(9)`, which lines up, and both
directions were watched: the taskbar button disappears on `'menubar'` and
returns on `'normal'`. The old-name/offset worry is closed.

**`attention()` needs the window to not be foreground.** `do_attention` passes
`FLASHW_TIMERNOFG`, which is defined to do nothing while the target window is
already in front — so a test page that flashes itself while focused proves
nothing. Verified two ways with the window minimized (PowerShell-minimized and
`tiny.win.minimize()`), against a direct-`FlashWindowEx` control on the same
window to prove the taskbar flashes on this machine at all.

**`app.icon` does not touch the Windows taskbar button.** `WM_SETICON` demonstrably
lands — `WM_GETICON` returns new handles the instant the call is made, and the
**title bar** icon visibly changes — but the taskbar button keeps showing
launcher-win.exe's own embedded icon and never updates. So the comment above
`do_appicon` ("the window icon, which is what the taskbar button shows") is
wrong on Windows 11; what `app.icon` really controls there is the title bar and
Alt-Tab. Whether the taskbar button can be redirected at all is unresolved —
built apps additionally get an AppUserModelID + `RelaunchIconResource` via
`apply_relaunch_props`, which dev spawns skip, and that was not tested.

**Linux badge/progress ride one DBus signal** — `com.canonical.Unity.
LauncherEntry`, addressed as `application://<app_id>.desktop`. KDE Plasma,
Ubuntu Dock and Dash-to-Dock listen; **vanilla GNOME Shell does not**, and
there is nothing to detect and no error when nobody is listening. Both the
detection and the `.desktop` id worry are settled — see the Linux notes under
the table above.

## macOS — `setSize` → `getState` on a TITLED window: **was broken, fixed**

Answered 2026-07-26. The suspicion was right, but only for **satellites**.

- **Main window: no drift.** It goes through `webview_set_size`, which speaks
  frame units, and `getState` reports `win.frame` — so 600×400 came back
  600×400 and stayed there over three read-modify-write passes.
- **Titled satellite: +32px per pass.** `win.open` windows took the other
  branch, `[win setContentSize:]`, against the same frame-reporting
  `getState`. Measured: asked 600×400 → reported 600×432 → 464 → 496 → 528,
  i.e. exactly the title bar each time. Borderless windows are unaffected
  (content == frame), which is why it never bit.

Fixed the way the Windows twin was: the satellite branch sets the FRAME size
(anchoring the top-left, as users expect), and window *creation* still takes a
content size — no feedback loop there, so nothing to ratchet. Re-measured
after the fix: `setSize 600x400 -> 600x400`, 0px drift over three passes.
Creation still reports 600×432 by design, matching Windows.

## Windows — `win.startDrag` / `win.startResize` gestures need a real mouse

Implemented 2026-07-25 (`do_ncdrag`), NOT yet watched. Both fake a non-client
button press — `WM_NCLBUTTONDOWN` with `HTCAPTION` for a drag, the matching
`HT*` edge code for a resize — and that hands control to a modal OS loop that
only ends when a real mouse button is released. So this cannot be driven
headlessly: synthesising the message with no button down risks wedging the UI
thread rather than proving anything. It needs hands on a mouse.

What was wrong before: `DRAGWIN` was matched with `line == "DRAGWIN"`, exact,
so the per-window `DRAGWIN@<id>` form the bridge sends for satellites was
dropped — a satellite's drag handle did nothing. `RESIZEWIN` was never handled
at all, and tiny.js gives **every frameless window** invisible resize grips
that call it, so custom grips were dead on Windows.

The parenthetical that used to close that paragraph — "native `WS_THICKFRAME`
borders still worked, which is probably why nobody noticed" — was the mistake.
It holds for the MAIN window only. A frameless SECONDARY answers
`WM_NCCALCSIZE` with no non-client area and then hands WebView2 the whole rect,
so its child HWND owns every edge pixel, `WindowFromPoint` never resolves to
us, and `WM_NCHITTEST` is never asked. Frameless satellites had **no grabbable
edge at all** on Windows, and the grips that would have covered for that were
gated on `__TINY_FRAMELESS`, which only the Linux launcher injected. Fixed
2026-07-28: `sec_shim_js` now injects the marker for frameless secondaries
(main deliberately stays unmarked — it has real side/bottom borders), and
`do_ncdrag` grew the guards below.

To check: a frameless window, drag it by a `data-tiny-drag` region and by a
satellite's own handle; then drag each of the eight edge grips and confirm the
right edge moves (a wrong `HT*` mapping resizes the opposite side).

- [x] All eight edges/corners of a frameless satellite, each moving the edge
      grabbed. Automated as far as it goes: a probe app confirms the marker is
      a boolean and all 8 grips mount on a frameless secondary, mount on
      neither a titled secondary nor main. The dragging itself needs a hand —
      watched on Windows 2026-07-28 in amp, edge resize working.
**Three of the four closed 2026-08-06 — Tarwin on the mouse, everything else
measured.** A frameless satellite sampled its own `getState` at 20Hz while the
backend pushed a 250ms heartbeat, so each verdict is a number rather than an
impression. Rig: `scratchpad/dragkit`.

- [x] **Click an edge without moving** — press and release in place. The window
      must NOT follow the pointer afterwards. `xxxMoveSize` entered with the
      button already up tracks until the next click; the trip from the grip's
      `mousedown` to `do_ncdrag` is two process hops, so this is easy to hit.
      Guarded with `GetAsyncKeyState(VK_LBUTTON)`, mirroring macOS.
      **SEEN: after the grip press the pointer travelled 4470px and the window
      moved 1px over 16.6s** (297 samples, one grip press registered). The
      guard holds.
      The first attempt of this one produced a bogus FAIL (215px) and the
      lesson generalises: it started measuring when the window OPENED, and the
      window opened flush against the left screen edge, so moving it somewhere
      clickable was counted as the window following the pointer. The rig now
      places itself away from the edge and starts the clock only when it sees a
      grip pressed — a capture-phase `mousedown`, since the grips
      `stopPropagation` in their own listener and a bubbling listener never
      sees them. It also counts POINTER travel, so a pass reads "pointer moved
      4470px, window moved 1px" instead of a bare "0px" that a motionless mouse
      would also produce.
- [x] `setResizable(false)` then drag an edge — must do nothing (the style bit
      is read live, because tiny.js gates its grips on one `getState` at load).
      **SEEN: 0px** while it was leaned on. The control matters as much: with
      `setResizable(true)` again, the same edge moved **221px** — without that,
      a 0 is equally consistent with the grips being dead all along.
- [x] Alive mid-drag: hold an edge and confirm posted work still lands (the
      modal loop pumps `WM_APP`, so dispatch keeps running inside it).
      **SEEN: 42 heartbeats over 10.8s** — the backend's 250ms tick, arriving
      uninterrupted — while the window resized 265px. Dispatch is not starved
      by the modal resize loop.
- [ ] A monitor LEFT of primary (negative screen x) — exercises the
      `MAKELPARAM` sign path in the point now passed as `lParam`. **Blocked on
      hardware, not on effort**: this box has one display
      (`{X=0,Y=0,1728x1084}`), so there are no negative screen coordinates to
      be had. Needs a second Parallels display placed to the LEFT of primary.

## ~~macOS — `startDrag` / `startResize` / `dragOut`~~ — done 2026-07-26

- [x] `win.startResize` — all eight edges dragged by hand on the deck's
      App ▸ Frameless card. AppKit has nothing to hand the gesture to (unlike
      `WM_NCLBUTTONDOWN` and `gtk_window_begin_resize_drag`), so `do_resizewin`
      runs its own tracking loop and the arithmetic was ours to get wrong —
      opposite edge anchored, `setMinSize` clamp holding rather than walking,
      `setResizable(false)` refusing. Guards checked headlessly too: called
      with no button held it returns in ~1 ms and leaves the app alive.
- [x] `win.startDrag` — the grab strip on the same card. The AppKit
      `performWindowDragWithEvent:` path had been in use via `data-tiny-drag`
      all along; the new `DRAGWIN@<id>` satellite form is what got watched.
- [x] `win.dragOut` — a file dragged out of Storage ▸ Files onto Finder.
      Can't be observed headlessly at all: the session finishes in *another
      application*.

- [x] **2026-10-25, #92 fix**: `do_dragwin` and `do_dragout` now hand the
      cached mouse-down (from a `sendEvent:` hook) to
      `performWindowDragWithEvent:` / `beginDraggingSessionWithItems:` while
      the button is held, via one `live_press()` helper. Verified with
      synthetic gestures (`test/drag-race/`: scrollwheel-poisoned
      `currentEvent`, old launcher 0 px / new 240 px; guards and aliveness
      re-checked) — but the original symptom was REAL-TRACKPAD-only
      (pressure/touch/gesture interleave, which CGEvent cannot inject), so
      it stays open until someone drags a frameless app by a
      `data-tiny-drag` strip with a finger and it feels native.
      `do_dragout` has no synthetic case — dropping real files needs a
      target application — so drag a file out of a tinyjs window into
      Finder/Slack with a trackpad too: it went from "no-ops when the race
      hits" to "uses the cached press", which no harness here exercises.
      **Verified 2026-10-25 on macOS (arm64) by hand with a trackpad**:
      window drag by a `data-tiny-drag` bar engages at once and feels native,
      and `startDrag({ files })` drops a file into Finder. Also: the
      first cut never cleared the cached press, and the button mask is
      system-wide, so after one click a page calling `startDrag()` dragged
      the window along with a drag in ANOTHER app (probe: 590 px). The press
      is now cleared on mouse-up and must belong to the target window;
      synthetic probe 0 px, confirmed by hand too.

Still open on the other two: the Windows section above, and Linux has no drag
source whatsoever — `DRAGOUT` is unhandled in `launcher-linux.cc` (tracked in
TODO-linux.md).

## Windows-only

- [x] ~~`app.badge`~~ — **SEEN on Windows 2026-07-28**, and `capabilities()`
      now reports `badge:true` there. `do_badge` renders an overlay HICON
      (`badge_icon()`) and hands it to `ITaskbarList3::SetOverlayIcon`.
      The trap it works around: GDI writes RGB but leaves the alpha byte of a
      32bpp DIB alone, so text drawn the obvious way is fully transparent. It
      paints a disc + up to two glyphs in colours that are never pure black,
      then sets alpha for every pixel that got written — and that holds up:
      the badge is neither invisible nor a black square. The third worry,
      `SetOverlayIcon` needing a taskbar button a dev spawn might not have,
      was unfounded — a plain `tinyjs dev` gets one. Longer text collapses to
      a bullet as designed (`'NEW'` → a centred dot).
- [x] ~~`app.icon` only took `.ico`~~ — fixed 2026-07-25. `do_appicon` still
      tries `LoadImage` first (it picks the right sub-image per size, which
      GDI+ can't), then falls back to `icon_from_png()` — the decoder the
      startup icon and tray icons already used. png was the one format every
      tinyjs app actually ships, so it silently no-op'd on its own `icon.png`.
- [x] ~~`icon('')` cleared to the class icon~~ — fixed alongside. It sent
      `WM_SETICON 0`; the startup icon is now kept in `g_icon_default` and
      restored, matching what the docs and macOS already promise.
      Watch for two traps if this is touched again: the GDI+ path uses ONE
      HICON for both sizes (naive paired `DestroyIcon` double-frees it), and
      the startup icon is shared and must never be destroyed. `free_app_icons()`
      guards both.
      Verified end to end: title bar goes terminal → amp's png → terminal, and
      `WM_GETICON` returns to the exact startup handle on reset.
- [x] `app.icon` **cannot move the taskbar button, and now that is settled** —
      the AppUserModelID theory is dead. Retested 2026-07-28 against a BUILT
      app (`dist/<name>.exe`, so `apply_relaunch_props` had run and the window
      carried the AppUserModelID + `RelaunchIconResource` a dev spawn skips),
      holding a deliberately garish magenta icon for 8s: the taskbar strip
      showed **zero changed pixels** for the whole hold, and crops before and
      during are identical. `icon()` resolved `true` and the title bar changes
      as before, so the call works — the shell simply does not follow it.
      So what `app.icon` controls on Windows is the title bar and Alt-Tab,
      full stop, for dev and built apps alike. Anyone wanting the taskbar
      button to change needs a different mechanism than `WM_SETICON`.

## Site-wrapper gate — the ADVERSARIAL probes, added 2026-08-06

The three legs of TODO-site-wrapper.md were each "verified live same day" —
and the origin stamp, which is the input the `"api"` origins keyhole trusts,
was still spoofable on two of them. Every harness drove our own cooperative
`tiny.js` client (one argument per call, no navigation mid-call), so nothing
ever asked the question a hostile wrapped site asks. The probe code is in
TODO-site-wrapper.md's "Adversarial probes the harness MUST include"; these
boxes are the per-OS ticks.

Tick only after SEEING the denial (or the absent window) on that OS. A gate
that silently allows looks exactly like a gate that was never consulted —
which is precisely how this got through the first time.

- [x] **Forged origin argument, macOS** — **DENIED, seen 2026-08-06.** A
      `"url"` app whose main frame is served from `http://127.0.0.1:8123`
      (keyhole: store/capabilities/win.open) with `http://127.0.0.1:8124`
      granted `"all"`. `window.__invoke(payload, 'http://127.0.0.1:8124')`
      denied; the same with three forged arguments denied; and the raw
      transport (`webkit.messageHandlers.tiny.postMessage` with a payload
      crafted to break out of the JSON string the launcher escapes it into,
      both `","…` and `\","…`) never produced a call at all. Every denial
      line in the backend log names the REAL origin — four denials, all
      `http://127.0.0.1:8123`, none `:8124`. Control in the same run: the
      gated method denied normally, so the probe can fail.
- [x] **Forged origin argument, Windows** — **DENIED, seen 2026-08-06** on a
      launcher rebuilt from source. A wraptest app served its main frame from
      `http://127.0.0.1:8123` (narrow keyhole: 137 methods denied) with
      `file://*` and `http://127.0.0.1:8124` both granted `"all"`, and the
      page tried to be seen as either. Four shapes, all denied, and the wire
      log is the proof — the page's forged value sits in the middle and the
      launcher's stamp is last:
      `CALL … ["{\"method\":\"win.setTitle\"…}","file://","http://127.0.0.1:8123"]`
      → `tinyjs: denied "win.setTitle" for http://127.0.0.1:8123`.
      Covered: one forged argument (`file://`), one aimed at the other
      trusted origin, THREE stacked forged arguments (in case "last" is read
      as "[1]" or "[2]"), and both raw `window.__webview__.post(...)` frames
      bypassing the shim entirely — including one with a client-shaped 32-hex
      id. The method under test was `win.setTitle` on purpose: a success is
      visible from OUTSIDE the page, and the runner's title timeline shows the
      window title never once became `SPOOFED-*`. The control (the same call
      made honestly) is denied, and the same call from the trusted origin in
      the same run is allowed — so the gate is on, not merely silent.
- [x] **Forged origin argument, Linux** — **DENIED, seen 2026-08-06** on a
      launcher rebuilt from source, four shapes. The wrapped site at
      `http://127.0.0.1:8123` (keyhole: `store.*`, `system.capabilities`,
      `win.find` — 135 names denied) tried to be seen as `file://` or as
      `http://127.0.0.1:8124`, both granted `"all"` in the same manifest:
      `window.__invoke(payload, 'file://')` (the extra argument the Windows
      bridge once read) → rejected; a raw
      `webkit.messageHandlers.tiny.postMessage` with a **forged token** and
      one with the page's own token **mutated by one character** → no `CALL`
      line at the launcher AT ALL (unknown token = not a window, dropped);
      the untokened raw form `'|9902:{...}'` → processed, and the wire shows
      the launcher's own stamp:
      `CALL main:9902 ["{\"method\":\"win.setTitle\"…}","http://127.0.0.1:8123"]`
      → `denied "win.setTitle" for http://127.0.0.1:8123`. `win.setTitle` was
      the method under test on purpose: success would be visible from OUTSIDE
      the page, and the X11 title timeline shows the main window's title never
      left `WRAPKIT-MAIN`. Controls both ways in the same run: the honest call
      is denied, an allowed one (`store.set`) resolves, and the popup on 8124
      gets `0` denied.
- [x] **Hostile subframe can't borrow the top frame's gate, Linux (#18)** —
      **fixed and seen 2026-10-02**, before/after on the same harness. A
      wrapped app at `http://127.0.0.1:8123` (`origins: {8123: "wrapper"}`)
      embeds a cross-origin iframe from `:8124`. The iframe reads
      `parent.__TINY_TOK` and gets `SecurityError`, then posts the untokened
      `'|9902:'+store.set` and `'|9903:'+win.setTitle` to
      `webkit.messageHandlers.tiny`. Pre-fix launcher (HEAD b52cd84) produced
      `CALL main:9902 [..., "http://127.0.0.1:8123"]` and `CALL main:9903 …`,
      the iframe stamped as the trusted top frame, with no denial logged.
      Fixed launcher: both dropped ("dropped an untokened call from a tokened
      window"), no `CALL` line. The top frame's own `tiny.store.set` worked
      in both runs.
      (The untokened-processed result in the entry above was correct for
      its date and is now intentionally dropped.)
- [x] **Navigate-then-call race, Linux** — **denied, seen 2026-08-06**, with
      a caveat that matters more than the tick. `location.href` was pointed at
      a 3s-slow page on the TRUSTED origin and 24 `win.setTitle` calls were
      fired at 100ms intervals from the still-live old document: all 24
      denied, and the window title never became `SPOOFED-race-*`. **But it
      also denies on the PRE-FIX launcher** — WebKitGTK 2.52.3 does not flip
      the active uri at provisional-load start (the same navigation's `start`
      NAV event still names the OLD url), so the hole the review predicted
      isn't reachable on this engine and this probe cannot fail here. The fix
      (origin captured at commit, in the token map) is kept as structural: it
      stops the stamp depending on when an engine flips its uri.
- [x] **Popup can't steal its opener's call token, Linux** — the token is the
      launcher's routing identity, so a hostile popup reading
      `window.opener.__TINY_TOK` would be able to speak AS the opener. Seen
      2026-08-06: a cross-origin popup got `SecurityError` (same-origin policy
      does this job; the probe is the proof it isn't bypassed by the
      related-view/shared-process construction popups need).
- [x] **Shared-manager mark is lifted when the last popup closes, Linux** —
      **fixed and seen 2026-08-06** (finding 3). While a popup is alive an
      untokened raw message on the opener's manager is ambiguous and must be
      DROPPED; once every popup is gone the same message must be processed
      again. Differential on one launcher rebuild: pre-fix the post-close
      message was still dropped ("dropped an untokened call on a shared
      content manager" — the opener stayed flagged for the process's life),
      post-fix it produced `CALL main:9905` and was denied on its real origin.
- [x] **Find state doesn't survive a navigation, Linux** — **fixed and seen
      2026-08-06**. Same term (`needle`, 3 matches) searched on the first page
      and again four navigations later: pre-fix the second search reported
      `activeMatch 2` (the launcher thought it was a step through the previous
      document's match list), post-fix `1` — a fresh search, which is what it
      is.
- [x] **Navigate-then-call race, macOS / Windows** — should already deny
      (macOS stamps the document's own `frameInfo.securityOrigin`; Windows
      stamps the message Source). Confirms the probe itself is sound —
      a probe that can't fail anywhere proves nothing.
      **Windows half seen 2026-08-06**: with a 3s-slow destination on the
      trusted origin, the in-flight call from the still-live old document was
      **denied**, and the window title never became `SPOOFED-race`. Caveat on
      how much that proves: the page is being torn down while the loop runs,
      so only the first verdict reliably reaches the store — the durable
      evidence is the external one (no spoofed title at any point).
      **macOS half seen 2026-08-06 — both halves now done, box closed.**
      Same rig as the forged-argument box: `location.href` to a 3s-slow page
      on the `"all"` origin, then the gated call from the still-live old
      document. Denied, and the backend log names `:8123`, not `:8124`.
- [x] **`tiny.win.id` inside a window-mode popup, Linux** — **fixed and seen
      2026-08-06.** The popup (on the trusted origin, window mode) reported
      `main` pre-fix and `popup1` post-fix, from the same harness on the same
      box. The fix is in `runtime/tiny.js` — `win.id` is a getter, so it reads
      `window.__TINY_WIN` when asked instead of at client-construction time;
      the launcher's correction was always landing (the page saw the right
      `__TINY_WIN` in both runs), the client had just read it too early.
      Routing was never at risk: the popup's calls are attributed by token,
      and in the same run its `win.setTitle` retitled the POPUP's window (X11
      census: `PU-SETTITLE-OK` on the second window, main untouched) and its
      `capabilities()` reported the 8124 grant (0 denied) rather than the
      wrapped site's keyhole.
- [x] **`tiny.win.id` inside a window-mode popup, macOS / Windows** — both
      give the popup its own shim, so this is a regression guard.
      **Windows half seen 2026-08-06: it is the popup's own id.** The popup
      page stamped it into its document title (the only channel that survives
      a popup whose RPC is dead — see the next box) and the window read
      `PU:id-popup1`, not `main`.
      **macOS half seen 2026-08-06 — both halves now done, box closed.** The
      popup wrote `tiny.win.id` to the store and it read `popup1`. Note this
      is now a real regression guard rather than a formality: cc749b2 made
      `tiny.win.id` a GETTER in the shared client for Linux's sake, so this
      box is what proves that change didn't disturb the two platforms whose
      popups already baked in the right id.
- [x] **A denied popup opens NO window, Windows** — **fixed and seen
      2026-08-06.** `abandon_popup()` now calls `put_Handled(TRUE)` BEFORE
      completing the deferral (and `ctrl->Close()`s the orphaned controller on
      the early return; `SecCtrlHandler` also got a destructor that abandons,
      so a completion that never fires can't hang `window.open()` forever).
      Census: across the whole run exactly three launcher windows existed —
      main, the allowed popup, and the `window.open('')` one — and the denied
      popup produced **none**, with no `Chrome_WidgetWin_*` browser window
      from msedgewebview2 either. The allowed popup appearing in the same
      census is the control that says the rig can see a popup at all.
- [x] **Reported `filename` names the file on disk, Windows** — **fixed and
      seen 2026-08-06**, both arms. `download_finish()` now derives the
      reported name from the FINAL path. Dedup arm: with a decoy `dup.txt`
      staged in Downloads, the download reported `dup (1).txt` and
      `dup (1).txt` is what exists (17 bytes). Ask arm: the save panel was
      renamed from outside to `renamed-by-hand.txt`, and both the `started`
      and `done` events report `renamed-by-hand.txt`, matching the file on
      disk. Before the fix both reported the suggestion, `dup.txt`.
      Rig note: you CANNOT force dedup by downloading twice from one page —
      Chromium blocks a page's second automatic download outright and the
      second click produces no download at all (measured twice). Stage the
      collision on disk instead. And the Downloads folder here is redirected
      to `\\Mac\Home\Downloads`, so the runner has to ask the shell for it,
      exactly as the launcher does.
- [x] **Back/forward after a policy ask, Windows** — **fixed and seen
      2026-08-06.** `NavigationStarting` now reads
      `ICoreWebView2NavigationStartingEventArgs3::get_NavigationKind` and does
      not ask for `BACK_OR_FORWARD` at all: the ask is cancel-and-re-issue,
      and a cancelled history move cannot be re-issued — `Navigate()`ing its
      url pushes a NEW entry, so Back grows the stack and Forward dies. A
      `RELOAD` is still asked, and re-issued with `Reload()` rather than
      `Navigate()`. Seen: `history.back()` landed on `/` and the hook log
      shows a bare `start` for it with **no policy ask**, where every
      ordinary navigation in the same run has one.
- [x] **A re-visited URL still asks, Windows** — **fixed and seen
      2026-08-06.** `g_nav_allow_once` entries are now stamped and consumed
      three ways (matched, expired after 10s, or superseded by any other
      navigation in that window) instead of surviving until something happened
      to match them. Seen: two visits to `http://127.0.0.1:8123/page2` in one
      run produced **two** `policy` asks.
- [x] **`beforeunload` prompts, Windows** — **fixed and seen 2026-08-06.**
      The `else` arm of `JsDialogHandler` auto-Accepted it; it now runs an
      MB_OKCANCEL and only Accepts on OK. Seen: a "Leave this page?" `#32770`
      appeared and was answered by the runner. Two things worth carrying:
      the page needs **sticky user activation** or Chromium suppresses the
      dialog entirely and "no dialog" looks exactly like the bug (the probe
      sends a real key through `app.keystroke` first); and an asked navigation
      prompts **twice** — beforeunload runs, the policy ask cancels the
      navigation, and the re-issue runs beforeunload again. Browser-consistent
      it is not; it is the cancel-and-replay design showing through, and it is
      the last thing that shape still costs.
- [x] **A call from a never-committed document settles, Linux** — seen
      2026-08-06: `window.open('')` then `tiny.win.getState()` from the blank
      popup **rejected** ("disabled by tinyjs.json") well inside the 3s
      timeout, on the pre-fix build too. The premise turned out not to hold
      here — WebKitGTK COMMITS the about:blank document (`navigate commit
      about:blank window popup3` in the hook log), so it gets a token like any
      other page and its calls are attributed with origin `null`, which the
      stranger gate denies. A guard was added anyway (a window-mode popup with
      no token 120ms after `create` gets one) so a document that genuinely
      never commits errors instead of hanging.
- [x] **A denied popup opens NO window, Linux** — seen 2026-08-06. Window
      census under XWayland across a full run: exactly THREE launcher windows
      existed — main, the allowed popup (which is the control that says the
      census can see a popup at all), and the `window.open('')` one — plus the
      two modal GTK dialogs at the end. The `onWindowOpen`-denied popup
      produced none, and the hook log shows its `policy` ask and its `open`
      outcome with the deny verdict.
- [x] **Reported `filename` names the file on disk, Linux** — seen
      2026-08-06. With a decoy `dup.txt` staged in `~/Downloads`, the download
      reported `dup (2).txt` in both the `started` and `done` hooks and
      `dup (2).txt` (17 bytes, the served body) is what is on disk. Linux
      derived the name from the final path already (both arms do); this is the
      probe that says so rather than an inference from the source.
- [x] **A call from a never-committed document settles, Windows** — seen
      2026-08-06: `window.open('')` then `tiny.win.getState()` from the popup
      **rejected** (the gate denied it, origin `null` on an about:blank
      document) well inside the 3s timeout. It settles, which is the claim.

### The harness

**Linux (2026-08-06).** The same shape, rebuilt in python: `server.py` serves
8123 (the wrapped site, narrow keyhole) and 8124 (trusted, `"all"`) with a
3s `/slow` and an attachment for the download probe; `wraptest/` is a `"url"`
app whose `src/main.js` records every hook into the app store (the only
channel that survives the page navigating out from under itself) and denies
the popup whose url carries `deny=1`; the page runs one PHASE per load
(`?p=a…d`) because the race probe destroys its own document; `run.py` stages
the download collision, launches `tinyjs dev` with `TINYJS_DEBUG=1` and
`TINYJS_TEST_AUTODLG=ok`, polls a window census + title timeline while it
runs, then prints store, census, Downloads and every denial line. Two things
it cost:
- **`GDK_BACKEND=x11`.** This box is a GNOME *Wayland* session, where
  `_NET_CLIENT_LIST` is empty and `Shell.Introspect` is portal-locked — the
  census silently saw ZERO windows, which reads exactly like "no popup
  opened". Under XWayland every window is an X11 window `xprop` can count.
  (Same rig as "Photographing a window on Linux" below.)
- **A differential, or it proves nothing.** Every fix here was run BOTH ways
  — `git stash` the launcher/client change, rebuild from source, re-run —
  and two of the five predicted bugs turned out not to reproduce on WebKitGTK
  2.52.3. A probe that passes on the unfixed build is evidence about the
  ENGINE, not about the fix, and both are written up that way above.

`scratchpad/wrapkit` (Windows session): `server.js` serves two origins — 8123 as
the wrapped site with a narrow keyhole, 8124 as a trusted origin granted
`"all"`, with a deliberately slow `/slow` for the race probe — `wraptest/` is a
`"url"` app pointing at 8123 with hook recorders in `src/main.js`, and
`run.ps1` is the part that matters: it enumerates top-level windows, tracks the
app window's title, and **answers dialogs itself** rather than letting
`TINYJS_TEST_AUTODLG` do it, so every dialog that appears is recorded before it
is dismissed. Rebuild it rather than reinvent it; the probes that found things
are the ones that assert from outside the page.

## Written 2026-07-26, never run off macOS

- [x] **kitchen-sink FFI, Linux** — verified 2026-07-28 on aarch64: the
      offsets are right. `ram` reads 1.9 GB against a ground truth of
      2049527808 bytes from `free -b` (exact), uptime 1.4 h (sane),
      `gethostname()` returns the real hostname, `getpid()` matches tjs.pid,
      and zlib round-trips (zlibVersion 1.3, compress→decompress true).
      x86_64 remains assumed-by-ABI, which is the direction the offsets were
      written for.
- [x] **kitchen-sink FFI, Windows** — *2026-11-04: the Windows branch now exists and
      works, seen from examples' unmerged `origin/ffi-dlopen` (backend API, not the
      page; `tjs:ffi` section near the end of this file). `sysinfo`'s cpu is
      `cpus[0].model` now, not hardcoded; "Apple Silicon × 4" is what this VM
      reports. The rest of this entry is the history.*
      **this entry described code that was
      never written.** Checked 2026-07-28: `kitchen-sink/src/main.js` has no
      Windows branch at all — `ffiLibs()` hardcodes
      `/usr/lib/libSystem.B.dylib` and `/usr/lib/libz.dylib`, and `kernel32`
      appears nowhere in the examples repo except the matcha and presto ports.
      Run on Windows, `ffiInfo` and `zlibRoundtrip` both throw a raw
      `uv_dlopen failed: The specified module could not be found.` — not the
      graceful "not here" the old text implied. (`sysinfo` does degrade
      properly, reporting `ram: 'n/a'`; it also prints a hardcoded
      `cpu: 'Apple Silicon × 4'` on Windows, which is its own small lie.)
      So this stays open, and it is example-app work in tinyjsapp-examples,
      not a tinyjs gap. The route is still the one sketched here:
      `kernel32.dll` `GetTickCount64()` and `GlobalMemoryStatusEx()`, whose
      struct's first field must be its own size (64) before the call or the
      API refuses — if `ram` comes back 0, check that first. zlib has no
      system copy on Windows, so that row should say so rather than throw.
- [~] **`chrome.trafficLights` reporting** — it only ever worked on macOS,
      but Linux stored the bit and echoed it back through `getState`, so an
      app that set it and read it back was told it had worked. Linux now
      reports the truth, and `capabilities().trafficLights` is `false` on both
      Windows and Linux. Worth confirming nothing depended on the old lie.
      Linux half verified 2026-07-28: `setChrome({windowControls:['close']})`
      resolves and `getState().chrome.windowControls` reports `null`, not an
      echo; the fleet sweep (all 26 examples launch) says nothing depended on
      the lie. **Closed 2026-08-05:** `trafficLights` no longer exists as a
      key anywhere in the runtime (post-0.30 rename to `windowControls`/
      `windowControlsPos`; only a cli.js comment mentions the old name), so
      there is nothing left on Windows to check under this name — and the
      current contract (getState reports what you GOT) was re-proven on
      Windows the same day in the windowControlsPos entry below.

- [x] **`chrome.windowControls` on Windows** — verified 2026-07-28, and the
      predicted Win32 coarseness is exactly what happens. `['close']` →
      `['close']`; `false` → `[]` (group gone); `true` → all three. And
      `['minimize']` → **`['close','minimize']`**: the min/max boxes require
      `WS_SYSMENU`, so "minimize without close" is not expressible and asking
      for it gets close too. The important part is that `getState` reports
      what you GOT, not what you asked for — so the round trip is honest
      rather than an echo, and an app can detect the difference.
- [~] **`chrome.windowControls` on Linux** — `gtk_window_set_deletable` for
      close, `_MOTIF_WM_HINTS` for minimize/maximize. Mutter and KWin read the
      hint; many WMs don't, so treat a no-op as "this WM ignores MWM", not a
      bug. `getState` deliberately reports `null` here rather than echoing the
      request. 2026-07-28: the call resolves on both sessions and `getState`
      does report `null`; `capabilities().windowControls` is session-gated
      (false on Wayland, true on X11). But the MWM half is worse than
      "the WM may ignore it": measured with xprop on GNOME 46/X11, the
      property reads GTK's own CSD signature (`0x3, 0x1, 0x0` — all
      functions, zero decorations) after `setChrome(['close'])`, i.e. **GTK
      rewrites _MOTIF_WM_HINTS over the launcher's request**, and under CSD
      the WM doesn't draw the buttons anyway. So on GNOME, minimize/maximize
      removal is structurally a no-op; only the `set_deletable` half (the
      close button, which GTK itself draws) can work — and **that half is now
      seen, 2026-08-01**. Photographed the window's FRAME (the close button
      lives in mutter's frame window, not our client window — find it with
      `xwininfo -id <client> -tree`'s "Parent window id"):
      `setChrome({ windowControls: false })` **removes the close button** —
      502 changed pixels in the titlebar, minimize and maximize still there.
      `setChrome({ windowControls: ['close'] })` — asking to keep close and
      drop the other two — changes **nothing at all** (0 changed titlebar
      pixels, all three buttons still up), which is the MWM half being a
      no-op exactly as described. So on GNOME the honest summary is: you can
      take the close button away, you cannot take minimize/maximize away, and
      `getState().chrome.windowControls` reports `null` rather than echoing
      either request. A real fix would drive the CSD title bar, not MWM.

      **2026-07-29: this arm carried a serious side effect, now fixed — it
      RE-FRAMED frameless windows.** `set_mwm_buttons` replaced the whole
      `_MOTIF_WM_HINTS` property with flags saying "functions specified"
      only; without the DECORATIONS flag the WM falls back to its default,
      which is decorated — erasing the `frame:false` GDK had written moments
      earlier. Since every frameless app also declares
      `windowControls:false`, each of the six frameless+windowPlacement
      examples wore a full mutter-x11-frames title bar on X11 (amp is how it
      was caught — the only pixel anybody looked at since the arm landed
      2026-07-26). The property now always specifies both sections and
      carries `gtk_window_get_decorated` through. Verified: amp's client is
      back to exactly 320x172 with no frame window wrapping it, and the menu
      suite + smoke + appsurface still pass on both sessions.
      Startup chrome also now rides the spawn env (`TINYJS_CHROME`, the
      Windows `TINYJS_TRANSPARENT` shape) so the launcher applies it BEFORE
      first show — previously the socket line landed with the window already
      on screen, so frameless apps flashed a decorated frame at launch.

## Needs a hand on the mouse — kitchen-sink, 2026-07-26

- [~] **`dialog.openFiles`** — the deck's Storage ▸ Files "Open many…" button.
      A native panel can't be driven from the page, so only the wiring either
      side of it is checked: multi-select on, an array back, `null` for a
      cancel. NSOpenPanel, the Windows common item dialog and GTK's chooser
      each need their own look, because "multi-select on" is a different flag
      in all three and a panel that quietly allows only one file returns a
      one-element array that looks entirely correct.
      **Windows done 2026-08-01** — and it does not need a mouse after all:
      naming two files in the File name box the way the shell itself does
      (`"alpha.md" "bravo.txt"`) returned a **2-element** array of absolute
      paths, so `FOS_ALLOWMULTISELECT` is really set, and a cancel resolved
      `null` rather than `[]`.
      **Linux done 2026-08-01**, also without a mouse: in the GTK chooser the
      selection extends from the keyboard, so Down, Down, **Shift+Down**
      (posted through the launcher's own XTest `keystroke`, which reaches a
      modal chooser fine) then Return returned a **2-element** array —
      `alpha.md` and `NOTES.MD`, absolute paths — so
      `gtk_file_chooser_set_select_multiple` really is set. Cancel → `null`
      was checked on the single-file chooser in the `{ types }` section.
      macOS's panel is the last one unwatched.
- [ ] **`macos.applescript` under the Automation permission** — the deck's
      *frontmost app* and *Finder window* samples. Verified by driving:
      arithmetic returns `42`, and a broken script comes back with
      AppleScript's own message ("Can't make "hello" into type number."). Not
      verified: the TCC prompt on first cross-app script, and that a **denied**
      prompt rejects with the reason instead of hanging. Both need a human to
      answer the dialog, and the answer is remembered per target app — reset
      with `tccutil reset AppleEvents` to see the prompt again.

- [ ] **the bouncing ball — hover and click** — the deck's App ▸ Window ops
      *Set a ball loose*. Driven on macOS 2026-07-27: the window really moves
      (its `getState().x/y` tracks the page's own numbers frame by frame), it
      reports `chrome: { frame: false, transparent: true }`, `alwaysOnTop:
      true`, it turns at the visible-rect edges and closes itself at zero. Not
      driven, because it needs a cursor: **hover holds it** and **click pops
      it**. That pair is the actual claim on the card — that a window being
      re-placed sixty times a second is still taking mouse events — so it wants
      an eye on it.

- [ ] **`app.authenticate` — the sheet itself** — the deck's System ▸ Secrets &
      permission *Unlock and reveal the token*. Driven on macOS 2026-07-27 as
      far as a script can go: `LAContext` accepts the policy even for an
      **unbundled dev binary**, and the call sits pending with the sheet up
      rather than resolving. Not verified: that a **pass** resolves `true` and
      reveals the value, that **cancel** comes back `false` and not a throw,
      and that the reason string passed in is the line the sheet actually
      shows. All three need a finger on the sensor.
- [ ] **`permissions.request('screen')` / `('accessibility')`** — same card.
      `check()` is fully driven (see below); `request()` is not, because both
      buttons put system UI on screen. Worth watching for one specific thing:
      **accessibility can't prompt** once denied, so macOS opens System
      Settings instead — and the status only changes on the *next launch*, so
      a re-check right after ticking the box still says `denied`. That is
      correct behaviour that looks exactly like a bug.

## Needs ears, a keypress, or a permission — kitchen-sink, 2026-07-27

Fourteen APIs went into the deck this night. What a script could reach was
driven and is recorded under each card; this is the remainder. Deliberately
*not* run, because the machine's owner was asleep and its output volume was 38:

- [ ] **`app.say` out loud** — Media ▸ Speech. Only the silent half is proven:
      `voices()` returns **181 voices across 49 languages, 1 of them enhanced**,
      the language filter picks **42 en voices**, and `say('')` resolves `true`
      in **23 ms** (so the wire and the promise work). Unproven, and it's the
      whole claim on the card: that `say()` resolves when playback **finishes**
      — time a long sentence and the number should track the audio — and that
      **`stopSpeaking()` makes a pending `say()` resolve `false`**, which the
      third button ("cut it off at 1s") exists to show. Also worth an ear: the
      `rate` slider, and passing a bare `en-AU` instead of a voice id.
- [ ] **The hardware media keys** — Media ▸ Now Playing. `nowPlaying.set()` is
      driven (the elapsed counter ticks and Control Center should show the fake
      track), but **nothing has ever pressed F8**: `onMediaKey` is wired and has
      never been seen to fire. Check play/pause/next/previous, the AirPods tap,
      and a Control Center **scrub**, which is the only source of a `seek` with
      a `time`. Then start Music and confirm it **takes the keys away** — the
      card claims the OS arbitrates by whoever set Now Playing last.
- [ ] **`app.recorder`** — Desktop ▸ Pixels to text. Rejects here with "screen
      recording permission required", which is the card's fallback path working.
      Grant Screen Recording and watch the real one: that **`start()` resolves
      only once capture is live** (it prints the ms), that `stop()` resolves
      `{ path, duration }` with the file **already finalised** (the card stats
      it immediately and says so), and that the mp4 actually plays.
- [x] ~~**`app.ai`**~~ — done 2026-07-27, **both** paths. On a stock build:
      `availability()` → `unsupported`, `capabilities().ai` → `false`,
      `generate()` rejects with "not built in". Then built against the macOS
      26.5 SDK on macOS 26.5.2 and run again:
      `availability()` → **available**, `capabilities().ai` → **true**,
      and a real completion came back. First generation **2.9 s**, the next
      **513 ms** — there's a warm-up, which the card now mentions. It treated
      `instructions` ("three short lines, no preamble") as a hint and answered
      in one sentence; that's the model, not the binding.
      `setup.sh` now links the shim in automatically whenever the SDK carries
      FoundationModels, so a plain `./setup.sh` on this machine produces an AI
      build — which is what the checkout has.
- [ ] **`app.selectedText` with something actually selected** — Desktop ▸
      Reaching other apps. Returns `null` with Accessibility **granted**, which
      the card correctly reports as "nothing was selected". Two attempts, both
      null: nothing selected anywhere, and — worth knowing — a fully selected
      text field **inside the deck's own page**, which means a WKWebView does
      not publish its selection over the AX interface this call reads. So the
      only way to see a string out of it is a selection in a *different* app;
      the three-second button exists for exactly that, and that path has never
      returned one.
- [x] ~~**`app.keystroke` / `app.paste` posting real events**~~ — done
      2026-07-27, and self-contained: a scratch input in the deck's own page,
      with the deck frontmost. `keystroke('h')` then `keystroke('i')` left
      **hi** in the field; `clipboard.write({text:'PASTED-OK'})` followed by
      `paste()` returned `{ok:true, trusted:true}` and left **PASTED-OK**;
      `keystroke('cmd+a')` selected the field. Real CGEvents, delivered, no
      second app needed to see it. Still unwatched: posting into *another*
      app after `win.hide()`, which is the actual use case, and the
      `trusted:false` path (needs Accessibility revoked).
- [ ] **`app.onNotificationAction`** — Desktop ▸ notify (demoed before tonight,
      still never verified). Needs a click on a real banner, which needs a
      packaged signed app; dev falls back to osascript.

## Secrets & permissions off macOS — never run there

Driven on macOS 2026-07-27 (kitchen-sink, System ▸ Secrets & permission):
`secrets` set→get→delete→get round-trips, `set` replaces rather than
duplicating, `delete` of an absent key resolves `true`, an unsaved key reads
`null`, 4 KB of emoji survives intact, an empty string comes back as `''` (not
null), and the item lands in the login keychain as a generic password with
service = the app id. `permissions.check` answered granted/denied/undetermined/
unsupported across all seven names without a prompt. None of that was run
elsewhere:

- [x] **Windows — `secrets` against Credential Manager** — verified 2026-07-28.
      set→get→delete→get round-trips in ~10ms; a second `set` REPLACES (get
      returns the new value, ONE delete leaves nothing behind, and `cmdkey
      /list` shows exactly one entry, target `<app id>/<key>`, so no duplicate);
      an unsaved key reads `null` not an error; `delete` of an absent key
      resolves `true`; an empty string comes back as `''`, not null.
      **One real difference from macOS, and it cost a fix:** Credential Manager
      caps a blob at **2560 bytes**, where the macOS keychain swallowed 4 KB of
      emoji whole. Over the cap `CredWriteW` fails with 1783
      (`RPC_X_BAD_STUB_DATA`), which names nothing, and the old message was a
      bare "credential write failed" — so the 4 KB emoji case in this file's
      macOS round trip simply exploded with no clue why. `do_secret` now
      rejects oversized values up front naming the byte count and the limit.
      The limit is BYTES of UTF-8, not characters: 4-byte emoji reach it four
      times faster than ASCII (2560 chars of ASCII fits; 640 emoji is already
      2560 bytes and is the last thing that does). NOTE the constant is
      hardcoded, not `CRED_MAX_CREDENTIAL_BLOB_SIZE` — MinGW's wincred.h still
      defines that as the pre-Vista 512, and using it rejected writes this
      machine accepts. Bisected against a live `CredWriteW`: 2560 writes, 2561
      fails.
- [~] **Windows — `authenticate` via Windows Hello** — the short-circuit half
      verified 2026-07-28: resolves `false` in **13ms**, no hang. Confirmed
      HONEST rather than a broken binding by probing the WinRT enum directly
      from a standalone program — the factory activates, the async op
      completes, and `UserConsentVerifierAvailability` is `1
      (DeviceNotPresent)`, i.e. this box has no Hello enrolled. Still unrun,
      and it needs hardware: a REAL verification resolving `true`, and a
      cancel resolving `false` rather than hanging or throwing.
      (Gotcha if you write your own probe: copy the statics IID out of
      `launcher-win.cc`. A wrong one answers `E_NOINTERFACE` and looks exactly
      like "Hello is unavailable".)
- [x] **Windows — `permissions.check`** — verified 2026-07-28. `granted` for
      all five known names (`accessibility`, `screen`, `notifications`,
      `microphone`, `camera` — there is no TCC), and `unsupported` for
      `automation`, `automation:<id>` and any unknown name. No prompts.
- [~] **Linux — `secrets` against the Secret Service.** Happy path verified
      2026-07-28 (the VM's keyring turned out unlocked): set→get→delete
      round-trips in single-digit ms, a second `set` REPLACES rather than
      duplicating (get returns the new value, one delete leaves nothing
      behind), an unsaved key reads `null` not an error. **No-daemon answer
      seen 2026-10-14**: in a private `dbus-run-session` with no keyring
      (x86_64 22.04 container, Xvfb) `secrets.set` and `secrets.get` both
      reject `no secret service` promptly — no hang. Note
      `capabilities().secrets` still says `true` there (it doesn't probe the
      bus the way badge/progress do), so feature detection can't see it;
      the rejection is the only signal. Still unwatched, because this box
      can no longer produce it: the locked-keyring behavior — GNOME may
      prompt to unlock, and it's worth knowing whether that blocks the call
      or fails. (Earlier this VM DID report IsLocked while autologin'd, per
      TODO-linux.md, so the locked case is real.)
- [x] **Linux — `authenticate` answers `false`.** Deliberate: no portable owner
      check exists, so the gate fails closed. Verified 2026-08-01:
      `tiny.app.authenticate('…')` **resolves `false`** (it does not throw, and
      it does not hang), and `capabilities().authenticate` is `false` on both
      sessions — so an app can feature-detect instead of calling and guessing.

## The hands-free AI loop — half confirmed by a human

Added 2026-07-27: the deck's AI card can listen, generate and speak back.
The speech half is the webview's own `webkitSpeechRecognition`, not a `tiny.*`
call; tinyjs only gained the `speechRecognition` permission key.

Confirmed: the key IS the fix. On an otherwise identical build,
`service-not-allowed` became `start` + `audiostart` the moment
`NSSpeechRecognitionUsageDescription` was present — and the machine's owner
accepted the prompt and saw real transcription, which is the part no script
here could reach.

- [ ] **A full turn, end to end** — speak a question, watch it transcribed
      into the prompt box, the model answer, and `say()` read it out, then
      loop. Each piece is verified alone; the loop as a whole is not. Worth
      watching for two specific things: whether the next listen picks up the
      Mac's *own* voice (the code awaits `say()` to finish precisely to avoid
      that), and whether a long answer makes the recogniser time out.
- [ ] **The dev path, from a clean TCC state.** Dev has no Info.plist, so the
      expected answer is `service-not-allowed` with no prompt — but the grant
      attaches to the SHARED launcher binary, so once it's been allowed for
      anything, dev runs can work. Both were observed here within an hour,
      which is why the card reports what happened rather than predicting it.
      `tccutil reset SpeechRecognition` to see the first case again.
- [ ] **Whether the transcription is on-device.** The card claims the *model*
      is local and deliberately does NOT claim it of the speech. WebKit gives
      no control over `SFSpeechRecognizer`'s `requiresOnDeviceRecognition`, so
      audio may go to Apple. Testable by pulling the network and seeing if
      recognition still works — worth knowing before anyone builds a privacy
      claim on the pair.
- [~] **Windows.** Tried 2026-07-28, and it is half good news. Both
      `window.SpeechRecognition` and `window.webkitSpeechRecognition` are
      **functions** in WebView2 (macOS WebKit has only the prefixed one), and a
      recognizer **constructs** fine with no permission plumbing and no
      Info.plist equivalent. But `start()` called from script produced **no
      event at all within 6s** — not `start`, not `audiostart`, not even an
      `error`. That is a different failure from macOS, which at least answers
      `service-not-allowed`, and silence is the worst of the three. Likely a
      missing transient user activation (the call came from a timer, not a
      click) or an unimplemented backend behind a present API. Needs a
      gesture-driven retry before anyone builds on it — and note the full
      hands-free loop isn't portable regardless, since `macos.ai` is macOS-only.

## `tiny.system.locale()` — read, but never seen change

Added 2026-07-27. Reading it is verified on macOS: `{ language: 'en-US',
languages: ['en-US'], system: ['en-US'], region: 'US', timeZone:
'America/Los_Angeles' }`, matching `defaults read -g AppleLanguages`.

- [ ] **The `locale` event.** An observer on
      `NSCurrentLocaleDidChangeNotification` is wired and has NEVER fired —
      changing the system language is the only way to trigger it, and that's a
      System Settings trip plus, on macOS, usually a logout. Check that the
      deck's row updates without a reload, and that the page's own
      `languagechange` fires alongside it (the handler is there; whether
      WebKit fires it in a WKWebView is equally unverified).
- [ ] **`languages` vs `system` actually differing.** They're identical on this
      machine, so the branch that says "differs" has never rendered. Set the
      system to a language the deck doesn't declare and they should split —
      that difference is the whole reason the field exists, and it's currently
      an argument rather than a demonstration.
- [ ] **A packaged app vs dev.** `NSLocale.preferredLanguages` is filtered by
      `CFBundleLocalizations`, which a dev run doesn't have — so the two may
      legitimately disagree, and which one is "right" depends on the bundle.

## Tool calling — built and driven, but only on this Mac

Added 2026-07-27. The full round trip is verified end to end in the deck:
4 tools offered, the model called `moveWindow({x:200,y:120})` and
`showBadge({count:4})`, the window actually moved (314,117 → 200,120) and the
restore button put it back exactly.

- [ ] **A handler that throws, and one that never returns.** Both paths are
      written — a throw becomes `{error}` handed to the model, and a silent
      handler hits a 20s timeout in the launcher — and neither has been run.
      The timeout matters most: it's the difference between a slow tool and a
      permanently wedged generation, and it blocks a launcher thread while it
      waits.
- [ ] **Several tool calls in one generation, repeatedly.** The reliability
      numbers in the docs come from a standalone Swift harness, not from the
      deck. Worth re-measuring through the real path, since that's what the
      card claims.
- [ ] **A tool called during a `Talk to it` turn.** Speech → generate-with-
      tools → speak has never been run as one sequence; the blocking C hop and
      the speech recogniser have no reason to conflict, which is exactly the
      kind of assumption that turns out wrong.

## AI in released builds — the one check CI can't make

The release job now links the FoundationModels shim into every macOS
tarball. Verified locally on macOS 26.5, building with the exact commands the
workflow runs: both slices present, `minos 14.0` on each, FoundationModels
**weak** in both, `tiny_ai_generate` present in both, the x86_64 slice loads
under Rosetta, and the resulting launcher generates (1.3–1.6 s). The three CI
assertions were checked against a deliberately-wrong build and each one fails
where it should.

- [ ] **Launch a release tarball on a real macOS 14 machine.** This is the
      claim the whole design rests on — weak-linking plus `@available` guards
      mean the launcher loads fine where FoundationModels doesn't exist — and
      it has never been tested on an OS that old. It was a footnote while AI
      was opt-in; now that it ships to everyone, a mistake here doesn't break
      a feature, it breaks the app at dyld time for every macOS 14 user. Check
      the app opens at all, and that `ai.availability()` answers
      `'unsupported'` rather than crashing.
- [ ] **Same on macOS 15**, the other version below the FoundationModels
      floor that people actually run.
- [ ] **A real Intel Mac.** The x86_64 slice is cross-compiled from an arm64
      runner and has only been seen under Rosetta, which is not the same
      thing as native execution.
- [ ] **The first release on the new runner.** `macos-26` also changes the
      SDK every other part of that job builds against (`tinyjs build`'s app
      bundle, the codesign step, the smoke test). Watch the run rather than
      assuming the launcher was the only thing affected.

## The 2026-07-27 capability corrections — asserted from source, never run

Six capability keys were flipped to `false` after auditing every name against
what each launcher dispatches. The evidence is in the source (a `GET` with no
arm for the name, or an explicit `got_unsupported`), which is strong — but no
Windows or Linux machine was involved, and "the launcher has no handler" is
exactly the kind of claim that deserves a run:

- [x] **Windows** — checked 2026-07-28, and it found a real bug. The three
      that moved to `tiny.macos.*` reject with the guard message
      ("tiny.macos.otherWindows is macOS-only (this is windows) — guard with
      tiny.system.isMacOS()"), and `pickColor()` rejects "unsupported on
      windows". **`spotlight()` did NOT reject — it resolved `[]`.** The
      launcher is honest (`SPOTLIGHT` reaches `got_unsupported`); the BRIDGE
      threw the answer away: `(await ask('SPOTLIGHT', …))?.paths ?? []` turned
      `{ok:false,error:'unsupported on windows'}` into an empty array. To a
      caller "no files matched" and "there is no search backend here" are
      opposite answers and only the second was true — and `capabilities()
      .spotlight` being `false` didn't help anyone who just called it. Fixed
      to check `ok` and throw, the way the neighbouring `pickColor` already
      did; re-verified rejecting.
      Swept the other `ask()` call sites for the same shape: `voices()` has it
      (`?.voices ?? []`) but all three launchers implement `VOICES`, so it can
      never receive an unsupported reply — left alone. Everything else coerces
      to an honest `'unsupported'`/`false` default.
      **Media keys on Windows — MEASURED 2026-08-06, and it is the app.**
      Reported from amp: play/pause works, `<<`/`>>` don't, and the suspicion
      was that the Mac host was eating them. It isn't, and no tinyjs code is
      involved either way. With amp playing, the SMTC session reads
      **`SourceAppUserModelId: msedgewebview2.exe`, `IsNextEnabled: False`,
      `IsPreviousEnabled: False`, title `"amp"`, artist empty** — so the
      session is CHROMIUM's own registration of the `<audio>` element, the app
      advertises no next/prev for Windows to route, and `nowPlaying.set()`
      reaches nothing (amp sets title/artist/album per track and the card
      still only says "amp"). Then, injecting the virtual keys INSIDE the VM
      with `keybd_event` so the Mac keyboard is not involved:
      `VK_MEDIA_NEXT_TRACK` changed nothing at all, while
      `VK_MEDIA_PLAY_PAUSE` moved Playing → Paused → Playing. The key path
      works; there is simply no handler behind next/previous.
      Fixed app-side the same day, in ~5 lines next to amp's existing
      `onMediaKey` block (`player.js:1371`):
      `navigator.mediaSession.setActionHandler('nexttrack'/'previoustrack')`
      pointing at the same `next`/`prev` the on-screen buttons use. Play/pause
      is deliberately left to the engine, which drives the audio element
      directly and so keeps amp's UI in step via its own play/pause events.
      **Confirmed by re-measuring: `IsNextEnabled` and `IsPreviousEnabled`
      both flipped `False` → `True`**, and Tarwin confirmed the keys now work.
      Costs macOS nothing (WebKit routes its media keys through `onMediaKey`,
      not mediaSession). Shipped from the examples repo by Tarwin.
      Still true, and still a tinyjs gap rather than an app one:
      `nowPlaying.set()` reaches nothing on Windows, so the OS card shows the
      page title and no artist. `mediaSession.metadata` is the closest an app
      can get until SMTC is wired into the launcher (the route is sketched in
      TODO-windows.md's `nowPlaying` entry).
      Rig: `scratchpad/mediakeys.ps1` — reads every SMTC session's controls
      and injects the virtual keys locally, which is what separates "the host
      ate it" from "the app has no handler". `MEDIAKEY` and `NOWPLAYING` appear
      **zero times** in launcher-win.cc (macOS has four), so `onMediaKey` — the
      hook amp uses (`player.js:1259`) — can never fire on Windows, and
      `nowPlaying.set()` is dropped. What DOES work is Chromium's own: a
      playing `<audio>` element registers with Windows SMTC by itself, and
      play/pause is the one action the engine handles with no page
      cooperation. Next/previous reach an app only if it registers
      `navigator.mediaSession.setActionHandler('nexttrack'/'previoustrack')`,
      and in amp that happens in exactly one place — the geiss-hdr
      visualiser's `HookUpMediaKeys()` (`src/geiss-hdr/audio_input.js:359`) —
      never in the main player. So Windows has nothing to send `<<`/`>>` to.
      App-side fix, not a tinyjs gap. To settle it by measurement rather than
      by reading: with amp playing, read the SMTC session's `IsNextEnabled`
      (a `false` is the direct proof), and inject `VK_MEDIA_NEXT_TRACK` /
      `VK_MEDIA_PLAY_PAUSE` **inside** the VM, which bypasses the Mac keyboard
      and separates "the host ate it" from "the app has no handler" for good.
- [x] **Linux/X11** — verified 2026-07-28, both sessions: the three now live
      on `tiny.macos.*` and each rejects with the guard message
      ("tiny.macos.X is macOS-only (this is linux) — guard with
      tiny.system.isMacOS()"); `system.wifi` resolves `null`;
      `system.locale()` resolves `null` with `capabilities().locale` false —
      the table matches reality, no under-claim found.
- [x] **`app.thumbnail` after the representation change** — macOS is measured
      (folders, `.app` bundles, `.css`, `.wasm` and executables all render now;
      a missing path still rejects). Linux measured 2026-07-28: an image
      renders (64 asked → 128 returned, the documented @2x), but a source
      file and a folder both reject `no thumbnail` — so the README's "any
      path" was over-promising off macOS.
      **Windows measured 2026-07-28, and the expectation was wrong: it matches
      macOS, not Linux.** Via `IShellItemImageFactory` a folder, a `.exe`, a
      `.jpg` and a plain `.txt` ALL render; only a nonexistent path rejects
      (`no thumbnail`). So Linux is the narrow one, not "Windows and Linux".
      **Second difference, in the size contract:** macOS and Linux treat
      `size` as points and render @2x (ask 64, get 128); Windows treats it as
      pixels and returns exactly what was asked (ask 64, get 64x64; a wide jpg
      at 64 comes back 64x36, so aspect IS preserved). Callers should read
      `width`/`height` off the result rather than assuming either. Docs
      corrected in README, SKILL.md and tiny.d.ts (both thumbnail sites).

## Window sizes are the page's box now — Windows and Linux unbuilt

The 0.30.0 size contract (`win.open`'s `size`, `setSize`, `setMinSize` and
`getState().width/height` all mean the page's box; `getState().outer` is the
footprint with decorations). Measured on macOS 2026-07-27 — declared 1100x720
gives a 1100x720 page and a 1100x752 outer, a titled satellite asked for
460x420 gets 460x420, a frameless one asked for 150x150 gets 150x150, and
setSize→getState→setSize holds still over three passes. The other two
launchers were edited to match but not compiled:

- [x] **Windows** — verified 2026-07-28 on Windows 11 Pro 26200, launcher
      rebuilt from source. `do_size` measures the live window/client insets and
      adds them (rather than calling AdjustWindowRect, which would inflate the
      page on the borderless-client windows this launcher makes); `getState`
      reports `GetClientRect` for width/height and `GetWindowRect` for `outer`.
      **Zero drift.** A titled satellite — the only Windows case where client
      != frame, since main and frameless secondaries answer `WM_NCCALCSIZE`
      with no non-client area — declared 460x420 reports 460x420 (outer
      473x456) and HELD 460x420 over three read-modify-write passes.
      `setSize(600,400)` → 600x400 (outer 613x436). Main declared 960x640
      reports 960x640 (outer 973x676); a frameless satellite at 150x150
      reports 150x150 with `outer` == the page box, as predicted.
      The ratchet has to be run BY the satellite: window ops are scoped to the
      calling page, so a driver page opening a satellite cannot size it — open
      the satellite on a page that measures itself.
- [x] **Linux** — verified 2026-07-28 on Ubuntu 24.04 aarch64, GNOME 46, in
      both Wayland and XWayland, launcher rebuilt from source. Declared
      960x640 gives a 960x640 page; after `menu.set` the page is STILL
      960x640 and `outer` grows by exactly the bar (729 → 755 on Wayland) —
      the give-back works. setSize 600x400 → getState → setSize holds
      600x400 over three passes, zero drift. A titled satellite asked for
      460x420 reports 460x420 (outer 460x457 on X11); a frameless 150x150
      reports 150x150 with outer == page box. One expectation was wrong in
      this file: `outer` under Wayland is NOT the page box — GTK draws
      client-side decorations, so frame extents include the CSD titlebar and
      shadow (960x640 page → 1012x729 outer). That's the honest footprint.
      Not checked: a user resize racing the menu idle pass (needs a hand).

## The ball on Windows and Linux — never run there

- [x] **Windows** — **both halves settled 2026-08-05**, headlessly, and it
      is the best result of the three platforms. Motion: a rAF loop on a
      frameless transparent satellite completed **721 moves in 721 frames
      over 6.01s — 120 moves/s on this 120Hz display, zero dropped** —
      with round-trip latency p50 1.2ms / p95 2.8ms / max 3.8ms, and
      sampling the window's REAL position from outside (GetWindowRect,
      DPI-aware) every ~125ms showed even ~270-physical-px steps with clean
      reversals at both bounce edges. Pixels: the ball held still over a
      magenta main window and a DPI-aware CopyFromScreen of its exact rect
      read **pure 255,0,255 at all four corners and pure 0,0,0 at center**
      — a real circle with transparent corners, not a black square (the
      WS_EX_NOREDIRECTIONBITMAP secondaries doing their job).
      Two rig traps, both already documented elsewhere in this file and both
      hit anyway: Add-Type compilation is seconds of skew — compile BEFORE
      launching the app or the "hold still" capture happens mid-flight (one
      corner read black off a stale rect exactly that way); and
      GetWindowTextW without CharSet.Unicode returns one-char garbage
      titles.
- [x] **Linux/X11** — mechanics verified 2026-07-28; **the pixels and the
      motion settled 2026-08-01.** A frameless `chrome:{transparent:true}`
      satellite drawing a `border-radius:50%` div was captured with `xwd`
      (depth **32**, so the RGBA visual really was granted) and its alpha
      channel composited against magenta: a clean circle with genuinely
      transparent corners. **Not a black square.**
      Motion, measured rather than eyeballed: a rAF loop calling
      `win.setPosition` for 6s managed **356 frames, 356 completed moves
      (59.3/s, zero dropped)** with round-trip latency **p50 1ms, p95 2ms,
      max 3ms**, and sampling the window's absolute X from outside every
      120ms showed even ~44px steps with a clean bounce at the edge. That is
      as close to "smooth" as anything short of an eye can get — the only
      thing left is a human saying it looks smooth.
- [x] **Linux/Wayland** — the honest-failure path works as written (blind):
      same probe, the ball closed itself between 2s and 7.5s — the 6s
      `no setPosition` path, not the 10s countdown. Verified 2026-07-28.

## The 2026-07-28 overnight Linux sweep — what got proven, what got fixed

Run on Ubuntu 24.04 aarch64 (Parallels), GNOME 46, both Wayland and
XWayland, launcher + client rebuilt from source first. Full fleet: **all 26
examples build and launch** (23 native; procsy/sqlittle/trolley built inside
an arm64 `node:22-trixie-slim` container — `node:22-slim` is bookworm, whose
glibc 2.36 is too old for our tjs, and the container needs `libffi8`
installed). worldclock and amp soaked 30s (the old tray-ticker bug killed
worldclock in seconds). `test/smoke.html` and `test/appsurface.html` pass on
both sessions.

Two real bugs found and fixed, one platform gap closed:

- [x] **`setAsDefaultHandler` always returned `'failed'`** — bridge checked
      `st.exit_code`, but tjs `wait()` returns `exit_status` (every other
      call site had it right). Fixed; now returns `'ok'` and the mimeapps
      entry lands. All-platform code path, so Windows gets the fix for free.
- [x] **`tinyjs build --cli` wrote no shim on Windows or Linux** — the
      `maybeWriteCliShim` call sat at the end of the macOS bundle branch,
      after the win/linux branches `return`. Both branches now call it.
      Linux verified end to end: shim written, `dist/bin/<name> file.txt`
      delivers the path to `onOpenFiles`. **Windows verified 2026-07-28 too:**
      `dist/bin/<name>.cmd` is written (`@echo off` + `"%~dp0..\<name>.exe"
      %*`, CRLF), a cold start through it delivers the absolute path to
      `onOpenFiles`, and a SECOND launch while the app is running exits
      immediately and forwards its path to the running instance (process count
      stays at 1). One polish bug fixed alongside: the shim hint printed the
      Unix `ln -sf … /usr/local/bin/<name>` line on Windows, where neither the
      command nor the path exists; it now prints a `setx PATH` line there.
- [x] **argv → onOpenFiles on Linux** — cold start delivers
      `["/abs/path"]` with flags skipped and nonexistent paths dropped; a
      second launch exits 0 and forwards its paths over the instance pipe to
      the running copy (watched in the log; one process left).
- [x] **Menu accelerators with modifiers on Linux/X11** — end to end with no
      hands: registered `key:'ctrl+shift+k'`, fired it through the
      launcher's own XTest `keystroke`, and the `menu {id}` event arrived.
      On pure Wayland the keystroke resolves `ok:true` but lands in
      XWayland where no Wayland window can hear it — `capabilities()`
      honestly says `keystroke:false` there, so apps can feature-detect,
      but know the resolve-true is not delivery.
- [x] **`win.hide({app:false})` / `win.hide()` / `show()`** — all resolve on
      Linux and the window still answers `getState` after the round trip
      (both are window-scoped here, which is all a hide can be on Linux).
- [x] **The 0.30 scaffold names** — from a live scaffolded app: all seven
      dialog calls are functions on `tiny.dialog.*` and ZERO remain on
      `tiny.win.*`, so the five buttons `tinyjs new` ships call things that
      exist. (First probe of this found the opposite — because a launcher
      built by hand without `native/gen-client.sh` had embedded a stale
      client. `setup.sh` and the dev auto-rebuild both regenerate it; a bare
      `c++` invocation does not. If tiny.* looks ancient, rebuild via
      setup.sh.)
- [x] **spotlight's `find` fallback** — works (a visible file in $HOME is
      found), and hides dotfiles BY DESIGN (`-name '.*' -prune`), so a
      query for `bashrc` honestly returns nothing. Boxes with no
      plocate/locate get name-only, non-hidden results.

## The 2026-07-28 Windows sweep — what got proven, what got fixed

Run on Windows 11 Pro 26200, **launcher rebuilt from source first** (the local
`launcher-win.exe` was two days stale — and being gitignored, a stale one never
shows up in `git status`; this is the third time it has nearly wasted a
session, so rebuild before believing anything). Everything
below was driven from self-contained pages via `TINYJS_HTML`, with the taskbar
photographed where the decoration lives outside the app window.

Proven, each recorded in its own section above: `app.badge` draws (and
`capabilities().badge` flipped to `true`), `attention` flashes and `presence`
round-trips with the amended page, the secrets round trip against Credential
Manager, `permissions.check` across all seven names, the window-size contract
with zero ratchet drift, `chrome.windowControls`' Win32 coarseness, thumbnail's
real scope, and `build --cli` + argv → `onOpenFiles` both cold and forwarded.

Three real bugs found and fixed:

- [x] **`spotlight()` resolved `[]` instead of rejecting** — the bridge's
      `?? []` swallowed the launcher's `unsupported`, reporting "nothing
      matched" on a platform with no search backend. See the capability
      corrections section.
- [x] **A secret over 2560 bytes failed with an unexplainable message** —
      Credential Manager's cap, surfaced as bare "credential write failed"
      (Win32 1783). Now rejected up front naming the size and the limit.
- [x] **`build --cli` printed a Unix `ln -sf` hint on Windows.**

- [x] **The 0.30 scaffold names on Windows** — from a live scaffolded app, the
      same check the Linux sweep made: all seven dialog calls are functions on
      `tiny.dialog.*` and ZERO remain on `tiny.win.*`, and `win.setTitle`,
      `win.setSize`, `menu.set`, `quit` and `api.call` all exist. So the
      buttons `tinyjs new` ships call things that exist here too.

Two long-open questions also got closed, both negative, both from a **built**
app rather than a dev spawn: `app.icon` cannot move the taskbar button even
with an AppUserModelID, and WebView2's `SpeechRecognition` constructs but never
fires. Details in their own sections.

Not covered, and still needing a hand or hardware: `startDrag`/`startResize`
(a modal OS loop that only ends on a real mouse-up), `dialog.openFiles`'
multi-select panel, a real Windows Hello verification, and media keys.

## Driving these checks headlessly on Windows

No clicking needed, and worth reusing — the decorations live outside the app
window, so the app's own logs can never confirm them:

- `TINYJS_HTML=<abs page> tinyjs dev` from any app dir, started via
  `Process.Start` so the run is scriptable, with `tinyjs.cmd` output redirected
  to a file (`[web] …` log lines land there).
- Screenshot the taskbar strip on a timer and diff frames. **Call
  `SetProcessDPIAware()` first** — `CopyFromScreen` works in physical pixels
  while an unaware process sees DPI-scaled bounds, so the first attempt here
  photographed the wrong part of the screen entirely. Taskbar rect comes from
  `SHAppBarMessage(ABM_GETTASKBARPOS)`.
- A *flash* is only distinguishable from a static button by consecutive-frame
  pixel diffs oscillating; a single frame proves nothing. Same for progress —
  compare the bar across held states rather than trusting one shot.
- **Diff against a reference frame from INSIDE the app's run.** Windows 11
  centres taskbar buttons, so the app's own button appearing shifts the whole
  strip sideways: a diff against a pre-launch frame is tens of thousands of
  changed pixels of layout shift, and a 770-pixel badge is invisible inside
  that. Pick a frame after the window is up but before the call under test.
- Have the page print `MARK <ms> <label>` at each state change and align frames
  to it afterwards, rather than trying to synchronise the two processes. The
  screenshotter's own start-up (Add-Type compiling) is seconds of skew.
- `WM_GETICON` read off the live window separates "the call did nothing" from
  "the call worked and the shell ignored it". That distinction is the whole
  point of this file, and it is what caught the `app.icon` taskbar finding.
- **Window relations are Win32 questions, not page questions** (2026-08-01):
  `GetWindow(h, GW_OWNER)` for a `parent:` relation, `EnumWindows` order for
  z-order (front to back), `IsWindowVisible`/`IsIconic` for what a minimize
  did, and the taskbar's own **button count via UI Automation** over
  `Shell_TrayWnd` for "does this window get a button" — a launcher with 7
  windows and 3 owned ones reads "launcher-win - 4 running windows", which is
  the claim stated as a number.
- **The common file dialog and UI Automation, the parts that cost time:** the
  dialog is NOT reachable as a child of the UIA root element (a scan of the
  desktop's children never finds it, though the taskbar is right there);
  `AutomationElement.FromHandle()` on its `#32770` HWND works fine, so find
  the HWND with `EnumWindows` first. Inside it, the file-type filter is a
  **Pane** with AutomationId `1136` whose *Name* is the live pattern
  (`*.md;*.txt`) — a `ControlType.ComboBox` query returns NOTHING and reads
  exactly like a missing filter. The file list is the `UIItemsView` element
  and its children are the visible names, i.e. the filter's effect as data
  rather than pixels. To drive one: `WM_SETTEXT` the visible `Edit` child and
  `BM_CLICK` the `&Open`/`&Save` button (both marshal cross-process);
  **SendKeys does not reach it** from a background script, and neither does
  `SetWindowText`. A folder path in the name box navigates there; `"a" "b"`
  in it multi-selects. Expanding the dropdown for a photograph needs a real
  click at the combo's `GetWindowRect` centre — and `SetProcessDPIAware()`
  first, or the coordinates are scaled wrong (this box is a 2x display).

## Photographing a window on Linux — the rig this file waited for (2026-08-01)

Every Linux entry above this date says "pixels unseen", because GNOME's
screenshot and `Shell.Introspect` D-Bus APIs are locked to portal callers on
this box. They are not the only way in. **Run the app under XWayland
(`GDK_BACKEND=x11`) and every window becomes an X11 window that `xwd` can
dump**, with no portal, no permission and no compositor cooperation:

```sh
xprop -root _NET_CLIENT_LIST          # window ids; _NET_WM_NAME to tell them apart
xwd -id 0x80002e -out shot.xwd        # the window's own pixels
python3 xwd2png.py shot.xwd shot.png  # ~40 lines: 25 big-endian header ints,
                                      # then rows of bytes_per_line, masks for RGB
```

What it cost to learn:

- **The window must be unobscured.** `XGetImage` on a covered window returns
  whatever is in the backing store — a satellite behind another window came
  out a flat fill of the WRONG page's background colour, which looks exactly
  like a page that failed to load. Raise it first, and sanity-check that the
  capture has more than one colour.
- **Decorations live in mutter's FRAME window, not ours.** Our client window
  is exactly the page box (that's how the size contract reads on X11), so a
  titlebar capture needs the parent: `xwininfo -id <client> -tree` →
  "Parent window id". That is how the close-button check above got its
  before/after.
- **Alpha survives.** A transparent window dumps at depth 32 with a real
  alpha byte, so compositing it over a lurid colour separates "transparent
  corners" from "black square" — the ball question, closed at last.
- **The dock is still out of reach.** Ubuntu Dock is a GNOME Shell surface,
  not an X window, so badge and progress remain the one thing here that
  genuinely needs a human's eye.

Also worth knowing, learned the same day: **the launcher's XTest
`keystroke` reaches a modal native dialog.** `gtk_native_dialog_run` spins a
nested main loop and the socket dispatch keeps running inside it, so a page
awaiting `dialog.openFile()` can still be told to press Escape, Tab, arrows
or Shift+Down at the chooser — which is how the file-type filter and
multi-select got driven with no hands. GNOME's own shortcuts (super+up to
maximize) are compositor-level and do NOT arrive this way; use the app's own
verbs (`win.zoom()`) for those.

## Per-window menu bars — Windows proven, macOS and Linux UNRUN (2026-07-28)

`tiny.menu.set` became the APP menu (every window draws it), plus
`tiny.win.menu.*` for one window's own and `chrome.menu:false` to hide one
window's bar. Written on **Windows**, which is the only platform it has been
compiled or run on.

| check | Windows | macOS | Linux |
| --- | --- | --- | --- |
| a secondary window shows the app menu | ✅ seen — nib's doc window, plus a scripted app measuring its own frame | ⬜ (always did — one shared bar) | ✅ measured 2026-07-28 — `win.menu.get` answers from every window and the bar's 26px row shows in `outer`; pixels themselves unseen |
| `size` is still the page's box with a bar | ✅ declared 700x400 → 700x400 page, frame grew 19px | n/a (the bar isn't in the window) | ✅ 460x420 → 460x420 both sessions — **after a fix; it was flaky on X11** (see below) |
| a window opened later inherits the app menu | ✅ seen | ⬜ | ✅ two later windows, both carrying it — one opened after a `menu.update`, showing the UPDATED state |
| `win.menu.set` overrides that window only | ✅ own id present, app id gone, other windows untouched | ⬜ **the focus swap is the risky part** | ✅ same three-way check |
| `win.menu.reset` goes back to the app menu | ✅ seen | ⬜ | ✅ and to the CURRENT app state, post-update — after a fix (below) |
| `win.menu.update` moves one window's tick | ✅ seen | ⬜ | ✅ other windows and the app spec untouched |
| `menu.update` moves every window's copy | ✅ seen | ⬜ | ✅ main + inheriting sat; overridden sat untouched |
| `chrome.menu:false` hides one bar only | ✅ frame 19px shorter, page unchanged | n/a — no-op by design | ✅ outer −26px, page box unchanged — **after a fix; it used to hand the row to the page** |
| items survive a hidden bar (accelerators) | ✅ `menu.get` still answers | n/a | ✅ `menu.get` answers while hidden |
| a closed window's items leave the registry | ✅ by construction (WM_DESTROY) | ⬜ | ✅ `menu.update` after a close is fine, survivors still answer |
| `{role:'edit', items}` — items under Select All (mac) / an "Edit" menu of just them (win, linux), in the declared slot; click, key, `menu.update`/`get` all reach them; bare `{role:'edit'}` still draws nothing off-mac | ✅ 2026-09-22 — seen by hand (kitchen-sink Edit card) | ✅ 2026-09-22 — seen by hand: items under Select All in the declared slot; `menu.get`/`update` reach them | ✅ 2026-09-22 — seen by hand (kitchen-sink Edit card) |
| stock roles in edit items (`{role:'standard'}` last, `{role:'copy'}` …) — order as declared; Copy greys out with nothing selected; an `enabled:false` item of ours stays grey after the menu opens (validateMenuItem via tag) | superseded — was ✅ for dropping roles; they render now (see the off-mac row below) | ✅ 2026-09-22 — all 6 shapes seen by hand (order, Copy greyed, disabled item stays grey) + `menu.get` read back | superseded — was ✅ for dropping roles; they render now (see the off-mac row below) |
| `standard:false` — no stock items; no items = no Edit menu; **⌘A/⌘C/⌘V/⌘Z/⌘⇧Z still work in a textarea** (install_stock_key_fallback); an app item bound to ⌘C wins over the fallback | n/a | ✅ 2026-09-22 — seen by hand: no-Edit and own-items-only menus, ⌘A/⌘C/⌘V/⌘Z work in a textarea; an app item on ⌘C gets the click and the clipboard is untouched, while ⌘A/⌘V still fall back | n/a |
| stock roles render off-mac (2026-09-22 change) — kitchen-sink "same on every OS": Undo…Select All + Find, shortcut text shown; clicking each works on a textarea selection; Ctrl+C/V still work in text fields (never claimed); **Linux**: Copy greys out with nothing selected; right-click `setContext` with `{role:'copy'}` works (Windows: also in a SECONDARY window — the ctx popup foregrounds main); tray skips roles | ✅ 2026-09-22 — checked by hand (kitchen-sink "same on every OS") | n/a (always rendered) | ✅ 2026-09-22 — checked by hand; context-menu undo/redo needed b68cf75 (no stock action for them in WebKitGTK) — Undo/Redo verified via XTest right-clicks |
| a menu declared before `{role:'edit'}` keeps its items | ✅ fixed + read back off the live `HMENU` — nib's File, 20 items | ✅ always worked | ✅ appends through a pointer, never flushes |
| a page-gated menu action fires in a fresh window | ✅ fixed — `document.hasFocus()` is true from load now (was false until the first click) | ✅ webview is first responder at once | ✅ `document.hasFocus()` true at load in every fresh secondary, both sessions — no grab_focus needed |
| `setHideOnClose` + closing the last window quits | ✅ fixed (`can_live_hidden`) — process and backend both exit | n/a — the Dock is the way back, flag unchanged | ✅ both halves, driven by a real WM_DELETE_WINDOW: no tray → process exits; tray → page goes `visible→hidden` and the app lives on |

What to watch for on **macOS**: the bar now follows focus through an
`NSWindowDidBecomeKeyNotification` observer, and per-window menus are only as
good as that. Open two windows, give one its own menu, ⌘\` between them — the
bar should swap and swap back, no flicker, no wrong bar left up. A save panel
or an alert taking key must NOT swap it (`winid_for_window` answers "" for
windows that aren't ours and the observer skips those). And a window with its
own menu closing while key must put the app menu back.

What to watch for on **Linux**: every secondary window now gets a `GtkBox` +
`GtkMenuBar` + its own `GtkAccelGroup` at creation, where before the webview
went straight into the toplevel. Confirm in this order — a plain secondary
window still looks and behaves exactly as it did (nothing mis-packed, webview
still fills), then the bar appears at all, then `win.open({ size })` still
lands on the page's box once the bar has taken its row. That last one is the
same repair main needed when its bar first appeared.

The Windows numbers came from a scripted app that opens windows and reports
`getState().outer.height - height` (the frame's share) for each. Worth
rebuilding on the other two: it turns "is the bar there" into a number, which
is the only way to tell a missing bar from one drawn over the page.

**Linux column filled 2026-07-28** (Ubuntu 24.04 aarch64, GNOME 46, Wayland +
XWayland, first-ever compile of this code) by exactly that kind of scripted
app — a driver window commanding two satellites over `tiny.store`, every row
settled by numbers (`getState` page/outer boxes, `menu.get` read back off the
live registry). The accelerator row was driven for real on X11: the
launcher's own XTest `keystroke('ctrl+shift+m')` produced the `menu` event
(remember the event then broadcasts to every window — apps filter by id, so
"which window fired" is not observable from pages, by design). On Wayland the
keystroke resolves `ok:true` and nothing can hear it, as capabilities
honestly report. Pixels remain unseen as everywhere else in this file — but a
bar with a measurable 26px row that answers `menu.get` and fires accelerators
is drawn, or GTK is lying on three channels at once.

**Three real launcher bugs found and fixed the same night**, all in the
never-compiled code:

- **The birth-size repayment raced X11's async layout, losing the bar's row
  for random windows.** `apply_menus` repaid the page box in a single idle;
  on X11 the bar often has no allocation yet at that point, so
  `menubar_height()` read 0, the resize was a no-op, and `room_given` was
  already spent — three windows opened the same way came out 460x420,
  460x394, 360x254. Wayland lays out fast enough to hide it. Now
  `repay_page_box()` polls (16ms, bounded) until the bar has a height.
- **`chrome.menu:false` handed the bar's row to the page** (420 → 446) with
  the outer size unchanged — Windows shrinks the frame and keeps the page
  box, which is what the size contract says. The toggle now measures the
  page box before the flip and restores it after.
- **`menu.update` evaporated on rebuild.** It patched live widgets and the
  registry but never the stored specs bars are REBUILT from — so a
  `win.menu.reset`, a `chrome.menu` toggle, or a window opened later all
  resurrected the stale checked/label state. The macOS bar can't have this
  bug (one live NSMenu, nothing rebuilds); **the Windows launcher has the
  same rebuild-from-spec architecture and should be checked for it** — open
  a window AFTER a `menu.update` and see which state it shows.
  **Checked on Windows 2026-08-06, and it had the bug — now fixed.** A driver
  page set an app menu with `{id:'tick', label:'Tick me', checked:false}`,
  called `menu.update('tick', {checked:true, label:'Ticked!'})`, then opened a
  satellite that read its own bar back: `win.menu.get('tick')` in the NEW
  window answered **`"Tick me"`, `checked:false`** while `menu.get('tick')`
  (the registry) correctly said `"Ticked!"`, `checked:true` — the two
  disagreeing is the bug stated as data. `do_menu_update` now patches
  `g_app_menu` (and any window's own override) alongside the live `HMENU`s;
  re-run after the fix, the satellite reads `"Ticked!"`, `checked:true`.
  Rig traps this cost, both mine not the launcher's: `tiny.win.open` is
  `open(id, opts)` — passing one object sends `id` as an object and the
  satellite never opens; and a satellite page with an unclosed `</script>`
  loads (the client boots, `client.hello` and `win.getState` reach the wire)
  while its own script never runs, which reads exactly like dead RPC in a
  secondary window. Stamp progress into `document.title` — a window title is
  readable from outside and survives a page that can't call anything.

**Two testing gotchas that produced convincing ghost failures first**, worth
knowing before trusting any multi-window store-choreographed page on Linux:

- **WebKitGTK suspends a fully occluded window's page.** Two same-size
  satellites stack exactly on Wayland (x/y in `win.open` are ignored there —
  the compositor places windows), the covered one's timers all but stop, and
  its command loop lags the whole run — replies arrive eventually, carrying
  states from the wrong era. Unfocused-but-visible pages run fine; give
  satellites different sizes so nothing is ever fully covered.
- **`tiny.store` persists across runs, so reply keys must carry a run
  nonce.** A re-run of a page that waits on `store` keys written by a
  previous run reads the OLD replies instantly and reports a full set of
  plausible, internally consistent, completely stale results.

## The fetch repair shim — verified on all three, POST bodies fixed on all three (Bug D)

bridge.js now wraps `globalThis.fetch` (search "fetch repair shim"): follows
redirects hop-by-hop and hands exactly two broken cases to the system curl —
root-path URLs (txiki emits `GET //`; strict CDNs 404) and TLS 1.2-only
hosts (`mbedtls connect -1 5 0`). Full background: TODO-txiki.md. Every
`tiny.fetch` and backend `fetch()` in every app now goes through it.

> **2026-08-14 — the excepted path was finally exercised, and it fails.**
> A SMALL POST body through the shim never settles: the shim pipes request
> bodies to curl over `--data-binary @-`, which is Bug C's broken stdin
> path. 7 bytes hangs forever; 320 KB is fine. The request itself SUCCEEDS
> (the listener logs the body and replies) — only the caller's promise never
> resolves, so it presents as a dead network on a request that was answered.
> Filed as **Bug D** in TODO-txiki.md with a repro and a temp-file fix.
> Measured macOS arm64 at first; since seen on Linux too, but NOT on Windows
> (2026-09-28 notes below):
>
> - [x] **Windows** — the temp-file fix works; the hang itself never reproduced here (2026-09-28, note below)
> - [x] **Linux** — the hang reproduces AND the temp-file fix clears it (2026-09-28, note below)
>
> **2026-09-28 — temp-file fix landed** (TODO-txiki.md Bug D). macOS arm64:
> a string and a binary body POSTed to a root-path URL both echo intact from
> a raw Python listener, and no `tinyjs-body-*` dirs are left in tmp. The
> two boxes above now mean "the fix works there". On Windows, check that the
> `@C:\...\body` path survives `launcher --run`. It has spaces when the
> user's profile does.
>
> **2026-09-28, Windows 11 (tjs v26.6.0, launcher-win.exe)** — the temp-file
> path is good, including the spaces. A tjs TCP listener speaking raw HTTP/1.1
> (NOT `tjs.serve` — see the note below) answered `POST /` with the body echoed
> back; the client imported runtime/bridge.js and used the shim's `fetch`, and
> the listener logged `user-agent: curl/8.21.0`, so every run really went
> through curl. All completed in 50–350 ms, none hung:
> - string 7B / 2KB / 300KB and Uint8Array 7B / 64KB, bodies byte-identical
>   on arrival, run with and without `TINYJS_LAUNCHER` set (so both the plain
>   `tjs.spawn` and the `launcher --run` CreateProcessW re-quoting)
> - TMP/TEMP pointed at `C:\Users\tarwin\tmp space (test)` and at
>   `C:\Users\tarwin\tmp sp&ce (x) 'q'` — `quote_arg` in launcher-win.cc gets
>   `@<path>\body` across intact, spaces, `&`, parens and quotes included
> - `Expect: 100-continue` forced by the caller: 200 when the listener answers
>   the interim block (the shim skips the 1xx) and 200 after curl's 1s wait
>   when it never does
> - no `tinyjs-body-*` dirs left behind once the process outlives `p.wait()`
>   (a process that exits the instant the response is read skips the cleanup —
>   the dir is in tmp, so it's litter, not a leak)
>
> The HANG never reproduced on Windows: the pre-fix `runtime/bridge.js`
> (HEAD~1, stdin-piped `@-`) answered the same 7-byte POST in 179 ms. So Bug D
> looks not-libuv-generic after all — Windows was fine either way, and this
> box means the fix is safe here, not that the bug was seen.
>
> **2026-09-28, Linux arm64 (Ubuntu 24.04 VM, tjs v26.6.0, curl 8.5.0)** —
> raw Python HTTP/1.1 listener echoing `POST /`, client importing
> runtime/bridge.js under `bin/tjs run`; listener logged `curl/8.5.0` on every
> request. Fixed shim: string 7B / 2KB / 300KB and Uint8Array 7B / 64KB all
> byte-identical, 3–16 ms. Pre-fix bridge.js (HEAD~2, `@-`): 7B, 2KB, u8 7B
> and u8 64KB all HUNG (5s cap) although the listener received and answered
> each one; only 300KB completed. So unlike Windows, Linux has Bug D exactly
> as macOS does. Also: TMPDIR=`…/tmp sp&ce (x) 'q'` passes the same five;
> an aborted in-flight POST rejects (`curl exit SIGTERM`) and leaves no
> `tinyjs-body-*` dir; none left behind after any run.
>
> Note for whoever verifies: **`tjs.serve` cannot be the peer.** It
> normalizes `//` back to `/`, so a txiki client against a txiki server
> looks healthy while the wire carries the bug. Use a raw `nc` listener.

Verified on macOS (unit via importing bridge.js under `tjs run`, then
end-to-end: amp's podcast backend through a TINYJS_HTML driver page — the 8
formerly-broken FAVES feeds all load, a real 404 stays 404, redirect chains
report the final `res.url`, somafm streams live through it, POST bodies
echo — but see the 2026-08-14 note above: that echo was a LARGE body, which
is exactly the size class Bug D spares. Small ones hang). Never run on:

- [x] **Windows** — verified 2026-07-30. Built amp: the podcasts all load
      and streams play (Tarwin, by hand — page → tiny.fetch → backend →
      shim → curl, plus a live stream, which is the streaming ReadableStream
      end of it). Instrumented alongside, in a purpose-built GUI-subsystem
      app: 19 curl hops to rss.art19.com + anchor.fm returning 200 with sane
      body lengths, so curl resolves off PATH in a packaged app and the `-i`
      header-block parse off the stdout pipe holds. Windows-only bug found
      and fixed on the way — every hop flashed a console window until the
      shim routed through `launcher --run` (CHANGELOG 0.30.0), which neither
      other OS could have shown. NOT exercised anywhere in this: a request
      body via `stdin: 'pipe'` + `getWriter()`, since nothing in amp POSTs.
- [x] **Linux** — verified 2026-07-30 (Tarwin, by hand): the formerly
      broken FAVES feeds load through amp's podcast window, which is the
      full stack — page → tiny.api → backend fetch → shim → curl.

[TODO-windows.md]: TODO-windows.md
[TODO-linux.md]: TODO-linux.md

## Window-state events + windowControlsPos — built 2026-07-30, macOS-verified only

`tiny.win.onState` / `export onWindowState` (all three launchers) and
`chrome.windowControlsPos` (macOS-only by design; the other two carry the
wire field and ignore it). macOS proven end-to-end by a self-driving page:
14/14 checks — enter/exit fullscreen, minimize/restore, disposer removes
exactly its own handler, backend export sees the same stream, lights move
9,9 → 40,30, survive a resize, report through `getState().chrome`, reset
clean. Kitchen-sink has interactive cards for both (Window sub-tab: "Window
state, pushed"; Chrome sub-tab: the windowControlsPos chips).

The other two are compiled-never-watched (Windows: WM_SIZE/WM_ACTIVATE in
both wndprocs + set_fullscreen; Linux: the already-connected
window-state-event signal now emits):

- [~] **Windows** — **core verified 2026-08-05**, launcher rebuilt from
      source first (it was two days stale again — predating the site-wrapper
      client changes). Headless drill: a TINYJS_HTML driver page recorded
      every `win.onState` event while driving itself, then opened a satellite
      (absolute-path `page:`) that drove ITSELF — window ops are scoped to
      the calling page, so the satellite has to run its own script. All four
      flags move: `minimize()` → `minimized:true` then `focused:false` (two
      events — WM_SIZE lands before WM_ACTIVATE, so there's a ~1ms
      `minimized:true, focused:true` state first; not spam, both are real
      transitions); `restore()` → one event; `zoom()` toggles `maximized`
      on and off; `setFullscreen(true/false)` moves `fullscreen`. The
      satellite's events carry **`win:'sat'`, not `main`** — the claim this
      box existed for — and focus moves both ways with the right ids:
      opening sat gave `main focused:false` + `sat focused:true`, sat's
      minimize handed focus to main, its restore took it back, its close
      returned it. 21 events, every one a distinct transition — deduped.
      Still needing a hand: `maximized` from the caption button, and
      no-spam during a live drag-resize (both need a real mouse).
- [x] **Linux (X11)** — verified 2026-08-01, launcher rebuilt from source.
      All four flags move: `setFullscreen(true/false)` → `fullscreen`
      true/false; `win.zoom()` → `maximized` true then false (cross-checked
      against `_NET_WM_STATE_MAXIMIZED_HORZ/VERT` on the live window);
      `minimize()` → `minimized:true`; and focus moves both ways with the
      right `win` field — opening a satellite gave `other focused:true` +
      `main focused:false`, closing it handed focus back. Deduped: one event
      per transition, no repeats.
      **Found a real bug doing it, and FIXED it the same day: `win.restore()`
      did not restore on GNOME.** `do_winop`'s `restore` was a bare
      `gtk_window_deiconify()`, which Mutter's focus-stealing prevention
      ignores — measured twice, the window stayed `_NET_WM_STATE_HIDDEN` and
      merely gained `_NET_WM_STATE_DEMANDS_ATTENTION`, while the call resolved
      `true` and no window-state event followed (correctly — nothing changed).
      Exactly the fire-and-forget trap this file exists for. The tell was that
      `win.show({ activate: true })` DID un-minimize, and its op is
      `gtk_widget_show` + **`gtk_window_present`**.
      `restore` now deiconifies and then presents — `present_with_time` with
      `gdk_x11_get_server_time()` on X11, so the WM has a real timestamp and
      no reason to read it as a steal, plain `present()` elsewhere.
      Re-verified after the fix: `_NET_WM_STATE_HIDDEN` →
      `_NET_WM_STATE_FOCUSED`, no DEMANDS_ATTENTION, and the events arrive
      (`minimized:false` then `focused:true`). Wayland re-checked too: still
      resolves, still no warnings, `minimized` still silent by design.
- [x] **Linux (Wayland)** — verified 2026-08-01, same drill. Fullscreen and
      focus flags move, `win.zoom()` moves `maximized`, and `minimize()`
      resolves `true` while `minimized` NEVER reports — the only event it
      produces is `focused:false`. Documented behaviour, not a bug.
- [~] **Windows + Linux** — the windowControlsPos chips in kitchen-sink's
      Chrome sub-tab must be a clean no-op: chrome line reports "(macOS
      only — ignored here)", nothing breaks, and the extra tab-separated
      CHROME/WINOPEN wire fields don't disturb the older parsers' fields.
      **Linux half done 2026-08-01**: `setChrome({ windowControlsPos: {x:40,
      y:30} })` resolves `true`, `getState().chrome` comes back with no
      `windowControlsPos` key at all (no echo of a thing that didn't
      happen), and every other chrome field reads normally afterwards — the
      wider CHROME line didn't disturb the parse. **Windows half done
      2026-08-05**, same shape, headless: `setChrome({ windowControlsPos:
      {x:40,y:30} })` and the `null` form both resolve `true`,
      `getState().chrome` has **no `windowControlsPos` key at all**, and a
      follow-up `setChrome({ windowControls: ['close'] })` still lands
      (reads back `['close']`) — the wider CHROME line doesn't disturb the
      parser. A satellite born with `windowControlsPos` in WINOPEN field 14
      opened at exactly its declared 300x200 page box with sane chrome. Both
      halves are now done; what remains is only the kitchen-sink chip UI
      saying "(macOS only — ignored here)", which is cosmetic.

## Off-screen window rescue (2026-07-30, policy verified macOS only)

The bridge arms rescue ONLY when the screen fingerprint (`__screens` in
the app store) differs from the previous run; armed, each window's first
show / first pos gets a `WINOP onscreen` chaser that clamps a window with
less than a 24px-square sliver visible onto the nearest monitor. Ordinary
off-screen moves are never touched (coo3d flings windows off-screen on
purpose). `win.ensureOnScreen()` is the manual verb;
`"offscreenRescue": false` kills the automatic parts. The launchers also
sweep VISIBLE windows when a display departs mid-session
(WM_DISPLAYCHANGE / GDK monitor-removed / NSScreen params notification —
gated by the same manifest flag via `WINOP rescue 0`).

Verified on macOS headlessly: dormant boot leaves (5000,5000) alone,
manual verb clamps, forged-fingerprint boot chases the first pos and
leaves the second. Compiled-never-watched elsewhere:

- [~] **Windows** — **policy verified 2026-08-05**, headlessly (launcher
      rebuilt from source first). Forged `__screens` boot: the fingerprint
      was detected and rewritten to the real one, and the first
      `setPosition(9999,9999)` was chased to **755,361 — exactly
      `work area − outer frame`** (1728−973, 1037−676), i.e. clamped flush
      into the corner, fully visible; a SECOND setPosition to 9999,9999
      stuck (the chaser is first-pos-only, matching the macOS measurement);
      `win.ensureOnScreen()` clamped it back. Unforged relaunch (dormant):
      the same first setPosition **sticks at 9999,9999** — rescue stays out
      of the way, the coo3d case — and the manual verb still clamps. Note
      the clamp respects the taskbar: y lands on the work area (1037), not
      the screen height (1084). Still unrun: unplugging a monitor holding a
      window mid-session (WM_DISPLAYCHANGE sweep) — needs hardware.
- [~] **Linux (X11)** — run 2026-08-01, and the interesting part is that
      **the policy is unobservable on GNOME, because Mutter never lets a
      window off-screen in the first place.** `setPosition(5000,5000)` on a
      960x640 window came back 264,128 — i.e. exactly `screen − window`
      (1224−960, 768−640), Mutter's own constrain pass parking it flush in
      the corner; a secondary opened at `x:9999,y:9999` likewise landed at
      924,568 (1224−300, 768−200). That happens identically on a DORMANT
      boot and on a forged-fingerprint ARMED one, so armed-vs-dormant can't
      be told apart here by geometry, and note the corollary: **the "apps
      fling windows off-screen on purpose" case (coo3d) simply doesn't work
      on GNOME** — the WM refuses regardless of what we do.
      What IS settled: the armed path runs clean — the forged `__screens`
      was detected and rewritten to the real fingerprint, the `WINOP
      onscreen` chasers fired against both main and a secondary, and the
      launcher logged **zero GTK warnings/criticals** (only the unrelated
      libEGL DRI3 lines XWayland always prints). Whether the clamp
      arithmetic is right needs a WM that permits off-screen windows.
      **Clamp arithmetic verified 2026-10-14** under a WM-less Xvfb
      (1280x800, so the work area is the whole screen and nothing but our
      code constrains the window) — x86_64, `ubuntu:22.04` container,
      launcher from `fe7bf71`. A 960x640 main, three boots of a
      self-driving page: first-ever and unforged second boot (dormant):
      `setPosition(9999,9999)` **sticks at 9999,9999**, twice;
      forged `__screens` boot (armed, and the fingerprint was rewritten to
      the real one): the first `setPosition(9999,9999)` is chased to
      **320,160** (1280−960, 800−640), the second sticks — first-pos-only, as
      on macOS and Windows. `ensureOnScreen()` in every boot: 9999,9999 →
      320,160; −5000,−5000 → **0,0**; half-off-screen 1000,100 (280px
      visible) **left alone**; a 20x20 sliver at 1260,780 (under the 24px
      rule) → 320,160. Still unrun: main only, not a secondary; and the
      monitor-removed sweep mid-session (needs a display to unplug).
- [x] **Linux (Wayland)** — verified 2026-08-01: a clean no-op. Every
      `getState()` reports 0,0 (gdk has no global origin there),
      `setPosition` changes nothing, and a forged-fingerprint armed boot
      firing `onscreen` at main and a secondary produced no warnings, no
      criticals and no geometry damage.
- [ ] **macOS live** — actually unplug an external display holding a
      satellite mid-session: the 0.5s-delayed pass brings it back (AppKit
      migrates most windows itself; frameless ones are the interesting
      case).

## tiny.audio.sampler — DONE: verified on all three platforms 2026-07-31

The Linux native backend (miniaudio decode + `pw_stream` mixer in the
launcher) was proven headlessly on this Linux box the day it was built:
load/play/set/stop/stopAll/master/unload round-trips from page AND backend
(one shared hub — voice ids interleave), error paths (unknown name, bad
file, play-after-unload) reject with real messages, equal-power pan law
matches StereoPanner's spec numbers to 4 decimals via the null-sink rig
(mono −1/0/+1 and the stereo cross-mix law), 20 looping voices with 50ms
pan automation under a ~57fps rAF-hammered canvas run at ERR=0 /
~240µs-per-quantum in `pw-top`, an active `tiny.audio.filters` chain picks
the sampler stream up (metadata `target.object` → `tinyjs-eq-<pid>`, and
post-filter capture measured 0.0619 RMS against 0.0625 expected for a 0.25
linear chain), and kill -9 mid-playback leaves zero orphaned
`tinyjs-sampler-*` nodes (a pw_stream dies with its process — no linger, no
sweep needed).

The macOS/Windows page host (Web Audio in the main window, bridge re-arms
on client hello) is written to the same verbs. **Windows ran it 2026-07-31**
(drill page + `tinyjs dev` from coo3d/, results via `tiny.store.set`) — the
first two boxes below are ticked there, macOS still hasn't run at all. What
Windows has NOT proven is that sound leaves the speakers: nobody listened.
The evidence short of ears is that a page-created `AudioContext` came up
`running` with no gesture ever and its clock advanced 0.71s in 0.7s of
wall time, and that every `load()` resolved, which only happens after
`decodeAudioData` really decoded the bytes.

**Closed out 2026-07-31:** Tarwin hand-tested the sampler apps on macOS and
Windows (Linux already instrument-verified above) and heard them working —
the remaining boxes below are ticked on that basis rather than on their
instrumented drills, so if a subtle regression ever needs chasing, the
drills as written are still the way to pin it down.

- [x] **mac/win basics** — the page1.html drill from the Linux pass (see
      git history of this entry, or improvise: load path + bytes, play,
      set, stop, master, unload, error paths) lands `ok: true` in
      store.json with `cap: 'page'`.
      **Windows 2026-07-31:** `cap: 'page'`; load by ArrayBuffer and by path
      (real mp3) both ok; play → handle, live `set({pan})`, `stop`,
      `master`, `loop` (a 1.6s sample held 2.5s), `stopAll`; 40 concurrent
      plays steal rather than reject; all three error paths reject with the
      real message (unknown name, missing file, play-after-unload).
      macOS: still unrun.
- [x] **mac/win: main-window reload mid-playback** — bank re-arms without
      app code (client hello → bridge replays loads); next `play()` works;
      the voices that were sounding die, documented.
      **Windows 2026-07-31:** after `location.reload()` with no reload-side
      `load()` at all, `play()` on a pre-reload name worked, and a name
      unloaded before the reload stayed gone. macOS: still unrun.
- [x] **mac: accessory app, window never shown** — backend
      `app.audio.sampler.play()` cold: sound comes out. Then again 10
      minutes later (App Nap — an audible-idle context may lose its power
      assertion between sounds; if it does, suspend-when-idle + resume-on-
      play is the fix sketched in TODO-audio-sampler.md).
- [x] **win: autoplay with no gesture ever** — sampler starts from
      injected eval on a never-clicked hidden window. launcher-win.cc sets
      `--autoplay-policy=no-user-gesture-required` via the bridge's
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS in dev; verify a PACKAGED app
      gets it too (attach mode doesn't go through the bridge's spawn env).
      **Half done 2026-07-31:** under `tinyjs dev` a never-clicked window's
      AudioContext reached `running` and kept rendering. **Packaged half
      done 2026-08-05:** a `tinyjs build` exe launched and never clicked
      created an AudioContext + oscillator graph with no gesture —
      `state` was `running` at 500ms, still `running` at 2s, and
      `ctx.currentTime` advanced **1.552s in 1.5s of wall time**, so the
      graph really renders. Packaged apps get the autoplay flag; the
      attach-mode worry is closed.
- [x] **win: hidden main window, 5+ min playback** — GPU/CPU quiet in Task
      Manager, audio unbroken (audibly-playing pages are exempt from
      intensive throttling — trust but verify).
- [x] **pan/pitch parity across OSes** — same `{vol, pan, rate}` numbers,
      record and compare (Linux numbers above are the reference; don't
      eyeball by ear).
- [x] **bytes-load fallback path** — a bank file OUTSIDE the page's read
      root on mac (no readAccess): host's fetch(file://) fails, the
      one-shot `sampler.bytes` fallback delivers, load still resolves ok.

## Dialog file-type filters (`{ types }`) — written 2026-07-31, macOS-built only

`openFile`/`openFiles`/`saveFile` take `{ types: ['md','txt'] }`; the bridge
sends one comma-separated field on the `DLG` line, each launcher maps it
natively. macOS machine-checked same day: launcher rebuilt and ran the panel
off the new wire field without crashing, and a standalone harness proved the
exact `apply_type_filter` mapping (`md` → `net.daringfireball.markdown`,
`markdown` → dynamic UTType, `txt` → `public.plain-text`; `.png`/`.bin` don't
conform → filtered). Windows and Linux are written, never compiled — the
usual "written, compiled at best, never seen".

- [ ] **macOS — eyeball the grey-out.** `openFile({ types: ['md','txt'] })`
      in a dir with mixed files: non-matching entries dim and refuse
      selection; a call with no `types` still shows everything; `saveFile`
      with types appends the extension.
- [x] **Windows — verified 2026-08-01**, launcher rebuilt from source first.
      `openFile({ types: ['md','txt'] })`: the type dropdown reads
      `*.md;*.txt` and holds exactly two entries — that pattern and
      `All files (*.*)` — photographed with the list expanded. In a dir of six
      mixed files it lists **alpha.md, bravo.txt, NOTES.MD** and the subdir
      only: `charlie.png` and `delta.bin` are hidden, and so is
      **`echo.markdown`** — `*.md` is not `*.markdown`, so spell both if you
      want both. Uppercase `NOTES.MD` matches without help (Win32 patterns are
      case-insensitive — the Linux add-both-cases workaround is a Linux
      problem only). Switching the dropdown to All files brings all six back,
      which is what proves the filter is filtering rather than the folder
      merely looking that way. `openFile()` with NO types has **no type
      dropdown at all** and shows everything. A cancel resolves `null`.
      `saveFile({ types: ['md'] })`: "Save as type" reads `*.md`, and a name
      typed with no extension came back as `…\untitled-note.md` —
      `SetDefaultExtension` lands.
- [x] **Linux — verified 2026-08-01**, launcher rebuilt from source, and for
      once **with pixels**: an X11 window (the app runs under XWayland with
      `GDK_BACKEND=x11`) can be dumped with `xwd -id` and turned into a PNG,
      so the chooser was photographed rather than inferred — see
      "Photographing a window on Linux" below.
      `openFile({ types: ['md','txt'] })` in a dir of six mixed files: the
      filter combo reads **"*.md, *.txt"**, and the list holds **alpha.md,
      bravo.txt, NOTES.MD** and the two subdirs only — `charlie.png`,
      `delta.bin` and **`echo.markdown`** are hidden, exactly as on Windows
      (`*.md` is not `*.markdown`; spell both if you want both). Uppercase
      `NOTES.MD` matches, so the add-both-cases workaround for GTK's
      case-sensitive patterns does its job.
      The **"All files" entry exists and works**: one Tab + Down off the file
      list moves the combo to it (photographed reading "All files") and all
      six files come back — which is what proves the filter was filtering
      rather than the folder merely looking that way.
      `openFile()` with NO types has **no filter combo at all** and lists
      everything. A cancel (driven with the launcher's own XTest
      `keystroke('escape')` — it reaches the chooser, because the modal runs a
      nested main loop and the socket dispatch keeps running inside it)
      resolves **`null`**. `pickFolder` is unaffected: no combo, files greyed
      out and unselectable, folders live.
      **One real difference from Windows — found here, FIXED 2026-08-01.**
      `saveFile({ types: ['md'] })` did **not** append the extension: a name
      typed as `untitled-note` came back as `…/untitled-note`, where Windows'
      `SetDefaultExtension` returns `untitled-note.md`. GTK has no equivalent,
      so `do_dialog`'s save arm now appends the first declared extension by
      hand. Re-verified after the fix, all against the real chooser:
      `untitled-note` → **`untitled-note.md`**; a name already carrying one
      (`notes.md`) is left alone, no doubling; and `saveFile()` with **no
      types** still returns `untitled-note` bare, so nothing appends when
      nobody asked. Not run: the same save with **"All files" selected**,
      which the guard (`gtk_file_chooser_get_filter() == type_filter`) is
      there to respect — the save dialog's filter combo is not in the Tab
      chain (six Tab+Down pairs never moved it, where the OPEN dialog needed
      exactly one), so switching it needs a real click. Same limitation the
      Windows section hit.
      Caveat GTK forces on the fix: the extension is appended AFTER the
      chooser closed, so its own overwrite confirmation saw the extensionless
      name. Windows appends inside the dialog and doesn't have that hole.

## `"about": "menu"` — the click itself, 2026-08-01

macOS-only by design (the other launchers have no default About item). The
manifest flag turns the About menu item into a `MENU about` line instead of
the standard panel; the app draws its own via `onMenu('about')` / the page
`menu` event.

Machine-checked same day: launcher rebuilt with the `ABOUTHOOK` handler, a
scratch app with `"about": "menu"` ran under `tinyjs dev` and `TINYJS_DEBUG`
showed `>> ABOUTHOOK 1` on the wire, and `MENU <id>` → onMenu is the
long-standing path every custom menu item already exercises. What nobody has
watched is the click itself — System Events was TCC-denied for the driving
terminal, and a menu-item click can't be synthesized without it.

- [ ] **Click "About <app>" in an app with `"about": "menu"`** — no standard
      panel; `onMenu` receives `'about'` (and the page `menu` event fires
      with `{ id: 'about' }`).
- [ ] **Click it in an app WITHOUT the flag** — the standard panel still
      shows (the flag defaults off; a regression here would take the free
      panel from every existing app).

## `win.open({ parent })` — above-your-own-windows, 2026-08-01

New open-time option: `parent: true` (= 'main') or a win id keeps the window
above that one via the native relation — macOS `addChildWindow`, Win32 owner
(`CreateWindowExW` hWndParent), GTK `set_transient_for` +
`destroy_with_parent`. Wire: WINOPEN field 14.

macOS machine-checked same day (launcher rebuilt, scratch app, CGWindowList
z-order probe — no TCC needed): the child stays ABOVE main after
`app.show()` raises and focuses main, and closing a parent satellite closes
its attached child (`onWindowClosed` fired for both — the deferred
child-close added to `windowWillClose`, since AppKit otherwise orphans
children where Win32/GTK destroy them). Not eyeballed on macOS: the window
riding along when its parent is dragged (child windows move with the parent
— documented difference vs the other two), and parent minimize taking the
child with it.

Windows and Linux are **written, never compiled** (no cross-toolchain on
this box):

- [x] **Windows — verified 2026-08-01**, launcher rebuilt from source first
      (the checked-in exe was a day stale again). None of this is visible to
      the pages, so it was measured from outside with Win32 + UI Automation:
      `parent: true` really is the Win32 OWNER relation (`GetWindow GW_OWNER`
      answers main), the owned window sits **above** main in the z-order after
      main is raised and focused (`SetForegroundWindow` succeeded; owned
      windows at z-index 11 and 10 against main's 12), and the taskbar shows
      **4 buttons for 7 windows** — the three owned ones have none.
      Minimizing main takes both owned windows with it, and restoring brings
      them back still above main. Note they are **hidden, not iconic**
      (`IsWindowVisible` false, `IsIconic` false), so a `win.onState`
      listener sees no `minimized` for a child riding its parent down.
      An owned+frameless satellite still gets its WM_NCCALCSIZE treatment:
      declared 150x150 → client 150x150, frame 150x150, `outer` == the page
      box. A satellite owning a satellite (`parent: 'mid'`) works the same,
      and closing the owner destroyed the child with it — `WINCLOSED` arrived
      for **both**, `onWindowClosed` seeing `grand` then `mid` (child first),
      and `win.windows()` afterwards lists neither, so nothing leaks in the
      backend registry.
- [x] **Linux — verified 2026-08-01**, first-ever compile of this code
      (launcher rebuilt from source; the checked-in binary was a day stale).
      Driven by a scratch app opening five satellites — `child` (parent:
      true), `plain` (none), `orphan` (parent: 'nosuchwin'), `mid` (parent:
      true) and `grand` (parent: 'mid') — and settled from OUTSIDE, since
      none of this is visible to a page.
      **X11**: `xprop WM_TRANSIENT_FOR` answers main's window id for `child`
      and `mid`, and mid's id for `grand`; `plain` and `orphan` have no such
      property at all. Z-order from `_NET_CLIENT_LIST_STACKING` AFTER main
      was raised and focused (`_NET_WM_STATE_FOCUSED` on main, so the raise
      really happened): plain, orphan, **main**, child, mid, grand — every
      transient above its parent, the unattached ones below.
      **Wayland**: measured at the protocol layer with `WAYLAND_DEBUG=1` —
      `xdg_toplevel@41(child).set_parent(@39)` and
      `@69(mid).set_parent(@39)` where @39 is main, `@81(grand).set_parent
      (@69)`, and `set_parent(nil)` for main, plain and orphan. Z-order
      isn't a client's business there, so the request is the claim.
      **Close cascade, both sessions**: closing `mid` destroyed `grand` with
      it and **`WINCLOSED` arrived for BOTH** (`onWindowClosed` fired twice,
      mid then grand), and `win.windows()` afterwards read
      `["main","child","orphan","plain"]` — nothing leaks in the backend
      registry.
      **Bonus, matching the Windows finding**: minimizing main took `child`
      down with it (both `_NET_WM_STATE_HIDDEN`) while `plain` and `orphan`
      stayed up.
      NOT settled: **taskbar skipping**. GTK's `set_transient_for` sets no
      `_NET_WM_STATE_SKIP_TASKBAR` (measured: `_NET_WM_STATE` is empty on
      the transients), so whether a transient gets its own entry is purely
      the shell's convention, and Ubuntu Dock groups by app rather than
      per-window — there is nothing here to observe it with. Unlike Win32,
      where an owned window provably has no button, treat "no taskbar entry"
      as unproven on Linux.
- [x] **All three — `parent` naming a nonexistent id** opens a normal
      unattached window rather than failing (macOS machine-checked; the
      other two follow the same null-check shape). **Windows confirmed
      2026-08-01**: `parent: 'nosuchwin'` gave a window with owner `(none)`,
      its own taskbar button, and the declared page box — indistinguishable
      from a plain satellite. **Linux confirmed 2026-08-01**, both sessions:
      `orphan` had no `WM_TRANSIENT_FOR` on X11 and got `set_parent(nil)` on
      Wayland — byte-for-byte the same treatment as the satellite opened
      with no `parent` at all.

## Menu combos the app doesn't currently claim leak to the webview's own accelerators (2026-08-01)

Found from nib on Windows 11: **Ctrl+P put up WebView2's print preview
instead of Open Quickly.** The first guess — that `put_Handled(TRUE)` in
`AccelHandler::Invoke` (native/launcher-win.cc) fails to suppress a browser
accelerator — is **wrong, measured wrong**. With a folder open, so that nib's
`quickopen` item is enabled, Ctrl+P opens the palette and no print sheet
appears: Handled does exactly what it says.

The hole is the `reg->enabled` term in that match loop:

```c
if (reg->kind == "menu" && reg->owner == owner && reg->enabled &&
    reg->key.size() == 1 && ...)
```

A **disabled** item doesn't match, so nothing calls `put_Handled`, so the key
falls through to the webview — and WebView2 has a whole set of built-in
browser accelerators no native app has: Ctrl+P print, Ctrl+F find-on-page,
Ctrl+R/F5 reload, Ctrl+O open, Ctrl+Shift+I devtools. nib greys Open Quickly
out with no folder open, and that is the entire bug: Ctrl+P with no folder →
print sheet; Ctrl+P with a folder → palette. macOS never had it, because a
disabled `keyEquivalent` is still consumed by the menu (AppKit beeps), and
the webview underneath has no competing default.

### What to change in the tinyjs repo

Preferred, and it's small: **a disabled item should still swallow its
combo.** In `AccelHandler::Invoke`, match without `reg->enabled`, then

```c
args->put_Handled(TRUE);
if (reg->enabled) pipe_write_line("MENU " + reg->id);
```

That reproduces AppKit's semantics exactly — the accelerator belongs to the
menu whether or not the item can fire right now — and needs no new API.

The second half of the fix (decided 2026-08-02): the combos are otherwise
still reachable by an app that has no menu item for them at all — an app
with no Find menu gets WebView2's find bar on Ctrl+F, and Ctrl+R reloads
the page out from under it. Decision: **the browser set is suppressed by
default.** `ICoreWebView2Settings3::put_AreBrowserAcceleratorKeysEnabled(FALSE)`
turns the whole set off in one call; a new tinyjs.json key
`"browserAccelerators": true` opts back in (same shape as `contextMenu` —
manifest key → createApp option → one startup pipe line → launcher setting;
see `CTXSUPPRESS` in bridge.js). QI-guard Settings3: on a runtime too old to
have it, keys stay enabled, which only means old behavior, not breakage.
`tinyjs dev` should keep devtools reachable regardless (force-enable in dev,
or open devtools programmatically via a dev menu item). Note the setting does
NOT affect the AcceleratorKeyPressed event, so menu accelerators keep working
and it composes with the disabled-item fix above rather than replacing it.

- [x] **Windows** — **verified 2026-08-03**, launcher rebuilt from source first
      (the checked-in exe was two days stale — again). A probe page under
      `TINYJS_HTML` registered `{id:'dis', key:'p', enabled:false}` alongside
      `{id:'en', key:'k'}`, fired the combos through the launcher's own
      `app.keystroke`, and ran with **`"browserAccelerators": true`** — without
      that the print sheet is suppressed for an unrelated reason and the test
      proves nothing.
      Item **disabled**: **no `MENU` line**, **no print sheet**
      (`beforeprint` never fired), and the page saw only the bare `Control`
      keydown — the `p` was swallowed, exactly the AppKit semantics the fix
      set out to copy. `menu.update('dis',{enabled:true})` and re-fire: `dis`
      arrives, still no print sheet. `ctrl+k` → `en`, unaffected.
      **The control is what makes those numbers mean anything**: with the same
      page, same run, same opt-in, a menu where **nothing claims ctrl+p** put
      the print sheet up — `beforeprint` fired once and `ctrl+p` reached the
      page. So the rig can see a print sheet, and the disabled item is what
      stops it.
      Detector note for anyone re-running this: WebView2's print preview is
      drawn **inside the webview**, not as a top-level window — an
      `EnumWindows` sweep never sees it, and a page-side `beforeprint`
      listener is what catches it. (DevTools, by contrast, IS its own window.)
- [x] **Windows — default suppression, verified 2026-08-03** by flipping one
      manifest key and re-running the identical page. Unclaimed **Ctrl+P**:
      `beforeprint` **1 → 0** the moment `"browserAccelerators"` came out of
      tinyjs.json. Unclaimed **Ctrl+F**: at the default the page **keeps
      focus** (`hasFocus` true, zero blurs) — no find bar; with
      `"browserAccelerators": true` it **loses focus** (`hasFocus` false, one
      blur) as the find bar takes the keyboard. Note the suppression does NOT
      take the key from the page — `ctrl+p` and `ctrl+f` keydowns arrive in
      both configurations; what goes away is the engine acting on them.
      **Devtools stays reachable under `tinyjs dev`** with no `debug` key in
      the manifest: F12 produced a top-level `Chrome_WidgetWin_1` **"DevTools
      - file:///…"** window ~5.4s in, in both configurations, and the page
      received **no keydown at all** for F12 — launcher-owned, as designed.
      It opened **detached**, which is the shape the Linux column had to fight
      for.
- [x] **Linux** — **run 2026-08-03**, X11/XWayland, launcher rebuilt from
      source, both in `tinyjs dev` and in a BUILT app; a probe page registered
      `{ id:'dis', key:'p', enabled:false }` and fired the combos through the
      launcher's own XTest `keystroke`. Ctrl+P with the item **disabled**:
      **no `MENU` line** — and the key **does reach the page** (its `keydown`
      listener logged `ctrl+p`). Enable the same item and re-fire: `dis`
      arrives and the page sees **only the bare Control keydown**, so an
      enabled accel really does consume the key. `ctrl+shift+k` on an enabled
      item fires `en`, unchanged.
      So the code-reading above was right about the mechanism and the Linux
      shape differs from the Windows fix in one way worth knowing: Windows
      now **swallows** a disabled combo, Linux **passes it to the page**.
      That is harmless today only because WebKitGTK's built-in set is the
      inspector's alone (and that is now gated on `debug`) — but a page-level
      `keydown` handler on Linux WILL see a combo its own menu item has
      greyed out, where macOS and Windows both eat it. Left as-is; if that
      asymmetry ever bites, the fix is a `key-press-event` handler that
      returns TRUE for any registered-but-disabled accel.
- [ ] **macOS** — nothing to change; confirm the disabled-item beep is still
      what happens after any refactor of the shared menu code.

~~Worked around app-side in the meantime: nib's doc.js takes Ctrl+P in the
page with `preventDefault`~~ — **the workaround is already gone**
(tinyjsapp-examples `bb339b4`, "drop the Ctrl+P page workaround — the
launcher owns disabled combos now"); `ctrlPAt` greps clean. nib therefore
rides the launcher fix on every platform now, and the **Windows** box above
was the one guarding it — that is where the print sheet was. **Re-run and
green 2026-08-03**, so nib's Ctrl+P is covered by the launcher on the
platform the bug came from. macOS is the only box left in this section.

Confirmed in the real app the same day, by hand rather than by probe: nib
from source (no workaround) on a launcher built from `0218c70`, **no folder
open** so Open Quickly is greyed — Ctrl+P did **nothing at all**. No print
sheet, which is the bug staying dead, and no palette either, which is the
point: a disabled item is inert, exactly as ⌘P has always been on macOS.
Worth saying plainly because it reads like a break — nib's removed
workaround used to hand-run `quickOpen()` on every Ctrl+P with no
folder-open check, so Windows and Linux used to open the palette there and
macOS never did. All three now agree.

**This pairing is not shippable yet.** The launcher fix is UNRELEASED —
`v0.36.0` does not contain `0218c70` — while nib's source has already
dropped its workaround, so a nib built against the current released tinyjs
puts the print sheet straight back. Both platform boxes are now green, so
what gates nib's next release is a tinyjs release carrying the launcher,
not more verification.

## An UPPERCASE menu key never got its shift on Linux — fixed 2026-08-03

Reported from nib on Linux: Ctrl+P would not open Open Quickly, and printed
instead. Nothing to do with the section above — the print sheet was **nib's
own `Print…` item firing**, not the webview's.

`{ key: 'P' }` means Ctrl+Shift+P; that contract is in both launchers'
comments ("`S` is Ctrl+Shift+S, the shift coming out of the character
itself") and macOS gets it free, because AppKit derives ⇧ from an uppercase
`keyEquivalent`. **GTK does not.** `gtk_accel_group_connect` lowercases the
keyval and keeps the modifiers exactly as passed, so `split_accel` handing
it `P` + CONTROL registered plain **Ctrl+P**. Measured with a two-item probe
(`{id:'upperP', key:'P'}` declared in an earlier menu than
`{id:'lowerP', key:'p'}`, nib's own shape):

- before: **Ctrl+P → `upperP`**, and **Ctrl+Shift+P → nothing at all**
- after: Ctrl+P → `lowerP`, Ctrl+Shift+P → `upperP`

`split_accel` now adds `GDK_SHIFT_MASK` for a single uppercase letter and
lowercases the character. This hit **every uppercase accel on Linux**, not
just nib's — in nib alone, Save As (`S`) sat on Save's Ctrl+S, Export (`E`),
Close Window (`W`), Link to a File (`U`) and Refresh (`R`) all landed on
their unshifted combos, and no shifted combo was reachable at all.

Confirmed end to end in the real app, X11, built `dist/nib` on a folder of
`.md` files: **Ctrl+P opens the "Open quickly" palette** (photographed) and
**Ctrl+Shift+P opens nib's Print dialog** (a `"Print"` top-level window).

Windows was checked and needs nothing — `split_accel` there already ends
with `if (key.size() == 1 && isupper(key[0])) shift = true;`
(`launcher-win.cc:516`). The stale comment on `ItemReg::needShift` ("from
`alt+`/`shift+` prefixes") is what makes it look otherwise.

- [x] **Linux, the rest of a real bar** — done the same day in nib's File
      menu, the pair that shares a letter: **Ctrl+S saves silently** (no
      chooser) and **Ctrl+Shift+S opens the Save As chooser** (photographed —
      a save dialog with nib's markdown type filter). Before the fix Ctrl+S
      would have hit whichever of the two registered first.

A trap worth carrying: **`pgrep -x launcher-linux` does not find a BUILT
app.** `tinyjs build` names the copy `dist/launcher`, so the kill recipe in
CLAUDE.md silently leaves built apps running — and a stale one keeps its
modal dialogs on screen and eats the XTest keys the next run was meant to
get, which reads as "the accelerator did nothing". Kill by pid, or match
`launcher` as well.

## Devtools off by default, `"debug"` manifest key, launcher-owned F12 (2026-08-02)

Surveying the launchers turned up that debugging is ON everywhere today: mac
creates its webview with `debug:1` (→ WebKit `developerExtrasEnabled`,
right-click Inspect), Linux hard-enables developer extras, and Windows never
touches `AreDevToolsEnabled` whose WebView2 default is TRUE. Every shipped
app has an end-user-reachable inspector. Decision (same day as the
accelerator call above, and composing with it):

- **`"debug"` in tinyjs.json, default off.** `false` (absent): no inspector,
  right-click Inspect gone, F12 dead. `true`: inspector available,
  launcher-owned F12 opens it. `"open"`: same, plus every window (main +
  parents/children) auto-opens its inspector at creation. `tinyjs dev`
  forces at least `true` regardless of the manifest.
- **The accelerator is F12 on all three platforms** — launcher-owned (not
  the engine's own F12, which the browserAccelerators suppression kills),
  modifier-free so it can't collide with app menu combos. mac laptops:
  fn+F12; Cmd+Opt+I registered as a mac alias for Safari muscle memory.
- **The inspector opens DETACHED (own window) everywhere.** Windows:
  `OpenDevToolsWindow()` is always standalone. Linux: ~~WebKitGTK's inspector
  is its own window unless `attach-request` is opted into~~ — **wrong,
  measured wrong 2026-08-03**: WebKitGTK 2.44 attaches by default whenever
  the app window is big enough, and it took an `attach`-signal handler to
  force the window out (see the Linux box below). mac: the
  private `_WKInspector` (`show`/`detach`) forces it out of the app window;
  the inspector UI's own detach button is the fallback if that API moves.
  One inspector window per app window — `"open"` on a six-window app means
  six inspector windows; that's the intended shape, not a bug.
- Independent of `browserAccelerators`: debug governs whether an inspector
  exists at all; browserAccelerators governs whether the engine's key set
  reaches the page. Neither implies the other.

- [x] **Windows** — **all four verified.** Dev case 2026-08-03 (fell out of
      the accelerator run): under `tinyjs dev` with no `debug` key, F12
      opens a detached "DevTools - file:///…" window; the page saw no F12
      keydown, so the launcher owns the key.
      **The three PACKAGED cases done 2026-08-05**, headlessly: three real
      `tinyjs build`s of a scratch app (launcher rebuilt from source first),
      each launched as `dist\winsweep.exe` under an EnumWindows title
      poller. **No `debug` key**: `app.keystroke('F12')` resolved
      `{ok:true, trusted:true}` into the focused packaged app and **zero
      DevTools windows appeared** the whole run (the poller demonstrably
      watching the right desktop — it saw the app's own "winsweep" window;
      and the same rig sees DevTools in the other two cases, so the zero is
      meaningful). **`"debug": true`**: the same F12 opened one standalone
      "DevTools - file:///…index.html" top-level window. **`"debug":
      "open"`**: a two-window app (main + `win.open` satellite) came up with
      **two** DevTools windows, one per app window (…index.html and
      …sat.html), no keypress at all — the one-inspector-per-window shape.
      Not driven: the right-click Inspect route (needs a real click);
      given F12-dead + no auto-open, the remaining exposure is the context
      menu only.
      Rig gotcha worth keeping: `GetWindowTextW` P/Invoked without
      `CharSet.Unicode` marshals the UTF-16 buffer as ANSI and every title
      reads as one garbage char — the first pass returned an empty title
      list that looked exactly like "no windows", on a desktop with plenty.
      Validate the enumerator against a known window before trusting a zero.
- [x] **Linux** — **all four run 2026-08-03** on Ubuntu 24.04 aarch64,
      GNOME 46, XWayland, launcher rebuilt from source. Settled from outside
      with `xwininfo -root -tree` (an inspector window is invisible to the
      page) and by photographing the app window with `xwd`:
      built app with **no `debug` key** — F12 does nothing, no inspector
      window and the app window is unchanged; **`"debug": true`** — F12
      opens a top-level **`"Web Inspector"` 1158x736** window; **`"debug":
      "open"`** — a two-window app (main + a `win.open` satellite) came up
      with **two** inspector windows already open, one per app window;
      **`tinyjs dev`** with no manifest key — F12 opens one. The
      right-click half was photographed too, via the Menu key: the context
      menu is Back/Forward/Stop/Reload with **no Inspect Element** when
      debug is off and **grows Inspect Element** when it is on.
      **It shipped attached, and that is now fixed.** As written, every
      inspector opened as a PANE INSIDE the app window — the exact thing the
      bullet above forbids — photographed on the first run. `show()` only
      creates the frontend page; WebKitGTK takes the attach decision later,
      when that page finishes loading, so the `detach()` issued in the same
      turn ran while nothing was attached yet and returned a no-op.
      `inspector_open` now connects the inspector's `attach` signal and
      bounces it back out from an idle (the default handler attaches, the
      idle detaches), which is what produces the standalone window measured
      above. Re-verified in all four configurations after the fix.
      One gap found in the process, also fixed: **`app.keystroke` could not
      send a function key on Linux.** `parse_combo` lowercases every part
      (letters need it) and `XStringToKeysym` is case-sensitive, so `'F12'`
      became `f12` → `NoSymbol` and the call answered `ok:false` — where
      macOS keeps an f1–f12 table and Windows maps F1–F24. F-keys now parse
      to `XK_F1 + n-1`; a page calling `tiny.app.keystroke('F12')` opens its
      own inspector, which is how the fix was verified.
- [ ] **macOS** — same four checks via fn+F12 and Cmd+Opt+I; right-click
      Inspect Element gone when debug is off. **Worth checking hard**: the
      attached-vs-detached bug the Linux column just turned up was in the
      one platform whose comment claimed the inspector "is its own window
      unless `attach-request` is opted into". `_WKInspector`'s `show`/
      `detach` pair is the same shape and may have the same timing hole —
      watch where the inspector actually appears, not just that it appears.

## Headless fail-fast in the launchers — verified on all three (2026-08-24)

Feedback from a tester whose sandbox couldn't open the WebKit window: the
failure was indistinguishable from an app bug, and they misdiagnosed it
(npm cache) before finding the real problem. Each launcher now checks for
a usable display BEFORE connecting the backend socket and, if there is
none, prints a two-line `tinyjs:`-prefixed explanation ("the app code is
likely fine; run from …") and exits 3 — early enough that bridge.js's
existing race reports `launcher exited before connecting` instead of
hanging on a peer that already gave up.

- [x] **macOS** — `CGSessionCopyCurrentDictionary()` NULL ⇒ no
      window-server session. Verified 2026-08-24 with the rebuilt
      `native/launcher-macos` under
      `sandbox-exec -p '(version 1)(allow default)(deny mach-lookup
      (global-name-regex "com.apple.windowserver.*")(global-name-regex
      "com.apple.CoreGraphics.*"))'`: message printed, exit 3. Positive
      path also verified: with a GUI session the check passes through to
      the socket-connect error, and the full smoke page
      (`TINYJS_HTML=test/smoke.html`) still runs clean.
- [x] **Linux** — `DISPLAY`/`WAYLAND_DISPLAY` both unset ⇒ message +
      exit 3; else `gtk_init_check()` false ⇒ message + exit 3. The old
      bare `gtk_init()` call after the socket connect is GONE (init now
      runs before the connect — g_set_prgname still precedes it, as the
      WM_CLASS comment requires). **Verified 2026-10-13, Ubuntu 24.04
      arm64:** `env -u DISPLAY -u WAYLAND_DISPLAY tinyjs dev` printed both
      `tinyjs:` lines, then `launcher exited before connecting:
      {"exit_status":3}`; `DISPLAY=:99 WAYLAND_DISPLAY=nonexist` took the
      `gtk_init_check` branch ("GTK could not open display ':99'"), same
      exit; bare `launcher-linux x.html bogus.sock` exited 3 with no
      display and reached `connect: No such file` with one. Every normal
      dev run that day opened its window.
- [x] **Windows** — window station lacks `WSF_VISIBLE`
      (`GetUserObjectInformationW(UOI_FLAGS)`) ⇒ message + exit 3, before
      the pipe connect. Note the check is deliberately permissive — if the
      UOI_FLAGS query itself fails we proceed rather than block a working
      desktop. **Verified 2026-10-13, Windows 11 arm64, no elevation**:
      an unnamed `CreateWindowStation` (lands as `Service-0x0-…$`, never
      visible) + `CreateDesktop`, then `CreateProcess` with
      `lpDesktop = <that>\Default` (a scheduled task as SYSTEM or S4U
      needs admin; this doesn't). In it: bare `launcher-win.exe` printed
      both `tinyjs:` lines and exited 3; `tinyjs dev` and a built app
      printed them and then `launcher exited before connecting:
      {"exit_status":3}`. Same launcher on the interactive desktop passed
      the check and reached "cannot connect" to the bogus pipe; every
      normal run today was unaffected.

## macOS per-arch builds (`--arch x86_64`) — never run on a real Intel Mac (2026-09-24)

Built on Apple Silicon only. An `--arch x86_64` .app (thin launcher +
txiki's x86_64 tjs) was launched under Rosetta: launcher and tjs both ran
translated and stayed up. `update.js`'s `mac` block choice was checked with
a mocked fetch, natively and under Rosetta (both pick arm64, by design —
`hw.optional.arm64` is 1 on Apple Silicon even for translated processes).
Still owed, on an actual Intel Mac:

- [ ] an `--arch x86_64` notarized dmg opens and runs (window, menus, a
      bridge call)
- [ ] a `--universal` .app opens and runs
- [ ] `sysctl -n hw.optional.arm64` fails / prints 0 there, so the updater
      picks `mac.x86_64` — an update from one Intel build to the next
      installs and relaunches
- [ ] a Shelf install on Intel pulls the `-macos-x86_64.dmg`

## Linux single-instance socket without `XDG_RUNTIME_DIR` (#12) — written on the Mac (2026-09-28)

Built apps on Linux used to fall back to `$TMPDIR/tinyjs-app-<id>.sock` when
`XDG_RUNTIME_DIR` was unset. That name is predictable and sits in a shared
dir, so another user could squat it or connect to it. The fallback is now
`$TMPDIR/tinyjs-<uid>/`, created 0700. It is refused (warning printed,
single instance and URL/file handoff off) unless it's a real directory we
own with no group/other bits. The socket is also `chmod 0600` after `listen()`
either way. The dir check ran on macOS (fresh, reused, 755, symlink, plain
file); nothing else has run yet:

- [x] **Linux, desktop session** (XDG set): socket still in
      `$XDG_RUNTIME_DIR`, mode 600; a second launch activates the first;
      `xdg-open myscheme://x` reaches `onOpenUrl`
- [x] **Linux, `env -u XDG_RUNTIME_DIR`**: socket lands in
      `/tmp/tinyjs-<uid>/`, dir 700, socket 600; second launch + URL handoff
      still work (the `.desktop` entry is rewritten with the new path)
- [x] **Linux, squatted**: as another user, `mkdir -m 777 /tmp/tinyjs-<uid>`
      first. The app prints the warning and still opens its window, and
      nothing that user drops in there reaches it

2026-09-28, Linux arm64 VM (GNOME Wayland; the no-XDG runs used
GDK_BACKEND=x11), a `tinyjs build` app with `urlScheme`: XDG set → socket
`/run/user/1000/…sock` `srw-------`, second launch exits 0 leaving one
instance, `xdg-open tjsinsttest://…` reached `onOpenUrl`. XDG unset →
`/tmp/tinyjs-1000/` created `drwx------`, socket `srw-------`, second launch
hands off, the `.desktop` Exec was rewritten to `--open /tmp/tinyjs-1000/…`
and `xdg-open` reached `onOpenUrl`. Refused, with the warning printed and the
window still opening, for: a 777 dir we own, a symlink to a dir we own, a
plain file, and (made via a root docker container, no sudo on the VM) a dir
owned by `nobody` at 700 and at 777. No socket was created in any refused dir.

## Security fixes #24 / #27 / #28 (2026-09-30) — verified on all three 2026-10-02

Branch `fix/security-24-27-28`. #28 (sampler refuses non-audio paths, a real
WAV still loads/reads back/plays) and #27 (no headers in curl's argv, temp dir
removed, credentials dropped on a cross-origin redirect, kept on same-origin)
were run on macOS. #24 then verified on a built macOS app, Linux (VM) and
Windows (unchanged, WebView2 prompts) on 2026-10-02.

- [x] **macOS #24, own page** — a BUILT app with
  `"permissions": {"microphone": "why"}`: `getUserMedia({audio:true})` from
  its own `file://` page → only the TCC prompt (first run), then granted.
  *(2026-10-02, built t24 test app: granted, TCC already allowed.)*
- [x] **macOS #24, foreign origin** — same app, page redirects to
  `http://127.0.0.1:<port>` (Sam's repro in #24) → WebKit's prompt appears;
  Deny → `NotAllowedError`. *(2026-10-02: a cross-origin iframe in the own
  page AND the redirected page each got WebKit's sheet; Don't Allow →
  `NotAllowedError` for both. Note the iframe's sheet reads "Allow "t24" to
  use your microphone?" — WebKit names the TOP-LEVEL page, which for
  file:// is the app, so it doesn't say which site inside is asking.)*
- [x] **macOS #24, trusted origin** — add
  `"api": {"origins": {"http://127.0.0.1:<port>": ["media.*"]}}` → no WebKit
  prompt. *(2026-10-02: first attempt still prompted — a packaged .app's
  launcher is started by LaunchServices, so the bridge's
  TINYJS_MEDIA_ORIGINS env never reached it. Fixed with the
  TinyjsMediaOrigins plist key written by `tinyjs build`; rerun: iframe and
  redirected page both granted with no sheet.)*
- [x] **Linux #24** — compiles; own `file://` page granted when declared;
  a redirected-to foreign origin → `NotAllowedError`; trusted via
  `api.origins` → granted; enumerateDevices labels follow the same rule.
  Seen 2026-10-02 (arm64 VM, real mic + v4l2 cams): own page mic+cam
  granted, labels shown; `location.href` → `http://127.0.0.1:<port>` got
  `NotAllowedError` for both and empty labels; with that origin keyed to
  `["media.microphone", "notify"]` the mic was granted (audio label shown)
  and the camera still refused.
- [x] **Windows #27** — curl.exe accepts `-H @file` (needs 7.55+) on a
  root-path request with headers; the `%TEMP%\tinyjs-fetch-*` dir is gone
  afterwards. *(2026-10-02, Windows 11, tjs 26.6.0 importing bridge.js
  against a local node echo server. Root-path hops went through curl 8.21
  (the Git Bash one first on PATH; the inbox System32 curl is also 8.21, so
  the 7.55 floor itself is untested). Empty header arrived empty. Same-origin
  302 kept Authorization/Cookie, cross-origin 302 (127.0.0.1 → localhost)
  dropped them on both the curl and native hops, and a 303 after POST became
  a GET without body or Content-Type. No tinyjs-fetch-* dir left behind.)*
- [x] **Linux #27** — same root-path request through curl. Seen
  2026-10-02: a 4s root-path GET with Authorization/Cookie — `ps` showed
  only `-H @/tmp/tinyjs-fetch-XXXX/headers`; dir 0700, gone after every
  request; server got the headers, a POST body, and an empty `x-empty;`.
  Redirects: cross-origin (curl hop and native hop) dropped auth/cookie and
  kept `x-keep`; same-origin kept them; 303 POST→GET dropped Content-Type;
  cross-origin bounce back to the first origin stayed stripped.
- [x] **Windows #28** — sampler loads a WAV from outside the page's read
  root (exercises `sampler.bytes`); `/etc/passwd`-style path refused.
  *(2026-10-02, Windows 11, `tinyjs dev` + a self-driving TINYJS_HTML page.
  A WAV in %TEMP% loads, reads back through `sampler.bytes` and plays, and
  so does an ArrayBuffer load (spilled file, no extension). The attack: the
  page stubs `window.__tinySampler.load` so the host never answers, which
  keeps the bank entry alive, then calls `sampler.bytes`. With the
  commit's magic-only check it read back a UTF-16LE text file (BOM `FF FE`
  passes as an MPEG frame sync) and a HEIC photo (`ftyp`), even one named
  `.m4a`. Fixed by also requiring an audio extension and an audio
  ISO-BMFF brand; after that, all of these are refused at load: the text
  file, the HEIC (as .heic and as .m4a), `secret.wav` holding plain text,
  win.ini, and drivers\etc\hosts.)*
- [x] **Linux #28** — native sampler still loads WAV/MP3. Seen
  2026-10-02: WAV, ID3 MP3 and raw-frame MP3 load and play; `/etc/passwd`
  and a text file refused; `sampler.bytes` → host-only. OGG was never
  decodable on Linux (miniaudio without stb_vorbis: "can't decode
  (wav/mp3/flac)") — not a regression. An MP3 with leading junk (e.g. a
  `----` header) is now refused by path though it still decodes from bytes.
  Seen BEFORE the audio-extension requirement (Windows #28 above): a path
  now also needs a .wav/.mp3/… name. Behaviour for normally named files is
  unchanged.

## Self-update relaunch (#16 follow-up, 2026-10-02) — Windows + Linux verified

After an update the app quit and never came back, on Windows for two
reasons: libuv puts every `tjs.spawn` child in a KILL_ON_JOB_CLOSE job, so
the relaunched exe died with the old instance; and the new instance found the
old one's single-instance pipe still up and handed off to it. Fix: `relaunch`
goes through `launcher --spawn` on Windows (CreateProcess outside the job),
and passes `--tinyjs-relaunched`, which makes the new instance wait (≤5 s)
for the pipe to free instead of handing off.

- [x] **Windows** — 1.0.0 → 1.0.1 → 1.0.2, each relaunch logs
  `args=["--tinyjs-relaunched"]` ~300 ms after install and stays up with its
  window. A plain double launch still hands off; a flagged launch beside a
  live owner hands off after ~5.2 s. *(2026-10-02, Windows 11, published
  test app + local manifest server.)*
- [x] **Linux** — only the flag + wait loop apply there (no job objects; the
  plain spawn should already survive). Watch a self-update relaunch: the new
  version's window must come up, and a second launch must still hand off.
  *(2026-10-02, arm64 VM, GNOME/X11 `DISPLAY=:1`, a `tinyjs publish`ed test
  app + `python3 -m http.server` manifest on 127.0.0.1: 1.0.0 → 1.0.1 →
  1.0.2, each new version's backend init logged `args=["--tinyjs-relaunched"]`
  ~300 ms after install and its page called back ~400 ms later; the old pid
  gone, one launcher left, still up after 6 s, and the new instance owning
  `$XDG_RUNTIME_DIR/tinyjs-app-<id>.sock` (srw-------). A plain second launch
  hands off in 0.06 s; a flagged launch beside the live owner hands off
  after 5.06 s. One instance survives both.)*
- [ ] **Any OS, first update INTO the fix** — the relaunch runs the OLD
  version's update.js, so an app built before this fix still won't come back
  after updating to one built with it. Expected, one time; worth seeing.

## Hardening batch 1 (`fix/hardening-1`, 2026-10-02) — verified on all three, shipped v0.45.0

Easy items from #26/#29/#30. What ran on the Mac is recorded per item;
everything else below is written and unwatched.

**#30.4 — setup.sh temp paths.** `/tmp/tjs-$$.gz`, `/tmp/txiki-src-$$` and
`/tmp/txiki-$$.zip` (guessable, pre-plantable by another local user) are now
one `mktemp -d` dir removed by an EXIT trap; the macOS launcher build's
`BUILD_TMP` lives inside it (a second `trap` would have replaced the first).
macOS: a fresh copy of the repo with no `bin/tjs` → download + launcher
compile OK, no temp dir left behind. The Linux branches are syntax-checked only.

**#26 — pinned txiki hashes.** `runtime/txiki.sha256` holds the sha256 of the
`tjs` binary inside each saghul/txiki.js zip we fetch (v26.6.0: macOS arm64,
macOS x86_64, Windows x86_64; each zip matched GitHub's published asset
digest first). Checked by: `cli.js` `fetchTxiki` (after download AND on every
cache reuse — the cache is user-writable and gets bundled + codesigned),
`setup.sh` (macOS download), `setup.ps1` (Windows download, so Windows CI
too) and `release.yml` (macOS packaging). Bumping txiki now means adding
lines there first. The Linux `setup.sh` path pulls `tjs` from our own
*latest* release — no fixed hash to pin.
macOS, seen 2026-10-02: `build --arch x86_64` with an empty cache downloads
and passes; with the real cache a full signed build completes; the cache
swapped for another valid x86_64 Mach-O (#26's repro) → build refused
("modified after download"). `setup.sh` with a wrong pin → "refusing to
install", nothing in `bin/`. release.yml's loop run locally: both arches
pass; a wrong x86_64 pin → `::error::` and exit 1.

- [x] **Windows #26** — `Remove-Item bin\tjs.exe`, run `setup.ps1` → installs
  as before. Edit one char of the `windows-x86_64` line in
  `runtime\txiki.sha256`, delete `bin\tjs.exe` again → setup stops with
  "refusing to install it" and no `bin\tjs.exe`. Restore the line. (The file
  checks out CRLF on Windows; `-split` treats the `\r` as whitespace.)
  *(2026-10-02, Windows 11: clean run downloads, hash matches the pin
  (d94fac3d…), launcher compiles, exit 0. Pin edited to e94fac3d… → "has
  sha256 d94fac3d…, expected e94fac3d… - refusing to install it", exit 1, no
  `bin\tjs.exe`, no txiki-* left in %TEMP%.)*
- [x] **Windows CI #26** — the next tag's Windows job goes through the new
  `setup.ps1` check; the macOS job through release.yml's. Both green.
  *(2026-10-02, v0.45.0 run 37081800489: Windows logged "downloading
  txiki.js v26.6.0 (windows-x86_64)" — a fresh download, not a cache — and
  passed the pin; the macOS "Assemble per-arch tarballs" step passed both
  arch pins. All four build jobs + release green, all assets published.)*
- [x] **Linux #30.4** — `rm bin/tjs && ./setup.sh` (prebuilt download) and
  `rm bin/tjs && TJS_BUILD=1 ./setup.sh` (source build) both still produce a
  working `bin/tjs`; no `/tmp/tmp.*` dir left afterwards.
  *(2026-10-02, Ubuntu ARM VM: prebuilt download → exit 0, `tjs` runs
  (26.6.0), no `/tmp/tmp.*`. Source build in an `ubuntu:22.04` arm64
  container (no cmake on the VM host) with gcc-12 → built in
  `/tmp/tmp.Mlsl3We3X6/txiki-src`, exit 0, `tjs` runs, dir gone after.)*

**#29.4 — built Windows/Linux apps ignore inherited env knobs.** `bridge.js`
decides `built` from `bundlePath()` (exe isn't `tjs` and has a `launcher`
beside it). When built it ignores `TINYJS_HTML`, `TINYJS_LAUNCHER` (both
lookups, incl. the Windows hidden-spawn one) and `TINYJS_SOCKET`, and strips
every inherited `TINYJS_*` and `WEBVIEW2_*` var from the launcher's env before
setting its own from the manifest — only `TINYJS_LAUNCHER_DEBUG` (Windows drag
logging) survives. The WEBVIEW2 one mattered most: the bridge APPENDED its
flags to an inherited `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`, so
`--remote-debugging-port=9222` rode straight into a shipped app. macOS is
untouched (`built` is always false there; packaged .apps read plist keys).
macOS, seen 2026-10-02: bridge imports; `TINYJS_HTML=… tinyjs dev` still
swaps the page (store write + self-quit). Nothing below has run.

- [x] **Windows #29.4** — `tinyjs build` any app, then from cmd, one at a time
  against `dist\<name>.exe`:
  `set TINYJS_HTML=C:\abs\other.html` → the app's OWN page loads;
  `set TINYJS_INJECT=document.title='pwned'` → title unchanged;
  `set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222` →
  `curl http://127.0.0.1:9222/json` refused while the app is up;
  `set TINYJS_DEBUG=open` → no devtools. Then the same app still updates,
  relaunches and single-instances (TINYJS_APP_EXE is set by us, not inherited).
  *(2026-10-02, Windows 11, a throwaway self-driving app. Control first: the
  same INJECT (`addEventListener('load',…title='pwned')` — a bare
  document.title assignment is overwritten by `<title>`), WEBVIEW2 and
  DEBUG=open vars under `tinyjs dev` → 9222 answered 200, a DevTools window
  opened, the page saw title "pwned". Built exe with all four set at once →
  own page loaded (override page's store marker untouched), 9222 refused, no
  DevTools window, title unchanged. Second launch handed off in 0.2 s, one
  launcher left. `publish`ed 1.0.1 + local manifest: installed 1.0.0
  updated and 1.0.1 came back up and ran its page.)*
- [x] **Windows #29.4, dev regression** — `TINYJS_HTML=… tinyjs dev` still
  loads the override page; F12 devtools still there in dev.
  *(2026-10-02: override page loaded and wrote its store marker; F12 sent via
  SendKeys to the dev window → "DevTools - file:///…/index.html" appeared.)*
- [x] **Linux #29.4** — built app: `TINYJS_HTML=/abs/other.html ./dist/<name>`
  → own page; `TINYJS_INJECT="document.title='pwned'"` → unchanged;
  `TINYJS_MEDIA=camera,microphone` on an app that declares no permissions →
  `getUserMedia` still `NotAllowedError`. Tray icon, WM class / .desktop
  match and the icon still right (TINYJS_APP_ID/ICON now always ours).
  *(2026-10-02, Ubuntu ARM VM, throwaway self-driving app. Control under
  `tinyjs dev` with INJECT (`window.__pwned=1` + load→title) and
  MEDIA=camera,microphone → title "pwned", mic granted. Built, with HTML,
  INJECT, MEDIA, `TINYJS_APP_ID=evil.id`, `TINYJS_ICON=/nonexistent.png` all
  set → own page, title unchanged, `NotAllowedError`, WM_CLASS
  `com.example.vfy`, .desktop Icon/StartupWMClass ours; tray item over D-Bus
  Id `com.example.vfy`, icon from `dist/`, Active. Launcher's
  /proc/environ: only `TINYJS_LAUNCHER_DEBUG` survived of the inherited set.)*
- [x] **Linux #29.4, dev regression** — `TINYJS_HTML=… tinyjs dev` unchanged.
  *(2026-10-02: override page loaded and wrote its store marker.)*

**#30.7 — bridge odds and ends** (pure bridge.js; nothing launcher-side).
- *Fetch streams keyed by window.* Every page counts `tiny.fetch` stream ids
  `f1, f2…` from scratch, so two windows streaming at once collided on `f1` —
  the later one replaced the earlier in the bridge's map, and any window could
  pull or cancel another's by id. Now keyed `<calling window>\n<id>`; a
  reloaded page's leftover stream under a reused id is cancelled first.
  macOS, seen 2026-10-02 (main + a `win.open` child, both `{stream:true}` from
  a local server, 300 000 bytes each): OLD bridge → main got `8192:A` (cut off
  after its first chunk), child `300000:B`; FIXED → `300000:A` / `300000:B`.
- *Titles via `one()`* — `\r` and `\t` flattened like `\n` (both setTitle
  paths). `setTitle(null)` now gives '' rather than "null".
- *Store is a null-prototype object.* On the plain `{}`,
  `set('__proto__', {x:1})` swapped the store's prototype (then `get('x')` →
  1, nothing persisted) and backend `get('constructor')` answered Object's
  function. A non-object store.json (`null`, an array) now loads as empty
  instead of throwing on every call. macOS, seen 2026-10-02 (dev page): OLD →
  `x` 1, `__proto__` not in the file or in `all()`; FIXED → `x` null,
  `__proto__` stored, saved and listed like any key.

- [x] **Windows + Linux #30.7** — the two-window stream test (each window gets
  its own full body) and a `tiny.store` round trip; same bridge code, so this
  is a smoke check, not a new path.
  **Windows seen 2026-10-02** (dev and built): main + a `win.open` child,
  both `{stream:true}` from a local node server, 300 000 bytes each →
  `300000:A` / `300000:B`; store round trip ok; `set('__proto__',{x:1})` →
  `get('x')` null, `__proto__` listed in `all()` and saved.
  **Linux seen 2026-10-02** (Ubuntu ARM VM, python http.server, readers
  paced 30 ms/chunk so both streams overlap): OLD bridge (284bcce^) →
  `177120:A` / `155648:B` (both cut short) and `get('x')` 1, `__proto__`
  missing from `all()`; FIXED, dev and built → `300000:A` / `300000:B`,
  `get('x')` null, `__proto__` stored and listed.

## Per-app WebView2 profile on Windows (#29.1, `fix/29-webview2-profile`, 2026-10-02) — Windows verified 2026-10-02

Stock webview keys the WebView2 user-data folder on the exe name, and every
tinyjs app's window belongs to `launcher.exe` (dev: `launcher-win.exe`) — so
ALL tinyjs apps on a machine shared one profile under
`%APPDATA%\launcher.exe`: cookies, IndexedDB, permissions, and localStorage
(file:// is a single origin under `--allow-file-access-from-files`, so two
apps' `localStorage.setItem('settings')` clobbered each other). The bridge
now sets `TINYJS_WEBVIEW2_DATA=%APPDATA%\<app-id>\WebView2` (after #29.4's
env strip, so it can't be inherited) and the win32_edge.hh patch uses it,
creating parents; unset keeps stock. Dev and built copies of one app share
its folder. **No migration** (decided 2026-10-02 — small user base): every
app starts with a fresh profile, i.e. wrapped-site logins and page
localStorage are gone once after updating. The old shared folder is left in
place. `tinyjs dev` now also rebuilds launcher-win.exe when win32_edge.hh
changes. Not compiled anywhere yet (no MinGW on the Mac).

- [x] **Windows, builds** — `setup.ps1` (or `tinyjs dev`, which now rebuilds
  on the header change) compiles cleanly.
- [x] **Windows, folder** — run an app (dev, then built) → `%APPDATA%\<app-id>\WebView2\EBWebView`
  appears; nothing new written under `%APPDATA%\launcher.exe` /
  `launcher-win.exe`.
- [x] **Windows, isolation** — two apps with different ids: app A
  `localStorage.setItem('k','A')`, app B reads `localStorage.getItem('k')` →
  null. Both running at the same time open fine.
- [x] **Windows, persistence** — app A's value survives a restart, and a
  `win.open` second window of app A sees it (same profile per app).
- [x] **Windows, update** — a built app self-updates and relaunches with its
  profile intact (same id → same folder).
  *(All five seen 2026-10-02, Windows 11: setup.ps1 compiled the patched
  header cleanly. Dev run created `%APPDATA%com.example.probeaWebView2EBWebView`;
  newest file under `%APPDATA%launcher.exe` / `launcher-win.exe` unchanged
  after dev, built and updated runs. probea and probeb running at once:
  probeb read `k` → null while probea's restart read `probea`, and its
  `win.open` child read `probea` too. The built exe read the value dev
  wrote (shared folder). Self-update 1.0.0 → 1.0.1 from a local manifest:
  the relaunched 1.0.1 page read `k` = `probea`.)*

## #29.2 instance-pipe ACL + #29.3 page win.open confinement (2026-10-02) — Windows verified 2026-10-02

**#29.3 (bridge.js, ALL platforms).** The page-facing `win.open` builtin runs
`page` through `pageForWindow`: http(s) URLs pass; otherwise the path is
normalized (`\` → `/`, `.`/`..` resolved) and must land inside `frontendDir`
(case-folded on Windows only). Any other scheme, a drive-relative `C:x`,
anything rooted (`/`, `\`, `\server`) outside the frontend, or no
frontendDir (html-string / TINYJS_HTML / url-only wrappers) → rejects with
"win.open from a page takes an http(s) URL or a page inside the app's
frontend". devUrl mode: relative only. Backend `app.openWindow` untouched.

**#29.2 (Windows only).** Pipe name `\.\pipe\tinyjs-app-<id>-<userName>`.
After listen the bridge sends `PIPELOCK <name>`; the launcher opens a client
handle with WRITE_DAC, checks the server is this user, and sets the
protected DACL `D:P(D;;GA;;;NU)(A;;GA;;;<user>)(A;;GA;;;SY)` (libuv's
default is SY/BA/owner full + Everyone/Anonymous read; it keeps no handle
JS can reach). Hand-offs check the server's user SID
(`GetNamedPipeServerProcessId` → token) before writing: the bridge's second
instance spawns `launcher.exe --handoff <pipe> <json>` (0 sent, 1 nobody
there, 2 not ours → single instance off), and `--open` uses the same check.

- [x] **Windows, DACL** — built app running → its pipe reads
  `D:P(D;;FA;;;NU)(A;;FA;;;<user SID>)(A;;FA;;;SY)`, server pid = the app.
  A scratch prototype first confirmed the DACL sticks to the libuv pipe and
  later instances still get created (accepts kept working after the lock).
- [x] **Windows, hand-off** — second launch of the built exe exits 0 in about
  1 s and the first stays up. `launcher.exe --open <pipe> <exe> C:\…\win.ini`
  exits 0. `--handoff` against a bare tjs listener delivers
  `{"activate":true,"paths":["C:\x y\ü.md"]}` byte for byte (Unicode argv
  intact). No listener → exit 1.
- [x] **Windows, squatter** — create
  `\.\pipe\tinyjs-app-<id>-<A's name>` as another user with an
  Everyone-write DACL, then as A launch the app → console says "held by
  another user", app runs with no single instance, the squatter's pipe
  receives nothing; `launcher --open` exits 2 and starts nothing.
  *(2026-10-02, built app `com.example.sqtest` from HEAD. No second account
  needed: SYSTEM is "another user" to the SID check. The squatter ran as
  SYSTEM via a one-off scheduled task: a .NET NamedPipeServerStream with
  Everyone ReadWrite and unlimited instances, logging every connection and
  every byte. Everyone-WRITE matters: under the default DACL (others
  read-only) the launcher's write-open gets ACCESS_DENIED and exits 2 before
  the SID compare ever runs. One instance isn't enough either: the bridge's
  empty probe takes it and the hand-off then reads PIPE_BUSY → exit 1, so
  you'd see no "held by" line. The app was launched from an elevated shell
  as tarwin, so OpenProcess on the SYSTEM server succeeds and EqualSid is
  what refused. Results: "held by another user" printed. The app ran its
  page through to the page's own `quit` call, the same as an unsquatted
  control run. The squatter logged exactly two connections per launch
  (bridge probe + `--handoff`), both 0 bytes. `launcher --open <pipe> <exe>
  C:\Windows\win.ini` exited 2, delivered 0 bytes, and started no app
  process. Not covered: a NON-elevated A, where OpenProcess on a SYSTEM
  process fails and the "can't open the server" branch refuses instead. It's
  the same fail-closed return, just not seen.)*
- [x] **Windows, win.open** — built app, page-side probes: `sub.html`,
  `./sub.html`, absolute-inside (both slash styles) open and render;
  `../…/Windows/win.ini`, `x/..\..\…`, `C:/Windows/win.ini`,
  `C:\Windows\win.ini`, `file:///C:/…`, `C:Windows/…`,
  `\localhost\C$\…`, `javascript:`, and `<frontend>-evil/sub.html`
  (sibling with the same prefix) all reject.
- [x] **macOS, win.open** — same probes from a built .app (frontend lives in
  Contents/Resources): legit pages still open; escapes reject.
  *(2026-10-02, built .app launched via `open`: `sub.html`, `./sub.html`,
  absolute-inside and `x/../sub.html` opened AND rendered (each child wrote
  its window id to the store); `../…/etc/hosts`, `x/..\..\..\sub.html`,
  `/etc/hosts`, `file:///etc/hosts`, `javascript:`, `<frontend>-evil/sub.html`
  and a NUL-suffixed name all refused. The packaged app's frontendDir is the
  real Resources/app/frontend (entry.js resolves it from import.meta.url),
  so attach mode doesn't null it out.)*
- [x] **Linux, win.open** — **seen 2026-10-02** on a built app (frontend
  extracted to `/tmp/tjs-<hash>/frontend/`) and in `tinyjs dev`. These open
  and render: `sub.html`, `./sub.html`, absolute-inside, and `x/../sub.html`.
  These reject: a `../` walk to a planted file outside, the absolute outside
  path, `file://…`, `javascript:`, a `<frontend>-evil/sub.html` sibling, a
  `x/..\..\…` backslash walk, `//abs` and a NUL. Before/after in dev,
  same probes on HEAD~1's bridge: all 12 OPENED, and four of them really
  loaded the planted outside page (walk, absolute, `//abs` as `file:////…`,
  sibling). Linux single-instance hand-off (restructured in the same
  commit) also re-checked on a built app: a second launch with a path exits
  0 in 0.06 s, one launcher stays, and `onOpenFiles` gets the path with its
  space intact.
- [x] **Any OS, regression** — kitchen-sink's four `tiny.win.open` windows
  (calllog, ball, traypanel, inspector) and matcha's settings still open.
  *(2026-10-02: clicked through by hand — all open.)*
  *(Read, not run: every page-side call passes a bare filename from its own
  frontend — `calllog.html`, `ball.html`, `inspector.html`, matcha's
  `settings.html` — the same shape as the macOS `sub.html` probe that
  opened. Still worth clicking through once.)*
- [x] **Windows, session-pipe token** — the bridge↔launcher pipe
  (`tinyjs-XXXXXX`) had the same default DACL, and the bridge took the FIRST
  connection as its launcher. Now the bridge puts 32 random bytes in
  `TINYJS_PIPE_TOKEN` (spawn env only), the launcher sends `HELLO <token>`
  first and unsets the variable, and `acceptWithToken` vets connections in
  parallel (10 s cap, never writes to one that fails). Adversarial check: a
  scratch `snoop.exe` polls `\.\pipe\` and connects READ-ONLY the moment a
  session pipe appears. **Before:** it won the race and read 499 bytes of
  wire (`GET 1 screens`, …). **After (3 runs):** it still connects, reads 0
  bytes and is hung up (109); the app comes up normally each time, menus and
  backend calls work, `tinyjs dev` too, and the instance pipe is still locked.

## #30 hardening (`fix/30-hardening`, 2026-10-02)

- [x] **macOS, `TINYJS_DEBUG` trace redaction (#30.7)** — the `>>`/`<<`
  trace no longer prints clipboard contents or keychain values. `SECRET`,
  `CLIPWRITE` and `CLIPCHANGE` print as `OP <id> [redacted]`, and so do a
  page's `secrets.*`/`clip.*` CALLs. Their answers (`GOT` for a `GET
  clipboard` or `SECRET`, `RET` for the page call) are matched by id and
  redacted too. *(2026-10-02: `TINYJS_DEBUG=1 tinyjs dev` with a page doing
  secrets set/get/delete plus a clipboard read: all 15 related lines show
  `[redacted]`, and the value appears only where the probe itself wrote it
  into `tiny.store`. A node test of `redact()` covered the same lines,
  `clipboard:count` left visible, and no ids left over.)*
- [x] **Windows, same** — the code is shared JS, but check that the CALL
  framing matches: run with `TINYJS_DEBUG=1`, call `tiny.app.secrets.set/get`
  from the page, and confirm the value never shows on stderr.
  *(2026-10-04: confirmed by the user.)*
- [x] **Linux, same.** *(2026-10-04: confirmed by the user.)*
- [x] **macOS, `tinyjs update` runs the bundled installer (#30.3)** — each
  release now ships `install` (or `install.ps1` on Windows) at the top of
  the package. `tinyjs update` copies it to a temp dir and runs it with
  `TINYJS_HOME=<this install>` and `TINYJS_VERSION=<latest tag>`, so
  tinyjs.app is no longer fetched on update. `--dry-run` prints the plan.
  *(2026-10-02: the v0.46.0 macOS tarball unpacked to scratch, with the new
  cli.js and `install` overlaid and VERSION set to 0.45.0. `--dry-run`
  printed the local installer, TINYJS_HOME and the tag. The real run
  downloaded and verified v0.46.0 and swapped the scratch dir: the marker
  file was gone, VERSION read v0.46.0, and no temp dir was left. HOME and
  PATH were scratch, so `/usr/local/bin/tinyjs` was untouched.)*
- [x] **Windows, same** — unzip the v0.46.0 Windows zip somewhere, copy in
  this branch's `cli.js` plus `docs\install.ps1` as `install.ps1`, set
  `VERSION` to `0.45.0`, then run `tinyjs.cmd update --dry-run` and
  `tinyjs.cmd update` from there. Check: the folder becomes v0.46.0, no
  `tinyjs-update-*` is left in `%TEMP%`, and the user PATH gets **one**
  backslash entry for that folder (the installer adds it, so remove it
  afterwards).
  *(2026-10-04, Windows 11, scratch dir with a marker file. `--dry-run`
  printed the local installer, a backslash TINYJS_HOME and v0.46.0. The
  real run downloaded v0.46.0, passed the checksum and swapped the folder:
  exit 0, VERSION v0.46.0, the marker was gone, no `tinyjs-update-*` or
  `tinyjs-install-*` was left in `%TEMP%`, and the user PATH had exactly one
  backslash entry (removed afterwards). Expected leftover: `<dir>.old` holding
  only the `bin\tjs.exe` that was running the update. Windows won't delete a
  running exe, so the installer's best-effort removal skips it, and the next
  update's `Remove-Item $old` clears it.)*
- [x] **Linux, same** (the same `sh` installer as macOS; check
  `--dry-run` plus one real run with a scratch HOME).
  *(2026-10-03, Ubuntu ARM VM: the v0.46.0 linux-arm64 tarball unpacked to
  scratch, with this branch's cli.js and `docs/install` as `install`
  overlaid, VERSION set to 0.45.0, plus a marker file. Run under `env -i`
  with a scratch HOME and `PATH=/usr/bin:/bin`. `--dry-run` printed the
  local installer, TINYJS_HOME and v0.46.0. The real run used the bundled
  installer (no tinyjs.app fallback line), downloaded and verified
  checksums, and swapped the dir: marker gone, VERSION v0.46.0, the new tjs
  reported "tinyjs v0.46.0", and the symlink landed in the scratch
  `~/.local/bin`. No `/tmp/tinyjs-update-*` or `/tmp/tmp.*` was left. The
  real `~/.tinyjs` and `~/.local/bin/tinyjs` were untouched (VERSION and
  full-iso listings identical before and after).)*
- [x] **After the next release:** the published tarballs and zip actually
  contain `install` / `install.ps1` (`tar -tzf … | grep install`).
  *(2026-10-04, v0.47.0: `tinyjs/install` is in all four tarballs
  (macOS arm64/x86_64, Linux arm64/x86_64), and `install.ps1` is in the
  Windows zip.)*
- [x] **macOS, `tiny-media://` proxy gated (#30.1)** — the proxy answers
  with `Access-Control-Allow-Origin: *`, so any page that could fetch it
  could read any URL, CORS bypassed. Now the requester is the `Origin`
  header WebKit stamps (or the window's top document when there is none),
  and it must be trusted for `proxy`: `file://`, the dev server, or an
  `"api".origins` key that allows `media.proxy`. Document loads
  (`Accept: text/html`) are refused, and every response carries
  `Content-Security-Policy: sandbox`. Probe pages in scratch: a `file://`
  app page, a cross-origin iframe from `http://127.0.0.1:8765`, and that
  iframe navigating a child to `tiny-media://proxy/?u=<evil2.html>`, which
  then fetches the proxy same-origin. *(2026-10-02, `tinyjs dev`.)*
  - **Before:** the iframe's CORS fetch read the secret, and so did the
    proxied page, which ran as origin `tiny-media://proxy`.
  - **After:**
    - The app page still reads it.
    - The iframe's CORS fetch fails ("refused for http://127.0.0.1:8765").
    - A no-cors fetch still gets an opaque response.
    - The document load is refused.
    - With the document check disabled for one run, the proxied page loaded
      but its script never ran (CSP sandbox), so that backstop works by
      itself.
  - **Wrapped site** (`"url": "http://127.0.0.1:8765/wrap.html"`): refused
    with no `"api"`, readable with `origins: { "http://127.0.0.1:8765":
    ["media.proxy", "store.*"] }`, and refused again with `["store.*"]` only.
  - A `crossorigin` `<audio>` through the proxy loads (2 s) and plays from
    the app page. My analyser read zero samples, but the v0.46.0 launcher
    reads zero in the same rig, so that's the rig, not the change.
- [x] **macOS, by ear** — an app that plays internet radio through
  `tiny.proxyURL` with an EQ or analyser still sounds and moves the meter.
  *(2026-10-02: amp's internet radio confirmed working by the user on this
  branch's macOS launcher.)*
- [x] **Linux, `tiny-media://` proxy gated (#30.1)** — same rule as macOS
  in `media_scheme_cb`. Ran `test/proxy-gate/` (README there).
  *(2026-10-03, arm64 VM, WebKitGTK 2.52.6, `tinyjs dev`. It compiled
  cleanly on the first Linux build.)*
  - `app` reads `SECRET-PAYLOAD`, `iframe-cors-err` is "Load failed",
    `iframe-nocors` is `opaque`, and `nav` is absent.
  - stderr shows `refused for http://127.0.0.1:8765` and `refused for
    file:// (document load)`. **WebKitGTK does pass the `Origin` header**,
    so iframes inside an app page are covered on Linux, not just the
    main-frame fallback.
  - **Wrapped site:** refused with no `"api"`, `SECRET-PAYLOAD` with
    `["media.proxy", "store.*"]`, and refused again with `["store.*"]`.
- [ ] **Linux, `<audio>` through `tiny-media://` doesn't load at all
  (pre-existing, not #30.1)** — found while trying the internet-radio check
  above. A gated-in `file://` page's `<audio src=tiny.proxyURL(…)>`
  rejects `play()` with `NotSupportedError`, for both WAV and MP3, with
  and without `crossOrigin`, even though upstream receives the GET. The
  same probe fails identically on the pre-#30.1 launcher (`14e17d6`) and
  on a build without the CSP header, so neither the gate nor the CSP
  header causes it. The direct `http://` URL plays, and its analyser reads
  real samples (peak 0.37) even without CORS. So WebKitGTK may not taint
  cross-origin media at all, and amp's radio may not need the proxy on
  Linux. amp's radio does play on Linux (user, 2026-10-04), which fits
  that reading. Still to do: find why the WebKitGTK media loader rejects
  the scheme. It may need
  `webkit_security_manager_register_uri_scheme_as_cors_enabled`, or range
  support.
- [x] **Linux, by ear** — amp's internet radio plays. *(2026-10-04:
  confirmed by the user. Given the entry above, that's probably not
  through the proxy, so it rules out a regression but doesn't exercise
  the proxy's media path.)*
- [x] **Windows, regression** — no proxy on Windows (`proxyURL` is
  mac + Linux), but internet radio was re-checked on this branch.
  *(2026-10-04: confirmed working by the user.)*
- [x] **macOS, other URL schemes reach `onNavigate` (#30.2)** — a
  navigation to anything other than http(s), file, about, data, blob,
  javascript or tiny-media, in any frame, is never loaded by the webview.
  The app gets a `kind: 'policy'` ask (the NAVQ now ends in a
  `main`/`sub`/empty frame flag); only `'external'` opens it with the OS,
  and no handler or no answer in 400 ms denies. *(2026-10-04: ran
  `test/other-scheme/` in `tinyjs dev`.)*
  - With no verdict, `deny` and `allow`: both URLs were logged with
    `isMainFrame` `false`/`true`, the page stayed on its `file://` href,
    and nothing opened.
  - `external`: macOS was asked to open both, and showed its "no
    application set to open the URL" dialog for each of the made-up
    schemes.
  - Regression: a main-frame `https://example.com/` is still asked with
    `isMainFrame: true` and default-allowed.
- [x] **Linux, same (#30.2)** — the ask happens at the NAVIGATION_ACTION
  stage (other schemes never reach RESPONSE). *(2026-10-03: compiled
  cleanly via `tinyjs dev`'s auto-rebuild; ran `test/other-scheme/`.)*
  - With no verdict, `deny` and `allow`: both URLs were logged with
    `isMainFrame` `null`, the page stayed on its `file://` href.
  - `external`: nothing visible with the made-up schemes (GIO has no
    handler and fails silently). With a throwaway user-level
    `x-scheme-handler/tinyjstest-b` handler registered, `external`
    launched it with `tinyjstest-b://main`; no-verdict and `deny` did not.
    Handler removed afterwards.
  - Regression: a main-frame `https://example.com/` is asked with
    `isMainFrame: true`, default-allowed, and start/commit/finish NAV
    events still arrive.
- [x] **Windows, same (#30.2)** — `ICoreWebView2_18::LaunchingExternalUriScheme`:
  cancel, ask, and ShellExecute on `external`. *(2026-10-04: compiled
  cleanly via `tinyjs dev`'s auto-rebuild; ran `test/other-scheme/`.)*
  - WebView2 raises the event only for schemes with a registered OS
    handler. The made-up schemes never reach `onNavigate` and fail quietly
    inside the engine: no prompt, no launch, and the page stays put. That
    matches the default deny, but nothing gets logged. With throwaway
    `HKCU\Software\Classes\tinyjstest-{a,b}` handlers registered (they
    append `%1` to a file):
  - With no verdict, `deny` and `allow`: both URLs were logged with
    `isMainFrame` `null`, the page stayed on its `file://` href, nothing
    was launched, and no WebView2 "open this app?" prompt appeared
    (screenshots).
  - `external`: both launched through the shell (`tinyjstest-a://sub/`,
    `tinyjstest-b://main/`), no prompt.
  - A clicked `mailto:` link with no verdict was asked (`isMainFrame`
    `null`) and refused: no mail client, no prompt.
  - Regression: a main-frame `https://example.com/` is still asked with
    `isMainFrame: true`, default-allowed, and start/commit/finish NAV
    events still arrive.
  - Not done: `mailto:` + `'external'` opening the real mail client. It
    goes through the same ShellExecute path the custom scheme took.
- [x] **macOS, `file://` universal access only reaches app pages
  (#30.7)** — no code change. Both launchers turn on file and universal
  access from file URLs (Vite needs them), and WebKit applies them to
  `file://` documents only. The probe checks that a wrapped `http` site
  can't become one. *(2026-10-04: `test/file-access/` in `tinyjs dev`:
  `fetch('file:///etc/hosts')` fails, the `file://` iframe never loads,
  and `location = 'file:///etc/hosts'` is ignored, so the page stays on
  its `http://` URL.)* Comments in both launchers record why this is safe.
- [x] **Linux, same** — run `test/file-access/` (README there). This is the
  OS the issue named. *(2026-10-04: confirmed by the user. The wrapped
  `http` site can't fetch, frame or navigate to `file://`.)*
- [x] **Windows, no handle growth from closed windows (#30.7)** — the
  `UncHandler` "registered without unregister" item. It's tied to its
  webview, and closing a window Close()s the controller and releases
  ctrl + wv (code read; comment at `install_unc_for`), so no code change.
  To confirm: in kitchen-sink, open and close a `tiny.win.open` window
  about 30 times and watch the launcher's Handles column in Task Manager
  (Details tab, add the column). It should level off rather than climb
  with each window. *(2026-10-04, headless: a `TINYJS_HTML` page in a
  `tinyjs new` scratch app ran 60 `win.open` → 1.2 s → `win.close` cycles
  while PowerShell sampled `launcher-win`'s `HandleCount` once a second.
  It went 351 at start, 354↔358 as each window opened and closed, then
  drifted down to 342↔346, and ended at 337. `win.windows()` was
  `['main']` after every close. WebView2 renderers are out of process, so
  each window costs the launcher only about 4 handles.)*
- [x] **macOS, page messages can't add wire lines (#30.6)** — the page
  picks the `seq` in its own call messages, and the launcher wrote it into
  the `CALL` line unchecked. A newline in it split the line, so a gated
  wrapped page could add its own lines: a `MENU` event, or a `CALL`
  stamped with the app's own `file://` origin. Fixed two ways in all three launchers: the page's seq (or, for
  the Windows main window, the library's call id) must be digits (hex for
  that id) or the message is dropped, and the line writer drops any line
  containing a raw CR/LF. *(2026-10-04, `test/wire-lines/` in `tinyjs dev`,
  added to the repo after v0.47.1 shipped.)*
  - **Before:** the forged `MENU` fired the app's `onMenu`, and the forged
    `file://` call wrote a store key the page's own origin is denied.
  - **After:** nothing from the probe reached the backend; normal calls
    still work, and the page's own gated call is still denied.
  - With the seq check disabled for one run, the writer guard alone dropped
    both lines ("dropped a wire line with a raw line break").
- [x] **Linux, same** — compiles clean (no warnings). *(2026-10-03,
  headless, `tinyjs dev` with `TINYJS_DEBUG=1`. A scratch app wraps
  `http://127.0.0.1:8765/<probe>` with `"file://*": "all"` and the page
  origin gated to `["ping"]`, and its `onMenu` writes the id to a file. The
  maintainer's `forge.html` is macOS-shaped, and on Linux it's dropped at
  the missing `|` before and after the fix, so it proves nothing here. A
  Linux variant posts `<__TINY_TOK>|<seq>:<payload>` with the real token
  and puts the forged `MENU` + `CALL q [store.set …, "file://"]` in the
  seq, with colons `:`-escaped since the first `:` ends the seq. It
  sends LF, CRLF and bare-CR variants, plus a bare `abc` seq.)*
  - **Before** (launcher built from 4340c98): LF and CRLF both got past the
    gate. `onMenu` fired, and `forged-lf`/`forged-crlf` landed in the
    store under `file://`. `CALL main:abc` reached the backend. Bare CR
    arrived as a single junk line (the bridge splits on LF only).
  - **After:** none of the probe's messages reached the backend. No
    `forged` keys, no `onMenu`, no `<< MENU`/`<< CALL q`/`main:abc`. The
    page's own `store.set` was still `denied`, and `ping` → `RET 0 "pong"`.
  - With the seq check stripped for one run, the writer guard alone dropped
    all three forged lines (LF, CRLF, CR). `main:abc` got through there,
    as expected.
  - kitchen-sink startup: 22 CALLs, 22 `RET … 0`, nothing dropped.
  - [x] Hand click-through of kitchen-sink menus and dialogs. *(2026-10-03:
    confirmed by the user.)*
- [x] **Windows, same** — compiles clean (MinGW g++ 16.1). *(2026-10-04,
  a probe app gated `"file://*": [ping, log, store.*, win.*]` with
  `"https://trusted.example": "all"`. The probe forges a `CALL` for the
  gated `sysinfo` under the trusted origin, with colons `:`-escaped so
  it fits in a seq. Main window: a raw `chrome.webview.postMessage` JSON-RPC
  with the forged line in `id`. Secondary window (`w2`): the forged line as
  the seq. Each sent with LF and CRLF. Trace via `TINYJS_DEBUG=1`.)*
  - **Before** (launcher built from 4340c98): all four forged lines came back
    `RET … 0 {"pid":…}`, so the gate was bypassed on BOTH windows.
    `not-hex-id` / `w2:abc` reached the backend too.
  - **After:** none of the six messages reached the backend. `ping` still
    worked and the page's own `sysinfo` was still denied in both windows,
    and `win.open` still worked.
  - With the id/seq checks stripped for one run, the writer guard alone
    dropped all four forged lines (`dropped a wire line with a raw line
    break`).
  - kitchen-sink startup: 40 CALLs, 40 `RET … 0`, nothing dropped.
  - [x] Hand click-through of kitchen-sink menus and dialogs. *(2026-10-04:
    confirmed by the user.)*

## #36 page printToPDF paths + #32 Windows blank first paint (`fix/36-pdf-path-32-paint`, 2026-10-04)

- [x] **macOS, a page's `printToPDF` path (#36)** — from the app's own
  pages (`file://`, the dev server) any path works, unchanged. From any
  other origin, the PDF is written directly only inside Downloads (a bare
  name lands there), the app's data dir or the temp dir. Anything else
  opens a save panel pre-filled with the name: the launchers answer a
  `DLG q<qid>` back to the backend as `GOT`, and the `save` op takes an
  optional suggested name. Cancelling rejects with "printToPDF: cancelled".
  *(2026-10-04: a wrapped `http` page with `win.*` granted, in `tinyjs dev`,
  `TINYJS_TEST_AUTODLG=cancel`.)*
  - `~/probe36-victim.txt` got a panel and was **not** written (still
    `ORIGINAL`). The first panel of the session outlasted the test hook's
    ~6 s and was answered by hand, so its PDF went to the picked folder:
    the panel is the gate either way.
  - `probe36.pdf` → `~/Downloads/probe36.pdf`; `/tmp/probe36.pdf` written.
  - `~/Downloads/../probe36-walk.pdf` → panel → cancelled → rejected.
  - An app `file://` page wrote to an arbitrary scratch path as before.
- [x] **Linux, same** — compiles clean (`reply_to_call` routes
  `q<digits>` back as GOT; `gtk_file_chooser_set_current_name`).
  *(2026-10-03, arm64 VM, a wrapped `http://127.0.0.1:8766` page with
  `win.*` + `store.*` + `app.keystroke` granted, in `tinyjs dev`,
  `GDK_BACKEND=x11`. This panel isn't armed by `TINYJS_TEST_AUTODLG`, so
  the page answered it with the launcher's own XTest `keystroke`.)*
  - `~/probe36-victim.txt` got a panel and was **not** written (still
    `ORIGINAL`). Photographed: the panel is transient for the app window,
    with `probe36-victim.txt` filled in and `*.pdf` as the filter.
  - Written directly: `probe36.pdf` → `~/Downloads/probe36.pdf`;
    `/tmp/probe36.pdf`; `~/.local/share/<id>/probe36-data.pdf`.
  - Panel → Escape → rejected with `"printToPDF: cancelled"`:
    `~/Downloads/../x`, `../x` (bare relative walk), `file:///home/…`,
    `/tmp/../home/…`.
  - Panel → Return wrote the PDF where the panel pointed.
  - An app `file://` page wrote `~/probe36-own.pdf` directly, as before.
  - **Found, then FIXED the same day:** the panel opened in the process cwd
    (the app dir under `tinyjs dev`) because `do_dialog` set no current
    folder, and the bridge's Downloads root was a hard-coded
    `$HOME/Downloads` rather than `XDG_DOWNLOAD_DIR` (the Linux twin of the
    Windows `FOLDERID_Downloads` finding). Now a save with a suggested name
    starts in the launcher's `downloads_dir()`. The bridge reads
    `user-dirs.dirs` for the #36 root and for Linux `app.paths`
    downloads/desktop/documents, ignoring a value of `$HOME` or `/` so the
    root can never become the whole home. Re-run with `XDG_CONFIG_HOME`
    pointing at a `user-dirs.dirs` with `XDG_DOWNLOAD_DIR="$HOME/Téléchargements-probe"`:
    `app.paths.downloads` read that dir, `~/Downloads/x.pdf` now got a panel
    and was refused, and the panel was photographed opening in
    `Téléchargements-probe`. The run was stopped before the bare-name case,
    so **a bare name landing in the XDG dir is not yet seen**.
  - Page calls reject with the raw string, not an `Error` (`e.message` is
    undefined). That's the existing convention, not specific to #36.
- [x] **Windows, same** — compiles clean (MinGW g++ 16.1) (`route_ret` routes
  `q<digits>`; `IFileSaveDialog::SetFileName`). *(2026-10-04, a wrapped
  `http://127.0.0.1:8766` page with `win.*` + `store.*` granted, in
  `tinyjs dev`, `TINYJS_TEST_AUTODLG=cancel`. The page was loaded by a CDP
  re-navigation, because the wrapped URL's FIRST load never arrives (the
  #32 entry below).)*
  - `%USERPROFILE%\probe36-victim.txt` got a panel and was **not** written
    (still `ORIGINAL`). Unanswered, the panel opens in Downloads with
    `probe36-victim.txt` filled in and `*.pdf` as the type.
  - Written directly: `probe36.pdf` → `%USERPROFILE%\Downloads\probe36.pdf`;
    `%TEMP%\probe36.pdf`, also with forward slashes; the app's
    `%APPDATA%\<id>\`; `c:\USERS\TARWIN\downloads\…` (case-folded).
  - Panel → cancelled → rejected: `Downloads\..\x`, `Downloads/..\x`,
    `Downloads.\x` (Win32 would trim it to `Downloads`; refused anyway),
    `\\localhost\c$\…`, `\\?\C:\…`, `C:x.pdf` (drive-relative),
    `\Users\…` (rooted, no drive), `file:///C:/…`, `sub\..\..\x`, and a
    name with a tab in it.
  - `Downloads\.. \x` was judged inside, and WebView2 failed it with
    `pdf failed`. Nothing landed in the home dir. `Downloads\...\x`, with a
    real `...` dir present, wrote inside that dir.
  - An app `file://` page (`TINYJS_HTML`) wrote `%USERPROFILE%\probe36-own.pdf`
    directly, as before.
  - **Finding (not a hole):** the bridge's Downloads root is hard-coded as
    `%USERPROFILE%\Downloads`. On this machine the Downloads known folder is
    redirected (`\\Mac\Home\Downloads`, which is what the panel shows), so a
    bare name lands in a folder the user doesn't think of as Downloads. Where
    that folder doesn't exist, it fails. `FOLDERID_Downloads` /
    `User Shell Folders` would be the real root.
- [x] **Windows, blank first window with a dev server (#32)** — the fix is
  the NAV-ask re-issue, not a repaint. The startup `Navigate(url)` hits our
  `NavigationStarting`, which cancels it for the policy ask (`NAVQ n1`).
  The backend answers `allow` in the same tick, and the re-issued
  `Navigate(url)` went out before WebView2 had finished the navigation we
  cancelled. It returns `S_OK`, and then nothing happens: no
  `NavigationStarting`, no completion, `location.href` stays `about:blank`.
  The only completion is the cancelled one (`OPERATION_CANCELED`), ~830 ms
  later, while the browser process is still cold. Every http first load
  was lost this way, a wrapped-site `"url"` included. Later navigations got
  away with it because their cancel finished first. Now the re-issue waits
  for that cancel's `NavigationCompleted` (matched by navigation id), with
  a 2 s timer fallback, and a newer navigation in the window drops it. The
  1px bounds nudge from 5d4b418 (a guess at a paint bug) is removed.
  *(2026-10-04/05, Windows, MinGW g++ 16.1, compiles clean.)*
  - Reproduced first: the react-ts template under `tinyjs dev`, no
    `setMenu`, is blank on the pre-branch launcher AND on the nudge build.
    CDP (`--remote-debugging-port`) reads `about:blank`.
  - Fixed build: the same project paints the React page on the first try,
    with `start`/`commit`/`finish` in the trace. That held twice in one
    session, because the dev watcher restarted the app partway through.
  - A wrapped-site `"url"` app: the first load, then a `location.reload()`
    (the `Reload()` re-issue path), then a link to a second page. Every
    step ran with its own `NAVQ`/`allow`/`start`/`finish`.
  - A `file://` app (no ask) loads as before.
  - Launching through `Start-Process -WindowStyle Hidden` leaves the main
    window unpainted, frame and title included. That's the launch flag,
    not this bug; a normal launch paints.
- [x] **Windows, `tiny.win.open` window** paints and sizes correctly
  (secondary path). *(2026-10-05: from the wrapped page above,
  `tiny.win.open('w2', { page: 'http://…/sec.html', size: '420x300' })`.
  It got `NAVQ n4 w2` → `allow` → `start`/`finish`, painted, and reported
  `innerWidth`×`innerHeight` = 420×300.)*

## PR #37 — Credits.html / "attribution" in the macOS About panel (2026-10-05)

- [x] **macOS, `Credits.html`** — `tinyjs build` copies a project-root
  `Credits.html` into `Contents/Resources/`, and the launcher then omits
  its own `Credits` key so AppKit shows the file. *(2026-10-05: scratch
  app built with this branch's rebuilt launcher; the file was in
  Resources/, `codesign --verify --strict` passed, and the About panel
  showed the Credits.html content and link in place of "Made with
  tinyjs" (seen by the user).)*
- [x] **macOS, `"attribution"`** — with no Credits.html, an `"attribution"`
  string in tinyjs.json replaces the credit line, in `tinyjs dev`
  (TINYJS_ATTRIBUTION env) and a built app (TinyjsAttribution plist key).
  An app with neither still shows "Made with tinyjs". *(2026-10-05: a built
  app with "attribution": "Attribution test — © 2026 Someone" and no
  Credits.html had the TinyjsAttribution plist key, and its About panel
  showed that line with — and © intact (seen by the user). The dev env
  path wasn't run separately.)*

## #41 TS/Vite build without node_modules (`fix/41-npm-install`, 2026-10-05)

- [x] **macOS** — a `--template` project (any `package.json`) now checks for
  `node_modules` (here or in a parent dir, for workspaces) or Yarn PnP's
  `.pnp.cjs` before running `frontend.build` / `frontend.dev`, and stops
  with "run `npm install` first" instead of the tool's own error.
  *(2026-10-05: a fresh `vanilla-ts` scaffold without `npm install`, with a
  global tsc from nvm on PATH, reproduced #41's exact `TS2688 …
  'vite/client'`. After the change, `tinyjs build` and `tinyjs dev` both stop
  with the message; after `npm install`, `tinyjs build` packages the app.)*
- [x] **Windows** — the parent-dir walk handles `C:\\…` paths: an
  uninstalled template stops with the message, and an installed one builds.
  *(2026-10-05: a fresh `vanilla-ts` scaffold under `%TEMP%` with this
  branch's `tinyjs.cmd`: `tinyjs build` and `tinyjs dev` both stopped with
  the message, exit 1. After `npm install`, `tinyjs build` produced
  `dist\v41.exe`. With `node_modules` moved up one directory (workspace
  shape) it still built, and with that removed too the walk climbed to
  `C:` and stopped with the message rather than looping.)*
- [x] **macOS, backend entry at the project root** (`"backend": "main.js"`)
  — the build copies the entry's folder, which is the project root, so it
  copied `.build` into itself until `ENAMETOOLONG`. Now `.build`,
  `node_modules`, `dist`, `.git`, `frontend` and the configured
  frontend dir/dist are skipped. *(2026-10-05: a scratch app crashed with
  ENAMETOOLONG before; after the change it builds with a 1 MB `.build`. A
  normal `src/` app still copies just `src/main.js`, with its frontend
  separate.)* Known leftover: a root entry still carries `src/` (and a
  `src/frontend` inside it) as extra files.

## `tinyjs new --template` prompts + package managers + Alpine (`feat/new-template-prompts`, 2026-10-05)

`--template` with no name asks framework (vanilla react vue svelte solid
preact lit alpine) and language; `--pm npm|pnpm|yarn|bun|vp[:pm]` picks the
package manager, else it asks from the ones whose `--version` runs (npm off a
terminal); dependencies install unless `--no-install`. tinyjs.json's
dev/build become `<pm> run dev|build`. Alpine = the vanilla template +
`alpinejs` + a demo component that calls the backend. Also fixed on the way:
`tjs.stdin.isTerminal` is a boolean (not a function) and stdin is a
ReadableStream, so no prompt in the CLI ever showed — `tinyjs wrap`'s
origins prompt included.

- [x] **macOS** — *(2026-10-05, scratch dir: interactive run in a pty
  (`script`) listed npm/pnpm/bun/vp — yarn not installed, correctly absent —
  and built `alpine` JS with bun + install. `alpine-ts --pm pnpm` scaffolded,
  type-checked and `tinyjs build` packaged it; the built Alpine app clicked
  its own button and stored "hi world — from the backend". `react-ts` with
  no terminal defaulted to npm + install and built. `--template=svelte
  --pm=pnpm --no-install` skipped install and printed `pnpm install` as a
  next step. No-terminal bare `--template`, `--pm nope` and an absent `--pm
  yarn` each fail with a message. `tinyjs wrap` in a pty now shows the
  origins prompt and "2" adds the subdomain wildcard.)*
- [x] **macOS, pre-1.0 vp refused** — `--pm vp` with a vp older than 1.0
  now stops with "run `vp upgrade` first", and the menu marks it. Why: vp
  0.1.x writes overrides for the 0.1 era (`vitest` →
  `npm:@voidzero-dev/vite-plus-test@latest`). That package stopped at 0.1.24
  when vite-plus 1.0 moved to real `vitest@5`, so the project gets
  vite-plus 1.0.0 plus a second `vite-plus-core@0.1.24`. The old core's
  native loader requires `vite-plus/binding`, which 1.0 no longer exports,
  and every `vp build` / `vp dev` dies with a misleading "Cannot find native
  binding … npm has a bug related to optional dependencies". *(2026-10-05:
  reproduced with the global vp 0.1.18 over npm and pnpm. Removing just the
  `vitest` override and reinstalling resolved everything to 1.0.0 + vitest
  5.0.1 and `npm run build` passed. vite-plus 1.0.0 itself is fine.)*
- [x] **macOS, vp ≥ 1.0 end to end** — *(2026-10-05, after `vp upgrade`
  to 1.0.0: `--template react-ts --pm vp:pnpm` wrote 1.0 pins (catalog
  vite-plus 1.0.0, no vitest override). `tinyjs dev` started `pnpm run dev`
  → `vp dev` on 127.0.0.1:5173 (VITE+ v1.0.0) and the app came up;
  `tinyjs build` packaged it.)* Getting there needed one more fix: vp 1.0
  also writes `devEngines.packageManager: pnpm`, and npm 11 then refuses
  `npx` in that project (EBADDEVENGINES), which is how the TS backend was
  bundled. esbuild now runs from the project's own node_modules/.bin when
  present (walking up for workspaces), else `npx` from a temp dir with
  absolute paths. Checked: the vp project and a plain npm `react-ts` both
  build, and with `esbuild` added as a devDependency the local copy is used.
- [x] **Windows, esbuild from a temp dir** — a `react-ts` scaffold (TS
  backend) builds, i.e. `npx` from `%TEMP%` with `C:/…` absolute paths and
  `node_modules\.bin\esbuild.cmd` when installed locally.
- [x] **Windows** — the prompts (stdin/TTY detection under conhost /
  Windows Terminal), `--version` probes through `cmd /c` (a missing pm must
  be left out, not hang), and an `alpine-ts` scaffold with npm and with
  pnpm that builds. Also `tinyjs wrap`'s origins prompt.
- [x] **Linux** — the same: prompts, probes, `alpine-ts` with npm and one
  other pm builds and its button reaches the backend.
  *(2026-10-05: Windows and Linux both confirmed by the user — prompts,
  package-manager probes, Alpine scaffold + backend button, `react-ts`
  build through the esbuild change, and wrap's origins prompt.)*

## macOS subframe calls gated (2026-10-07, shipped v0.50.1)

WebKit hands the `tiny` message handler to every frame, so a cross-origin
iframe could post hand-built calls (all allowed with no `"api"`) and its
replies settled the main frame's pending calls with the same seq. The macOS
launcher now marks subframe calls (`"tiny:subframe"` between payload and
origin, seq prefixed `s` so no RET reaches `__tinyResolve`); the bridge runs
one only when an `"api".origins` key names its origin. Probe:
`test/subframe-gate/` (README has the table).

- [x] **macOS** — *(2026-10-07: pre-fix build, no `"api"`: `pwned` stored,
  0 denials, 3381 replies delivered to the main frame. Fixed build: no
  `"api"` and `"wrapper"` both deny the iframe's `store.set` and every spam
  call, main frame sees only its own 41 replies, its calls all succeed;
  with `"origins": {"file://*": "all", "http://127.0.0.1:8765": ["store.*"]}`
  the iframe's `store.set` runs, replies still 41. Regressions: smoke.html
  passes on a top-level page; proxy-gate's wrapped-site trio (no `"api"` /
  `media.proxy`+`store.*` / `store.*`) refused / SECRET-PAYLOAD / refused.)*

## PR #50 — `"mainWindow": false` windowless tray agents (`feat/headless-tray`, 2026-10-12)

macOS starts the launcher with no main window or WebView (`g_windowless`;
`TINYJS_NO_MAIN_WINDOW` in dev, `TinyjsNoMainWindow` plist key when
packaged). Every macOS app now runs `[NSApp run]` / `[NSApp stop:]` directly
instead of `webview_run` / `webview_terminate` (same calls the Cocoa backend
makes), so regular apps need a regression look too. Windows and Linux are
truly windowless as well (their entries below). Fixture + steps:
`test/windowless-tray/README.md`.

- [x] **macOS (Apple Silicon)**: timed fixture run in dev and packaged:
  `BOOT []`, tray rect non-null, `PAGE` ×2, `WINDOW_CLOSED editor`,
  `CLOSED []` ×2. No WebKit processes before the first open. `dialogs` run:
  confirm `true` / prompt `"value"`. Manual: tray clicks, `<input type=file>`
  pick + cancel. Regression: a regular app shows `["main"]` and its page;
  ⌘Q, menu Quit and closing the main window all exit cleanly; open-file /
  URL-scheme handoff still reaches a windowless app.
  *(2026-10-12: timed fixture passes dev + packaged after the rename;
  `dialogs` run gives confirm `true` / prompt `"value"` on both opens;
  packaged windowless app receives `open -a` file and `tinyhandoff://` URL
  handoffs. By hand: tray Open/Close editor, page Close button, tray Quit,
  file picker pick + cancel all correct, icon survives every close. Regular
  app: page loads, `["main"]`, and ⌘Q / menu Quit / red close each exit 0.
  Non-boolean `mainWindow` and `mainWindow: false` + `url` rejected. Not
  checked: the "no WebKit processes before first open" claim — trusted to
  the PR's vmmap run. Seen along the way, NOT from this PR: packaged macOS
  apps get a bogus onOpenFiles([".../entry.js"]) at launch — the argv scan
  (bridge.js cliPaths) reads the backend's `run entry.js`; f3e64c3 fixed
  dev only.)*
- [x] **macOS x86_64 (Rosetta)**: *(2026-10-12: `TINYJS_UNIVERSAL=1
  ./setup.sh` + `tinyjs build --arch x86_64`, both executables x86_64-only,
  run translated on Apple Silicon / macOS 27. Timed fixture and `dialogs`
  run both pass, exit 0. Not a real Intel Mac.)*
- [ ] **macOS 14** (any CPU; an Apple Silicon VM via UTM/tart would do):
  the AppKit startup/shutdown change has only been seen on macOS 26/27.
- [ ] **Real Intel Mac**: nice to have; Rosetta covers the x86_64 code path.
- [x] **Windows 11 (x64)**: now truly windowless too (`run_windowless` in
  launcher-win.cc: no webview library instance, no main window; a hidden
  `TinyjsHost` window carries the tray, hotkeys, clipboard timer, broadcasts
  and `tiny_dispatch`; the first named window creates the WebView2
  environment with embed()'s profile and options). Main-window
  `app.hide()`/`show()` hide and restore the visible named windows, RELOAD
  reloads them, ATTENTION/BADGE/PROGRESS are no-ops, APPICON goes to the
  named windows. *(2026-10-12: timed fixture passes dev + packaged (`BOOT
  []`, `MENU`, `PAGE` ×2, `WINDOW_CLOSED editor` ×2, `CLOSED []` ×2, exit
  0); no `msedgewebview2.exe` before the first open, six while the editor
  is up, none after it closes; idle launcher + WebView2 private bytes ~12
  MiB vs ~111 MiB with a hidden main window (working set 20 vs 383 MiB).
  Real tray menu (shell callback posted to the host, keys posted to the
  menu window): Close with nothing open is harmless, Open, Open again via
  left click doesn't duplicate, Close, a WM_CLOSE to the host is ignored,
  second Open/Close, Quit exits. Scripted: hide one window, app.hide/show
  restores only what it hid, reload re-runs both pages, main-window
  title/size/center/presence/eval leave the host hidden, icon/attention/
  badge/progress don't crash, clipboard watch + hotkey (via
  `app.keystroke`) fire, close/reopen, page `tiny.win.close()`. Page
  `tiny.dialog.confirm` + `window.confirm`/`prompt` (AUTODLG ok),
  `window.open` with `"popups": "window"`, localStorage shared with the
  regular profile. App with no frontend dir: `[]`. Non-boolean
  `mainWindow` and `+ url` rejected. Regression, same build: regular app
  `["main"]` visible 960x640, hide/show/title/size, named windows
  open/close/reopen, clipboard, hotkey; `"activation": "accessory"` main
  starts hidden as a tool window and shows on `app.show()`. By hand, same
  day, real mouse on the real notification-area icon: Close with nothing
  open, Open (right-click), Open again (left-click, no duplicate),
  `<input type=file>` cancel (`cancel` event) and pick (`change:
  ["pick-me.txt"]`), `tiny.dialog.openFile` cancel (`null`) and pick (a
  `\\Mac\…` UNC path), page Close button → `WINDOW_CLOSED`, reopen, title-bar
  X → `WINDOW_CLOSED`, tray Quit exits. Not covered: packaged open-file /
  URL-scheme handoff.)*
- [x] **Linux (arm64, GNOME/Wayland VM)**: now truly windowless too
  (`g_windowless` in launcher-linux.cc: no main GtkWindow/WebView; main-window
  `app.hide()`/`show()` hide and restore the visible named windows, RELOAD
  reloads them, ATTENTION is a no-op, APPICON goes to the named windows).
  *(2026-10-12: timed fixture passes dev + packaged (`BOOT [] null`, `MENU`,
  `PAGE` ×2, `WINDOW_CLOSED editor`, `CLOSED []` ×2, exit 0); no WebKit
  children before the first open; idle launcher+children RSS ~111 MiB vs
  ~329 MiB with a hidden main window. Scripted: hide → editor invisible, show
  → visible, reload re-runs the page, attention/icon don't crash, main
  `getWinState()` null. Scratch app with no frontend dir starts, `[]`.
  Regular app: `["main"]`, visible, page loads. Non-boolean `mainWindow` and
  `+ url` rejected. `dialogs` run: confirm `true` / prompt `"value"` on both
  opens.)*
- [x] **Linux tray clicks, page input, X11, x86_64**: *(2026-10-12, same VM.
  Tray: sent the real AppIndicator menu's dbusmenu `Event … clicked` (what
  the shell sends on a click) — Close with nothing open is harmless,
  Open → `PAGE`, Open again doesn't duplicate, Close → `WINDOW_CLOSED`, a
  second Open/Close cycle, icon still registered after every close, Quit
  exits; WebKitWebProcess starts on Open and exits on Close. Page input via
  real XTest keys (`app.keystroke`) under `GDK_BACKEND=x11`: `<input
  type=file>` cancel (`cancel` event) and pick (typed path → `change:
  ["wlpick"]`), then the page's Close button → `WINDOW_CLOSED editor`,
  `[]`, reopen works. X11 (XWayland, `GDK_BACKEND=x11`): timed fixture +
  tray clicks pass. x86_64: launcher built in an ubuntu:22.04 amd64
  container + the v0.50.1 release tjs, run under Rosetta on XWayland with
  the host session bus (`--security-opt apparmor=unconfined`, else the
  tray can't reach the bus): timed fixture, tray clicks, `dialogs` run and
  the keyboard picker/Close test all pass. Not covered: a real X11
  session (only GTK's X11 backend on XWayland), real Intel hardware, and the
  file picker on native Wayland (portal, out of process — XTest can't reach
  it).)*
- [x] **Windows DPI (review fix, 2026-10-12)**: `run_windowless` now calls
  `enable_dpi_awareness()`. The webview library did that while building main,
  so without it windowless named windows were DPI-unaware. At **150%
  scaling**, open the fixture's editor next to a named window from a regular
  app: text equally sharp (not soft or stretched); `app.window('editor')
  .getState()` reports the same size for the same `size` in both modes;
  `setPosition` / `center` land in the same place. Repeat after dragging the
  editor to a monitor at a different scale, if there is one.
  *(2026-10-12, Windows 11 VM at 200% (192 dpi), scratch windowless +
  regular apps opening the same 400x300 `editor`: in both, the editor's
  window reports per-monitor awareness (`GetWindowDpiAwarenessContext`) and
  dpi 192, the page sees `devicePixelRatio` 2 and a 400x300 viewport,
  `getState()` gives 400x300 at scale 2, `setPosition(100,100)` → 100,100
  and `center()` → 658,350 in both, 826x671 physical pixels either way.
  By eye, same day: the two editors side by side, text and an 11px line
  equally sharp. Not covered: 150%, and a second monitor at another scale —
  this box has one.)*
- [x] **Windows `hide({ app: false })` (review fix)**: with `"mainWindow":
  false` and the editor open, it must do nothing (it used to hide every named
  window, unlike macOS/Linux). Plain `app.hide()` / `app.show()` still hide
  and restore the editor.
  *(2026-10-12, same windowless scratch app: after `hide({ app: false })`
  the editor's `getState().visible` stays true; `app.hide()` → false;
  `app.show()` → true.)*
- [x] **Linux icon for windows opened later (review fix)**: named windows now
  open with the current `app.icon()` instead of always the startup icon.
  Windowless fixture: `app.icon('/abs/other.png')`, then open the editor →
  taskbar/alt-tab shows the new icon; `app.icon('')`, reopen → back to the
  startup icon. Regular app: same for a named window opened after
  `app.icon()`.
  *(2026-10-12, arm64 GNOME VM, `GDK_BACKEND=x11` on XWayland: scratch
  windowless and regular apps with a red `icon.png`; `app.icon(blue.png)`
  then open the editor → its `_NET_WM_ICON` (read with xprop off the
  client window, not mutter's frame) is blue at 32/48/64; close,
  `app.icon('')`, reopen → red. Regular app's main window follows both
  times. Not covered: native Wayland (no per-window icon there, the shell
  uses the .desktop), and the shell's own taskbar/alt-tab render by eye.)*

## #51 — "latest" from the releases redirect, not the REST API (2026-10-12)

`docs/install`, `docs/install.ps1` and `cli.js` `fetchLatestVersion` now
read the tag off `github.com/<repo>/releases/latest` → `/releases/tag/vX.Y.Z`
instead of the rate-limited `api.github.com` call.

- [x] **macOS `install` lookup** under sh, dash and bash: resolves v0.51.0;
  `TINYJS_VERSION` still pins; a missing repo exits 1 with "could not
  determine latest release". *(2026-10-12, lookup block run on its own.)*
- [x] **`fetchLatestVersion` in tjs** returns v0.51.0 via `res.url` after a
  followed HEAD. *(2026-10-12, macOS, function extracted and run.)*
- [x] **Windows `install.ps1` on PS 5.1 AND PS 7**: `irm
  https://tinyjs.app/install.ps1 | iex` resolves and installs the latest
  tag. The final URL comes from `BaseResponse.ResponseUri` on 5.1 and
  `BaseResponse.RequestMessage.RequestUri` on 7, so both shells need running.
  *(2026-10-13, Windows 11 arm64: the live script — same bytes as
  docs/install.ps1 — under Windows PowerShell 5.1 and portable pwsh 7.6.7,
  each with `TINYJS_HOME` in a scratch dir: both resolved v0.52.0,
  checksum OK, `installed: tinyjs v0.52.0`. User PATH restored after.)*
- [x] **`tinyjs update --check` on Windows and Linux** from an installed
  release (not a checkout, which reports `dev`). *Windows 2026-10-13:
  fresh v0.52.0 install → "already up to date (v0.52.0)"; the same
  install with VERSION set to v0.51.0, and a real v0.51.1 install → "v0.52.0
  is available". Linux arm64 2026-10-13: the live `tinyjs.app/install`
  (same bytes as docs/install) under `env -i` with a scratch `HOME` and
  `PATH=/usr/bin:/bin` (so it can't relink the checkout's
  `~/.local/bin/tinyjs`): v0.52.0 → "already up to date (v0.52.0)"; VERSION
  set to v0.51.0 → "v0.52.0 is available"; a real `TINYJS_VERSION=v0.51.1`
  install → "v0.52.0 is available (you have v0.51.1)".*

## #53/#54 — dev watcher follows the backend's folder; src/backend layout (2026-10-13)

`cmdDev` watches the backend entry's folder instead of `src`, skips a
frontend nested inside it, and skips `.build`/`node_modules`/`.git`/`dist`
for a root backend. `frontend.reload: false` drops the dev page reload.

- [x] **macOS, all layouts**: new `src/backend/`, old `src/main.js`, root
  `"./main.js"` (one restart, no loop), `reload: false`, and the react-ts
  Vite template (HMR only on `src/App.tsx`, restart on `backend/main.ts`).
  *(2026-10-13.)*
- [x] **Windows**: same checks. `tjs.watch` reports backslash paths there;
  the watcher normalises them. *(2026-10-13, Windows 11 arm64, a node
  harness counting dev's output per edit, 5s idle first: `src/backend/`
  — main.js and a nested lib/util.js restart, frontend edits only reload;
  old `src/main.js` — restarts for main.js and src/lib/util.js, the nested
  src/frontend is skipped (reload only), so the backslash skip works; root
  `"./main.js"` — no restarts idle (no .build loop), one per main.js or
  README.md edit, frontend reload only; `reload: false` — frontend edit
  silent, backend restarts; react-ts Vite — App.tsx/App.css are vite
  `hmr update` only, backend/main.ts restarts.)*
- [x] **Linux**: same checks. inotify isn't recursive in libuv, so only
  top-level files in the backend folder restart (unchanged from before).
  **Gap closed 2026-10-14** (`watchTree` in cli.js, per-folder watches on
  Linux only) — see the #58 Linux entry below for the re-run.
  *(2026-10-13, Ubuntu 24.04 arm64, a shell harness counting dev's
  output per edit, 7s idle first: `src/backend/` — main.js restarts,
  nested lib/util.js does NOT (the known inotify gap, and the only one),
  frontend edits reload only; old `src/main.js` — main.js restarts,
  src/lib/util.js doesn't, src/frontend edits reload only; root
  `"./main.js"` — no restarts idle or after a restart (no .build loop),
  one per main.js or README.md edit, frontend reload only; `reload: false`
  — frontend edit silent, backend restarts; react-ts Vite (Vite in the
  arm64 node container with host networking, `frontend.dev` dropped so
  dev only probes `devUrl`, a PATH shim mapping `npx esbuild` to the
  native esbuild binary since this VM has no Node) — App.tsx/App.css are
  vite `hmr update` only, backend/main.ts restarts once.)*

## #58 — `src/shared/`: code both sides import (`feat/58-shared`, 2026-10-14)

`generateBuild` copies the shared folder (sibling of the backend's folder)
to `.build/app/shared` in dev and build; the macOS bare binary gets
`dist/shared`. The dev watcher restarts on shared edits. The macOS
launcher widens the page's file:// read root to the frontend's parent when
a `shared/` sits beside it (with main's launcher the page's import fails).
Probe app: `src/backend/main.js` and a `<script type="module">` page both
import `../shared/settings.js`; the page stores both values via
`tiny.store.set`.

- [x] **macOS**: dev (both sides read it; editing it restarts with the new
  value), built `.app`. *(2026-10-14.)*
- [x] **Windows**: dev and the built exe. Two unknowns: whether WebView2
  loads a `type="module"` script from file:// across folders at all, and
  whether the TPK extraction puts `shared/` beside `frontend/`.
  *(2026-10-14, Windows 11 arm64, project folder with spaces; the probe
  also re-exports from `shared/lib/deep.js`. Both unknowns are yes:
  `import('../shared/settings.js')` loads from file:// (the launcher
  already passes `--allow-file-access-from-files`), in dev from
  `src/frontend/` and in the built exe from
  `%TEMP%\tjs-<buildid>\frontend/`, where `shared/` sits beside it — run
  from a relocated `dist/` (still just the exe + launcher.exe). Backend and
  page agree in each run. Dev restarts on a top-level shared edit AND on a
  nested `lib/` edit (Windows' watcher is recursive, unlike inotify), and
  the page comes back with the new value.)*
- [x] **Linux**: dev and the built binary (same TPK question; WebKitGTK
  already allows file access from file URLs). inotify isn't recursive, so
  only top-level shared files restart dev.
  *(2026-10-14, Ubuntu 24.04 arm64, Wayland, WebKitGTK 2.52.6; same probe
  as Windows incl. `shared/lib/deep.js`. Dev: the page's
  `import('../shared/settings.js')` loads from `src/frontend/` and the
  backend logs the same values. A top-level `settings.js` edit restarts
  and both sides come back with the new value; a nested `lib/deep.js` edit
  does NOT restart (the known inotify gap). Built binary, run from a
  relocated `dist/` in a path with spaces (just the binary + `launcher`):
  extracts to `/tmp/tjs-<buildid>/` with `shared/` (and `shared/lib/`)
  beside `frontend/`; page and backend agree.)*
- [x] **Linux, subfolder edits in dev** (fixes the inotify gap above and
  in #53): `watchTree` gives each subfolder its own watch on Linux, and
  watches a folder made while dev runs (it drops the watches when the
  folder is removed). *(2026-10-14, same VM, harness counting dev's
  output, 8s idle first: `shared/lib/deep.js` restarts and the page shows
  the new value; `src/backend/lib/util.js` restarts;
  `src/frontend/css/app.css` reloads without a restart;
  `mkdir -p shared/new/deeper` then editing a file in it restarts.
  Skips still hold: old `src/main.js` layout, `src/frontend/css` edit
  reloads only, `src/lib` restarts; root `"./main.js"`, no restarts while
  idle or after a restart (no `.build` loop), `lib/` restarts, a frontend
  edit reloads only.)*
- [x] **macOS + Windows, no regression from `watchTree`**: there
  `perDir` is false, so it's the one `tjs.watch` as before. Re-run one
  nested backend edit and one nested frontend edit in dev to be sure.
  *(2026-10-14, Windows: re-run, all good. macOS: no restarts while idle;
  `src/backend/lib/util.js` restarts once; `src/frontend/css/app.css`
  reloads only; `src/shared/lib/deep.js` restarts; the backend comes back
  with both new values.)*

## #96/#97 — dev-mode `navigator.mediaDevices` on macOS 26+ (`fix/macos-dev-mediadevices-96`, 2026-10-26)

WebKit hides `navigator.mediaDevices` entirely unless `MediaDevicesEnabled`
is on; its macOS default demands a TCC grant or a bundle usage string, and
dev has neither because the bridge spawns the launcher through a
title-named symlink (`bridge.js`), so `mainBundle` is the temp workDir —
no Info.plist is ever seen. Wrapping the launcher in a real `.app` cannot
help for the same reason. The launcher flips the private
`_setMediaDevicesEnabled:` WKPreferences SPI in dev/spawn mode only, inside
`apply_webview_prefs` (the one seam both window paths use: secondaries
build their config with it, main applies it to the created webview's
configuration before the first navigation). Probe matrix and WebKit
source pointers: issue #96. Self-driving page: `test/media-dev/`.

- [x] **macOS dev, main window** — `mediaDevices: "object"`, `secure: true`;
  pre-fix binary same flow: `"undefined"`. *(2026-10-26, macOS 27.0.1
  arm64, repo launcher + TINYJS_LAUNCHER A/B, results via tiny.store.)*
- [x] **macOS dev, getUserMedia end to end** — the TCC mic prompt appeared
  once on first run, and after Allow the stream opened
  (`granted: MacBook Air Microphone`, track stopped after). Later runs of
  the same launcher binary get no prompt — the grant persists, the
  documented attaches-to-the-shared-launcher behaviour. Note this also
  explains why the bug looked new: with a standing grant even a pre-fix
  launcher exposes the API, so macOS 26/27 TCC tightening was what pulled
  the mask off. *(2026-10-26.)*
- [x] **macOS dev, secondary window** — `tiny.win.open('second')` on the
  same page: `mediaDevices: "object"` (the `do_winopen` config path;
  `TINYJS_HTML` runs can't cover it — a materialized page has no sibling
  for the default `index.html`, so the page must be the app's frontend).
  *(2026-10-26.)* Both windows re-measured after the prefs-seam refactor
  (`apply_webview_prefs`, one seam for both paths): main `object` +
  `gum: granted`, second `object`. *(2026-10-27, macOS 27.0.1 arm64, temp
  app dir + page as frontend, no TCC prompt — standing grant.)*
- [x] **macOS packaged `.app` on the fixed launcher** — no code path
  changes (`g_bundle_mode` gates the SPI out), but per house rules an
  eyeball of a built app declaring `permissions.microphone` belongs here:
  prompt once, record, `mediaDevices` still exposed. Method (no tool-dir
  install needed): `tinyjs build` a temp app, replace
  `<app>.app/Contents/MacOS/<name>` with the freshly built fixed
  launcher, re-sign ad-hoc — the same embed the build would have done.
  One trap: the verification needs a bundle id TCC has never granted —
  the first attempt reused the same executable the dev run (below) had
  just gotten a mic grant for, and the packaged `getUserMedia` came back
  `granted` with NO dialog. Rebuilt as a fresh id, the prompt appeared
  (naming the app, showing the declared usage string — confirmed by
  hand; the dev-mode dialog shows no reason text, the no-Info.plist
  difference #96 documents), blocked `getUserMedia`
  until Allow, then `md_main` = `object` + `granted`, `md_second` =
  `object`. *(2026-10-27, macOS 27.0.1 arm64, `test/media-dev/page.html`
  as the app's frontend, results via tiny.store.)*
- [x] **Clean-TCC evidence, no reset run** — a system-wide
  `tccutil reset Microphone` was judged out of scope on this machine
  (it clears every app's mic grant, not just ours). Evidence instead:
  the FAIL side is the #96 field report itself — `navigator.mediaDevices`
  `undefined` under `tinyjs dev` on macOS 26+, from a reporter with no
  standing grant on this launcher; the fixed side was observed from an
  un-granted identity — the 2026-10-27 dev run above used a freshly
  built ad-hoc launcher (new code identity, no prior TCC grant for it):
  its first `getUserMedia` popped the TCC mic dialog (answered by hand),
  then reached `granted: MacBook Air Microphone`.

## `rememberWindows` + declared size (#106, 0.70.3)

- [x] **Windows** — *(2026-11-04, Windows 11 arm64 VM, 200%, dev run, read back
  via getState)* changed `size`: main and a win.open window take the new size at
  their saved position; a size set by setSize comes back while the declared size
  is unchanged; a placement with no `declared` resets its size once and keeps its
  position. **Separate, older (since #70), shared code so probably every OS:** a
  `win.open` called straight away in `init` skips the saved placement altogether,
  because `screensCache` is still null (bridge.js, `win.open`). Opened 800 ms
  later it restores fine.

## txiki.js 26.11.0 + no more curl in fetch (`txiki-26.11.0`, 2026-11-04)

Pin bumped from v26.6.0. The curl half of bridge.js's fetch wrapper is gone;
redirects are still followed by hand (TODO-txiki.md says why). On each OS:

    bin/tjs run test/fetch/check.js           (Windows: bin\tjs.exe)
    bin/tjs run test/update-fetch/check.js

then the rest of the suite (`test/*/check.js`, `test/*/check.py`), and a real
built app that fetches (amp is the good canary: feeds and radio streams).

- [x] **macOS arm64** — *(2026-11-04, macOS 27.2)* check.js all ok, real hosts
  included; the whole repo suite passes, GUI harnesses included. Page-level
  tiny.fetch (TLS 1.2, root path, redirect chain, h2 manual redirect, header
  gate, POST, radio stream + cancel) in `tinyjs dev` and a built universal app.
  amp itself not run.
**2026-11-04, bridge.js now sends `connection: close` on every fetch and check.js
has keep-alive and parallel cases (TODO-txiki.md). The results below predate that;
redo them on whichever txiki build is pinned next.** So far: macOS arm64, 26.11.0
tag and master 4cf1ecc, check.js all ok with real hosts. ubuntu:22.04 arm64
container: tag aborts (rc 134) on the h3 case; master passes except 3 hosts failing
DNS on Docker Desktop's resolver. Windows: 26.11.0 tag only, under Windows x86_64
below. amp not run on the others. Master not run on Windows.

- [ ] **Linux** — *(2026-11-04, ubuntu:22.04 arm64 container, headless)* built
  with `TJS_BUILD=1 CC=gcc-12 CXX=g++-12 ./setup.sh`: floor holds (GLIBC 2.35,
  GLIBCXX 3.4.29, CXXABI 1.3.9), no new libs NEEDED. check.js and update-fetch
  pass there. Not yet seen on a desktop: a built app, tiny.fetch from a page,
  amp's feeds. x86_64 only by CI.

  **Desktop pass, 2026-11-04, NOT ticked: regressions.** Ubuntu 24.04 arm64
  (Parallels), binaries built as CI does in ubuntu:22.04, arm64 and x86_64; old
  = `main` + 26.6.0. Passing: floor, arch and linked features on both arches;
  check.js and update-fetch on arm64 and in an x86_64 container (Rosetta); the
  rest of the suite on arm64 (signed-updates' packaged-app case skips off
  macOS); page-level tiny.fetch in `tinyjs dev` and a built app (TLS 1.2, root
  path, redirect chain, h2 manual redirect, header gate, POST, radio stream +
  cancel, 0 curl calls); `SSL_CERT_FILE` with a private CA, on TLS 1.2 and 1.3;
  all 60 of amp's picked feeds when fetched one at a time.

  Failing on 26.11.0, fine on old. check.js misses all of these: its listeners
  send `connection: close` and it never fetches in parallel.
  - **Same-origin redirect on a keep-alive connection fails**, every time.
    bridge.js cancels the 3xx body and refetches, which hits the next bug.
    Native follow works. Real: `https://stream.gensokyoradio.net/3` (301 to
    `/3/`), `https://www.prun.net/stream`; amp's now-playing for gensokyo is null.
  - **A fetch right after abort() or reader.cancel() rejects** on that origin
    ("Network request failed"), as does one already in flight. 1 to 5 ms later
    it's fine. Native fetch too, so not bridge.js.
  - **While an HTTP/1.1 stream is open, another fetch to that origin never
    answers.** Local fixture over http and https, and a second stream on
    icecast.radiofrance.fr. A second stream on ice1.somafm.com was fine
    (protocol not checked). Sending `connection: close` on the stream avoids it
    (fixture only).
  - **Parallel fetches to one h2 host fail or kill the process.** simplecast:
    2 of 4 and 2 of 8 succeed. `www.google.com`, three rounds of 4 on bare tjs:
    SIGABRT, 5 of 5, in `uv.epoll_ctl_flush` under `uv_run`; a single round of
    4 tiny.fetch in a built app returned four 200s, so it may take a later
    round. amp exits on 4 parallel `podFetchFeed` to simplecast (2 of 2).
    cloudflare, raw.githubusercontent, itunes and art19 pass.
  - **`tjs:ffi` dropped `Lib`, `CFunction` and `DlSymbol`**, so a static import
    is a SyntaxError at startup. matcha exits 1 in dev and built. boo, coo3d,
    kitchen-sink, kraa, kraa3d and treez import the same names. Not in
    CHANGELOG. Not Linux-specific: it's the module's export list.

  Hosts the curl fallback was rescuing: 5 of amp's 415 stations (`wmse.streamguys1.com`,
  `s2.voscast.com`, 2 on `mediaserv38.live-streams.nl`, `server.laradio.online`)
  fail "certificate validity has expired". curl accepts them and OpenSSL
  verified wmse's chain; `SSL_CERT_FILE` with the system bundle doesn't help.
  Cause not found. No real station in the sweep failed on an `ICY 200 OK` status
  line, though a fixture sending one is rejected ("HS: URI missing").

  `http://localhost` reaches a `::1`-only server when /etc/hosts maps localhost
  to ::1 (22.04 container). Stock Ubuntu maps it to 127.0.0.1 only, so there it's
  refused.
  Not from this branch: `tiny.fetch` with a `URLSearchParams` body goes out as
  `text/plain` (tiny.js stringifies it), and amp's `tiny.proxyURL` radio check
  got media error 4 on old and new alike.
  Still not run: amp's own UI by hand, a real x86_64 desktop.

  **Rerun on ca71c32 (`connection: close`), 2026-11-04, same VM, arm64, resolver
  systemd-resolved (127.0.0.53). Replaces the fetch failures above for fetches
  through bridge.js. Stays unticked: the tag aborts, master fails megaphone.**
  - **26.11.0 tag:** check.js aborts (rc 134) at the h3 case, after 47 ok. Repeated
    rounds of parallel fetches to google.com and simplecast abort, 4 of 4. Before
    that, the keep-alive, redirect, abort and open-stream cases all pass. Page tests
    and amp not rerun on it.
  - **Master 4cf1ecce** (built in ubuntu:22.04 with gcc-12, loads there; floor holds
    but now sits AT the ceiling: GLIBC 2.35, GLIBCXX 3.4.30, CXXABI 1.3.13): check.js
    all ok including h3, the 5 s first lookup and 20 MB × 4 to simplecast;
    update-fetch and the rest of the suite pass (dialogs needs a short cwd, below).
    Page-level tiny.fetch same in dev and built, 0 curl calls. amp's handlers through
    its real backend: 7/7 feeds, 4 parallel `podFetchFeed` return, gensokyo's
    now-playing is back. 8 × 3 rounds to google, simplecast, cloudflare: all ok.
    The three truncated-DNS hosts resolve in 0.6 to 1.6 s on this resolver.
    Sweep of amp's lists: radio 393/415 (curl 397; the gap is the 5 RSA-only hosts
    in TODO-txiki.md, and radiorakel timing out once), feeds 55/60.
  - **OPEN, master only, not in check.js: `feeds.megaphone.fm` fails about 1 fetch
    in 5** ("Network request failed", under 150 ms), serial too: 24/30, with 300 ms
    gaps 12/15, 1.5 s gaps 6/8. It's the 5 feeds missing from that sweep; amp picks
    10 from this host. `connection: keep-alive` on master 30/30, tag with
    `connection: close` 30/30, 26.6.0 30/30. The host advertises h3, but that's not
    shown to be the cause: google and cloudflare do too and pass 30/30.
  - `test/dialogs/check.js` EADDRINUSE was the rig: its socket under `tjs.cwd` passes
    108 bytes, and a long enough cwd truncates onto `.tmp` itself. Passes 3/3 from a
    short cwd. Run the suite from a short path.
  Not run: x86_64 of master, amp's UI by hand, Wayland vs X11 differences.
- [ ] **Windows x86_64** — never run. New `tjs.exe` hash is pinned from the
  release zip. Watch: #1165's connects (`http://151.101.2.133/`, 6 in a row),
  non-ASCII script paths and arguments (#1164; try it from a path like
  `C:\中文\`, and a built app given a non-ASCII file), no curl.exe in the task
  list during a fetch, stdin writes.
  *2026-11-04, Windows 11 arm64 VM, so the x64 `tjs.exe` ran EMULATED; not seen
  on x64 hardware.* Seen working: check.js all ok, real hosts included (#1165's
  connects, 6 in a row, stdin writes, non-ASCII script path and argument); no
  curl.exe during check.js, a dev + built app, or amp. Page-level tiny.fetch in
  dev and built (TLS 1.2, root path, redirect chain, h2 manual redirect, header
  gate, POST bodies, stream + cancel), from a Cyrillic/CJK/emoji project folder,
  built app given a non-ASCII file. tinyjs itself installed under such a folder
  runs `--version` and `dev` (26.6.0 ENOENTs there). SSL_CERT_FILE honoured.
  Built amp copy, its own RPCs called from inside the exe (no CDP in built apps):
  8/8 feeds, resolveStream, 2/3 streams. The third (FIP) is the instant-refetch
  failure below. Suite: pass or skip, except:
  - **OPEN, regression — keeps this box unticked:** on an HTTP/1.1 keep-alive
    origin an in-flight streaming response blocks every other fetch to that
    origin until it's cancelled or aborted, and an unread endless body blocks
    them for good. Local repro, 26.6.0 fine, 26.11.0 hangs, details in
    TODO-txiki.md. Not yet run on macOS/Linux.
  - FIP (nginx, chunked keep-alive): a fetch right after cancel/abort fails at
    once. 26.6.0 fails too ("HS: URI missing"), so not a regression.
  - update-fetch fails as written, on 26.6.0 too: `inst + '/cli.js'` is a mixed
    path, and tjs can't resolve relative imports from one (`bin\tjs.exe run
    test/x/check.js` hits the same thing; use backslashes). A scratch copy with
    backslash paths passes all six, tarpit 30.7 s.
  - capabilities needs TINY_JS set (`here` is a /C:/ path), then all ok.
    linux-mime ends in ENOENT on `rm` once PATH has no Git Bash; no FAIL lines.
    Neither is new.
  - resources crashes in `publish`: bsdtar on `данные` (known, entry above).
    Passes with that fixture renamed `données`.
  - `http://localhost` still can't reach a ::1-only server here ("conn fail:
    wsaerrno 0", `[::1]` works), so cli.js's second probe and the scaffold's
    `--host 127.0.0.1` stay.
  Not run: a real ears-on radio play in amp; the #67 reporter's network.

  **Rerun on ca71c32 (`connection: close`), 2026-11-04, same VM, 26.11.0 tag, x64
  emulated. Replaces the OPEN regression and FIP bullets above for fetches through
  bridge.js; native pooled fetch wasn't re-measured.** check.js all ok, including the
  new keep-alive and parallel cases, 3 rounds of 4 to google.com and 4 at once to
  simplecast: the Linux abort didn't show. Page-level tiny.fetch dev and built from
  a non-ASCII folder: all ok but `tiny.proxyURL` (documented macOS/Linux only), 0
  curl.exe. Built amp 0.15.1 (examples `origin/main` 6dc2423): 8/8 feeds, 3/3
  streams including FIP, `.pls`, 0 curl.exe.
  `tjs:ffi`, built and launched, examples 6dc2423: matcha, boo, coo3d, kraa, kraa3d,
  treez and kitchen-sink all exit 1 with no backend; the import alone gives
  "SyntaxError: Could not find export 'Lib'". Examples `origin/ffi-dlopen` 098328f
  (not merged): all 7 backends run. matcha's SetThreadExecutionState through dlopen
  returned 0x80000000 then 0x80000001, so its call landed with the high bit intact.
  Only startup was checked for the other six here; FFI itself has its own section
  below.
  The rest of the suite and update-fetch weren't rerun (cli.js doesn't load bridge.js).
- [ ] **Windows arm64** — never run. CI's source build: the WAMR patches
  `git apply --check` clean on 26.11.0's deps/wamr (same pin as 26.6.0), but
  neither the MSVC build nor the wasm test step has run. New since 26.6.0: the
  build applies `patches/*.patch` with git at configure time. They have LF
  context and the leg's clone has `core.autocrlf=false`.
- [ ] **macOS x86_64** — *(2026-11-04, Rosetta only)* `check.js --offline`
  passes, and a `--universal` build run with `arch -x86_64` did every
  page-level tiny.fetch case. Not seen on a real Intel Mac.

## Release mitigations on txiki 26.11.0: curl fallback, Linux without HTTP/3 (2026-11-06)

`bin/tjs run test/fetch/check.js` (Windows `bin\tjs.exe`) covers both. macOS arm64
passes, on the tag and on the tag built with HTTP/3 off, which only shows that the
build works and fetch still does without QUIC. It says nothing about the abort.

- [x] **Linux: HTTP/3 off stops the abort** — *(2026-11-06, 4f4e8e2, Ubuntu 24.04
  arm64 VM, systemd-resolved; tjs built in ubuntu:22.04 with gcc-12 for arm64 and
  x86_64, floor assertions pass, GLIBCXX now 3.4.29)* check.js all ok (64) on both,
  x86_64 under Rosetta in a 22.04 container, so not a real x86_64 desktop. Rounds of
  8 parallel fetches to google and simplecast, 3 rounds a run: the old tag build
  dumped core in 10 of 10 runs, this build 8/8 in every round (10 runs on 41b806d, 6
  on 4f4e8e2). Responses keep `alt-svc`, and the only UDP socket is DNS. update-fetch
  and the suite pass; page tiny.fetch same in dev and built. megaphone 30/30 serial.
  Sweeps: feeds 60/60, radio 397/415. Of the 4 stations curl reached on 11-04 that
  missed, 3 fail in plain curl now too, and s2.voscast then answered 5 of 5.
  amp's backend handlers: 7/7 feeds, 4 parallel `podFetchFeed` and `tiny.fetch` to
  google return, gensokyo's now-playing works. Its `resolveStream` on prun.net times
  out, and its `tiny.proxyURL` radio check gets media error 4, both on 26.6.0 too.
  Curl on PATH is what reaches RSA-only `wmse.streamguys1.com` (200; rejected with it
  off PATH; it refuses ECDHE and DHE, and its certificate is valid). amp by hand on
  this build under `tinyjs dev`, listened to on the VM: works, and it exits clean.
  - **Fallback:** a certificate only curl trusts (`CURL_CA_BUNDLE`, standing in for
    the OS store): GET 200, POST rejected. With `SSL_CERT_FILE`, both 200 and no
    curl. Cancel and abort leave no curl running; the URL and headers aren't in argv.
  - **Fixed since:** a fetch where the runtime and curl both fail, then an exit at once,
    left its `tinyjs-fetch-*` dir with the headers file in it. It's awaited now, and
    check.js has a case for it (macOS arm64). Rerun on a2e94c4: check.js all ok (68) on
    both Linux arches, once each. With a private TMPDIR, no dir is left after that exit,
    after a HEAD then an exit at once, or with a live stream's body unread. An exit
    mid-stream also left no dir, though one such run returned rc 1, cause not captured,
    and 9 more (6 on a2e94c4, 3 on f907a19) returned 0. curl goes once its next write fails.
  - **Still open:** with no curl, the error for an
    RSA-only host reads "The certificate validity has expired", which is misleading.
- [ ] **Linux: a CI build has HTTP/3 off** — not run. The cache key moved to v3, and
  the glibc floor and verify steps need to pass on a `release-test/*` run.
- [x] **Windows: curl fallback** — check.js's `curl fallback` cases, in a built GUI
  app too: `curl.exe` must not flash a console window. *(2026-11-06, 41b806d, Windows
  11 26200 arm64 VM, x64 tjs emulated, System32 curl.)* Rerun on 4f4e8e2 where it says so
  below. check.js all ok, 2 of 2 on 4f4e8e2, once its `.curlrc` case had a backslash
  path (tjs can't load bridge.js from a forward-slash one); that case's `-q` is real,
  since System32's curl sends the file's header without it and not with it. One earlier
  run hit the slow fetch below. A built app, and
  one in a Cyrillic/CJK/emoji folder with a TEMP to match:
  a WinEvent hook saw the control (plain `tjs.spawn(['curl', ...])`) show a console,
  and no console created or shown across every fallback, including the first
  `curl --version` probe. `tinyjs dev` works too (flash not measurable there).
  curl.exe's parent is the launcher, its argv has no headers, and it's gone after
  cancel, abort and `app.quit()` with a stream open. Refused all the same:
  self-signed, expired, wrong-host and untrusted-root on badssl. A POST goes out
  once. `&`, `%PATH%`, `^`, `|`, `$()`, quotes and backslashes in a URL reach the
  server as text. 3 MB binary byte for byte. wmse (RSA-only) HEAD and GET work.
  Built amp 0.15.1 (examples main 94456db): 8/8 feeds, 3/3 streams, `.pls`, no
  curl.exe seen. test/ffi and matcha + kitchen-sink from ffi-dlopen pass again. Suite:
  pass or skip, except capabilities, linux-mime, update-fetch and resources, as
  recorded above (their workarounds not rerun). Not run: a certificate only the OS
  trusts, since that means changing this machine's store.
  **Fixed by 4f4e8e2, rerun in all of those apps:** a non-ASCII response header no
  longer corrupts the body (41b806d gave `ose\r\n\r\nBODY`), and the header and URL
  files are gone once headers arrive, so `app.quit()` with a stream open no longer
  leaves `authorization: Bearer ...` in temp (a hung server before headers: not
  tried). System32's curl.exe by full path, `-q`, `--globoff`, `--config`: seen in the
  argv, no console flash, no secret or URL in it. A URL now goes out normalized through curl
  (`/sp%20ace`, `/back/slash`), while native still sends `/sp ace` raw, as on 26.6.0.
  Built amp again: 8/8 feeds, 3/3 streams, `.pls`, no curl.exe seen.
  **OPEN, runtime, slow or failing after a host advertises h3:** TODO-txiki.md. 4f4e8e2
  doesn't change it: google through bridge.js, median 2392 ms of 30.
- [ ] **Windows arm64** — not run. Keeps HTTP/3 on; nothing has shown whether the
  abort happens there.

**CONNECT proxy through the curl fallback.** curl's `-i` printed the proxy's own reply
first, so the status was the proxy's, headers were empty and the real header block
landed in the body; a proxy refusing CONNECT read as the site's own 403. Also in the
code before the bump. Now `--suppress-connect-headers` (curl 7.54). check.js has a
local proxy for it: a refusal (offline) and wmse by GET and HEAD. All three fail
without the flag.
- [x] **Windows** — *(2026-11-06, same VM, x64 tjs emulated, System32 curl)* check.js all
  ok, 2 of 2. A built app with `https_proxy` set against a separate proxy: real headers
  and a clean body, by GET and HEAD. Rest of the built-app run and amp unchanged. An
  authenticating proxy (407 first) not tried.
- [x] **macOS arm64** — *(2026-11-06, curl 8.7.1, SecureTransport)* check.js's three
  proxy cases pass, including wmse by GET and HEAD. Built apps with `https_proxy` not tried.
- [x] **Linux** — *(2026-11-06, f907a19, Ubuntu 24.04 arm64 VM, curl 8.5.0, and x86_64
  under Rosetta in a 22.04 container, curl 7.81.0)* check.js all ok (67), 2 of 2 on arm64
  and once on x86_64; update-fetch passes. A separate tunnelling proxy with `https_proxy`
  set, to RSA-only wmse: GET has the real headers and a clean body, HEAD matches what
  curl gets without a proxy, in bare tjs and a built app, plus a stream through
  `tiny.fetch`. A proxy refusing CONNECT rejects with "http_proxy -> 403". With the flag
  taken out of a scratch copy, the same runs show the bug: the proxy's header, the real
  block in the body, and its 403 as the response. An authenticating proxy (407 first)
  not tried.

## `tjs:ffi` on txiki 26.11.0: `Lib`/`CFunction` gone, `dlopen()` only (2026-11-04)

Examples' `origin/ffi-dlopen` (098328f, NOT merged) moves its 7 FFI apps over; on
`main` they exit at startup on 26.11.0. On Windows:

    bin\tjs.exe run C:\full\path\to\test\ffi\check.js

- [x] **Windows, x64 tjs EMULATED on arm64** — *(2026-11-04, Windows 11 26200, the
  Parallels VM, tjs 26.11.0)*
  - test/ffi/check.js, all 33: ints, the uint32 high bit both ways, 64-bit ints,
    float/double beside ints, UTF-8 strings, buffers, wide strings through buffers,
    pointers and views (the exe's own MZ/PE header), bool_u32, structs by pointer,
    struct returns by value at 8 and 16 bytes (`StructType` and `defineStruct`),
    POINT passed by value, Pointer/PointerType, JSCallback (EnumWindows, 26k qsort
    calls, a throw inside one), variadic doubles, `name`, data symbols, `'c'`/`'m'`,
    `dlopenCProto`, catchable load errors, `optional`, `close`/`using`, a kept
    result through GC and a timer, spread and detached symbols, a non-ASCII DLL
    path, and a soak (working set 27.0 MB before and after).
  - matcha, built, its own functions: api.activate/deactivate, tray left click,
    `allowDisplaySleep` both ways, a timed session ending by its own timer, after
    GC. CallNtPowerInformation from ANOTHER process saw ES_SYSTEM_REQUIRED come
    and go at every step, and go when the process was killed. This VM holds
    ES_DISPLAY_REQUIRED already (baseline 0x2), so that bit can't be read from
    outside; the check.js case reads it back from SetThreadExecutionState's return
    value (0x80000003, 0x80000001, 0x80000000), on matcha's expression but not in
    the app. Not done: a real tray click with the mouse, or an actual idle-sleep wait.
  - kitchen-sink, built: `ffiInfo` RAM, uptime and pid agree with the same calls
    made from Python; `ffiSysctl` and `zlibRoundtrip` throw their explanations;
    `sysinfo` says ram n/a. Backend API only; its page wasn't opened.
  - boo, coo3d, kraa, kraa3d, treez: start, and only `dlopen` in their macOS branch,
    so Windows runs no FFI in them.
  - The same dlopen, StructType, string, buffer and STES shapes also pass on 26.6.0.

  Limits seen, none used by an app. `errno()` reads 0 after a CRT call fails, here
  and on 26.6.0: it doesn't see a DLL's C runtime, so read `_errno()` from that DLL
  or use GetLastError. `dlopenCProto` throws on a `(void)` parameter list ("Unknown
  type") and on calling a struct returned by value; this parser is JS, so probably
  not Windows-only, but that's unchecked. A u64 past 2^53 comes back as a rounded
  number and BigInt args are refused, as tjs.d.ts says, while a `defineStruct` u64
  field unpacks to a BigInt.
- [ ] **Windows arm64, native tjs** — not run. Different FFI ABI (libffi on Windows
  arm64), and its tjs only comes from CI, so x64 emulation says nothing about it.
  Needs a `release-test/*` build, then test/ffi/check.js and matcha on it.
- [ ] **Windows x64 hardware** — not run. test/ffi/check.js and matcha.
- [x] **macOS arm64** — *(2026-11-04, macOS 27.2, M3 Max, examples `origin/ffi-dlopen`
  098328f, scratch copies with throwaway ids)* CGEventCreate, CGEventGetLocation (a
  struct of two doubles by value), CFRelease, sysctlbyname and libz, called directly:
  all fine on the 26.11.0 tag, master 4cf1ecc and 26.6.0, including 200k cursor calls.
  Values agree with `sysctl`, and zlib's output size with Python's. All 7 apps stay up
  under `tinyjs dev` and built, with nothing logged, on the tag. boo, coo3d, kraa,
  kraa3d and treez each read the real pointer through their own `cursor()`, seen with
  a probe appended to the scratch copies, dev and built. kitchen-sink's backend API,
  called from a test page under `tinyjs dev`: ffiInfo, ffiSysctl (a bad name throws)
  and zlibRoundtrip are right. Not checked: whether the apps visibly react to the
  pointer, kitchen-sink's own page. `test/ffi/check.js` is Windows only and didn't run.
- [ ] **macOS x86_64** — not run. The same calls on a real Intel Mac.
- [x] **Linux arm64** — *(2026-11-04, Ubuntu 24.04 desktop, examples
  `origin/ffi-dlopen` 098328f, tjs 26.11.0 tag and master 4cf1ecce, same results)*
  All 7 apps build, and their built backends are still up after 7 s with no errors
  logged; windows weren't looked at. kitchen-sink's `...libc.symbols`, through its
  backend API: gethostname, sysinfo uptime and RAM agree with `hostname` and /proc,
  getpid matches tjs.pid, libz.so.1 round-trips, ffiSysctl throws its explanation.
  `test/ffi/check.js` skips here (Windows only), so the feature matrix didn't run.
  A leftover `.call(x)` throws `RangeError: expected 1 arguments but got 0` on a
  one-argument symbol (`abs.call(-5)`) rather than quietly losing it. x86_64 not run.
