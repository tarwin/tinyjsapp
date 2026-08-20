# scriptc spike — measured 2026-08-20, scriptc 0.0.33, macOS arm64

Can the backend run on [vercel-labs/scriptc](https://github.com/vercel-labs/scriptc)
(TS→native AOT via LLVM, `--dynamic` embeds quickjs-ng) instead of txiki?
Full findings: ../TODO-scriptc.md. This dir is the evidence.

**`./run-spike.sh`** — compiles `spike-backend.ts` to a ~460KB native binary
and drives the real `native/launcher-macos` with it: window opens, title set,
DOM mutated over the wire, launcher events received. `spike-transcript.txt`
is the overnight run's wire log. (An `nc` pair relays the launcher's Unix
socket to TCP — scriptc's net stack is TCP-only, see TODO.)

Probes (each is one measured claim):

- `fetch-rootpath.ts` — txiki Bug A analog: scriptc emits `GET / HTTP/1.1`,
  correctly (txiki: `GET //`).
- `fetch-tls12.ts` — txiki Bug B analog: TLS 1.2-only host (rss.art19.com)
  answers 200 in ~250ms (txiki: mbedtls handshake failure).
- `unixsock.ts` — the newline wire protocol over TCP loopback (works; the
  same code with a Unix socket path does not compile — no AF_UNIX).
- `spawn-stdin.ts` — DOES NOT COMPILE, on purpose: child stdin pipes and fd
  stdin are compile fences (`stdin takes "ignore" or "inherit"`). txiki's
  Bug C hangs at runtime instead; scriptc refuses at build time.
- `app-dyn.js` — plain JS as the ENTRY module: builds, but dynamic dispatch
  (`api[name]`) traps at runtime — your own module graph is always the
  static tier, `--dynamic` or not.
- `island-app.ts` + `fakeapp/` — the same plain JS embedded as an npm-style
  package: full JS semantics in the quickjs-ng island, works perfectly.
  This binary is also the one that cross-compiled to Linux musl and ran on
  Ubuntu 22.04 / Alpine containers, HTTPS included.
- `loop.ts` — perf: 20M-iter loop 294ms vs tjs 548ms; JSON stringify 3.2MB
  8ms vs 32ms.
- `amp-island.ts` + `tjs-stub.js` — amp's REAL backend (~150KB: main.js,
  meta, lookup, icy, cue — copy them from ../../tinyjsapp-examples/amp/src
  into node_modules/fakeamp beside tjs-stub.js as index.js) embedded whole
  in the island: module graph loads, 88 api methods visible across the
  boundary. 2.2s build, 1.86MB binary. Loaded, not yet exercised.
