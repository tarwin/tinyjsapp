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
      behind), an unsaved key reads `null` not an error. Still unwatched,
      because this box can no longer produce them: the no-daemon answer
      (`no secret service`) and the locked-keyring behavior — GNOME may
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

## The fetch repair shim — verified on all three, POST bodies BROKEN (Bug D)

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
> Measured macOS arm64 only; presumed all three (libuv-generic) but not
> observed elsewhere — the boxes below stay unticked for it:
>
> - [ ] **Windows** — small-POST hang through the shim, and the temp-file fix
> - [ ] **Linux** — same
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

## Headless fail-fast in the launchers — macOS verified, Linux + Windows UNRUN (2026-08-24)

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
- [ ] **Linux** — `DISPLAY`/`WAYLAND_DISPLAY` both unset ⇒ message +
      exit 3; else `gtk_init_check()` false ⇒ message + exit 3. The old
      bare `gtk_init()` call after the socket connect is GONE (init now
      runs before the connect — g_set_prgname still precedes it, as the
      WM_CLASS comment requires). NOT COMPILED — written on the Mac.
      Check: `env -u DISPLAY -u WAYLAND_DISPLAY tinyjs dev` prints the
      tinyjs: lines and the bridge says "launcher exited before
      connecting"; a normal desktop run still opens the window.
- [ ] **Windows** — window station lacks `WSF_VISIBLE`
      (`GetUserObjectInformationW(UOI_FLAGS)`) ⇒ message + exit 3, before
      the pipe connect. NOT COMPILED — written on the Mac. Check both
      ways: an `ssh` (non-console) session or a service context fails
      loud; a normal desktop run is unaffected. Note the check is
      deliberately permissive — if the UOI_FLAGS query itself fails we
      proceed rather than block a working desktop.
