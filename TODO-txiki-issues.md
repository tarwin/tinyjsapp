# txiki.js issues to post

Drafts for <https://github.com/saghul/txiki.js/issues>, to check and post by hand.
Background and raw numbers: TODO-txiki.md.

**Checked 2026-11-06.** Upstream's latest is still release v26.11.0 and master
4cf1ecc, and none of these are posted. tinyjs v0.71.0 ships 26.11.0 with
`connection: close`, a curl fallback, and HTTP/3 off on Linux. Look at upstream's
master and issues again before posting, since they move quickly.

Each script was run with plain `tjs` on macOS arm64 against 26.6.0, the 26.11.0 tag
and master, without bridge.js, except where the draft or the checklist says otherwise.

| Order | Issue | 26.6.0 | 26.11.0 | master | Post? |
|---|---|---|---|---|---|
| 1 | Linux abort, please release | ok | **aborts** | ok | yes, after the check below |
| 2 | Open stream blocks its origin | ok | **hangs** | **hangs** | yes |
| 3 | Parallel h2 fetches rejected | ok | **lost** | **lost** | yes |
| 4 | Slow h2 downloads | 4.6 s | **37 s** | **38 s** | yes |
| 5 | Credentials sent across origins | **leaks** | **leaks** | **leaks** | privately |
| 6 | DNS lookups fail after a wait (macOS) | ok | ok | **fails** | if asked, see issue 1's note |

## Before posting

- [ ] Issue 1: run its script on the Ubuntu VM with a plain 26.11.0 tag build (HTTP/3
  on), then master. Nobody has run this exact script: it's a shorter form of the
  parallel rounds that dumped core in 10 of 10 runs. Don't use tinyjs v0.71.0's
  `tjs`, which has HTTP/3 off.
- [ ] Issue 1: put the link in if issue 6 gets posted.
- [ ] Issue 5: use the repo's private vulnerability reporting, not a public issue.

---

## Issue 1

**Title:** 26.11.0 aborts on Linux after a server advertises h3; fixed on master, could there be a release?

````markdown
On Linux, 26.11.0 calls `abort()` (exit 134) when it makes a new connection after a response with h3 in `Alt-Svc`. It kills the whole process, so the error can't be caught from JS.

```js
const url = 'https://www.google.com/robots.txt';
for (let round = 1; round <= 3; round++) {
  await Promise.all([1, 2, 3, 4].map(() =>
    fetch(url, { headers: { connection: 'close' } }).then((r) => r.arrayBuffer())));
  console.log('round', round, 'ok');
}
tjs.exit(0);
```

`connection: close` makes it certain, because every connect is cold. I also saw it with parallel fetches to `feeds.simplecast.com`.

| Build | Result |
|---|---|
| Ubuntu 24.04 arm64, 26.11.0 | aborts (exit 134) |
| ubuntu:22.04 arm64 container, 26.11.0 | aborts (exit 134) |
| Ubuntu 24.04 arm64, 26.11.0 with `LWS_WITH_HTTP3` and `LWS_ROLE_QUIC` off | passes |
| Ubuntu 24.04 arm64, master 4cf1ecc | passes |
| macOS arm64, 26.11.0 | passes |
| Windows, x64 26.11.0 under emulation on arm64 | passes |

From strace and debugging: lws races QUIC on the next connect, then closes the UDP socket while its `uv_poll` is still armed, with no event-lib call to stop it. The fd is also closed twice. libuv then gets EBADF from `EPOLL_CTL_ADD` in `uv__epoll_ctl_flush` and aborts.

I didn't isolate which commit fixed it on master. The move to lws 5.1 (ebcf5c1) is my guess.

Two other 26.11.0 bugs are also fixed on master:
- A fetch after `reader.cancel()` or an aborted fetch rejects with "Network request failed".
- A redirect to `file:///…` throws outside the returned promise, so it can't be caught and the process exits.

Could these go out in a release? I've shipped 26.11.0 with HTTP/3 turned off on Linux to avoid this, and would like to turn it back on.

Note: master has a new DNS failure on macOS that 26.11.0 doesn't have. If the first hostname lookup comes 3 s or more after the first fetch, it rejects after 5 s with "dns lookup failed". My first resolver is link-local (`fe80::…%en0`). It first appears at ebcf5c1 (the commit before it passes). I can open a separate issue with a repro if that helps.
````

---

## Issue 2

**Title:** `fetch`: an open streaming response blocks later requests to the same origin (26.11.0, master)

````markdown
While a streaming response is still open, a second fetch to the same origin never settles. 26.6.0 is fine. A different origin is unaffected, and sending `connection: close` on both requests avoids it, so it looks like connection pooling.

This affects anything holding a long response open, like a radio stream or event stream, while making other requests to that host.

```js
const enc = new TextEncoder();
const listener = await tjs.listen('tcp', '127.0.0.1', 0);
const info = await listener.opened;
(async () => {
  const accept = info.readable.getReader();
  for (;;) {
    const { value: conn, done } = await accept.read();
    if (done) return;
    (async () => {
      const { readable, writable } = await conn.opened;
      const r = readable.getReader(), w = writable.getWriter();
      let head = '';
      while (!head.includes('\r\n\r\n')) head += new TextDecoder().decode((await r.read()).value);
      if (head.startsWith('GET /stream')) // first chunk, then stays open
        await w.write(enc.encode('HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n5\r\nhello\r\n'));
      else
        await w.write(enc.encode('HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nok'));
    })().catch(() => {});
  }
})();
const base = `http://127.0.0.1:${info.localPort}`;
const res = await fetch(base + '/stream');
await res.body.getReader().read();
console.log('stream open');
const second = fetch(base + '/plain').then((r) => r.text());
const limit = new Promise((_, no) => setTimeout(() => no(new Error('still pending after 8 s')), 8000));
console.log('second fetch:', await Promise.race([second, limit]).catch((e) => e.message));
tjs.exit(0);
```

- 26.6.0: `second fetch: ok`
- 26.11.0 and master: `second fetch: still pending after 8 s`

I'd expect the pool to open a new connection when the existing one is busy and can't multiplex. Tested on macOS arm64.
````

---

## Issue 3

**Title:** `fetch`: parallel and repeat requests to an h2 host reject with "Network request failed" (26.11.0, master)

````markdown
Four parallel fetches to one h2 host lose two of them. 26.6.0 gets all four, and `connection: close` avoids it.

```js
const url = 'https://feeds.simplecast.com/54nAGcIl'; // about 20 MB
const results = await Promise.allSettled(
  [1, 2, 3, 4].map(() => fetch(url).then((r) => r.arrayBuffer())));
console.log(results.map((r) => r.status === 'fulfilled' ? r.value.byteLength : r.reason.message));
tjs.exit(0);
```

- 26.6.0: `[ 20611248, 20611248, 20611248, 20611248 ]`
- 26.11.0 and master: `[ 20611248, 'Network request failed', 'Network request failed', 20611248 ]`

In three rounds in one process, 26.11.0 lost two in every round. Master lost two in round 1 only, then took about 63 s per round, compared with 15 to 25 s on 26.6.0.

It may be related: on 26.11.0, one at a time, repeat pooled fetches to `https://www.google.com/robots.txt` also fail, after 5 or 10 s:
- macOS arm64: 2 of 8 failed.
- Ubuntu 24.04 arm64, built with HTTP/3 off: 4 of 7 failed, so this one isn't QUIC.

YouTube and Megaphone were fine in the same runs.

I haven't found the cause.
````

---

## Issue 4

**Title:** HTTP/2 downloads are about 8x slower from distant servers: lws caps the receive window

````markdown
Since h2 was turned on in 26.11.0, large downloads from distant servers are limited to about 108 KB per round trip.

20.6 MB from `https://feeds.simplecast.com/54nAGcIl`, about 200 ms away, on macOS arm64:

| Client | Time |
|---|---|
| curl (h2) | 5.0 s |
| txiki 26.6.0 | 4.6 s |
| txiki 26.11.0 | 37.2 s |
| txiki master 4cf1ecc | 37.7 s |

The cause is in `do_windows` in lws `lib/sansio/h2/http2.c` (line 3922 at 5f39aa4): `m = n + 65536` keeps the window around 64 to 130 KB. Changing only that constant on master (2 runs each):

| Constant | Time |
|---|---|
| 64 KiB (current) | 37.6 s |
| 256 KiB | 12.4 s |
| 1 MiB | 6.1 s |
| 4 MiB | 5.1 s |
| 16 MiB | 5.0 to 5.4 s |

At 4 MiB, an 8.5 MB GitHub archive went from 9.4 s to 3.0 s, and speed.cloudflare.com didn't change. Raising `vinfo.http2_settings[4]` alone isn't enough: it makes large h2 fetches fail.

There's no way round this from JS. A larger constant could be a patch in `patches/` or a change in lws. Happy to send a PR for whichever you prefer.
````

---

## Issue 5: report privately

Use GitHub's "Report a vulnerability" on the repo; private reporting is enabled.

**Title:** `fetch` sends Authorization and Cookie to another origin on a redirect

````markdown
`fetch` follows a redirect to another origin and sends the original `Authorization` and `Cookie` headers with it. The fetch spec drops `Authorization` there, and browsers don't let a page set `Cookie` at all. A server, or anyone who can make it redirect, can collect the caller's credentials.

```js
const enc = new TextEncoder(), dec = new TextDecoder();
async function serve(reply) {
  const info = await (await tjs.listen('tcp', '127.0.0.1', 0)).opened;
  (async () => {
    const accept = info.readable.getReader();
    for (;;) {
      const { value: conn, done } = await accept.read();
      if (done) return;
      (async () => {
        const { readable, writable } = await conn.opened;
        const head = dec.decode((await readable.getReader().read()).value);
        const w = writable.getWriter();
        await w.write(enc.encode(reply(head)));
        await w.close();
      })().catch(() => {});
    }
  })();
  return info.localPort;
}
let seen = '';
const other = await serve((head) => { seen = head; return 'HTTP/1.1 200 OK\r\ncontent-length: 0\r\nconnection: close\r\n\r\n'; });
const first = await serve(() => `HTTP/1.1 302 Found\r\nlocation: http://127.0.0.1:${other}/x\r\ncontent-length: 0\r\nconnection: close\r\n\r\n`);
await fetch(`http://127.0.0.1:${first}/`, { headers: { authorization: 'Bearer secret', cookie: 'a=b' } });
console.log('sent to the other origin:', seen.split('\r\n').filter((l) => /^(authorization|cookie):/i.test(l)));
tjs.exit(0);
```

26.6.0, 26.11.0 and master 4cf1ecc all print:

```
sent to the other origin: [ 'authorization: Bearer secret', 'cookie: a=b' ]
```

The two ports count as two origins here. I'd expect both headers to be gone, and `Proxy-Authorization` too.

tinyjs works around this by following redirects itself, so I don't need this urgently. A smaller, non-security point in the same code: a POST turned into a GET by a redirect keeps its `Content-Type`.
````

---

## Issue 6: only if they ask

Issue 1's note covers this. Post it if they want the detail, or before the next
release if it's still on master.

**Title:** `fetch`: DNS lookups fail if the first lookup comes a few seconds after the first fetch (macOS, master)

````markdown
On master (4cf1ecc), on macOS, if a process does a fetch to an IP address, waits a few seconds and then fetches a hostname, the lookup rejects after 5 s. Later hostnames in that process fail too. 26.6.0 and 26.11.0 pass. This looks like a regression that hasn't been released yet.

```js
const listener = await tjs.listen('tcp', '127.0.0.1', 0);
const info = await listener.opened;
(async () => {
  const accept = info.readable.getReader();
  for (;;) {
    const { value: conn, done } = await accept.read();
    if (done) return;
    const { readable, writable } = await conn.opened;
    await readable.getReader().read();
    const w = writable.getWriter();
    await w.write(new TextEncoder().encode('HTTP/1.1 200 OK\r\ncontent-length: 0\r\nconnection: close\r\n\r\n'));
    await w.close();
  }
})();
await (await fetch(`http://127.0.0.1:${info.localPort}/`)).arrayBuffer();
await new Promise((r) => setTimeout(r, 5000));
const t = Date.now();
try {
  const r = await fetch('https://github.com/robots.txt');
  console.log(r.status, Date.now() - t, 'ms');
} catch (e) {
  console.log('FAILED:', e.message, Date.now() - t, 'ms');
}
tjs.exit(0);
```

- 26.11.0: `200 247 ms`
- master: `FAILED: Network request failed: dns lookup failed 5014 ms`

Waits of 0, 0.5 and 1.5 s work. Waits of 3, 5 and 8 s fail. It doesn't happen on Ubuntu with systemd-resolved.

My Mac's first resolver is link-local, with a working one second:

```
nameserver[0] : fe80::…%en0
nameserver[1] : 192.168.1.1
```

It first appears at ebcf5c1 (deps: bump libwebsockets v5.0-stable → v5.1.0): 5a3a56f just before it passes, and ebcf5c1 fails. In lws it's already there at 371e418e3 (2026-10-19). I couldn't bisect further because older lws commits don't build against this txiki.

What I found:
- lws's server list has lost the `%en0` scope, so sends to that address fail with EHOSTUNREACH (logged as "dns write failed").
- A query goes to every server, but in `lws_async_dns_writeable` (`lib/system/async-dns/async-dns.c`, lws 5f39aa4) a failed write on its primary socket does `goto qfail`, failing the query whatever the other servers did.
- In a scratch build, returning instead of failing while another socket was still alive made this case pass.

Not explained: why lookups under about 3 s don't fail this way. I also didn't try keeping the scope, which is probably needed as well.
````

---

## Not posting yet

- **Slow repeat fetches on Windows after a host offers h3:** about 2.4 s each with
  `connection: close`, against about 0.5 s on 26.6.0. Only measured on an x64 binary
  emulated on an arm64 VM, and not seen on macOS or Linux. Try real x64 hardware or a
  native arm64 build first, since it may be the VM. Numbers in TODO-txiki.md.
- **Truncated DNS answers fail the lookup (master):** `q->asked = 0` in lws
  `async-dns-parse.c` (line 1084 at 5f39aa4) stops the query ever completing. Deleting
  the line fixed it, but it's only been seen through Docker Desktop's resolver. A
  comment on issue 6 would do.
- **`feeds.megaphone.fm` on master with `connection: close`:** about 1 fetch in 5
  failed on Linux arm64, with no cause found and 30/30 on macOS. Check it again if a
  release comes from master.
- **Misleading error for RSA-only servers:** "The certificate validity has expired",
  because lws decodes a verify result that was never set. A small report, whenever.
  Compiling out `TLS_RSA_*` looks deliberate, so it isn't a bug in itself.
- **Bad first nameserver on Linux, and master's first response being about 1 s
  slower:** neither is compared with the tag or explained.

## Not bugs, or already fixed

- **`new Request` POST bodies** don't go out as `[object Blob]`. They're valid chunked,
  and an earlier note here was wrong because our test server couldn't read chunked.
- **Redirect to `file://` killing the process, and fetch failing after a cancel or
  abort:** fixed on master, and mentioned in issue 1.
- **Windows arm64 releases:** already upstream's #1171.

---

## libuv issue

For <https://github.com/libuv/libuv/issues>, not txiki. It's why built Windows apps
did nothing when double-clicked in tinyjs 0.71.0; 0.71.1 works around it with
`stdin: 'ignore'` (2b73d2f).

**Checked 2026-11-06:** v1.x head 9e40720a0c still has this code, and searching
libuv's issues found no report of it.

- [ ] Build the repro against v1.53.0, launch it from Explorer, and fill in the
  `TODO` row. **This hasn't been run:** the v1.52.1 row and the
  `UpdateProcThreadAttribute` result were measured, but v1.53.0's failure so far is
  only seen through txiki 26.11.0.
- [ ] Check that the error is `UNKNOWN`, and the suggested fix hasn't been tried, so
  reword it if the run disagrees.

**Title:** win: uv_spawn fails with UV_UNKNOWN when no stdio handle is valid (regression in 1.53.0)

````markdown
* **Version**: 1.53.0 (and v1.x at 9e40720a0c, by reading the code)
* **Platform**: Windows 11 Pro 10.0.26200, x64

Since #5100, `uv_spawn` fails with `UV_UNKNOWN` if the parent has no valid std handles and stdin, stdout and stderr are all `UV_INHERIT_FD`. A GUI-subsystem program started from Explorer is in exactly that state, so it can't spawn a child this way. The same call worked in 1.52.1.

`uv__stdio_create` deliberately tolerates invalid fds 0-2 and marks those slots `INVALID_HANDLE_VALUE`. `uv_spawn` then leaves them out of the handle list, so the list is empty and it calls:

```c
UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                          handle_list, 0 * sizeof(HANDLE), NULL, NULL);
```

Windows rejects a zero-length list with `ERROR_BAD_LENGTH` (24). I checked that call on its own: a zero-length list fails with 24, and a one-handle list succeeds. `uv_translate_sys_error` has no case for 24, so it comes back as `UV_UNKNOWN`.

Setting any one stream to `UV_IGNORE` avoids it, because libuv opens a NUL handle for that stream and the list is no longer empty.

### Repro

Build as a GUI program and start it from Explorer (a double-click, or `explorer.exe repro.exe`). It logs to `%TEMP%\uv-spawn-repro.txt`, since it has no console.

```c
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

int main(void) {
  char log[1024];
  snprintf(log, sizeof log, "%s/uv-spawn-repro.txt", getenv("TEMP"));
  FILE* f = fopen(log, "a");

  char* args[] = { "cmd.exe", "/c", "exit", NULL };
  uv_stdio_container_t stdio[3];
  for (int i = 0; i < 3; i++) {
    stdio[i].flags = UV_INHERIT_FD;
    stdio[i].data.fd = i;
  }
  uv_process_options_t opts = { 0 };
  opts.file = args[0];
  opts.args = args;
  opts.stdio = stdio;
  opts.stdio_count = 3;

  uv_process_t p;
  int r = uv_spawn(uv_default_loop(), &p, &opts);
  fprintf(f, "%s uv_spawn: %d (%s)\n", uv_version_string(), r, r ? uv_err_name(r) : "ok");
  fclose(f);
  return 0;
}
```

```
gcc -mwindows -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -Iinclude -Isrc repro.c src/*.c src/win/*.c \
  -lws2_32 -liphlpapi -luserenv -lpsapi -ldbghelp -lole32 -lshell32 -luuid -lsynchronization -o repro.exe
```

| libuv | Result from Explorer |
|---|---|
| 1.52.1 | `uv_spawn: 0 (ok)` |
| 1.53.0 | TODO |

I found this through txiki.js 26.11.0, whose bundled libuv includes #5100: `tjs.spawn` throws `UNKNOWN` in a double-clicked build when all three streams are inherited, and in none of the other stdio combinations I tried. It's easy to miss, because anything run from a terminal has handles.

### Possible fix

Skip the attribute when the list is empty. `CreateProcessW` would also need `bInheritHandles` to be false in that case, because it's currently always 1, and without a list the child would inherit every inheritable handle, which is the leak #5100 closed. So roughly `bInheritHandles = n > 0`. I haven't tested this.
````
