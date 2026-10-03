# tiny-media proxy gate probes (#30.1)

Adversarial check that `tiny.proxyURL`'s `tiny-media://` proxy only serves
the app's own pages. macOS and Linux (Windows has no proxy).

1. Serve this folder: `python3 -m http.server 8765 --bind 127.0.0.1`
2. From any scratch app dir: `TINYJS_HTML=$PWD/../tinyjsapp/test/proxy-gate/main.html tinyjs dev`
   (the path must be absolute).
3. After about 5 s, read `"px"` from the app's `store.json`.

Expected:

| key | meaning | expected |
|---|---|---|
| `app` | the app's own `file://` page fetches through the proxy | `SECRET-PAYLOAD` |
| `iframe-cors-err` | cross-origin iframe, CORS fetch | present (`iframe-cors` with the secret = **hole**) |
| `iframe-nocors` | same iframe, no-cors | `opaque` (unreadable, fine) |
| `nav` | proxied page fetching the proxy same-origin | **absent** (`nav:SECRET…` = **hole**) |

stderr shows `tinyjs: tiny-media proxy refused for http://127.0.0.1:8765`
and `… (document load)`.

Wrapped site: an app with `"url": "http://127.0.0.1:8765/wrap.html"` stores
`"wrap"`. With no `"api"` it should be refused (`err …`). With
`"api": { "origins": { "http://127.0.0.1:8765": ["media.proxy", "store.*"] } }`
it should be `SECRET-PAYLOAD`, and with `["store.*"]` only it should be
refused again.
