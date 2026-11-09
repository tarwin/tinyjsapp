# setResizable(false) + rememberWindows (#113)

`cd test/remember-nonresizable/app && tinyjs dev`, twice. Results print as
`remember-nonresizable: PASS|FAIL|INFO …` lines in the dev console.

The backend opens `kid` (400x300); kid.html calls `tiny.win.setResizable(false)`.

The bug (macOS): `-[NSWindow isZoomed]` is YES for any window without the
Resizable style bit, so every move after setResizable(false) reported
`maximized: true`, rememberWindows saved it, and the next launch zoomed kid.

- **First run** (no saved state): the backend moves kid to 320,220 (a move
  after setResizable(false) is what triggered the bug); no `maximized:true`
  should be reported. Then drag kid by hand; no FAIL lines should appear. Quit.
- **Every later run**: the backend reads kid's saved entry first, checks it
  isn't `maximized`, and checks kid came back at 400x300 at the saved
  position. It doesn't move kid, so drag, quit and rerun as often as you like.

Check by hand too: `window-state.json` in the app's data dir (macOS:
~/Library/Application Support/app.tinyjs.test.remembernonresizable/) keeps
kid's `"maximized": false`.

Start over: delete `window-state.json` and `store.json` there.
