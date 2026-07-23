# Linux port — remaining work

Tracking list for the Linux port (GTK3 + WebKitGTK 4.1 launcher,
`native/launcher-linux.cc`). The core port is done and verified — see the
Portability section of the README for the current support matrix. Wire op
names refer to the protocol table in the README. Session note:
X11-dependent features (keystroke) work on X11/XWayland via XTest; global
hotkeys work on both X11 (XGrabKey) and Wayland (GlobalShortcuts portal).

--------------------------------------------------------------------------
## Picking this up on a fresh machine (e.g. Parallels)

**Branch:** all Linux work lives on `feat/linux` (branched off `main`). It is
pushed to `origin`. `git checkout feat/linux` after cloning/pulling.

**Build from source** (the checkout is the toolchain — no install needed):
```
sudo apt install build-essential pkg-config libgtk-3-dev \
     libwebkit2gtk-4.1-dev libayatana-appindicator3-dev \
     cmake ninja-build           # cmake/ninja only if tjs must build from source
./setup.sh                       # fetches a prebuilt tjs OR builds txiki.js; compiles the launcher
```
`setup.sh` on Linux: downloads a prebuilt `tjs` from the tinyjsapp releases
if one exists, else builds txiki.js from source (`TJS_BUILD=1 ./setup.sh`
forces the source build; ~a few minutes). It then compiles
`native/launcher-linux` with AppIndicator + X11 support auto-detected.

**Put `tinyjs` on PATH:** `ln -s "$(pwd)/tinyjs" ~/.local/bin/tinyjs` (or
/usr/local/bin). The `tinyjs` wrapper runs `bin/tjs run cli.js`. Because this
is a `dev` checkout, `tinyjs dev` auto-rebuilds `native/launcher-linux`
whenever `launcher-linux.cc` or `runtime/tiny.js` change — so you always run
the latest launcher without a manual step.

**Node** (only for the TypeScript/Vite examples — procsy, sqlittle, trolley,
and the `--template *-ts` scaffolds): install any Node 18+; the previous VM
used `~/.local/node` (Node 22). Plain-JS examples need no Node.

**Examples** live in `../tinyjsapp-examples/` (sibling of the repo), one dir
per app + a `catalog.json`. To test one: `cd ../tinyjsapp-examples/<name> &&
tinyjs dev` (or `tinyjs build` then run `./dist/<name>`). All 26 built
cleanly on the previous machine.

**Test harness / smoke pages** used during the port (recreate as needed; they
were in a scratch dir, not committed):
- The committed self-driving GUI smoke is `test/smoke.html` — run any app with
  `TINYJS_HTML=<abs path> tinyjs dev` and it prints `SMOKE RESULTS … ` then
  quits. CI runs it under `xvfb-run`.
- Ad-hoc "extended smoke" pages exercised clipboard/store/secrets/multi-window/
  chrome/notify/power/printToPDF/mpris/audioTap/hotkeys/the whole
  unsupported-op surface. Pattern: an html page whose inline script calls
  `window.__invoke(JSON.stringify({method, params}))`, collects results, and
  ends with `call('log',{msg:…})` + `call('quit')`. Reuse `test/smoke.html`
  as the template.

**Wire protocol reference:** the launcher speaks the same newline-delimited
protocol as macOS/Windows. If you need the exhaustive op-by-op spec, it can be
re-derived from `native/launcher.cc` + `native/launcher-win.cc` (a distilled
version was generated into a scratch file during the port but not committed).

## This Parallels VM (set up 2026-07-22/23, aarch64, GNOME **Wayland**)

- **GPU acceleration works** — WebGL reports `Apple GPU` / `Apple Inc.`, so the
  UTM software-compositing problem is gone. Re-check `amp`'s audio here; the
  skipping should be fixed.
- **Node**: the distro `nodejs` package has no npm, which the TS examples need
  (`npx esbuild`, `vite`). Node 22 + npm 10 is installed at `~/.local/node` —
  put `~/.local/node/bin` first on PATH. With it, all 26 examples build.
  Do NOT commit the `package-lock.json` churn an install here produces: npm
  drops the darwin/win32 optional binaries, which would break mac/win builds.
- **WebKitGTK plays a graph-routed `<audio>` element TWICE.** When an app pipes
  a media element through Web Audio (`createMediaElementSource`, e.g. for an EQ),
  WebKitGTK does NOT mute the element's own output the way macOS/Windows WebKit
  do — it plays the element straight to the speakers (an `S16LE` GStreamer
  stream) AND through the graph (an `F32LE` Web Audio stream). Two copies of the
  same track a few ms apart phase into a stuttering mess. The graph taps the
  signal pre-volume, so the fix is to sit the captured element at `volume = 0`
  on Linux and carry volume with a gain node — the leaked copy goes silent (its
  PipeWire stream suspends) and the graph still plays. `amp` does this now, gated
  on `tiny.system.isLinux()`; any app using `createMediaElementSource` needs the
  same. Verified with pw-top: two output streams → one, and the graph node went
  from ~1000 xruns to 0.
  NOTE: an earlier theory blamed the pulsesink vs pipewiresink choice and forced
  `GST_PLUGIN_FEATURE_RANK=pipewiresink:MAX`. That was wrong and is reverted —
  pulsesink (WebKit's rank-266 default) plays both the plain-element and the
  graph paths at 0 xruns, while forcing pipewiresink actually *xruns the Web
  Audio graph path* (~644). The real bug was always the double-routing above,
  not the sink.
- **Media codecs are incomplete out of the box** — `gstreamer1.0-plugins-bad`,
  `-ugly` and `libav` are not installed, so WebKit plays MP3/Ogg/Opus/WAV/FLAC
  but reports `""` for AAC/M4A and `isTypeSupported: false` for every MSE type.
  That is silence for most podcasts and internet radio. Fix:
  `sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav`.
  Not an amp bug and not a launcher bug — verified with a bare page: a tone,
  a WAV, an Ogg and an MP3 all produce healthy signal, PipeWire reports ERR=0
  even under heavy visual load, and ALSA shows samples reaching the hardware.
- **gnome-keyring is locked** (autologin VM), so `secrets.set` fails with
  `org.freedesktop.Secret.Error.IsLocked`. Not a launcher bug — unlock the
  login keyring, or run `secrets` tests after a real password login.
- **Wayland session**, so per the notes below: `mouse`/window x,y report 0,0,
  `captureScreen` rejects, `keystroke` is unsupported. An X11 session lights
  those up.
- One harmless `Gtk-CRITICAL … gtk_widget_get_scale_factor` line at startup for
  any app with a tray. It comes from inside libayatana-appindicator (we never
  call it); setting the icon after the menu doesn't avoid it.

## Environment gotchas seen on the previous VM (UTM on Apple Silicon)

- **GPU acceleration was broken** (the constant `libEGL … failed to create
  dri2 screen` / `MESA-LOADER` errors). A `/dev/dri/card0` exists but GL fails
  to init, so WebKit composited in **software** → CPU-bound. This is why
  `amp`'s audio was skippy: its Web Audio analysers + rAF visualizers +
  audioTap saturate the CPU with no GPU to offload compositing. **Parallels
  should fix this** (working virtio-gpu) — re-check `amp` there; the skipping
  is expected to be gone. Not a launcher bug. If GL still fails, the EGL noise
  is harmless but perf will suffer.
- **`glxinfo` wasn't installed** — `sudo apt install mesa-utils` to inspect the
  renderer (llvmpipe = software).
- **The previous VM's session was Wayland** (`XDG_SESSION_TYPE=wayland`,
  `XDG_CURRENT_DESKTOP=ubuntu:GNOME`). Under Wayland: global pointer position
  and window x/y are hidden (`GET win`/`mouse` report 0,0 — a platform limit,
  not a bug); frameless drag uses the compositor move (see the fix below);
  captureScreen falls back to unsupported (X11-only path); keystroke synthesis
  is unsupported (XTest is X11-only). On an X11 session those all light up.

## NEEDS INTERACTIVE RE-TEST (couldn't verify headlessly on the last machine)

Items 3 (tray) and the MPRIS half of the fleet have since been verified on
Parallels **without a human**, by speaking the same D-Bus a panel speaks —
see "Driving the desktop over D-Bus" below. What's left genuinely needs eyes
or a keypress:

1. **Frameless window drag** (`fix(linux): frameless window drag on Wayland`,
   commit 573b22c). `win.startDrag`/`data-tiny-drag` now grabs the live
   button-press device+timestamp and calls
   `gdk_window_begin_move_drag_for_device`. Test: `cd ../tinyjsapp-examples/amp
   && tinyjs dev`, then drag the window by its titlebar/drag region — it should
   move. If it still doesn't move, the button-press-event handler on the
   WebView may not be firing (check by logging in `on_button_press`); the
   fallback path (seat pointer + GDK_CURRENT_TIME) is the old broken behavior.
2. **Wayland global hotkeys** (GlobalShortcuts portal). Register a hotkey, and
   the compositor should show a one-time approval dialog; after approving,
   pressing the combo should fire `onHotkey`. Verified the `CreateSession`/
   `BindShortcuts` D-Bus calls reach the portal, but the approval dialog +
   physical keypress couldn't be automated.
3. **Tray** — DONE, verified over D-Bus (2026-07-23). The item registers with
   `org.kde.StatusNotifierWatcher` (Status=Active, a host is registered), the
   dbusmenu layout is correct, and `Event(clicked)` on the entries produced
   `tray {"id":"one"}` / `{"id":"two"}`; a tray with no menu gets the synthetic
   entry and produces `trayclick {}`. Note the doc line above was wrong: a bare
   icon click is `tiny.tray.onClick()`, not `onTray(null)` — `tray.on()` is
   menu items. Only "does the icon look right on screen" is unverified.
4. **notify action buttons** — buttons appear and route to
   `onNotificationAction` (no reply-field support on Linux). Can't be driven
   over D-Bus: `ActionInvoked` is emitted by the notification daemon, which
   owns the name, so this one needs a click.
5. **pickColor** — the portal eyedropper dialog appears and returns `#rrggbb`.

Everything else (dev/build/publish/auto-update/dialogs/menus/clipboard/
secrets/mpris/system-audioTap/spotlight/the full unsupported-op surface) was
verified end-to-end on the previous machine.

### Driving the desktop over D-Bus (no human needed)

Anything the desktop shell talks to us through can be exercised by speaking
that protocol directly — this closed out the tray and MPRIS without a click.
Run an app with a page that logs `window.__emit` events, then from a shell:

```sh
# tray: find our item, read its menu, click an entry
gdbus call --session -d org.kde.StatusNotifierWatcher -o /StatusNotifierWatcher \
  -m org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierWatcher \
  RegisteredStatusNotifierItems                     # -> :1.NNN@/org/ayatana/...
busctl --user call :1.NNN <menupath> com.canonical.dbusmenu GetLayout iias 0 1 1 label
busctl --user call :1.NNN <menupath> com.canonical.dbusmenu Event isvu 2 clicked s "" 0

# mpris: read what nowplaying.set published, then send transport commands
gdbus call --session -d org.mpris.MediaPlayer2.<app_id> -o /org/mpris/MediaPlayer2 \
  -m org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player Metadata
gdbus call --session -d org.mpris.MediaPlayer2.<app_id> -o /org/mpris/MediaPlayer2 \
  -m org.mpris.MediaPlayer2.Player.Next            # -> media-key {"command":"next"}
```

(`busctl` parses a leading `-1` as an option, so pass a positive GetLayout
depth.) MPRIS checked out fully: metadata (title/artist/album/length) matches
what `nowplaying.set` was given, PlaybackStatus tracks `playing`, and
Pause/Next/Previous/PlayPause all arrive as `media-key` events.
--------------------------------------------------------------------------

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
- [x] **Global hotkeys** — X11/XWayland sessions grab keys directly
      (XGrabKey); pure-Wayland sessions go through the
      `org.freedesktop.portal.GlobalShortcuts` portal (the compositor shows
      a one-time approval dialog, then presses arrive as `onHotkey`).
      `keystroke` synthesis stays X11/XWayland-only (XTest) — no Wayland
      equivalent without the RemoteDesktop portal.
- [x] **launchAtLogin** — autostart `.desktop` entry, built apps only.
- [x] **`.desktop` self-registration + single instance + deep links** — a
      built app registers its own `.desktop` entry on first run (app-menu
      listing, icon, deep links via `urlScheme`, file associations via
      `fileExtensions`); a second `open` activates the running instance
      instead of launching another copy.
- [x] **pickColor** — portal-based system eyedropper.
- [x] **spotlight** — name search via the indexed `plocate`/`locate` when
      present, else a bounded, pruned `find` under `$HOME` (name-only, capped
      at 100 hits / 4s). The honest Linux degradation of Spotlight's
      name+content search.
- [x] **Unsupported ops report cleanly** — every capability with no Linux
      equivalent (`ocr`, `applescript`, `recorder`, `moveWindow`, …) rejects
      with a specific reason (e.g. "screen recording isn't supported on
      Linux yet"); query-style ones (`wifi`, `frontmostApp`, `selectedText`,
      `otherWindows`, `tray.position`) resolve `null`; fire-and-forget ones
      (`haptic`, Dock badge, `share`, `quickLook`) are silent no-ops.
      Nothing hangs — verified by an audit smoke that probes them all.
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
      `parec` (`@DEFAULT_MONITOR@`) or `pw-cat --record` (sink capture;
      verified 2026-07-23 on a box with no pulseaudio-utils — silence while
      idle, 100% FS while a wav plays through the default sink),
      chunked to interleaved LE Int16 at the requested interval. Matches
      the Windows WASAPI-loopback behavior: system scope only, `scope:'app'`
      is approximated by the system mix (see Still open for true per-app).
- [x] **Fleet sweep on Parallels (2026-07-23)** — all 26 examples build AND
      launch. Seven died on the first pass; the fixes are in the commits above
      plus the examples repo:
      * `spotlight` silently returned `[]` for most queries — the reader
        dropped output when GLib reported `G_IO_IN | G_IO_HUP` together.
      * `tray.set` on a ticker used freed memory (worldclock died in seconds).
      * `getWinState().screen` was 0x0 on Wayland, so treez sized its windows
        to zero height.
      * `app.voices` was a hardcoded `[]`, and `say` sent voice ids to `-l`.
      * boo/kraa/kraa3d/coo3d/treez dlopen'd CoreGraphics for the cursor on
        any non-Windows OS — they now take the polled-`mousePosition` path off
        macOS (real coordinates on X11, 0,0 on Wayland), and deja no longer
        spawns macOS's `screencapture` on Linux.
- [x] **Auto-update verified on Linux (2026-07-23)** — end to end against a
      local manifest (`assertSafeUrl` allows http://127.0.0.1 for exactly
      this): published 0.1.0, installed the tarball, published 0.2.0, and the
      installed app's `update.check()` reported 0.1.0 -> 0.2.0, `update.install()`
      swapped the bundle in place, and the binary then reported 0.2.0. A
      manifest with a bad sha256 is refused and the install is left untouched.
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

Ordered roughly by value/effort. Each lists the concrete route. All of these
currently fail cleanly (reject with a specific message, or resolve
null/empty) so nothing here is a correctness hazard — they're missing
features, not bugs.

- [~] **Examples: Linux builds for shelf installs** — BUILT AND VERIFIED
      LOCALLY, not yet published. In `../tinyjsapp-examples` (all uncommitted,
      for review):
      * `shelf/src/main.js` gained a Linux path mirroring the Windows one —
        installs under `$XDG_DATA_HOME/tinyjs-apps/<folder>/`, sha256-verified
        tar extract, `.tinyjs-shelf.json` marker, `ps`-based running detection,
        guarded uninstall, `xdg-open` for reveal/urls.
      * `shelf/src/frontend/app.js` — `normalizeEntry` handles a `linux` block;
        since Linux builds are per-arch, the backend exposes `arch()` and the
        page picks `linux[arch]`. An entry with no build for this arch is
        dropped from the list, exactly like a missing win block.
      * `shelf/gen-catalog-linux.js` (new) — merges linux blocks into
        catalog.json + the bundled catalog.js from tarballs staged in
        `_builds/<dir>/`. Deliberately additive: `gen-catalog.js` rebuilds the
        whole catalog from the macOS dmgs and would drop the win/linux blocks.
      * All 26 examples were `tinyjs publish`ed for linux-arm64 and the
        tarballs staged into `_builds/<dir>/`; catalog.json now carries 25
        linux blocks (~120 MB of artifacts — decide whether they belong in the
        repo like the win zips, or in releases).
      Verified end to end against a local HTTP server: 25/25 entries
      installable, install → launch → running-detection → uninstall clean for
      three different app types, and the checksum + non-repo-URL guards both
      reject.
      REMAINING: (1) **x86_64 tarballs** — this VM is aarch64, so the catalog
      is arm64-only and shelf on an x86_64 desktop would list nothing; release
      CI already builds linux-x86_64 for tjs, so extend it to the examples or
      publish from an x86_64 box, then re-run gen-catalog-linux.js there.
      (2) push the tarballs so the raw.githubusercontent URLs resolve.
- [ ] **recorder** — screen recording to a video file. Route: the
      `org.freedesktop.portal.ScreenCast` portal (CreateSession →
      SelectSources → Start → returns a PipeWire node fd), feed the PipeWire
      stream into a GStreamer pipeline (`pipewiresrc ! videoconvert !
      x264enc/vaapih264enc ! mp4mux ! filesink`). Interactive: the portal
      shows a source-picker dialog. Large (~a few hundred lines). This same
      ScreenCast path would ALSO give **captureScreen on pure Wayland** (X11
      sessions already have captureScreen via GdkPixbuf/XGetImage) — do both
      together. `RECORD`/`CAPTURE` currently reject with a clear message.
- [ ] **audioTap scope:'app'** — true per-process capture (own window only).
      The system mix is already captured (see Done). Route: a PipeWire
      sink-input filter targeting the app's own stream nodes, like the
      Windows process-loopback path. Non-trivial (PipeWire node graph
      walking). Today `scope:'app'` transparently gets the system mix.
- [ ] **authenticate** — a "prove it's the user" gate. POOR FIT on Linux:
      polkit authorizes *actions*, not identity, so there's no clean generic
      call. Options if pursued: register a private polkit action + agent
      prompt, or a PAM conversation (security-sensitive — don't hand-roll
      lightly), or fprintd for fingerprint-only. Currently returns `false`
      (fail-closed — safe: an app gating a sensitive action blocks). Probably
      leave as-is unless a real use case appears.
- [ ] **selectedText / otherWindows / moveWindow / frontmostApp** — reading/
      moving *other apps'* windows + selection. Doable via EWMH
      (`_NET_ACTIVE_WINDOW`, `_NET_CLIENT_LIST`, `XMoveResizeWindow`) +
      AT-SPI (for selectedText) on **X11 sessions only**; nothing portable on
      Wayland (by design — apps can't see each other). Low value on a
      Wayland-default desktop. Currently all resolve `null` / reject cleanly.
- [ ] **ocr / thumbnail (non-image files) / quickLook** — no clean system
      equivalents. `thumbnail` already scales images via GdkPixbuf; extending
      to arbitrary types could use the `org.freedesktop.thumbnails.Thumbnailer1`
      D-Bus service or GIO thumbnailers. `ocr` could shell to `tesseract` if
      present. `quickLook` has no Linux analog. All reject/degrade with a
      specific message today — fine to leave.

## Not planned / no OS equivalent

`applescript`, `haptic`, `dock.setBadge`/`dockIcon` (the Unity launcher API
is dead — `dock.bounce` now works via an urgency hint, but badges/custom
icons have no equivalent; could revisit via LauncherEntry), `setAllSpaces`
(sticky windows exist on X11 only), `tiny.app.ai`, `wifi` (NetworkManager
DBus — revisit if asked), `share`. All reject or report `'unsupported'` so apps can
feature-detect.
