# tinyjs — working notes for agent sessions

Runtime + CLI for tiny native apps: txiki.js backend (`runtime/bridge.js`),
webview client (`runtime/tiny.js`, compiled into each launcher via
`native/gen-client.sh`), three native launchers. Wire protocol: newline-
delimited lines over a Unix socket; `CMD@<winid>` targets a window, bare
`CMD` means main — window-scoped ops must dispatch via `forWin(m)`, not the
global app handle.

Releases are git tags (`vX.Y.Z` on main); a tag push builds macOS + Windows
+ linux-x86_64 + linux-arm64 in CI. Update CHANGELOG.md AND
docs/changelog.html before tagging. Linux burn-down + platform notes:
TODO-linux.md. Native-filters-on-other-OSes plan: TODO-audio-filters.md.

## Linux specifics that cost real debugging — read before touching

- `tinyjs dev` auto-rebuilds the launcher when launcher-linux.cc or
  runtime/tiny.js is newer than the binary. Manual build: see setup.sh.
- Web Audio reaching ctx.destination crackles under WebKitGTK (graph renders
  on a normal-priority thread; rtkit won't promote it). This is measured
  fact, not theory — don't retry latencyHint/buffering "fixes".
- PipeWire limits (all measured, PipeWire 1.0.5): filter-chain segfaults
  past ~30 declared nodes; the `gain` builtin kills any config containing
  it (use `linear`); `pw-cli set-param` silently emits an EMPTY pod past
  ~20 key/value pairs — chunk to ≤18 (launcher does).
- NEVER make system-wide audio changes (pw-metadata clock.force-quantum
  etc.) — a forced quantum once broke ALL system audio including Firefox.
  Per-app nodes only, named `tinyjs-<kind>-<pid>`, destroyed by exact
  node.name match with the awk id-carry pattern (a windowed grep once
  destroyed OTHER apps' nodes and silenced Firefox).

## Testing recipes (headless, no clicking)

- Self-driving test page: `TINYJS_HTML=/abs/path/page.html tinyjs dev`
  (from any app dir). Page saves results via `tiny.store.set(...)`; read
  them from `~/.local/share/<app-id>/store.json`. Wait ~2s for `tiny`.
- Drive a running app via MPRIS:
  `gdbus call --session -d org.mpris.MediaPlayer2.<app_id_underscored> -o
  /org/mpris/MediaPlayer2 -m org.mpris.MediaPlayer2.Player.Play` (also
  Stop, Properties.Get Position/Volume, Properties.Set Volume).
- Kill launchers by exact name: `for p in $(pgrep -x launcher-linux); do
  kill $p; done`. NEVER `pkill -f` a pattern that matches your own shell —
  it kills the agent's command (exit 144) and reads as a mystery failure.
- Audio measurement: `pw-cat --record --target <sink-or-name> -P
  '{ stream.capture.sink=true }'` + a few lines of Python (rms/peak).
  CAUTION: the built-in ALSA sink's monitor MONO-ISES — L/R comparisons
  through it are meaningless. For stereo truth, create a null sink
  (`support.null-audio-sink`, audio.position=[FL FR]), point the stream at
  it via `pw-metadata <id> target.object <name>`, capture ITS monitor —
  and validate the rig with a hard-panned reference first.
  Xruns: `pw-top -b` ERR column. Destroy test sinks when done.
