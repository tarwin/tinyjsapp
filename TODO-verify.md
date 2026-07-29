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
| `badge('3')` shows a count | ✅ seen | ✅ seen — red disc, white '3' (2026-07-28) | 🔶 signal correct, drawing unseen |
| `badge('NEW')` (non-numeric) | ✅ arbitrary text | ✅ collapses to a bullet (2026-07-28) | 🔶 hides: dbus shows count-visible=false (2026-07-28) |
| `attention()` | ✅ bounces | ✅ flashes the taskbar button | ✅ X11 urgency bit / ❌ Wayland |
| `icon(png)` replaces the icon | ✅ seen | ✅ seen (fixed 2026-07-25) | ✅ X11 (fixed 2026-07-26) / ❌ Wayland |
| `icon(ico)` replaces the icon | n/a | ✅ seen | n/a |
| `icon` reaches the taskbar button | ✅ Dock icon | ❌ **title bar + Alt-Tab only** | ⬜ dock uses the .desktop icon |
| `icon('')` restores | ✅ seen | ✅ back to icon.png (fixed 2026-07-25) | ✅ byte-identical restore |
| `progress(0..1)` draws a bar | ✅ seen | ✅ seen — 45% then 90% | 🔶 signal correct, drawing unseen |
| `progress` + `icon` compose | ✅ seen — the macOS-specific risk | n/a | n/a |
| `badge` + `progress` compose | n/a | ✅ seen together (2026-07-28) | 🔶 one signal carries both |
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
- [ ] **Click an edge without moving** — press and release in place. The window
      must NOT follow the pointer afterwards. `xxxMoveSize` entered with the
      button already up tracks until the next click; the trip from the grip's
      `mousedown` to `do_ncdrag` is two process hops, so this is easy to hit.
      Guarded with `GetAsyncKeyState(VK_LBUTTON)`, mirroring macOS.
- [ ] `setResizable(false)` then drag an edge — must do nothing (the style bit
      is read live, because tiny.js gates its grips on one `getState` at load).
- [ ] Alive mid-drag: hold an edge and confirm posted work still lands (the
      modal loop pumps `WM_APP`, so dispatch keeps running inside it).
- [ ] A monitor LEFT of primary (negative screen x) — exercises the
      `MAKELPARAM` sign path in the point now passed as `lParam`.

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

## Written 2026-07-26, never run off macOS

- [x] **kitchen-sink FFI, Linux** — verified 2026-07-28 on aarch64: the
      offsets are right. `ram` reads 1.9 GB against a ground truth of
      2049527808 bytes from `free -b` (exact), uptime 1.4 h (sane),
      `gethostname()` returns the real hostname, `getpid()` matches tjs.pid,
      and zlib round-trips (zlibVersion 1.3, compress→decompress true).
      x86_64 remains assumed-by-ABI, which is the direction the offsets were
      written for.
- [ ] **kitchen-sink FFI, Windows** — **this entry described code that was
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
      the lie. Windows unchecked.

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
      close button, which GTK itself draws) can work — and that half still
      needs eyes. A real fix would drive the CSD title bar, not MWM.

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
      behind), an unsaved key reads `null` not an error. Still unwatched,
      because this box can no longer produce them: the no-daemon answer
      (`no secret service`) and the locked-keyring behavior — GNOME may
      prompt to unlock, and it's worth knowing whether that blocks the call
      or fails. (Earlier this VM DID report IsLocked while autologin'd, per
      TODO-linux.md, so the locked case is real.)
- [ ] **Linux — `authenticate` answers `false`.** Deliberate: no portable owner
      check exists, so the gate fails closed. The card claims this in prose;
      confirm the button actually says so rather than looking broken.

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
      Still unrun: pressing a media key with Now Playing set.
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

- [ ] **Windows** — per-frame `win.setPosition` on a second window: is a
      `SetWindowPos` per frame smooth, or does it stutter/trail? And does
      `chrome: { transparent: true }` on a WebView2 child window give a real
      circle, or a black square behind it?
- [~] **Linux/X11** — mechanics verified 2026-07-28, pixels not: opened the
      way the deck opens it, the ball lived the full 10s flight (present at
      7.5s, gone by 13s), i.e. per-frame setPosition ran to completion.
      Whether it's SMOOTH, and whether the RGBA visual gives a real circle
      rather than a black square, still needs eyes.
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

## Per-window menu bars — Windows proven, macOS and Linux UNRUN (2026-07-28)

`tiny.menu.set` became the APP menu (every window draws it), plus
`tiny.win.menu.*` for one window's own and `chrome.menu:false` to hide one
window's bar. Written on **Windows**, which is the only platform it has been
compiled or run on.

| check | Windows | macOS | Linux |
| --- | --- | --- | --- |
| a secondary window shows the app menu | ✅ seen — nib's doc window, plus a scripted app measuring its own frame | ⬜ (always did — one shared bar) | ⬜ never compiled |
| `size` is still the page's box with a bar | ✅ declared 700x400 → 700x400 page, frame grew 19px | n/a (the bar isn't in the window) | ⬜ main was repaired before; secondaries are new code |
| a window opened later inherits the app menu | ✅ seen | ⬜ | ⬜ |
| `win.menu.set` overrides that window only | ✅ own id present, app id gone, other windows untouched | ⬜ **the focus swap is the risky part** | ⬜ |
| `win.menu.reset` goes back to the app menu | ✅ seen | ⬜ | ⬜ |
| `win.menu.update` moves one window's tick | ✅ seen | ⬜ | ⬜ |
| `menu.update` moves every window's copy | ✅ seen | ⬜ | ⬜ |
| `chrome.menu:false` hides one bar only | ✅ frame 19px shorter, page unchanged | n/a — no-op by design | ⬜ |
| items survive a hidden bar (accelerators) | ✅ `menu.get` still answers | n/a | ⬜ |
| a closed window's items leave the registry | ✅ by construction (WM_DESTROY) | ⬜ | ⬜ |
| a menu declared before `{role:'edit'}` keeps its items | ✅ fixed + read back off the live `HMENU` — nib's File, 20 items | ✅ always worked | ✅ appends through a pointer, never flushes |
| a page-gated menu action fires in a fresh window | ✅ fixed — `document.hasFocus()` is true from load now (was false until the first click) | ✅ webview is first responder at once | ⬜ **check this** — WebKitGTK may need `gtk_widget_grab_focus` on the webview the same way |
| `setHideOnClose` + closing the last window quits | ✅ fixed (`can_live_hidden`) — process and backend both exit | n/a — the Dock is the way back, flag unchanged | ⬜ same rule written for GTK, unrun: check a tray app still survives, and that closing the last window exits |

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

[TODO-windows.md]: TODO-windows.md
[TODO-linux.md]: TODO-linux.md
