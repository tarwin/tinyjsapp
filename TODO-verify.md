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
| `badge('3')` shows a count | ✅ seen | ❌ not built ([TODO-windows.md]) | ⬜ needs KDE/Ubuntu Dock |
| `badge('NEW')` (non-numeric) | ✅ arbitrary text | — | ⬜ hides — Unity badge is an int |
| `attention()` | ✅ bounces | ✅ flashes the taskbar button | ⬜ urgency hint |
| `icon(png)` replaces the icon | ✅ seen | ✅ seen (fixed 2026-07-25) | ⬜ window icon |
| `icon(ico)` replaces the icon | n/a | ✅ seen | n/a |
| `icon` reaches the taskbar button | ✅ Dock icon | ❌ **title bar + Alt-Tab only** | ⬜ |
| `icon('')` restores | ✅ seen | ✅ back to icon.png (fixed 2026-07-25) | ⬜ |
| `progress(0..1)` draws a bar | ✅ seen | ✅ seen — 45% then 90% | ⬜ Unity protocol |
| `progress` + `icon` compose | ✅ seen — the macOS-specific risk | n/a | n/a |
| `progress(null)` clears | ✅ seen | ✅ bar goes, button stays | ⬜ |
| `presence('menubar')` hides it | ✅ seen | ✅ button vanishes | ⬜ skip-taskbar hint |
| `presence('normal')` restores | ✅ seen | ✅ button comes back | ⬜ |

Windows column checked 2026-07-25 on Windows 11 Pro 26200, launcher rebuilt
from current source first — the checked-in `launcher-win.exe` was three days
stale and predated the intent-verb rename, so testing it would have proved
nothing. `capabilities()` reported `badge=false icon=true presence=true
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
there is nothing to detect and no error when nobody is listening.
`capabilities()` guesses from `XDG_CURRENT_DESKTOP` — verify that guess is
right on the box you're on, since a wrong guess is worse than no guess.

Also unverified on Linux: whether the `.desktop` id the launcher builds
matches the file the bridge actually wrote into
`~/.local/share/applications/`. A mismatch means the signal is addressed to an
app the shell has never heard of — silently doing nothing.

## macOS — does `setSize` → `getState` round-trip on a TITLED window?

Unverified suspicion, found while fixing the Windows twin (2026-07-25). The
contract is stated in launcher-macos.cc: "width/height are frame size — the
same units setSize uses, so set → get round-trips". But `getState` reports
`win.frame` while the size op calls `[win setContentSize:]`, and those differ
by the title bar. On Windows the identical mismatch (GetWindowRect vs
AdjustWindowRect) made any read-modify-write of the size ratchet up by the
frame on every pass — a window grew 13px wider and 36px taller each time, and
amp's windowshade guard walked off the right of the screen.

It has never bitten on macOS because tinyjs windows there are usually
borderless, where content == frame and the drift is exactly zero. A **titled**
window should drift. To check: `setSize(w, h)` on a titled window, read
`getState()` back, and see whether height comes back `h` or `h + titlebar`; then
feed it back a few times and watch for a ratchet. If it drifts, the fix is the
one applied on Windows — set the FRAME size, and leave window creation taking a
content/client size (no feedback loop there).

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

## Windows-only

- [ ] `app.badge` isn't implemented at all — needs an overlay HICON rendered
      at runtime. Notes in the task list / [TODO-windows.md]. Until then
      `capabilities().badge === false` on Windows, which is honest. Confirmed
      2026-07-25: `badge('3')` resolves `true`, draws nothing.
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
