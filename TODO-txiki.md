# TODO — txiki.js runtime bugs worth fixing at the source

Two wire-level bugs in txiki.js v26.6.0's `fetch` (the runtime we ship as
`bin/tjs`). Both are currently papered over by the **fetch repair shim** in
`runtime/bridge.js` (search "fetch repair shim"), which routes exactly the
broken cases through the system curl. The honest fix is in txiki itself:
patch it, ship pinned builds (CI already builds tjs for Linux — extend to
macOS + Windows), PR the patches upstream to saghul/txiki.js, and delete the
shim once a fixed tjs is pinned everywhere.

Both bugs were found 2026-07-30 debugging amp podcast feeds that loaded in
every browser and curl but not in the app. Neither is theoretical: eight of
amp's ~60 baked-in FAVES feeds hit one or the other. The shim is verified on
all three OSes through amp's full stack (Windows 2026-07-30: feeds and
streams in the built app, plus the console-flash the curl spawns caused
there, now fixed). POST request bodies are the one path nothing has
exercised anywhere — see TODO-verify.md.

## Bug A — root-path URLs go out as `GET //`

Any URL whose path is `/` (or empty) produces a request line with a double
slash:

```
fetch('https://feed.articlesofinterest.club/')   → GET // HTTP/1.1
fetch('http://127.0.0.1:8899')                   → GET // HTTP/1.1   (pathless too)
fetch('https://host/some/path/')                 → GET /some/path/   (correct)
```

S3-backed CloudFront reads `//` as a key that starts with a slash → object
miss → the distribution's 404 page, served with `x-cache: Error from
cloudfront`. Verified byte-for-byte with a raw TLS replay: `GET /` → 200,
`GET //` → 404 on the same host, same headers.

- Repro harness: `nc -l 8899 > req.txt` + `tjs run` a fetch of
  `http://127.0.0.1:8899/` — look at the request line in req.txt.
- Real victims: Radiotopia's `publicfeeds.net` custom feed domains
  (feed.articlesofinterest.club, feed.proxypodcast.com) and anything a
  `rss.pdrl.fm` tracker 302s to (feed.hyperfixedpod.com,
  thisday.feed.electionhistory.show) — redirects are followed inside txiki,
  so the re-mangling can't be dodged by normalizing the input URL. There is
  NO JS-level workaround short of not using the built-in fetch: both `/` and
  pathless URLs mangle, so nothing you write in the URL avoids it.
- Where to look: txiki's fetch/XHR request-line construction — almost
  certainly a `'/' + path` join where path already starts with (or is) `/`.
  Likely a one-line fix.

## Bug B — TLS 1.2-only hosts never complete a handshake

```
fetch('https://rss.art19.com/the-allusionist')
  → Network request failed: mbedtls connect -1 5 0
```

Hosts that negotiate at most TLS 1.2 fail at connect; every host that
supports TLS 1.3 works. Mapped empirically (openssl s_client confirms the
protocol ceiling of each):

| host                 | max TLS | txiki fetch |
|----------------------|---------|-------------|
| feeds.simplecast.com | 1.3     | works       |
| feeds.megaphone.fm   | 1.3     | works       |
| pypi.org (Fastly)    | 1.3     | works       |
| rss.art19.com (Fastly)| 1.2    | mbedtls connect -1 5 0 |
| anchor.fm (Fastly)   | 1.2     | mbedtls connect -1 5 0 |

Note the failure is not "Fastly" — pypi.org is Fastly and works; it's the
TLS 1.2 ceiling of the older Fastly profiles art19/anchor sit on. The error
also (misleadingly) appears for `http://` URLs on those hosts because txiki
follows the http→https redirect internally and then fails the handshake.

- Where to look: the mbedtls build configuration txiki compiles —
  `MBEDTLS_SSL_PROTO_TLS1_2` (or the 3.x equivalent minimum-version setting)
  is presumably off/misconfigured, making the client 1.3-only. A 1.2
  handshake is a config flag, not new code.

## The plan

1. **Patch + pin.** Clone txiki v26.6.0, fix A (request-line join) and B
   (mbedtls config), build with the existing CI recipe (setup.sh already
   knows how — Linux builds ship from our releases today; add macOS and
   Windows jobs), pin `TJS_VERSION` to the patched build everywhere.
2. **Upstream.** PR both fixes to saghul/txiki.js — A with the nc repro,
   B with the host table above. Small, uncontroversial patches.
3. **Delete the shim.** Once a fixed tjs is pinned on all three platforms,
   remove the fetch repair shim from bridge.js and the reachability notes it
   earned in README.md / docs/docs.html — the built-in fetch should just
   work. Keep the e2e check: the 8 amp feeds in `test/` lore (see
   TODO-verify.md entry) are a good canary, they cover both bugs plus
   redirect chains.

## While the shim exists — its contract (so nobody "fixes" it wrong)

- curl is consulted in exactly two cases: the URL (or a redirect hop) has a
  root path, or the native fetch REJECTED (wire-level failure, no response).
  A real HTTP response, error or not, is never second-guessed.
- Redirects are followed in the shim (`redirect: 'manual'` per hop, cap 20,
  303/301/302-POST downgrade to GET, 307/308 preserve method+body) so hops
  landing in a broken case divert individually.
- Bodies stream both ways (curl stdout → ReadableStream; string/Uint8Array
  request bodies via `--data-binary @-`). Stream/Request-object inputs and
  non-http(s) schemes bypass the shim entirely.
- Every redirect hop is re-validated as http(s) — a hostile `Location:
  file:///etc/passwd` throws instead of reaching curl (which would read it).
  curl is additionally pinned with `--proto =http,https` and the URL sits
  after `--` so it can never parse as a flag. Verified with a local
  redirect-to-file:// server.
- No curl on the machine → native behavior, bugs and all.
- Windows: every curl spawn (and the `curl --version` probe) goes through
  `launcher --run`, or each hop flashes a console window — a GUI-subsystem exe
  has no console for a console child to attach to. The shim runs OUTSIDE
  `createApp`, so it resolves the launcher itself (`readyHiddenArgv`, env
  `TINYJS_LAUNCHER` or beside the exe) instead of using the path createApp
  found; missing launcher → plain spawn, i.e. a flash, never a failed fetch.
- Nothing in this file's import graph may take a TOP-LEVEL await before the
  shim is installed: that makes bridge.js an async module, and an app's
  backend module body then runs first and gets the raw fetch (this was real —
  update.js's bundle probe did it, see CHANGELOG 0.30.0).
