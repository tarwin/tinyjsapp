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

## app surface — badge / attention / icon / progress / presence

Added 2026-07-25 (the dock→intent-verb rename + `progress`).

| check | macOS | Windows | Linux |
| --- | --- | --- | --- |
| `badge('3')` shows a count | ✅ seen | ❌ not built ([TODO-windows.md]) | ⬜ needs KDE/Ubuntu Dock |
| `badge('NEW')` (non-numeric) | ✅ arbitrary text | — | ⬜ hides — Unity badge is an int |
| `attention()` | ✅ bounces | ⬜ FlashWindowEx | ⬜ urgency hint |
| `icon(png)` replaces the icon | ✅ seen | ⬜ `.ico` only, not png | ⬜ window icon |
| `icon('')` restores | ✅ seen | ⬜ | ⬜ |
| `progress(0..1)` draws a bar | ✅ seen | ⬜ ITaskbarList3 | ⬜ Unity protocol |
| `progress` + `icon` compose | ✅ seen — the macOS-specific risk | n/a | n/a |
| `progress(null)` clears | ✅ seen | ⬜ | ⬜ |
| `presence('menubar')` hides it | ✅ seen | ⬜ **see note** | ⬜ skip-taskbar hint |
| `presence('normal')` restores | ✅ seen | ⬜ | ⬜ |

**Windows `presence` — highest-risk item.** The rename changed the wire op
from `WINOP dock` to `WINOP presence`, and the Windows handler matched the old
name for one commit. Both the name and the `substr()` offset it parses were
fixed together; neither has been run. If presence is a no-op on Windows,
suspect that offset first.

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

## Windows-only

- [ ] `app.badge` isn't implemented at all — needs an overlay HICON rendered
      at runtime. Notes in the task list / [TODO-windows.md]. Until then
      `capabilities().badge === false` on Windows, which is honest.
- [ ] `app.icon` takes `.ico` via `LoadImage`; a `.png` path silently fails to
      load and the call becomes a no-op rather than clearing the icon. Decide
      whether that should convert, reject, or stay as-is.

[TODO-windows.md]: TODO-windows.md
[TODO-linux.md]: TODO-linux.md
