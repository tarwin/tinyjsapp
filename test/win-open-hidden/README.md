# win.open({ show: false }) (#102)

`cd test/win-open-hidden/app && tinyjs dev`, twice. Results print as
`win-open-hidden: PASS|FAIL|INFO …` lines in the dev console.

The backend opens `kid` with `{ parent: true, show: false }`; kid.html calls
the backend while hidden, then `tiny.win.show()` ~2.5s after loading.

- **Run 1** (fresh state): kid stays hidden, its page runs JS + an RPC while
  hidden, `show()` brings it up where it was placed. Then it moves kid to
  300,200 and maximizes it, so rememberWindows saves both. Quit.
- **Run 2**: kid opens hidden AT 300,200 (the remembered normal rect; the
  #102 bug was a visible window at the old rect that jumped later) and comes
  up maximized on show (the bridge holds a remembered maximize until then).

By eye: kid appears once and never flashes somewhere first; after show it
stays above main (`parent`); on macOS it also moves with main. Wayland has
no window positions, so run 2's position check can't pass there.

Start over: delete `window-state.json` and `store.json` in the app's data
dir (`app.tinyjs.test.winopenhidden`: ~/Library/Application Support/… on
macOS, %APPDATA%\… on Windows, ~/.local/share/… on Linux).
