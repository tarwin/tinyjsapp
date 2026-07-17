# Changelog

All notable changes to tinyjs. Versions are git tags (`vX.Y.Z`); a tag push
builds and publishes the release. The rendered version of this file lives at
https://tinyjs.app/changelog.

## Unreleased

- **`tiny.app.ai`** (on-device LLM) — landed behind an opt-in build, not yet
  in the released tarballs. `ai.generate(prompt, { instructions })` runs
  Apple's FoundationModels locally (offline, no API key, private);
  `ai.availability()` → `'available' | 'unavailable' | 'unsupported'`.
  Implemented as a small Swift shim linked into the launcher with
  `TINYJS_AI=1 ./setup.sh` (needs the macOS 26 SDK); the binary keeps the
  macOS 14 floor and weak-links FoundationModels, so stock builds and older
  systems just report `'unsupported'`. Shipping it in the release tarballs
  needs the CI runner bumped to a macOS 26 image — held for a deliberate
  pipeline change so the default `macos-14` build never depends on the newer
  SDK. Proven working end-to-end locally (real generation, ~250ms).

## 0.24.0 — 2026-07-16

- **`tiny.proxyURL(url)`** — get a cross-origin stream (internet radio) into
  Web Audio. A `MediaElementSource` on a cross-origin `<audio>` outputs silence
  by spec, so radio can't drive an EQ or analyser. `proxyURL` returns a same-app
  `tiny-media://…` URL that streams the remote http(s) resource through the
  native layer (NSURLSession — redirects, byte-range/seek, native buffering)
  and injects `Access-Control-Allow-Origin: *`; with `<audio crossorigin=
  "anonymous">` the element is CORS-approved and untainted, so the full Web
  Audio graph gets real samples. Implemented as a `WKURLSchemeHandler`
  registered on every webview via a swizzle of `-[WKWebView initWith
  Frame:configuration:]` — no backend hop, no base64, no TCP port. Verified
  end-to-end: a `MediaElementSource` on a proxied SomaFM stream drove a live
  `AnalyserNode` with real (non-silent) waveform data. macOS only for now; the
  design (custom scheme + streamed upstream + injected CORS) maps to WebView2
  (`WebResourceRequested`) and WebKitGTK (`register_uri_scheme`) when those
  backends land.

## 0.23.0 — 2026-07-16

- **`tiny.fetch(url, init)`** — a `fetch` that runs in the backend (a native
  process), so it has **no CORS, CSP, or mixed-content limits**: the page can
  reach any origin. Same shape as `window.fetch` and resolves to a real
  `Response` (`res.json()` / `res.text()` / `res.headers` / `res.ok` all work);
  request bodies can be strings, `ArrayBuffer`/typed arrays, `Blob`, or
  `URLSearchParams`. Pass `{ stream: true }` for a **live streaming body** —
  the backend keeps the connection open and the page pulls chunks on demand
  (`res.body.getReader()`) with natural backpressure. That's what an endless
  source like internet radio needs, where a buffered fetch would never resolve;
  `reader.cancel()` (or closing the window) tears the upstream connection down.
  Transport reuses the existing request/response bridge — chunks travel base64
  over the CALL→RET channel, so there's no new wire frame and no launcher
  change. Note: a page with CORS-free network reach has the app's full network
  reach — not a new trust boundary for tinyjs (every page already has an RPC
  channel to a backend with full system access), but worth knowing.

## 0.22.6 — 2026-07-16

- **`tinyjs notarize --dmg`** — rebuild the installer dmg from the *stapled*
  .app. `stapler` mutates the bundle in place after Apple's verdict, so a dmg
  made at `build --dmg` time holds the pre-staple .app with no ticket embedded —
  online Gatekeeper hides this (it fetches the ticket), but offline / first
  launch before network it blocks the app. `notarize` now rebuilds the dmg from
  the stapled bundle when `--dmg` is passed, and refreshes an existing dmg on
  disk automatically (that copy is guaranteed stale). Ship a dmg with
  `tinyjs build --dmg && tinyjs notarize --dmg`.

## 0.22.5 — 2026-07-16

- **`chrome: { acceptsFirstMouse: true }`** — opt a window into delivering the
  click that focuses it straight through to the page. macOS normally swallows
  that first click into web content ("click once to focus, again to act"),
  which also bites between an app's own windows and makes an unfocused window's
  DOM drag region need an extra click. On per window (main or secondary), via
  `tiny.win.setChrome`, `app.openWindow`'s `chrome`, or `tinyjs.json` `"chrome"`
  (applied before first paint); reported back in `getState().chrome`. Off by
  default — matches the platform, no change for existing apps.

## 0.22.4 — 2026-07-16

- **Multi-line JavaScript sent to the page keeps its newlines.** Script pushed
  across the bridge — `app.eval`, a window handle's `eval`, and the event
  pushes behind `app.push` / `push` — was flattened onto a single line before
  it crossed the socket, so a `//` line comment silently commented out
  everything after it (a whole multi-line snippet could vanish after its first
  comment). Snippets now travel wire-escaped and are unescaped in the launcher,
  so real newlines survive intact; event payloads containing newlines or
  backslashes round-trip unchanged.

## 0.22.3 — 2026-07-15

- **`tinyjs notarize` fails fast on the wrong signature.** It now inspects what
  the built `.app` is actually signed with (via `codesign`) and stops
  immediately unless that's a *Developer ID Application* certificate — naming
  what it found (ad-hoc, unsigned, or e.g. an "Apple Development" cert) instead
  of uploading, waiting minutes, and coming back with Apple's opaque "Invalid".
  It checks the artifact on disk, not just the configured identity, so a build
  that predates a config change is caught too.
- The notarize "needs a real Developer ID" error now mentions the
  `TINYJS_SIGN_IDENTITY` env var, not just `signIdentity` in tinyjs.json — both
  have always worked (so does `TINYJS_NOTARY_PROFILE` for the notary profile);
  the message just never said so.

## 0.22.2 — 2026-07-15

- **`tinyjs uninstall`.** Cleanly removes the install (`~/.tinyjs`, or
  `$TINYJS_HOME`) and the `tinyjs` PATH symlink — the exact inverse of the
  installer. It removes only a symlink that points back into the install it's
  running from (so an unrelated `tinyjs` on your PATH is left alone), prints
  what it'll delete, and prompts before doing it (`--yes` skips the prompt; a
  non-interactive run without `--yes` refuses rather than deleting silently).
  Safe by construction: it won't touch a source checkout, `/`, or a directory
  containing `.git`. The shell-profile PATH line is left in place with a note,
  since editing dotfiles on uninstall is too invasive to do silently.
- **Friendlier installer finish.** The install script now explains that it
  linked into a dir *because it's already on your PATH*, and warns that a
  running shell caches command lookups — so if this same terminal still says
  `tinyjs: command not found`, run `hash -r` or open a new one.

## 0.22.1 — 2026-07-15

- **Tray icons keep their aspect ratio.** A PNG tray icon was force-scaled to
  a hardcoded 18×18, squishing wide "pill"/wordmark icons into a square and
  throwing away the PNG's DPI. Custom icons now scale to the 18pt menu-bar
  height while preserving width (a 58×22pt icon becomes 47.5×18pt, not
  18×18), and the variable-length status item widens to fit. Square icons are
  unchanged; `sf:` SF Symbol icons were never affected.

## 0.22.0 — 2026-07-15

- **`chrome.squareCorners`** — drop macOS's rounded window corners.
  `setChrome({ squareCorners: true })` (or `"chrome": { "squareCorners":
  true }` in tinyjs.json, applied before first paint) makes the window
  **borderless**: square, no titlebar, no traffic lights. It's a deliberate,
  explicit choice — you lose the native titlebar drag (use `data-tiny-drag`)
  and it reads as un-native — but resize edges, the drop shadow, and
  keyboard focus are all kept (a `canBecomeKeyWindow` override restores focus
  that borderless windows normally can't take). Works on the main window and
  secondary `openWindow` windows; `getState().chrome.squareCorners` reports
  it, and it survives `setSize`.

## 0.21.0 — 2026-07-15

Fixes and window ergonomics from real-app usage.

- **Fix: `store.set()`/`delete()` can no longer crash the backend.** A write
  failure (bad path, full disk) used to surface as an unhandled rejection
  and take the process down; the store now swallows its own write errors
  (resolving `false`, keeping the in-memory value) and uses a unique temp
  file per write so a burst of un-awaited `set()`s can't race on the rename.
- **`openWindow` gains `chrome` and `{ x, y }`** — applied *before* the
  window first paints, so a frameless/vibrancy panel never flashes its
  titlebar and a positioned window opens where you asked instead of
  center-then-jumping. `getState().chrome` is now per-window (secondary
  windows report their own frame/trafficLights/transparent, not the main
  window's).
- **`readAccess` option** — `<audio>`/`<video>`/`<img>` can only load
  `file://` assets under the frontend dir by default (`MEDIA_ERR_SRC_NOT_
  SUPPORTED` otherwise). `createApp({ readAccess: true })` (home) or
  `readAccess: '/path'` (also `"readAccess"` in tinyjs.json) widens the
  read root so media anywhere under it loads directly — no base64 → Blob
  round-trip.
- Docs: noted that WebKit throttles `requestAnimationFrame`/timers in
  occluded or off-screen windows (do continuous work in the visible window
  or the un-throttled backend).

## 0.20.0 — 2026-07-14

Deep-Mac citizen — the small native niceties apps otherwise shell out (or
give up) for.

- **`win.printToPDF(path)`** — render the page to a vector PDF via
  WKWebView → `{ path }`. Invoices, reports, receipts without a print
  dialog.
- **`app.haptic(pattern)`** — trackpad haptic feedback (`'generic'` |
  `'alignment'` | `'level'`), a no-op on Macs without a Force Touch pad.
- **`app.dockIcon(pngPath)`** — replace the Dock icon (render a canvas for
  progress rings or unread badges); `''` resets to the bundle icon.
- **`app.battery()`** — `{ percent, charging, plugged, minutesRemaining }`
  (or `null` on a desktop) from IOPowerSources.
- **`app.wifi()`** — `{ ssid, bssid, rssi, noise, txRate }` from CoreWLAN
  (`ssid`/`bssid` need the Location permission on macOS 14+).
- **`app.spotlight(query)`** — find files by name or content via
  NSMetadataQuery (home scope, up to 100 paths) — no `mdfind` spawn.

## 0.19.0 — 2026-07-14

Window superpowers — the primitives behind overlays, HUDs, desktop pets,
and window managers.

- **Window stacking & behaviour** — `win.setClickThrough(bool)` (mouse
  events pass straight through — draw-on-screen overlays), `win.setLevel(
  'normal'|'floating'|'overlay'|'desktop')` (`overlay` floats above almost
  everything incl. most fullscreen apps; `desktop` pins behind normal
  windows for wallpaper/pets), `win.setAllSpaces(bool)` (follow the user
  onto every Space). All three round-trip through `getState()` and have
  backend twins on `app` and `app.window(id)`.
- **`app.selectedText()`** — the text selected in the frontmost app, for
  PopClip-style popovers (Accessibility permission; `null` if none).
- **`app.otherWindows()` / `app.moveWindow(pid, rect)`** — enumerate other
  apps' on-screen windows and move/resize their frontmost window: a
  Rectangle/Magnet "snap the active window" primitive (Accessibility).
- **`tray.position()`** — the tray icon's on-screen rect, so a dropdown
  window can anchor under it (the Tauri `positioner` gap).

## 0.18.0 — 2026-07-14

- **`app.recorder`** — record a display to an .mp4: `start({ path,
  screenId? })` resolves once capture is running, `stop()` resolves
  `{ path, duration }` once the file is finalized. ScreenCaptureKit's
  `SCStream` feeds H.264 frames into an `AVAssetWriter` on a dedicated
  serial queue. Video only for now (no audio track); one recording at a
  time. Needs the `'screen'` permission and macOS 14+ — the permission is
  preflighted so an ungranted `start()` rejects immediately with a clear
  message instead of hanging.

## 0.17.0 — 2026-07-14

Media tier — apps that behave like real media citizens. (Screen recording
to .mp4 is next, in its own release.)

- **`app.nowPlaying`** — `set({ title, artist, album, duration, elapsed,
  playing })` puts your app in Control Center and on the lock screen, and
  arms the hardware media keys; presses (F7/F8/F9, AirPods taps, Control
  Center transport) arrive as the `media-key` page event /
  `onMediaKey(info, app)` export with `{ command: 'play'|'pause'|'toggle'|
  'next'|'previous'|'seek', time? }`. `clear()` tears it down. (MediaPlayer
  framework: MPNowPlayingInfoCenter + MPRemoteCommandCenter.)
- **`app.say(text, { voice, rate })`** — text-to-speech via
  AVSpeechSynthesizer; resolves when playback *finishes* (false if
  interrupted). `app.voices()` lists the installed voices (`{ id, name,
  lang, quality }`), `app.stopSpeaking()` interrupts.
- **Notification actions & reply** — `tiny.notify(title, body, { actions:
  [{ id, title, reply?, placeholder?, destructive? }] })` adds buttons or a
  text-reply field to a banner (packaged apps); taps come back as the
  `notification-action` event / `onNotificationAction(info, app)` export
  with `{ id, action, reply }` (reply = the typed text).

## 0.16.0 — 2026-07-14

The Mac's superpowers — the frameworks macOS ships that JS can't normally
touch. All on-device, plus auto-update quality-of-life.

- **`app.pickColor()`** — the system eyedropper (NSColorSampler): pick any
  pixel in any app, **no screen-recording permission needed**. Resolves
  `'#rrggbb'`, or `null` when the user cancels.
- **`app.ocr(path)`** — on-device OCR via Vision (accurate mode) →
  `{ text, blocks: [{ text, confidence, box }] }` with normalized top-left
  boxes. `captureScreen()` + `ocr()` = screenshot-to-text in two calls.
- **`app.thumbnail(path, size?)`** — a preview png for *any* file type
  Quick Look understands (PSD, video, 3D models, …) → `{ path, width,
  height }`, rendered @2x.
- **`app.secrets`** — Keychain-backed `get`/`set`/`delete` (generic
  passwords under the app id) — the keytar/safeStorage role. Tokens go
  here, never in `tiny.store`; values survive reinstalls.
- **`app.authenticate(reason)`** — Touch ID, falling back to the account
  password sheet → `true`/`false` (false covers cancel).
- **`app.applescript(source)`** — run AppleScript in-process (NSAppleScript,
  no osascript spawn) under the same `'automation'` permission the
  framework already manages. Control Music, Spotify, Finder — anything
  scriptable.
- **Auto-update polish** — `"update": { "auto": "launch" | "daily" }`
  checks in the background (packaged apps only) and fires the
  `update-available` page event / `onUpdateAvailable(info, app)` backend
  export with `{ current, latest, notes }`; `tinyjs publish --notes "…"`
  (or `--notes-file FILE`) puts release notes in the manifest, and
  `update.check()` now returns them.

## 0.15.0 — 2026-07-14

Round three: preview, capture, and idle.

- **`app.quickLook(paths)`** — the real Finder-spacebar preview panel
  (QLPreviewPanel — no `qlmanage` spawn) for any file(s) tinyjs apps
  manage; an array pages with the arrow keys, `quickLook()` closes.
- **`app.captureScreen(screenId?)`** — screenshot a display via
  ScreenCaptureKit → `{ path, width, height }` (a png in the temp dir the
  caller owns). Takes a display id from `screens()`, defaults to the
  primary. Needs the `'screen'` permission and macOS 14+ — rejects with
  the reason otherwise (the framework is weak-linked, so older systems
  still launch).
- **`app.idleTime()`** — seconds since the user's last input, session-wide
  (pause pollers / dim UI when they walk away).

## 0.14.0 — 2026-07-14

More stop-shelling-out, round two.

- **`app.power`** — `preventSleep(reason, { display? })` / `allowSleep()`
  hold a single IOPMAssertion instead of spawning `caffeinate`: the
  assertion dies with the process, so a crashed app can never wedge the
  Mac's sleep. The reason string shows up in `pmset -g assertions`.
- **`app.frontmostApp()`** — `{ name, bundleId, pid }` of the active app
  (who focus returns to after `hide()` — pairs with `paste()`).
- **`app.beep()` / `app.playSound(target)`** — system beep, a system sound
  by name (`'Ping'`, `'Glass'`, …), or an audio file path; resolves `false`
  if the sound didn't load. No more `afplay` spawns.
- **`win.share({ text?, url?, paths?, x?, y? })`** — the native share sheet
  (NSSharingServicePicker), anchored at page coordinates — pass the click's
  `clientX`/`clientY`. Backend twin: `app.window(id).share(opts)`.

## 0.13.0 — 2026-07-14

Theme: stop shelling out. Five small APIs replacing the `open`/osascript
spawns and hardcoded paths tinyjs apps still needed.

- **`app.shell`** — `open(target)` (URL with any scheme, or a file path,
  in its default app), `reveal(path)` (show in Finder), `trash(path)`
  (move to the Trash — recoverable, prefer it over deleting user files).
  Each resolves `true` or rejects with the reason (`'no such file'`,
  `'no application registered for URL'`).
- **`app.launchAtLogin`** — `get()`/`set(v)` →
  `'enabled' | 'disabled' | 'requires-approval' | 'unsupported'` via
  SMAppService. Packaged .apps on macOS 13+; `'requires-approval'` means
  the user must allow it in System Settings → General → Login Items. Dev
  mode reports `'unsupported'` (no bundle identity to register).
- **`app.screens()`** — every display in the same top-left coordinates as
  `setPosition`: `{ id, name, x, y, width, height, visible, scale,
  primary }` — `visible` excludes the menu bar and Dock, so multi-monitor
  palette placement is one call.
- **`app.paths`** — `{ home, data, cache, logs, temp, downloads, desktop,
  documents }`, per-app-id where it matters. Prefer these over hardcoding
  `~/Library` paths (they're what a future non-macOS backend would remap).
- **`app.dock`** — `setBadge('3')` / `setBadge('')`, `bounce()` /
  `bounce({ critical: true })`.
- Docs: noted that `'screen'` permission never reads `'undetermined'`
  (macOS only exposes a yes/no preflight), and that dev-mode TCC grants
  attach to the shared launcher binary rather than your app.

## 0.12.1 — 2026-07-14

- **Clipboard metadata** — `clipboard.read()` now also returns `concealed`
  (the org.nspasteboard Concealed/Transient markers password managers set —
  clipboard-history apps must skip these), `sourceApp` (`{ name, bundleId }`
  of the app the content came from: the frontmost app when the change was
  first noticed — exact while `watch()` runs, best-effort on a later read;
  your own `write()`s are attributed to your app), `sourceURL` (the page a
  Chromium-browser copy came from), and `imageSize` (`{ width, height }` in
  pixels, so no more shelling out to `sips` for dimensions).
- Fix: a backend `export function onClipboardChange(e)` was silently dead in
  `tinyjs dev`/`build` apps — the generated entry didn't forward it to
  `createApp` (direct `createApp({ onClipboardChange })` users were fine).

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
