# Subframe calls need an "api".origins key

`evil.html` is a cross-origin iframe inside the app's own page. WebKit gives
every frame of a macOS window the `tiny` message handler (the shim and
tiny.js are main-frame only), so the iframe posts calls by hand:
a `store.set('pwned')`, then a stream of unknown-method calls on seq
1..100 to see whether their replies land on the MAIN frame's pending calls.

macOS only: Linux drops subframe messages at the launcher (#18) and
WebView2 hands over only top-level documents' messages. On those two the
iframe's calls never arrive, which is also a pass.

1. In this folder: `python3 -m http.server 8765 --bind 127.0.0.1`
2. From a scratch app dir:
   `TINYJS_HTML=$PWD/../tinyjsapp/test/subframe-gate/main.html tinyjs dev`
   (absolute path), wait about 14 s, read `"sg"` and `"pwned"` from the
   app's `store.json`.
   `sg.resolves` counts every reply that reached the main frame: a few
   dozen (the page's own calls) means the iframe's replies stayed out.

| tinyjs.json `"api"` | expected |
|---|---|
| none | `sg.own` `ALLOWED`, no `pwned` key, `sg.crosswired` 0, `sg.resolves` under 100 |
| `"wrapper"` (top-level lists only) | same: lists don't reach subframes |
| `{ "origins": { "file://*": "all", "http://127.0.0.1:8765": ["store.*"] } }` | `pwned` is `yes` (a listed origin keeps its key), `sg.crosswired` 0 |

With `TINYJS_DEBUG=1` the refused calls log as
`denied "store.set" for subframe http://127.0.0.1:8765 (tinyjs.json "api".origins)`.
Before the fix, the no-`"api"` run stored `pwned` and `sg.resolves` ran
into the thousands: every iframe reply was handed to the main frame. (Whether
one of them happens to reject a main-frame call, `sg.crosswired`, is a
timing race, so it often reads 0 even then; `resolves` is the reliable
signal.)
