# Platforms — what runs where, and the per-OS truths

macOS is the primary platform; Windows and Linux are in beta. Same project,
same tinyjs.json, same `tiny.*` api, same commands on all three.

| | macOS | Windows | Linux |
|---|---|---|---|
| app users need | macOS 14+ (universal: Apple Silicon + Intel) | Windows 10/11 with the WebView2 runtime (preinstalled on 11) | glibc 2.35+: Ubuntu 22.04 / Debian 12 / Mint 21 and newer, `webkit2gtk-4.1`, X11 or Wayland |
| webview | WKWebView | WebView2 (Chromium) | WebKitGTK 4.1 |
| `tinyjs build` output | `dist/<Name>.app` (codesigned) + bare `dist/<name>` | portable `dist/`: `<name>.exe` + `launcher.exe` + `frontend/` | portable `dist/`: backend binary + `launcher` + `icon.png`; per-arch tarballs from `publish` |
| publish / auto-update | zip + dmg, notarized | `-win.zip`; https+sha256 trust | `-linux-<arch>.tar.gz` × {x86_64, arm64}; `.desktop` self-registers on first run |
| toolchain to develop tinyjs ITSELF | Xcode CLT, `./setup.sh` | MinGW-w64 g++ (`winget install BrechtSanders.WinLibs.POSIX.UCRT`), `setup.ps1`, `tinyjs.cmd` | `apt install build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libayatana-appindicator3-dev libpipewire-0.3-dev`, same `./setup.sh` |

Works on ALL THREE: the whole bridge (api calls, push events, `tiny.fetch`
streaming), dev/hot-reload, Vite `devUrl`, multi-window with per-window
bridge, menu bar in every window (+ per-window menus, accelerators — ⌘ maps
to Ctrl off macOS), native dialogs, custom context menus, clipboard
(read/write/watch), global hotkeys, `shell.open/reveal/trash`, `secrets`,
`store`, `power.preventSleep`, theme + sleep/wake events, `screens`,
`printToPDF`, window ops (incl. levels, clickThrough, frameless/transparent
chrome, `data-tiny-drag`, `onState`, `ensureOnScreen`, `minSize`, zoom),
`win.onDrop` + `startDrag`, deep links / file associations / argv / single
instance, auto-update, sqlite, `notify`, `tiny.audio.sampler` (native mixer
on Linux, page host elsewhere — identical API), `say`/`voices`,
`launchAtLogin` (built apps on win/linux), `captureScreen` (linux: X11).

## The capability honesty system

`tiny.system.capabilities()` lists only the EXCEPTIONS — test
`caps.x !== false`, never `if (caps.x)`. Fire-and-forget calls no-op where
unsupported, query calls resolve `null`, capability calls reject with the
reason — nothing hangs. `tiny.macos.*` off macOS REJECTS (never null).
`tiny.system.requirements`/`missing`/`promptMissing` turn installable gaps
(codecs, speech, tray, mouseTracking) into an actionable prompt.

## Windows notes

- Drag & drop with real paths BOTH ways; tray + `notify` (balloons — no
  action buttons or reply fields); `frontmostApp` works; `app.badge` +
  `app.progress` + full app surface work; `authenticate` = Windows Hello
  (false where not enrolled); `audioTap` = WASAPI loopback ('system').
- `capabilities().audioFilters` is FALSE — measured permanent (the only
  silencing lever is persisted mixer state shared by every WebView2 app on
  the machine); `tiny.audio.pageChain(ctx)` is the sanctioned fallback.
- Transparent MAIN window must be declared in tinyjs.json `"chrome"` (not a
  late `setChrome`); transparency and a Win32 menu bar are mutually
  exclusive; frameless/transparent windows never draw a menu bar.
- No `pickColor` (works on mac + linux), no `spotlight` backend, no
  `nowPlaying`/media keys.
- WebView2 = Chromium: `MediaElementSource` taps post-volume (WebKit: pre).

## Linux notes

- Sessions matter: X11-only — `mousePosition` (Wayland hides the global
  pointer; `"windowPlacement": true` forces XWayland, or arm
  `tiny.app.mouseTracking` for the portal route), `keystroke`/`paste`
  (XTest), `captureScreen`, `app.icon`/`app.attention`/`app.presence`
  (GTK3's Wayland backend has nowhere to put them). `capabilities()` reports
  per session — trust it, not the desktop name.
- `app.badge` + `app.progress`: Unity LauncherEntry DBus — BUILT apps only
  (the signal addresses the `.desktop` entry), docks that implement it (KDE
  Plasma, Ubuntu Dock, Dash-to-Dock; not vanilla GNOME Shell).
- Tray is AppIndicator/StatusNotifier — menu-based; bare icon click emulated
  via a synthetic entry; `tray.position()` → null.
- `notify` DOES support action buttons (freedesktop), no reply fields.
- `pickColor` (portal), `spotlight` (`plocate`/`locate`/bounded `find`),
  `nowPlaying`/media keys (MPRIS: media widget + lock screen) all work.
- `idleTime` needs GNOME. `authenticate` always false (no identity check
  exists — gates fail closed). `thumbnail` images-only.
- getUserMedia works, gated by the manifest `"permissions"` block (no OS
  prompt exists — undeclared = NotAllowedError); MediaRecorder records
  video/mp4 (webkit 2.52+).
- **Audio rule: no Web Audio into `ctx.destination`** — crackles under
  WebKitGTK (measured; no graph-side fix). Elements play directly; SFX →
  `tiny.audio.sampler` (native there); EQ → `tiny.audio.filters` (native);
  never `pageChain`. An element routed into a graph ALSO plays natively
  (double-play) — element volume 0 if you must route one.
- No native HLS (vendor hls.js), no WebGPU, codecs depend on GStreamer
  plugins (`requirements(['media.aac', …])` prompts the install).
- Frameless windows get WM resize grips — declare `minSize` on satellites.
- Deep links / file associations / single instance work via the
  self-registered `.desktop` entry (first run of a built app).

## Not supported (feature-detect; they fail cleanly)

- **Windows**: `proxyURL`, `recorder`, `pickColor`, `nowPlaying`/media keys,
  `share`, `setAllSpaces`, `wifi`, `spotlight`, `system.locale`,
  notification actions, `tiny.macos.*` (rejects).
- **Linux**: `recorder`, `ocr`, `share`, `wifi`, `selectedText`/
  `otherWindows`/`moveWindow`/`frontmostApp`, `authenticate` (false),
  `system.locale`, `setAllSpaces` (maps to sticky windows), `tiny.macos.*`
  (rejects).
- **macOS has everything** except: nothing — it's the reference platform.
  (`capabilities().ai` is a BUILD fact: needs a launcher compiled against
  FoundationModels + macOS 26 + Apple Intelligence ON.)

Plans and burn-downs: tarwin/tinyjsapp TODO-windows.md, TODO-linux.md.
