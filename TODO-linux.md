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
re-derived from `native/launcher-macos.cc` + `native/launcher-win.cc` (a
distilled version was generated into a scratch file during the port but not
committed).

## This Parallels VM (set up 2026-07-22/23, aarch64, GNOME **Wayland**)

- **GPU acceleration works** — WebGL reports `Apple GPU` / `Apple Inc.`, so the
  UTM software-compositing problem is gone. Re-check `amp`'s audio here; the
  skipping should be fixed.
- **Node**: the distro `nodejs` package has no npm, which the TS examples need
  (`npx esbuild`, `vite`). This VM has NO host node (checked 2026-07-28) —
  build the TS examples (procsy/sqlittle/trolley) in a container instead:
  `docker run --platform linux/arm64 --rm -v <tinyjsapp>:<same path> -v
  <examples>:<same path> node:22-trixie-slim` + `apt-get install libffi8`,
  then `npm ci && tinyjs build` as uid 1000 inside. It must be **trixie**
  (glibc 2.41): `node:22-slim` is bookworm/glibc 2.36, too old for our tjs;
  and plain `docker run` grabs the cached **amd64** image here — always pass
  `--platform linux/arm64`. `npm ci` (never `install`) keeps
  `package-lock.json` untouched; npm-install churn drops the darwin/win32
  optional binaries, which would break mac/win builds.
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
  UPDATE 2026-07-28: the keyring was unlocked when re-checked, and the full
  secrets round trip works (details in TODO-verify.md) — if it reads locked
  again after a reboot, that's the autologin state above, not a regression.
- **Wayland session**, so per the notes below: `mouse`/window x,y report 0,0,
  `captureScreen` rejects, `keystroke` is unsupported. An X11 session lights
  those up. UPDATE 2026-07-29: `mousePosition().window.inside` is now HONEST
  on Wayland — the frozen last-on-surface coords used to keep the bounds
  check stuck `true` after the cursor left the app; `mouse_json` now lets
  `gdk_device_get_window_at_position` (NULL = pointer not over any of our
  surfaces) veto it, plus a `gdk_window_is_viewable` veto for hidden windows
  (GDK never clears its Wayland pointer focus on unmap). Coords still freeze
  (platform limit — no unprivileged global pointer query on Wayland).
  UPDATE later that day: the sanctioned route IS now taken, as an OPT-IN —
  `tiny.app.mouseTracking.start()/stop()` (MOUSETRACK on the wire) arms a
  ScreenCast portal session with cursor_mode METADATA and reads
  spa_meta_cursor off the PipeWire stream (pixels never mapped). Consent
  dialog once (restore token round-trips through the app's store;
  re-arm measured at 24ms, no dialog), sharing indicator while armed,
  window-relative coords outside the window via origin calibration (real
  origin = portal global − surface-local, re-pinned on every hover). Needs
  libpipewire-0.3-dev at build time (setup.sh probes; without it start()
  answers 'unsupported'). X11: start() is a no-op ok. Verified end-to-end
  on this VM 2026-07-29 with a human click on the dialog + eyeball of the
  live readout. Verified headlessly before that: fullscreen app sees
  live coords + inside:true; a second fullscreen app covering it (real
  wl_pointer.leave, no cursor motion needed) flips inside:false with coords
  frozen in-bounds; uncover flips it back; hide() reads inside:false, show()
  recovers. Apps can now trust `inside` to detect "lost the cursor" and
  degrade (boo/kraa eyes, tray anchoring).
- One harmless `Gtk-CRITICAL … gtk_widget_get_scale_factor` line at startup for
  any app with a tray. It comes from inside libayatana-appindicator (we never
  call it); setting the icon after the menu doesn't avoid it.
- **WebKitGTK suspends a fully occluded window's page** (measured 2026-07-28):
  cover a window completely and its timers all but stop — a 150ms poll loop
  woke so rarely its replies lagged the whole run. Unfocused-but-visible is
  fine. Bites test choreography especially hard on Wayland, where `win.open`'s
  x/y are ignored (compositor placement) and same-size windows stack exactly.
  Apps that run work in a background window should assume that window's page
  can be frozen at any time.
- **The app surface (`icon`/`attention`/`presence`) is X11-only** — measured
  2026-07-26 with `WAYLAND_DEBUG=1`: under a Wayland session all three produce
  zero protocol bytes, because GTK3's Wayland backend has nothing to carry a
  window icon, an urgency bit or a skip-taskbar hint. On XWayland each moves
  its X property. `capabilities()` now gates them on `ON_X11`.
  `badge`/`progress` are unaffected — they are a DBus signal, not a window
  property — but only a **built** app registers the `.desktop` entry the
  signal is addressed to, so they cannot show in `tinyjs dev`.
- **A window icon bigger than 256×256 never reaches the shell** — GDK only
  writes `_NET_WM_ICON` while the property fits X11's per-request limit, and
  every tinyjs `icon.png` is 1024×1024, so `gtk_window_set_icon_from_file()`
  quietly left only the legacy `WM_HINTS` pixmap. Bisected against a plain
  GTK3 control: 256 lands, 512 does not. The launcher now scales to an icon
  list instead (`set_window_icon()`), which fixed both `app.icon()` and the
  startup icon. If you touch that path, re-check with `xprop _NET_WM_ICON` —
  the call returns success either way.
- **Ubuntu Dock owns `com.canonical.Unity`** on this session. That name is the
  reliable "a launcher implements LauncherEntry" probe (libunity looks for the
  same thing), and `capabilities()` now uses it instead of pattern-matching
  `XDG_CURRENT_DESKTOP`.

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

- [x] **getUserMedia / camera+mic (2026-07-29)** — the launcher now answers
      WebKit's `permission-request` signal (previously unhandled = WebKit's
      default deny, so every camera app failed with NotAllowedError while
      PERMCHK said 'granted'). Because desktop Linux has NO consent layer
      under us (no TCC, no WebView2 prompt — /dev/video* is open to the
      session, which is why GNOME's own camera app never asks), the grant is
      manifest-gated: cli.js forwards tinyjs.json's `"permissions"` block
      into createApp, bridge.js exports it as `TINYJS_MEDIA=camera,microphone`
      on the launcher spawn, and only what's declared is allowed (device-info
      /enumerateDevices labels ride the same gate; getDisplayMedia stays
      denied). Verified both ways on this VM 2026-07-29: undeclared app →
      NotAllowedError; declared app → live "FaceTime HD Camera (V4L2)" +
      "Built-in Audio Analog Stereo" with labels. Bonus finding: WebKitGTK
      2.52's MediaRecorder records video/mp4 (like macOS), not webm.
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
      (Dock badge, `share`, `quickLook`) are silent no-ops.
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
- [x] **audioTap scope:'app'** — real per-app capture, so `scope:'app'` means
      the same thing it does on macOS/Windows instead of quietly handing back
      the system mix. PipeWire has no per-app capture primitive, so the
      launcher builds one: a private null sink (`tinyjs-tap-<pid>`), this
      app's own playback streams fanned into it — they keep playing to the
      real sink, so nothing about playback changes — and pw-cat reading that
      sink's monitor. Our streams are found by name: WebKit tags them with the
      app id (`node.name == <app id>`), so their ports are
      `<app id>:output_FL/FR`. A 250ms timer re-links, because streams come and
      go (new windows, a track stopping and starting) and re-linking an
      existing link is a no-op. Needs pw-cli/pw-link/pw-cat and a known app id;
      without any of those it falls back to the system monitor.
      CAREFUL: teardown must destroy ONLY our own node. `pw-cli ls Node` prints
      "id <n>," before each node's properties, so the owning id is the last one
      seen before the matching node.name — matching a window of nearby lines
      instead (grep -B20) sweeps up neighbouring ids and destroys OTHER apps'
      streams; that bug silenced Firefox until it was reloaded.
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
- [x] **Fleet re-sweep 2026-07-28 (app-surface-api branch, overnight)** —
      all 26 examples build AND launch against the rebuilt launcher; amp and
      worldclock soaked 30s. Size contract, capability corrections, argv →
      onOpenFiles, menu modifier accelerators, FFI offsets and the ball all
      verified the same night — details and the two bugs fixed
      (setAsDefaultHandler `exit_status`, `build --cli` shim never written
      off macOS) in TODO-verify.md § "2026-07-28 overnight Linux sweep".
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

- [x] **Per-window menu bars — verified 2026-07-28, both sessions,** the same
      night's second sweep. The full checklist in TODO-verify.md § "Per-window
      menu bars" is green on Linux, driven by a scripted multi-window app.
      First compile surfaced three real bugs, all fixed the same night: the
      birth-size repayment raced X11's async layout (random windows lost the
      bar's 26px from their page box — the flaky one the checklist predicted),
      `chrome.menu:false` handed the bar's row to the page instead of the
      frame, and `menu.update` never reached the stored specs so any rebuild
      (reset / chrome toggle / a window opened later) resurrected stale state.
      That last one is an architecture twin of the Windows launcher — check it
      there. Only pixels remain unseen (bar cosmetics); the hide-on-close rule
      was driven with a real WM_DELETE_WINDOW both with and without a tray.

- [ ] **`tiny.system.locale()`** — declared `false`; no locale arm in the
      launcher. `g_get_language_names()` is the route — it already does the
      `LANGUAGE`/`LC_ALL`/`LC_MESSAGES`/`LANG` fallback chain the OS itself
      uses, so it beats reading one variable by hand. Time zone from
      `/etc/localtime`'s symlink target. There's no system-wide "language
      changed" signal to hook, so the event may simply not exist here — which
      is worth stating rather than leaving the capability ambiguous.

Ordered roughly by value/effort. Each lists the concrete route. All of these
currently fail cleanly (reject with a specific message, or resolve
null/empty) so nothing here is a correctness hazard — they're missing
features, not bugs.

- [ ] **`tiny.system.wifi`** — declared `false`; the launcher has no `wifi`
      arm at all, so a query falls through `GET` to `null`. Portable in
      principle (unlike `ocr`/`recorder`), which is why it stays on
      `tiny.system` rather than moving to `tiny.macos`.

      Linux is the only platform other than macOS that can fill the shape
      **completely** — including `noise`, which Windows cannot supply at all.
      Two sources, and the cheap one is a plain file read:
      * `/proc/net/wireless` — per-interface link quality, **signal level in
        dBm** and **noise level in dBm**. No dependency, no D-Bus, no
        permission, no spawn. That's `rssi` and `noise` done.
      * `ssid` / `bssid` / `txRate` — NetworkManager over D-Bus is the
        cheapest route given the launcher already speaks it for MPRIS, the
        Secret Service and notifications: `org.freedesktop.NetworkManager`,
        the device's `ActiveAccessPoint` → `Ssid` (ay), `HwAddress`,
        `MaxBitrate`. Caveat worth knowing before writing it: **MaxBitrate is
        the AP's maximum, not the current transmit rate**, so it is not the
        same number macOS reports. The honest current rate needs nl80211 via
        libnl (what `iw dev … link` reads), which is another dependency and a
        lot more code.

      Not session-dependent — unlike most of this file, nothing here cares
      about X11 vs Wayland. Machines without NetworkManager (a bare
      wpa_supplicant setup) would get `ssid: null` with the signal fields
      still populated, which the API's existing nullability already covers.
      All of the above is from documentation and prior knowledge of these
      interfaces; none of it has been run on the VM.

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
      DONE 2026-07-24 on the Rosetta VM: x86_64 tarballs built in an amd64
      container (see § "x86_64 builds" below) and the payload moved to
      GitHub Releases (tag `<dir>-v<version>` per app) instead of committing
      another ~120 MB — `merge-manifest-linux.js --release` /
      `gen-catalog-linux.js --release` + `upload-releases-linux.sh`; runbook
      in ../tinyjsapp-examples/CLAUDE.md. 2026-07-25: mac/win payloads moved
      to the same releases and ALL payload blobs purged from the examples
      repo's history (git filter-repo, 683 MB → 19 MB) — only
      `_builds/<dir>/manifest.json` survives, since shipped apps poll those
      raw URLs. Never commit a payload there again.
- [x] **Examples: does closing shelf close the app it launched?** — NO on
      Linux, measured 2026-07-26: **the Linux branch needs no fix.** A tjs
      script spawning `tjs.spawn([exe], {stdio: 'ignore'})` and then exiting
      leaves the child running — it is reparented to the user `systemd` (PPID
      1731) and keeps going. There is no job-object equivalent here, so the
      Windows failure mode does not reproduce and the un-fixed shape below is
      correct as written. (Run the parent under `setsid`, as the desktop does,
      or your own terminal's SIGHUP confounds the result.) Kept for the
      Windows history: `shelf/src/main.js` `openApp()` spawned the app as shelf's own
      direct child (`tjs.spawn([exe], {stdin/stdout/stderr:'ignore'})`, with a
      comment claiming ignoring stdio made it "detached-ish" — it does not).
      Measured 2026-07-25: launching two real installed apps side by side, the
      direct-spawn one died the instant shelf quit while one launched via
      `explorer.exe` kept running; `detached: true` did **not** save it either.
      Fixed on Windows by handing the launch to the shell, the same shape as
      `open -a` on macOS.
      The Linux branch keeps that shape deliberately — it was left alone rather
      than changed on the strength of a Windows measurement, and the Linux
      measurement above now says leaving it was right. `gio open`/`xdg-open` or
      a `setsid` double-fork would have been the fix had the child died.
      (Careful reading the Windows evidence: the agent shell there runs inside
      a job object with `KILL_ON_JOB_CLOSE`, so only the within-run A/B is
      trustworthy, not the absolute mechanism.)
- [ ] **recorder** — screen recording to a video file. Route: the
      `org.freedesktop.portal.ScreenCast` portal (CreateSession →
      SelectSources → Start → returns a PipeWire node fd), feed the PipeWire
      stream into a GStreamer pipeline (`pipewiresrc ! videoconvert !
      x264enc/vaapih264enc ! mp4mux ! filesink`). Interactive: the portal
      shows a source-picker dialog. Large (~a few hundred lines). This same
      ScreenCast path would ALSO give **captureScreen on pure Wayland** (X11
      sessions already have captureScreen via GdkPixbuf/XGetImage) — do both
      together. `RECORD`/`CAPTURE` currently reject with a clear message.
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
- [ ] **`win.dragOut` — no drag source at all.** `DRAGOUT` is unhandled in
      `launcher-linux.cc`, so dragging files *out* of an app (onto a file
      manager, a mail compose window) silently does nothing, while `onDrop` —
      the inbound direction — works. The bridge sends the same line as on the
      other two platforms; it needs a GTK drag source on the WebView offering
      `text/uri-list`, started from the page's mousedown via
      `gtk_drag_begin_with_coordinates`. Portable across X11 and Wayland, so
      no session split. Surfaced in the kitchen-sink deck (Storage ▸ Files),
      which says outright that Linux can't do it yet.
- [ ] **ocr / thumbnail (non-image files) / quickLook** — no clean system
      equivalents. `thumbnail` already scales images via GdkPixbuf; extending
      to arbitrary types could use the `org.freedesktop.thumbnails.Thumbnailer1`
      D-Bus service or GIO thumbnailers. `ocr` could shell to `tesseract` if
      present. `quickLook` has no Linux analog. All reject/degrade with a
      specific message today — fine to leave.

## Not planned / no OS equivalent

`applescript`, `dock.setBadge`/`dockIcon` (the Unity launcher API
is dead — `dock.bounce` now works via an urgency hint, but badges/custom
icons have no equivalent; could revisit via LauncherEntry), `setAllSpaces`
(sticky windows exist on X11 only), `tiny.app.ai`, `wifi` (NetworkManager
DBus — revisit if asked), `share`. All reject or report `'unsupported'` so apps can
feature-detect.

## x86_64 builds from the next VM (Ubuntu + Rosetta under Parallels)

Planned: moving to an Ubuntu VM with Parallels' Rosetta integration to produce
the x86_64 tarballs. **Yes — that one VM can build BOTH arches:**

- **arm64**: natively, exactly the flow used here (`tinyjs publish` per app →
  copy tarballs into `_builds/<dir>/` → `node shelf/gen-catalog-linux.js`).
- **x86_64**: the VM itself stays aarch64 (Parallels on Apple silicon cannot
  boot an x86 kernel) — Rosetta translates x86-64 *userspace* binaries. So
  build inside an amd64 userspace running under Rosetta:
  1. Parallels VM config → CPU & Memory → enable **"Use Rosetta to run
     x86-64 binaries"**; in the guest, `systemd-binfmt` registers the
     interpreter (check `ls /proc/sys/fs/binfmt_misc | grep rosetta`).
  2. Cleanest route: an amd64 container —
     `docker run --platform linux/amd64 -v $PWD:/work ubuntu:24.04` (or a
     `debootstrap --arch=amd64` chroot). Inside: apt the deps `setup.sh`
     names, build `bin/tjs` and the launcher (both come out x86_64 because
     the toolchain itself is x86_64-under-Rosetta), then `tinyjs publish`
     per app as usual. Multiarch apt on the host
     (`dpkg --add-architecture amd64`) also works but webkit dev packages
     make it fiddlier than a container.
  3. Copy the `*-linux-x86_64.tar.gz` into `_builds/<dir>/` and re-run
     `node shelf/gen-catalog-linux.js` — it ADDS `x86_64` blocks next to the
     committed `arm64` ones (already built for two arches). Merge each
     `_builds/<dir>/manifest.json` the same way: add `linux.x86_64`
     `{ url, sha256, version }` beside `linux.arm64`; the updater picks its
     own arch and reports "no update" rather than offering a foreign build.

Caveats worth knowing before that session:
- Rosetta exposes no AVX — irrelevant for compiling (gcc/GTK don't need it)
  and runtime code paths CPUID-detect, so they just won't use it.
- Builds run ~2-3× slower under translation. Fine for a release pass.
- Smoke-test the x86_64 launcher under Rosetta in the VM, but the real
  confidence check is one run on actual x86 hardware — PipeWire/GTK behave
  the same, but it's the audio path we've been bitten on before.
