# Cross-platform checks still owed

Things implemented on one machine that nobody has yet watched happen on
another. macOS is the primary dev box, so Windows and Linux entries here are
usually "written, compiled at best, never seen".

Tick a box only after seeing it with your own eyes on that OS — a
fire-and-forget wire op that reaches a shell which ignores it looks exactly
like one that was never implemented.

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

Two checks in that page are structurally unable to pass on Windows, so read
their `ok` as "the call resolved", not "the decoration appeared":

- it calls `attention()` while the page is the foreground window, and
  `FLASHW_TIMERNOFG` is a documented no-op in exactly that case;
- it calls `presence('normal')` and then quits ~0.2s later, so a restored
  taskbar button and a departed app look identical.

Both need a hold and a defocus to mean anything on Windows — see the headless
recipe at the bottom. Worth fixing in the page itself.

## app surface — badge / attention / icon / progress / presence

Added 2026-07-25 (the dock→intent-verb rename + `progress`).

| check | macOS | Windows | Linux |
| --- | --- | --- | --- |
| `badge('3')` shows a count | ✅ seen | ❌ not built ([TODO-windows.md]) | 🔶 signal correct, drawing unseen |
| `badge('NEW')` (non-numeric) | ✅ arbitrary text | — | ⬜ hides — Unity badge is an int |
| `attention()` | ✅ bounces | ✅ flashes the taskbar button | ✅ X11 urgency bit / ❌ Wayland |
| `icon(png)` replaces the icon | ✅ seen | ✅ seen (fixed 2026-07-25) | ✅ X11 (fixed 2026-07-26) / ❌ Wayland |
| `icon(ico)` replaces the icon | n/a | ✅ seen | n/a |
| `icon` reaches the taskbar button | ✅ Dock icon | ❌ **title bar + Alt-Tab only** | ⬜ dock uses the .desktop icon |
| `icon('')` restores | ✅ seen | ✅ back to icon.png (fixed 2026-07-25) | ✅ byte-identical restore |
| `progress(0..1)` draws a bar | ✅ seen | ✅ seen — 45% then 90% | 🔶 signal correct, drawing unseen |
| `progress` + `icon` compose | ✅ seen — the macOS-specific risk | n/a | n/a |
| `progress(null)` clears | ✅ seen | ✅ bar goes, button stays | 🔶 signal correct |
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

Still genuinely needing eyes on Linux: whether Ubuntu Dock *draws* the badge
and progress bar for a built app, and `badge('NEW')` hiding rather than
showing something wrong.

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
that call it, so custom grips were dead on Windows (native `WS_THICKFRAME`
borders still worked, which is probably why nobody noticed).

To check: a frameless window, drag it by a `data-tiny-drag` region and by a
satellite's own handle; then drag each of the eight edge grips and confirm the
right edge moves (a wrong `HT*` mapping resizes the opposite side).

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

Still open on the other two: the Windows section above, and Linux has no drag
source whatsoever — `DRAGOUT` is unhandled in `launcher-linux.cc` (tracked in
TODO-linux.md).

## Windows-only

- [ ] `app.badge` — **written 2026-07-26 on macOS, never run on Windows.**
      `do_badge` renders an overlay HICON (`badge_icon()`) and hands it to
      `ITaskbarList3::SetOverlayIcon`. `capabilities()` deliberately still
      reports `badge:false`; flip that only once the badge has been SEEN, not
      merely compiled.
      The trap it works around: GDI writes RGB but leaves the alpha byte of a
      32bpp DIB alone, so text drawn the obvious way is fully transparent. It
      paints a disc + up to two glyphs in colours that are never pure black,
      then sets alpha for every pixel that got written. Plausible failure
      modes to look for: an invisible badge (alpha fix not doing its job), a
      black square (mask/`CreateIconIndirect` wrong), or nothing at all
      (`SetOverlayIcon` needing a taskbar button that a dev spawn may not
      have). Longer text collapses to a bullet — 16px fits 1–2 glyphs.
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
- [ ] `app.icon` still cannot move the **taskbar button** — see the note above.
      Unresolved whether it can be moved at all; built apps additionally carry
      an AppUserModelID + `RelaunchIconResource` that dev spawns skip, and that
      path is untested.

## Written 2026-07-26, never run off macOS

- [ ] **kitchen-sink FFI, Linux** — `libc.so.6` `sysinfo()` decoded by struct
      offset (uptime / loads / totalram × mem_unit / procs at 0/8/32/104/80)
      plus `gethostname()`, and zlib via `libz.so.1`. The offsets are the
      x86_64 ABI; **aarch64 uses the same layout, but that is an assumption
      worth one check** — if `ram` reads absurd, the struct is being decoded
      at the wrong offsets.
- [ ] **kitchen-sink FFI, Windows** — `kernel32.dll` `GetTickCount64()` and
      `GlobalMemoryStatusEx()`. The struct's first field must be its own size
      (64) before the call or the API refuses; if `ram` is 0, that's the
      first thing to check. zlib is deliberately absent on Windows and the
      card says so rather than failing.
- [ ] **`chrome.trafficLights` reporting** — it only ever worked on macOS,
      but Linux stored the bit and echoed it back through `getState`, so an
      app that set it and read it back was told it had worked. Linux now
      reports the truth, and `capabilities().trafficLights` is `false` on both
      Windows and Linux. Worth confirming nothing depended on the old lie.

- [ ] **`chrome.windowControls` on Windows** — `WS_SYSMENU` /
      `WS_MINIMIZEBOX` / `WS_MAXIMIZEBOX`. Win32 is coarser than macOS: the
      min/max boxes require `WS_SYSMENU`, so "minimize without close" isn't
      expressible and asking for it gets close too. Check that `['close']`
      leaves only close, that `false` removes the group, and that `getState`
      round-trips what you set.
- [ ] **`chrome.windowControls` on Linux** — `gtk_window_set_deletable` for
      close, `_MOTIF_WM_HINTS` for minimize/maximize. Mutter and KWin read the
      hint; many WMs don't, so treat a no-op as "this WM ignores MWM", not a
      bug. `getState` deliberately reports `null` here rather than echoing the
      request.

## Needs a hand on the mouse — kitchen-sink, 2026-07-26

- [ ] **`dialog.openFiles`** — the deck's Storage ▸ Files "Open many…" button.
      A native panel can't be driven from the page, so only the wiring either
      side of it is checked: multi-select on, an array back, `null` for a
      cancel. NSOpenPanel, the Windows common item dialog and GTK's chooser
      each need their own look, because "multi-select on" is a different flag
      in all three and a panel that quietly allows only one file returns a
      one-element array that looks entirely correct.
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

## The ball on Windows and Linux — never run there

- [ ] **Windows** — per-frame `win.setPosition` on a second window: is a
      `SetWindowPos` per frame smooth, or does it stutter/trail? And does
      `chrome: { transparent: true }` on a WebView2 child window give a real
      circle, or a black square behind it?
- [ ] **Linux/X11** — same two questions through GTK, plus: WebKitGTK needs an
      RGBA visual for a transparent window, and the ball is the first place a
      *secondary* window asks for one.
- [ ] **Linux/Wayland** — the honest-failure path: `getState().canPosition` is
      false there, and ball.html is supposed to show `✋ no setPosition` and
      close after six seconds rather than sit still counting down. Written
      blind; never seen.

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
- `WM_GETICON` read off the live window separates "the call did nothing" from
  "the call worked and the shell ignored it". That distinction is the whole
  point of this file, and it is what caught the `app.icon` taskbar finding.

[TODO-windows.md]: TODO-windows.md
[TODO-linux.md]: TODO-linux.md
