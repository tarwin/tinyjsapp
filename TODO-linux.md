# Linux port — remaining work

Tracking list for the Linux port (GTK3 + WebKitGTK 4.1 launcher,
`native/launcher-linux.cc`). The core port is done and verified — see the
Portability section of the README for the current support matrix. Wire op
names refer to the protocol table in the README. Session note:
X11-dependent features (global hotkeys, keystroke) work on X11/XWayland via
XTest/XGrabKey; pure-Wayland routes go through portals and are listed below
where missing.

## Done

- [x] Core port: launcher (GTK3 + WebKitGTK 4.1, Unix socket — same
      protocol as macOS), bridge, CLI, `tinyjs dev`/`build`/`publish` +
      auto-update (per-arch `"linux": { "<arch>": { url, sha256 } }`
      manifest block alongside mac/win; swap + relaunch), native dialogs
      (open/save/folder/alert/confirm/prompt), menus + `key:` accelerators
      (Ctrl+key), clipboard (text/html/image/files + watch), window ops
      (hide/show/center/minimize/fullscreen/ontop/resizable/position/zoom/
      level/click-through/sticky), chrome (frameless + transparent;
      `vibrancy` is a no-op), multi-window, custom context menus +
      suppression, secrets (Secret Service/GNOME Keyring),
      `power.preventSleep` (logind inhibitor), `shell.open/reveal/trash`,
      `store`/`paths` (XDG dirs), `screens`/`mousePosition`/`getWinState`,
      `printToPDF`, the print dialog, `playSound`/`beep`.
- [x] **Tray** — AppIndicator/StatusNotifier; menu-based (a bare icon-click
      with no menu set is emulated via a synthetic menu entry),
      `tray.position()` returns `null`.
- [x] **Notifications** — `org.freedesktop.Notifications`, with action
      buttons; no reply fields.
- [x] **Theme + sleep/wake** — dark/light incl. live changes, sleep/wake
      events.
- [x] **Global hotkeys + keystroke (X11)** — XGrabKey/XTest via
      X11/XWayland sessions; pure Wayland not covered (see Still open).
- [x] **launchAtLogin** — autostart `.desktop` entry, built apps only.
- [x] **`.desktop` self-registration + single instance + deep links** — a
      built app registers its own `.desktop` entry on first run (app-menu
      listing, icon, deep links via `urlScheme`, file associations via
      `fileExtensions`); a second `open` activates the running instance
      instead of launching another copy.
- [x] **pickColor** — portal-based system eyedropper.
- [x] **captureScreen** (X11 sessions only) and **thumbnail** (images
      only, via GdkPixbuf).
- [x] **say / voices / stopSpeaking** — via speech-dispatcher's `spd-say`
      when installed.
- [x] **battery** and **idleTime** (GNOME) and `dock.bounce` (urgency
      hint).
- [x] **nowPlaying + media keys** — a real MPRIS
      (`org.mpris.MediaPlayer2`) player object: metadata shows in the
      GNOME/KDE media widget and lock screen, and Play/Pause/Next/Previous/
      Seek route back as `onMediaKey`. This is the correct Linux mapping,
      not a stopgap.
- [x] **audioTap (system)** — captures the default sink's monitor via
      `parec` (`@DEFAULT_MONITOR@`) or `pw-cat --record` (sink capture),
      chunked to interleaved LE Int16 at the requested interval. Matches
      the Windows WASAPI-loopback behavior: system scope only, `scope:'app'`
      is approximated by the system mix (see Still open for true per-app).
- [x] **Install script + release CI** — `curl -fsSL tinyjs.app/install | sh`
      detects Linux and installs to `~/.tinyjs` (needs the system
      `libwebkit2gtk-4.1-0` runtime); `setup.sh` now also builds on Linux
      (needs `build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
      libayatana-appindicator3-dev`; downloads a prebuilt `tjs` or builds
      txiki.js from source with `TJS_BUILD=1`); release CI gained
      `linux-x86_64`/`linux-arm64` jobs that build txiki.js from source and
      ship it as `tjs-linux-<arch>.gz`, checksummed alongside the macOS and
      Windows assets.

## Still open

- [ ] **Wayland-native global hotkeys** — the X11 XGrabKey path covers
      X11/XWayland sessions; the `org.freedesktop.portal.GlobalShortcuts`
      portal would cover pure Wayland (user-facing rebind dialog).
- [ ] **audioTap scope:'app'** — the system mix is captured (done above);
      true per-process (own-window-only) capture needs a PipeWire
      sink-input filter, like the Windows process-loopback path.
- [ ] **recorder** — screen recording to a video file; not implemented on
      any Linux session yet. The PipeWire ScreenCast portal would be the
      route, and would also cover **captureScreen on pure Wayland** (X11
      sessions already get captureScreen via XGetImage — done above).
- [ ] **authenticate** — polkit agent prompt or fprintd.
- [ ] **ocr / thumbnail (non-image files) / quickLook / spotlight** — no
      clean system equivalents; thumbnail already scales images via
      GdkPixbuf (done above), spotlight could shell to `tracker3 search` /
      `locate` when present.
- [ ] **selectedText / otherWindows / moveWindow / frontmostApp** — EWMH on
      X11 sessions; nothing portable on Wayland. Currently 'unsupported'.
- [ ] **Examples: Linux builds for shelf installs** — publish per-app Linux
      tarballs (+ `linuxUrl` in catalog.json) and a tarball install path in
      shelf.

## Not planned / no OS equivalent

`applescript`, `haptic`, `dock.setBadge`/`dockIcon` (the Unity launcher API
is dead — `dock.bounce` now works via an urgency hint, but badges/custom
icons have no equivalent; could revisit via LauncherEntry), `setAllSpaces`
(sticky windows exist on X11 only), `tiny.app.ai`, `wifi` (NetworkManager
DBus — revisit if asked), `share`. All reject or report `'unsupported'` so apps can
feature-detect.
