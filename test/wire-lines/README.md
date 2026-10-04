# Page messages can't add wire lines (#30.6)

`forge.html` is a hostile wrapped page. It posts malformed call messages
straight to the launcher's message channel (not through `tiny`). If they got
through, the backend would see a `MENU pwned` event and a call stamped with
the app's own `file://` origin.

`forge.html` is macOS-shaped: it posts `<seq>:<payload>` to
`window.webkit.messageHandlers.tiny`. Linux messages are
`<__TINY_TOK>|<seq>:<payload>`, so a Linux run needs the real token in
front. On Windows, post with `window.chrome.webview.postMessage`: a
secondary window takes the same `<seq>:<payload>` shape, and the main
window takes the webview library's JSON-RPC with the forged line in `id`.
The Linux and Windows runs (TODO-verify.md) used variants built that way,
and also tried LF, CRLF and bare-CR, plus an all-letters seq.

Fixed in v0.47.1; kept out of the repo until that release.

1. In this folder: `python3 -m http.server 8765 --bind 127.0.0.1`
2. A scratch app whose tinyjs.json has
   `"url": "http://127.0.0.1:8765/forge.html"` and
   `"api": { "origins": { "file://*": "all", "http://127.0.0.1:8765": ["ping"] } }`.
   Its `src/main.js` also needs an `onMenu(id)` that writes `id` to a file.
3. Run `TINYJS_DEBUG=1 tinyjs dev` and wait about 8 s.

Pass: the store has no `forged` key, `onMenu` never fired, and the trace
has no `<< MENU` or `<< CALL q` lines. The page's own `store.set` is still
logged as `denied` (the gate works for honest calls).
