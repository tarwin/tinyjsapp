# Changelog

All notable changes to tinyjs. Versions are git tags (`vX.Y.Z`); a tag push
builds and publishes the release. The rendered version of this file lives at
https://tinyjs.app/changelog.

## 0.12.0 — 2026-07-14

- **Microphone + camera** — `getUserMedia()` now just works in tinyjs pages:
  the launcher answers WebKit's per-origin media-capture prompt itself, so
  users see a single system dialog naming your app instead of a double prompt
  naming `file://` or localhost. `tiny.app.permissions.check/request` gain
  `'microphone'` and `'camera'` (the TCC layer underneath, via
  AVCaptureDevice) for onboarding flows.
- **`"permissions"` in tinyjs.json** — packaged apps declare
  `{ "microphone": "why", "camera": "why" }`: the strings are injected as
  `NSMicrophoneUsageDescription`/`NSCameraUsageDescription` (required — macOS
  kills a bundled app that captures without them), and builds signed with
  `signIdentity` get the matching hardened-runtime device entitlements
  (`com.apple.security.device.audio-input`/`.camera`) — without which the
  hardened runtime denies capture outright even after the TCC grant.

## 0.11.0 — 2026-07-14

- **Native clipboard** — `tiny.clipboard` / `app.clipboard`: NSPasteboard in
  the long-lived launcher process. `read()` returns
  `{ kind, changeCount, text, html, paths, image, color }` (image lands as a
  png temp file), `write()` takes any combination of the same (image: png
  path, data: URL, or base64), `changeCount()` is a cheap change probe, and
  `watch(ms)`/`onChange` push `clipboard-change` events with a `self` flag
  for your own writes (backend: `onClipboardChange` auto-starts the
  watcher). No more per-second `osascript`/`pbpaste` spawns or scratch-file
  plumbing — and multi-file writes can't lose their tail to the short-lived
  writer flush race.
- **Drag files out of the app** — `tiny.win.startDrag({ files, image? })`
  (alias `tiny.win.dragOut`) starts a native NSDraggingSession from a page
  mousedown, so real files can be dragged into Finder, Slack, or any other
  app. Optional custom drag-image png; file icons (stacked for multi-file
  drags) otherwise.
- **Native keystrokes** — `tiny.app.keystroke('cmd+v')` / `tiny.app.paste()`
  post a CGEvent from the launcher: one permission (Accessibility) whose
  prompt names *your app*, instead of osascript→System Events needing
  Automation + Accessibility with prompts naming osascript or the terminal.
  Resolves `{ ok, trusted }` so apps can detect the missing grant.
- **Permission checks** — `tiny.app.permissions.check(name)` / `request(name)`
  for `accessibility`, `screen`, `notifications`, and
  `automation[:<bundle-id>]` → `granted` / `denied` / `undetermined` /
  `unsupported`, so apps can build a proper onboarding flow ("needs
  Accessibility to paste — Open Settings") instead of discovering failure at
  first use. `request` prompts (Accessibility opens System Settings pointed
  at the app).
- **hide() now hides the app, not just the window** — `tiny.win.hide()` /
  `app.hide()` use `NSApp hide`, so macOS deactivates the app and returns
  focus to the previously active app automatically. Palettes can
  `hide()` → `paste()` with no frontmost-pid bookkeeping. Secondary windows
  (`app.window(id).hide()`) still just order out. `show()` unhides +
  activates as before.
- **`show({ activate: false })`** — surface the window without stealing
  focus, for overlay/HUD panels that must appear over the active app
  (clicking the window still activates normally).
- **`tiny.app.mousePosition()`** — cursor position three ways:
  global (`x`/`y`, same top-left coordinates as `win.setPosition` — open a
  palette at the mouse), `window` (relative to the calling window's content
  area in `clientX`/`clientY` units, with an `inside` flag — valid even
  while the cursor is outside the window), and `screen` (the display the
  cursor is on: `{ x, y, width, height, scale }`).
- Fix: builds with a real `signIdentity` now sign with the **hardened
  runtime** and a secure timestamp (`codesign --options runtime
  --timestamp`) — both required by notarization, which previously rejected
  the .app. Ad-hoc builds are unchanged. codesign stderr is no longer
  swallowed when a real identity is used, so keychain errors surface.
- Docs: README gains a "Distributing to other people" walkthrough — why
  ad-hoc-signed apps are blocked on other Macs, that Developer ID +
  notarization require the paid Apple Developer Program ($99/yr, a free
  Apple ID won't do), and the one-time cert / `notarytool
  store-credentials` setup ending in `tinyjs build && tinyjs notarize`.

## 0.10.2 — 2026-07-13

- Fix: `app.notify()` / `tiny.notify()` can no longer crash the backend when
  fired without `await`. The dev-mode osascript fallback could reject (e.g.
  `ENOENT` when launched with a minimal `PATH`), and an unhandled rejection
  kills the txiki process. notify now uses the absolute `/usr/bin/osascript`
  path and never rejects — it resolves `false` if delivery failed.

## 0.10.1 — 2026-07-13

- Fix: Vite-template scaffolds now include a `backend/tsconfig.json`
  (`jsconfig.json` for JS templates), so editors resolve `tjs` /
  `TinyApiHandler` / `TinyApp` in the backend — the Vite tsconfig only covers
  `src/`, and TS inferred projects ignore sibling ambient `.d.ts` files.

## 0.10.0 — 2026-07-13

- **Vite / React / Vue / TypeScript** — `tinyjs new myapp --template react-ts`
  (vue-ts, svelte-ts, solid-ts, vanilla-ts, …) scaffolds create-vite wired to
  tinyjs. `tinyjs dev` runs the Vite dev server with HMR inside the native
  window (`frontend.devUrl`); `tinyjs build` runs `frontend.build` and ships
  `frontend.dist`. The zero-dependency default scaffold is unchanged.
- **TypeScript backends** — a `.ts` backend entry (configurable via
  `"backend"`) is bundled with esbuild, which also makes npm packages usable
  in the backend.
- **`tiny` is injected everywhere** — no `tiny.js` script tag; every page in
  every window (file:// or dev server) gets the global automatically.
- **TypeScript definitions** — `types/tiny.d.ts` ships in scaffolds: the full
  `tiny.*` surface, `TinyApp`, `TinyApiHandler`, window state, menus, chrome.
- Vite builds run under `file://` (WebKit file-access flags — module scripts
  with `crossorigin` now load).

## 0.9.0 — 2026-07-13

- **Menu-bar agents** — `"activation": "accessory"` in tinyjs.json launches
  the app with no Dock icon and the window hidden, with no flash of either
  (packaged apps get `LSUIElement`; dev mode matches). Show the window later
  with `tiny.win.show()`; `setDockVisible(true)` restores a regular app.
- **SF Symbol tray icons** — `tiny.tray.set({ icon: 'sf:cup.and.saucer.fill' })`
  renders a crisp, auto-templating menu-bar icon straight from an SF Symbol
  name; no shipped assets needed. PNG paths still work.
- **Tray primary action** — `tiny.tray.set({ primaryAction: true, menu })`
  splits clicks: left click fires `tiny.tray.onClick` (backend
  `onTray(null, app)`), right/ctrl-click opens the menu — the classic
  Caffeine-style toggle.

## 0.8.0 — 2026-07-13

- **Multiple windows** — `tiny.win.open(id, { page, title, size })` turns any
  frontend html file into a window. Every window runs the full `tiny.*`
  bridge; `win.*` calls target the caller's own window; `tiny.win.id`,
  `tiny.win.close()`, `tiny.win.windows()`. Backend: `app.openWindow`,
  `app.window(id).…` (eval/push/close/title/size/position/chrome/getState),
  `app.push` broadcasts, `onWindowClosed` export + `window-closed` event,
  and api handlers receive `meta.window` (who called).
- **Unified page RPC** — one injected bridge for all windows (call ids carry
  the window id), replacing the webview library's binding; dialogs reply
  through the same path.

## 0.7.0 — 2026-07-13

- **Frameless windows** — `tiny.win.setChrome({ frame, trafficLights,
  transparent, vibrancy })` (also `"chrome"` in tinyjs.json; packaged apps
  apply it before first paint). Frameless keeps native resize edges, focus,
  shadows, and fullscreen. Vibrancy materials: sidebar, hud, menu, popover,
  window, content, and friends.
- **Drag regions** — mark any element `data-tiny-drag` to make it a titlebar:
  dragging moves the window natively, double-click zooms; buttons/inputs
  inside are excluded automatically (`data-tiny-nodrag` to opt out).
- `tiny.win.zoom()`, `tiny.win.startDrag()`; `getState()` now reports the
  chrome state.
- Fix: window chrome and `setResizable(false)` now survive `setSize()` (the
  webview library rewrites the styleMask on every resize; overrides are
  re-applied).

## 0.6.0 — 2026-07-13

- **Native Notification Center** — packaged apps built with a signing
  identity (even "Apple Development") get real banners: the app's own icon,
  a proper permission prompt on first `tiny.notify()`, banners while
  frontmost, and clicks routed back (`tiny.app.onNotificationClick` /
  backend `onNotificationClick`, cold starts included). `notify` gains
  `{ id, subtitle, sound }`. Ad-hoc and dev builds fall back to the
  osascript banner automatically; a user's explicit "Don't Allow" is
  respected (notifications drop silently).

## 0.5.0 — 2026-07-13

- **Stateful menus** — items support `checked`, `enabled: false`, and nested
  `submenu` everywhere (menu bar, tray, context menus).
  `tiny.menu.update(id, { label?, checked?, enabled? })` patches a live item
  without redeclaring; `tiny.menu.get(id)` reads one back.
- **Window read-backs** — `tiny.win.getState()` returns position, size,
  fullscreen/minimized/visible/focused/always-on-top/resizable, and screen
  info; set → get round-trips exactly. New `tiny.win.restore()` and absolute
  `tiny.win.setFullscreen(bool)`.
- Wire protocol grows its first launcher→backend replies (`GET`/`GOT`).
- **`tiny.app.info()`** — `{ version, tinyjs, runtime }`: the app's version,
  the tinyjs version it was built with, and the txiki.js runtime.

## 0.4.0 — 2026-07-13

- **Deep links** — claim a scheme with `"urlScheme": "myapp"` in tinyjs.json;
  `open myapp://…` delivers `tiny.app.onOpenUrl` / backend `onOpenUrl`, on
  cold start too (launch events are buffered until the app is ready).
- **File associations** — `"fileExtensions": ["md"]` puts you in "Open With";
  files arrive as real paths via `tiny.app.onOpenFiles` / `onOpenFiles`,
  including Dock-icon drops.
- **Single instance** — a second `open` (URL, file, Dock) activates the
  running app and delivers the event to it instead of launching a copy.
- **New .app architecture** — the launcher is now the bundle executable (it
  listens on the socket and spawns the backend), which is what makes the
  above work; the C shim is gone. Dev mode and the bare `dist/<name>` binary
  keep the backend-first arrangement. Bundles still sign and notarize clean.

## 0.3.1 — 2026-07-13

- **Global hotkeys** — `tiny.hotkey.register(id, 'cmd+shift+k')` /
  `.unregister(id)` / `.on(fn)`; fire system-wide, even while other apps are
  focused (backend: `app.hotkey.*`, `onHotkey` export).
- **System events** — `tiny.theme.get()` / `tiny.theme.on(fn)` for dark/light
  (initial state included), plus `sleep` / `wake` events (`onSystem` export).
- **`tiny.store`** — persistent JSON settings in
  `~/Library/Application Support/<bundle id>/store.json`:
  `get` / `set` / `delete` / `all`, atomic writes.
- **Custom context menus** — `tiny.menu.setContext(items)` replaces the
  right-click menu with native items (`null` restores WebKit's default);
  clicks via `tiny.menu.onContext(fn)` / `onContextMenu` export.
- **`tiny.win.print()`** — native print panel for the page.
- **`tinyjs build --dmg`** — also emit `dist/<name>-<version>.dmg` (the .app
  plus an /Applications shortcut).
- **`tinyjs notarize`** — submit the built .app via `notarytool` and staple
  the ticket (needs a Developer ID `signIdentity` and a keychain profile).
- SQLite documented: txiki ships `tjs:sqlite` (`Database`) — examples in the
  README and agent skill.
- The launcher now links Carbon (for hotkeys); `setup.sh` and the release
  workflow updated accordingly.

## 0.3.0 — 2026-07-13

- **Tray / menu-bar apps** — `tiny.tray.set({ title, icon, tooltip, menu })`,
  click events, `tiny.win.setHideOnClose(true)`, and
  `tiny.app.setDockVisible(false)` for menu-bar-only apps.
- **Multi-file frontends** — the inliner is gone; `src/frontend/` ships as
  real files and relative js/css/images just work. Hot reload bypasses
  WebKit's caches (`reloadFromOrigin`), so edited subresources show up.
- **Auto-update for your apps** — `"update": { "url": … }` in tinyjs.json plus
  `tinyjs publish` (zip + manifest with sha256). `update.check()` /
  `update.install()` download, verify checksum and code signature, swap the
  .app with rollback, and relaunch. Hardened: https-only manifest/download
  (localhost exempt for testing), mandatory sha256, Team ID pinning when
  signed with a real identity, translocation detection.
- **Notifications** — `tiny.notify(title, body)`.
- **Drag & drop with real paths** — `tiny.win.onDrop((paths) => …)`; file
  drags are accepted even if the page doesn't handle HTML5 DnD.
- **Window controls** — `center`, `minimize`, `fullscreen`, `setPosition`
  (top-left origin), `setAlwaysOnTop`, `setResizable`, `hide`/`show`.
- Installer: prefers a writable bin dir already on PATH (`/usr/local/bin`,
  then `/opt/homebrew/bin`), offers to add `~/.local/bin` to your shell
  profile otherwise, and the "not on PATH" warning is now unmissable.

## 0.2.1 — 2026-07-13

- **WebGPU** — pages load as `file://` documents (secure context, so
  `navigator.gpu` exists) and the launcher enables the WebKit feature flag on
  macOS 15 and earlier. `requestAdapter()` / `requestDevice()` verified.
- **CLI self-update** — `tinyjs update` (with `--check`), plus a once-a-day
  update notice in `tinyjs dev`.

## 0.2.0 — 2026-07-12

- **`tiny.*` frontend namespace** — `tiny.api.call/on`, `tiny.win.*`,
  `tiny.menu.*`, `tiny.log`, `tiny.quit` via the `tiny.js` shim.
- **Native menus** — default app menu (About with app name/version + Quit)
  and Edit menu (copy/paste shortcuts); custom menus via `tiny.menu.set`.
- **System dialogs** — `alert` / `confirm` / `prompt` (NSAlert) alongside the
  file panels.
- **Agent skill** — `tinyjs new` scaffolds `.claude/skills/tinyjs/SKILL.md`
  so coding agents know the API.
- tinyjs.app landing page + `curl tinyjs.app/install | sh` installer.
- (v0.1.1 was superseded by this release the same day.)

## 0.1.0 — 2026-07-12

- Initial release: txiki.js backend + native WebKit launcher over a private
  Unix-domain socket (no HTTP server, no ports), `tinyjs new/dev/build`,
  hot reload, native file dialogs, codesigned `.app` bundles (~6 MB shipped)
  via the stock-runtime + shim layout, tag-triggered release workflow.
