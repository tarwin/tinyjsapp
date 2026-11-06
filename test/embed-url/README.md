# tiny.embedURL (#107)

Headless, every platform: `bin/tjs run test/embed-url/check.js` → `PASS`.

In a real window: `cd test/embed-url/app && tinyjs dev`. It needs internet access.

| panel | expected |
|---|---|
| left, direct iframe | Error 153 on macOS. Record what other engines do. |
| right, `tiny.embedURL` | the video's poster and a play button. Press it: the video plays. |

The caption under the panels reads `refused: ok` when `tiny.embedURL` rejected a
host that isn't allowed. The same result is in the app's `store.json` under
`"embedUrl"`.

While it runs, check that the server only listens on loopback. The port is in the
right panel's URL under the caption:

- macOS: `lsof -nP -iTCP -sTCP:LISTEN | grep tjs`
- Linux: `ss -ltnp | grep tjs`
- Windows: `netstat -ano | findstr LISTENING`, then match the `tjs` pid

Expected: `127.0.0.1:<port>`, never `0.0.0.0` or `*`.
