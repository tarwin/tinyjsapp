# TODO — txiki.js runtime bugs

## Next: don't ship 26.11.0, wait for the release after it (2026-11-04)

**The 26.11.0 tag aborts on Linux.** After a response with h3 in `Alt-Svc`, lws
races QUIC on the next connect and closes the UDP socket while its uv_poll is
armed, with no event-lib op call (fd also closed twice, seen in strace). libuv
gets `EPOLL_CTL_ADD` → EBADF in `uv__epoll_ctl_flush` and calls abort(). macOS
isn't hit. bridge.js's `connection: close` makes it certain: every connect is
cold, so round 2 of parallel fetches to google.com or simplecast dies.

**Master at 4cf1ecc (2026-11-03) doesn't abort** (ebcf5c10 moved to lws 5.1;
which commit fixed it wasn't isolated) and also fixes the redirect-after-cancel
and fetch-after-abort rejections and redirect to `file://`. With
`connection: close`, it passes all of `test/fetch/check.js` on macOS. On an
ubuntu:22.04 arm64 container it passes too, except the DNS bug below.

New on master, report upstream:

- **A truncated DNS answer fails the whole lookup (cause proven).** When an A
  answer has arrived and the AAAA answer comes back with TC set, the truncation path
  in `async-dns-parse.c` does `responded &= ~rn; asked = 0`. The TCP retry only
  re-asks AAAA (`which = responded & 1`) and doesn't set `asked`, so `responded`
  ends up 3 against `asked` 0, `responded != asked` holds forever, and the query
  never completes. The client retries each second and rejects at 5 s, "dns lookup
  failed". Seen on Docker Desktop's resolver (192.168.65.7), which duplicates the
  records for `feed.proxypodcast.com`, `feed.articlesofinterest.club` and
  `rss.pdrl.fm` past 512 bytes. Deleting the `asked = 0` line in a scratch build
  made all three resolve; putting it back made them fail. Any resolver that sets TC
  on AAAA after answering A should hit this. Those three hosts resolve fine through
  systemd-resolved (Ubuntu 24.04 VM, 2026-11-04), whether or not it sets TC there.
- **A first lookup 3 s or more after the first fetch fails on macOS with a link-local
  resolver (send path proven, trigger timing not).** Not about streams, as first
  thought: a finished fetch to 127.0.0.1, a wait, then any new hostname: 0, 0.5 and
  1.5 s work, 3, 5 and 8 s reject at 5 s, every run, with or without
  `connection: close` or bridge.js. Later hostnames in that process fail too. An
  app doing anything else for 3 s before its first lookup is exposed, and it would
  look like the network being down. 26.6.0 and the 26.11.0 tag pass.
  This Mac's `State:/Network/Global/DNS` lists `fe80::aa42:a1ff:fe43:5908%en0` then
  `192.168.1.1`; lws's list has lost `%en0`, and without a scope a send fails with
  EHOSTUNREACH (python: same address, no scope "No route to host", scope en0 fine;
  dig with %en0 answers). lws logs "dns write failed". In `lws_async_dns_writeable`
  a broadsiding query goes to every server, but a failed write on its primary
  socket takes `goto qfail` and fails the query, whatever the others did. In a
  scratch build, returning instead when another socket is alive made the 5 s case
  pass on both hostnames. Open: why lookups under 3 s don't go this way (lws polls
  the server list every 3 s, but `LWS_ASYNCDNS_WATCH_MS=60000` changed nothing), and
  the scope should be kept as well, which wasn't tried (a test that dropped fe80 from
  `lws_plat_asyncdns_get_server` had no effect: the server stayed in use).
- **Bad first nameserver on Linux** (ubuntu:22.04 container, 2026-11-04, master):
  `nameserver fe80::1` first: fetch rejects "Connection failed" in 0 ms, even with a
  good second server. An unreachable `10.255.255.1` first: works but takes 9.4 s.
  Not compared with the tag.
- **Master's first response is about 1 s slower** (macOS: 1.25 s to github.com
  against 0.24 s on the tag). In the logs the query goes to fe80 then to 192.168.1.1
  a second later; dropping fe80 was the invalid test above, so unproven.

`check.js` missed the 3 s bug because it reaches its first real host in 0.49 s; it
now waits first.
With the query-stays-alive change (scratch build, which also had the truncation
fix), check.js goes from 13 failures to all passing on macOS.

Tag only, not master: repeating `https://www.cloudflare.com/` with long gaps once it
has learned h3 sometimes rejects "Network request failed" after 3 s (seen in check.js
twice and once in 6 repeats with 15-40 s gaps; master 0 of 6). Cause not looked at.

**What bridge.js does until then:** every fetch sends `connection: close`, so it
gets its own connection, as on 26.6.0. Master still needs this: pooled, an open
stream blocks its origin and a cold round of parallel fetches to simplecast loses
2 of 4. A caller who sends another `Connection` value gets the pool. Take it out
once those two pass pooled in check.js. cli.js doesn't load bridge.js, but only
fetches one at a time.

**Released on the 26.11.0 tag with these mitigations (decided 2026-11-06):**
- Linux builds with HTTP/3 off (`setup.sh`), to stop the abort. Take it out, and
  bump the cache key in release.yml, once a release has the fix.
- A GET or HEAD the runtime can't make goes through curl (`bridge.js`), for RSA-only
  servers and OS-trusted certificates. Other methods fail rather than send twice.
- Slow large h2 downloads stay as a known issue on macOS and Windows x64: their
  binaries come from upstream's releases, so the window can't be patched here.

**Master with `connection: close` loses about 1 in 5 fetches to
`feeds.megaphone.fm`** (Linux arm64, 2026-11-04): "Network request failed" in under
150 ms, 24/30 serial, gaps don't help. The tag with `connection: close`, master with
`keep-alive` and 26.6.0 all get 30/30. Cause not found, not tried on macOS or Windows,
and check.js doesn't cover it. Check this before pinning master.

**RSA-only servers can't connect, and it won't change by itself.** Five of amp's
stations (`wmse.streamguys1.com`, `s2.voscast.com`, `server.laradio.online`, 2 on
`mediaserv38.live-streams.nl`) only offer `TLS_RSA_*`: openssl with ECDHE/DHE or
TLS 1.3 gets nothing back. txiki's `src/mbedtls_config.h` undefines
`MBEDTLS_KEY_EXCHANGE_RSA_ENABLED`, so the handshake fails with the verify result
never set; lws decodes that 0xFFFFFFFF as every flag at once, hence the bogus
"certificate validity has expired". 26.6.0 refused them too; curl was what
reached them. A sixth (`mediaserv38…:8107`) refuses even openssl, so it's just down.

Nothing is committed to using master yet: mac and Windows x64 binaries come from
upstream's releases, so pinning a commit would mean building them ourselves.

## Earlier status

Status at **v26.11.0** (pinned 2026-11-04). Bugs A, B, C and D below are all
fixed in the runtime, and the curl half of the "fetch repair shim" is gone from
`runtime/bridge.js`. `bin/tjs run test/fetch/check.js` checks all of it on any
OS, against raw listeners and real hosts. Its `upstream:` lines report whether
the bugs below are still there.

**Don't follow the old plan's "delete the shim" all the way.** bridge.js still
follows redirects itself, because native fetch doesn't apply the rules:

## Still open upstream (worth PRs to saghul/txiki.js)

Not filed yet. Also on this list, details under "Next" at the top of this file:
the DNS bugs on master, and the misleading "certificate validity has expired"
on `TLS_RSA_*`-only servers (the verify result is never set; those ciphers being
compiled out is a separate question for upstream).

- **A cold round of parallel fetches to one h2 host loses requests.** 4 at once to
  `feeds.simplecast.com`, pooled: 2 of 4 reject with "Network request failed" on
  the tag and master. On master only the first round does; later rounds pass.
  `connection: close` avoids it. Cause not found.
- **Redirect to another scheme kills the process (fixed on master 4cf1ecc, nothing
  to report; still on the 26.11.0 tag).** `fetch(url)` where the
  server answers `Location: file:///etc/passwd` throws `Error: Connection
  failed` outside the returned promise: nothing can catch it, exit 1. The XHR
  underneath `open()`s the new URL with no scheme check. Why bridge.js and
  cli.js follow by hand. Same on 26.6.0.
- **Credentials cross origins.** `Authorization`, `Cookie` and
  `Proxy-Authorization` follow a redirect to any origin. Also a POST turned
  into a GET keeps its `Content-Type`. bridge.js handles both (#27).
- **HTTP/2 downloads are slow from far-away servers: lws's receive window.**
  26.11.0 turned h2 on, and master 4cf1ecc is the same. 20.6 MB from
  `feeds.simplecast.com` (197 ms away), alternating on one connection,
  2026-11-04: 26.6.0 and curl (h1 or h2) 4 s; 26.11.0 and master 37 s, a flat
  ~108 KB per round trip. Cause: `do_windows` in lws `http2.c` (`m = n + 65536`,
  then `lws_h2_update_peer_txcredit_thresh(..., m, m)`) keeps the window around
  64-130 KB, so distant hosts can't send faster. Same 37 s with
  `connection: close`. Sweeping that constant on master (2 runs each):
  64 KiB 37.6 s, 256 KiB 12.4 s, 1 MiB 6.1 s, 4 MiB 5.1 s, 16 MiB 5.0-5.4 s. At
  4 MiB check.js passes on macOS, a 8.5 MB GitHub archive goes 9.4 s → 3.0 s, and
  speed.cloudflare.com doesn't change. Measured on macOS only. Only that constant
  needs to move; raising `vinfo.http2_settings[4]` alone makes large h2 fetches
  fail. No JS-level way round it. Worth a PR: one line in txiki's carried lws
  patches, or in lws itself.

- **A streaming response blocks its whole origin on an HTTP/1.1 keep-alive
  server (new in 26.11.0, still on master 4cf1ecc).** bridge.js avoids it with
  `connection: close`. First measured on Windows (x64 tjs, emulated on
  arm64), against a raw local listener sending chunked keep-alive. While one
  fetch's endless body is being read, another fetch to that origin hangs (6 s
  limit hit, the stream keeps flowing). If the body is never read, every later
  fetch hangs. `abort()` or `reader.cancel()` frees it. 26.6.0 opened a new
  connection each time and passes all of this. Finite bodies left unread are
  fine; a slow finite download wasn't tried, nor h2 or `Connection: close`
  servers locally (SomaFM, which sends `Close`, isn't affected). Looks like the
  new connection pooling queueing on the one connection, not proven. Real host:
  `http://icecast.radiofrance.fr/fipjazz-hifi.aac` with the body left unread.

Smaller cleanup left: `http://localhost` now reaches a `::1`-only server on
macOS (26.6.0 got ECONNREFUSED) but NOT on Windows ("conn fail: wsaerrno 0",
measured 2026-11-04; `http://[::1]:port` works). Linux not seen. cli.js keeps
its second readiness probe and the scaffold its `--host 127.0.0.1`.

## What the curl fallback used to do besides A and B

It also caught every other wire-level failure, so a host that only curl could
reach worked by accident: a TLS-inspecting proxy whose CA is only in the OS
store, say (curl uses the system store on macOS and Windows; txiki uses its
embedded bundle, or `SSL_CERT_FILE` / `TJS_CA_BUNDLE`). Those now fail. cli.js
keeps one curl attempt for the update check (`latestViaCurl`, #67), since a
broken update check can't be fixed by updating.

Example apps: nib's Actions stage stdin in a temp file because of Bug C
(`nib/src/actions.js`, `startRun`). That can now be a plain pipe once nib builds
on this runtime.

# History: how these were found

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
there, now fixed). POST request bodies were the one path nothing had
exercised anywhere — now measured, and they found Bug D below: a small POST
body through the shim hung forever (fixed in the shim 2026-09-28). See
TODO-verify.md.

### Bug A — root-path URLs go out as `GET //`

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
- **Method-agnostic** (re-measured 2026-08-14, macOS arm64, same shipped
  `bin/tjs` v26.6.0): `POST http://127.0.0.1:8899/` goes out as
  `POST // HTTP/1.1` with an otherwise perfect request — correct
  `content-length`, body intact on the wire. So it really is the URL join,
  not anything method- or body-specific.
- **Do NOT verify this against `tjs.serve` — it masks the bug.** A txiki
  client posting to a txiki server reports `pathname === '/'`, because
  `tjs.serve` normalizes `//` back to `/` on the receiving side. Two of our
  own runtimes talking to each other look perfectly healthy while the wire
  carries `//`. Only a raw listener (the `nc` harness above) tells the
  truth. This nearly produced a "Bug A is fixed" conclusion on 2026-08-14.
- Where to look: txiki's fetch/XHR request-line construction — almost
  certainly a `'/' + path` join where path already starts with (or is) `/`.
  Likely a one-line fix.

### Bug B — TLS 1.2-only hosts never complete a handshake

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

### The plan

0. ~~**Unblock D first**~~ — **done 2026-09-28**: the shim stages bodies in
   a temp file now (see Bug D).
1. **Patch + pin.** Clone txiki v26.6.0, fix A (request-line join), B
   (mbedtls config) and C (the `uv_try_write` fast path, which retires D at
   the source), build with the existing CI recipe (setup.sh already knows
   how — Linux builds ship from our releases today; add macOS and Windows
   jobs), pin `TJS_VERSION` to the patched build everywhere.
2. **Upstream.** PR the fixes to saghul/txiki.js — A with the nc repro,
   B with the host table above, C with the `tr` repro (check against
   PR #1028 first). Small, uncontroversial patches.
3. **Delete the shim.** Once a fixed tjs is pinned on all three platforms,
   remove the fetch repair shim from bridge.js and the reachability notes it
   earned in README.md / docs/docs.html — the built-in fetch should just
   work. Keep the e2e check: the 8 amp feeds in `test/` lore (see
   TODO-verify.md entry) are a good canary, they cover both bugs plus
   redirect chains.

### While the shim existed — its contract (so nobody "fixes" it wrong)

- curl is consulted in exactly two cases: the URL (or a redirect hop) has a
  root path, or the native fetch REJECTED (wire-level failure, no response).
  A real HTTP response, error or not, is never second-guessed.
- Redirects are followed in the shim (`redirect: 'manual'` per hop, cap 20,
  303/301/302-POST downgrade to GET, 307/308 preserve method+body) so hops
  landing in a broken case divert individually.
- Bodies stream both ways (curl stdout → ReadableStream; string/Uint8Array
  request bodies via `--data-binary @<file>`, the file in a fresh 0700
  temp dir removed once curl exits). Stream/Request-object inputs and
  non-http(s) schemes bypass the shim entirely. **Never go back to `@-`:**
  that's a stdin pipe, i.e. Bug D, until a tjs with upstream #1028 is pinned.
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

### Bug C — small writes to a child's stdin are never delivered

> **Root cause found upstream (2026-07):** [saghul/txiki.js#1027] — the
> stdin sink misreads `tjs_stream_write`'s `true` ("`uv_try_write` took it
> all, inline") as "async write pending" and waits for a completion that
> never comes. Big writes take the queued path, which is why size looked like
> the variable. Fixed by [PR #1028] (merged 2026-07-12), but **no txiki tag
> contains it** — v26.6.0 (2026-06-22) was still the latest as of
> 2026-09-28, and our pinned tjs still hangs on the first `await
> w.write()`. Re-check on each new txiki release; bumping `TJS_VERSION` to
> one that has it retires this and makes the workarounds below optional.

[saghul/txiki.js#1027]: https://github.com/saghul/txiki.js/issues/1027
[PR #1028]: https://github.com/saghul/txiki.js/pull/1028

`tjs.spawn(argv, { stdin: 'pipe' })`, then a single small write through the
writer: the child receives nothing, and neither the `write()` nor the
`close()` promise ever settles. The same code with a large payload works.

```js
const p = tjs.spawn(['/usr/bin/tr', 'a-z', 'A-Z'], { stdin: 'pipe', stdout: 'pipe' });
const w = p.stdin.getWriter();
await w.write(enc.encode('abc def\n'));   // never resolves; child gets nothing
await w.write(enc.encode('abc def\n'.repeat(40000)));  // 320 KB: fine, and echoed back
```

Measured 2026-08-04 on the shipped `bin/tjs` (macOS arm64), with `wc`, `cat`
and `tr` alike, with and without `cwd`/`env`/`stderr: 'pipe'` — the payload
size is the only variable that matters. Not awaiting doesn't help: the bytes
never reach the child, so it sits on an open stdin and no EOF ever arrives.
Smells like a `uv_try_write` fast path that completes the write synchronously
and never runs the JS completion callback (the big case falls back to the
queued path and does).

Related, already noted in the README's gotchas: only the FIRST write to a
child's stdin was delivered at all (upstream fix
[PR #1028](https://github.com/saghul/txiki.js/pull/1028), untagged as of
writing). Worth checking whether #1028 also fixes this, and whether the
tagged release we pin has it, before anyone builds on the stdin pipe again.

- Real victim: nib's Actions (`nib/src/actions.js`, `stdin: "doc"` — a
  document piped through a formatter). It works around the bug entirely by
  staging stdin in a temp file and redirecting through a shell:
  `sh -c 'exec "$@" < "$0"' <tmpfile> <cmd> <args…>`, which keeps the argv
  intact and has no size limit. That workaround should stay until a fixed
  runtime is pinned — but if the pipe ever works, `startRun` is the one place
  to change.

### ~~Bug D — a SMALL POST body through the shim hangs forever~~ — fixed 2026-09-28

**Fixed** by the cheap fix below: `curlFetch` writes the body to a file in a
fresh 0700 temp dir, passes `--data-binary @<file>`, spawns curl with
`stdin: 'ignore'`, and removes the dir when curl exits. Verified on macOS
arm64 against a raw Python listener: string and binary bodies to a root-path
URL both echo intact, no temp dirs left behind. Windows and Linux: see
TODO-verify.md. The write-up below is kept as history.

Bug C is not confined to apps that spawn things. **The fetch repair shim
itself pipes request bodies to curl over `--data-binary @-`** — i.e. the
exact broken stdin path — so any POST small enough to hit the fast path
never completes. Found 2026-08-14 by reasoning from C's contract to the
shim's, then reproduced first try (macOS arm64, shipped `bin/tjs` v26.6.0):

```
POST http://127.0.0.1:8913/  body = 7 bytes       → never settles (killed at 6s)
POST http://127.0.0.1:8913/  body = 320 KB        → completes, 200, echo correct
```

The cruel detail: **the request succeeds.** The listener logs
`POST / bodylen=7` and replies. curl sent the body and got a response — only
the `write()`/`close()` promise inside the shim never settles, so the shim
never advances to reading curl's stdout and the caller's `await fetch(...)`
hangs on a request the server already answered. No error, no timeout, no
log. Symptomatically indistinguishable from a dead network.

Reach: any POST whose URL (or any redirect hop) has a root path, or whose
host is TLS 1.2-only — those are the only two cases that divert to curl.
Small JSON payloads to an API root (`POST https://api.example.com/`) are
precisely the shape that hits it. Nothing in amp POSTs, which is why five
weeks of three-OS shim verification never touched it.

- **Fix properly** by fixing C (or pinning a tjs with upstream #1028 in it).
- **Fix now, cheaply**, if C outlives this: give the shim nib's workaround —
  stage the body in a temp file and hand curl `--data-binary @<file>`
  instead of `@-`. That deletes the stdin pipe from the shim entirely, has
  no size limit, and is a smaller change than it sounds (one branch in the
  body-handling path). Worth doing regardless — the shim has no business
  depending on a runtime bug's fast path.
- Repro harness (two files, `tjs run` each; peer may be `tjs.serve` here —
  we're testing body delivery, not the request line Bug A mangles):

  ```js
  // sink.js
  const s = await tjs.serve({ port: 8913, async fetch(req) {
    const b = await req.text();
    console.log('SERVER saw', req.method, 'bodylen=', b.length);
    return new Response('got ' + b.length);
  }});

  // shim-post.js  — start sink.js first
  await import('<repo>/runtime/bridge.js');        // installs the shim
  const body = JSON.stringify({ a: 1 });           // 7 bytes -> hangs
  // const body = JSON.stringify({ a: 'x'.repeat(320*1024) });  // -> passes
  setTimeout(() => { console.log('*** STILL HANGING ***'); tjs.exit(2); }, 6000);
  const r = await fetch('http://127.0.0.1:8913/',  // ROOT PATH -> diverts to curl
    { method: 'POST', body, headers: { 'content-type': 'application/json' } });
  console.log('completed:', r.status, await r.text());
  ```

  The tell is `SERVER saw POST bodylen= 7` printing while the client still
  hangs — that's what makes it Bug C and not a transport failure.
- Only measured on macOS arm64. The stdin path is libuv-generic so all three
  OSes are presumed affected, but that is inference, not observation —
  TODO-verify.md.

# Runtime surface: what txiki actually HAS (audited 2026-08-14)

Audited while sizing up whether a *wrapped-agent-backend* app could run its
whole backend on tjs (prompted by taking KiroCrew apart — an Electron shell
whose only job is `loadURL('http://localhost:PORT?token=…')` in front of a
590 MB bundled Python gateway that serves a Vite SPA). Four things were
assumed missing that are present and working — writing them down so nobody
re-derives an architecture around absences that aren't there. All probed on
the shipped `bin/tjs` v26.6.0, macOS arm64 (26.11.0 adds `MessageChannel`,
`BroadcastChannel` and `EventSource`, and reworks the `tjs:ffi` API):

| Surface | State | Proof |
|---|---|---|
| `tjs:sqlite` | works | `Database(':memory:')`, `prepare().all()` round-trip |
| `tjs:ffi` | works | `Lib('libSystem.B.dylib')` → `strlen("hello world")` = 11 |
| `Worker` (global) | works, real threads | 20M-iteration loop in a worker; main loop ticked 80× at 10 ms during it |
| `tjs.serve` | works | fetch-style handler; GET + POST, bodies to 2 MB |
| `crypto.subtle` | present | WebCrypto global, not `tjs:crypto` |

Also present: `SharedArrayBuffer`, `Atomics`, `WebAssembly`, `ReadableStream`,
`tjs:hashing`, `tjs:path`, `tjs:uuid`, `tjs:posix-socket`. Genuinely absent:
`MessageChannel`, `BroadcastChannel`, `tjs:worker` (the *module* — the
`Worker` global is the supported spelling), `tjs:signal`, `tjs:engine`.

Perf, same machine, same workload (an 8 MB transcript-shaped object plus a
20M-iteration interpreted loop) — QuickJS is in CPython's class, and beats
it on interpreted code:

| | CPython 3.12 | tjs (QuickJS) | node (V8) |
|---|---|---|---|
| JSON stringify 8 MB | 15 ms | 38 ms | 12 ms |
| JSON parse 8 MB | 15 ms | 20 ms | 9 ms |
| tight loop, 20M iters | 1553 ms | **876 ms** | 126 ms |

### What this means for the KiroCrew shape

An agent backend is IO-bound — it waits seconds on model round-trips, and
its genuinely hot work (embedding inference, vector search, PDF raster) is
already native code being marshalled. So the interpreter is not the ceiling;
`tjs:ffi` reaches the same llama.cpp/pdfium `.so`s Python was calling, and
`tjs:sqlite` covers the session/history store. The ceiling is the four bugs
in this file, and **Bug D is the one that matters most for this shape**: an
agent backend is nothing *but* small JSON POSTs. A backend built on tjs
today would hang the first time a model endpoint sat behind a root-path URL
or a TLS 1.2 host — silently, on a request the server had already answered.

Fixing A, B and D is what turns "viable in principle" into "safe to build
on". None of that argues for rewriting KiroCrew's own 502k lines of Python;
it argues that the *next* app of that shape doesn't need Python at all.
