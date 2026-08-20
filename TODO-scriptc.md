# TODO — scriptc as the backend runtime (explored 2026-08-20, parked)

Question asked: can [vercel-labs/scriptc](https://github.com/vercel-labs/scriptc)
replace txiki/QuickJS on the backend? Everything here was MEASURED on
scriptc 0.0.33, macOS arm64 (plus Linux containers where stated) — not read
off the README. Evidence, probes, and a working demo live in
`spike-scriptc/` (`./run-spike.sh` opens a real launcher-macos window driven
by a scriptc-compiled backend). Report artifact from the session:
https://claude.ai/code/artifact/e633e509-6e18-40e2-a10a-b264a0a45d8b
Status: PARKED — revisit checklist at the bottom.

## What scriptc is (and isn't)

NOT another JS engine. A TS→native AOT compiler: TypeScript frontend, LLVM
IR, clang link, its own C runtime (~40 files, vendored curl + mbedtls,
kqueue/epoll/wsapoll loops). Plain-JS / `any` code runs via `--dynamic`,
which embeds quickjs-ng (~620KB) as an "island" inside the binary; island
values cross to static code by validated copy. Apache-2.0, self-declared
experimental, moving fast. Node 24+ and clang are needed at BUILD time
only; produced binaries are self-contained. Docs are unusually honest
(their limitations page: "Honesty is the product").

The hard boundary that shapes everything: **your own module graph ALWAYS
compiles through the strict static tier** — `--dynamic` does not relax it
(measured: a plain-JS entry with `api[name]` dispatch builds, then traps
SC1090 at runtime). The island runs (a) npm-resolved packages embedded at
build time and (b) `any`-typed expressions. So "runtime for arbitrary app
JS" == "put the app in an island package".

## Headline results (all measured)

- **The spike works.** A 120-line TS backend (460KB binary, 1.7s warm
  compile) spawned the stock `native/launcher-macos`, received the full
  event stream (SYS/WINSTATE/NAV/CALL — origin still the last element of
  the CALL array), set TITLE, mutated the page via EVAL, QUIT cleanly.
  Wire log: `spike-scriptc/spike-transcript.txt`.
- **All four txiki bugs (TODO-txiki.md) are absent.**
  - Bug A analog: raw listener sees `GET / HTTP/1.1` — no `//` mangling.
  - Bug B analog: rss.art19.com (TLS 1.2-only Fastly) answers 200 in
    254ms, full 1.6MB body — their mbedtls is configured right.
  - Bug C/D territory: child stdin pipes don't EXIST (compile fence, see
    gaps) — a build error instead of txiki's silent runtime hang.
  - The entire fetch repair shim in bridge.js would be dead code.
- **amp's real backend loads whole in the island.** main.js + meta/lookup/
  icy/cue (~150KB, top-level `await import` included) embedded behind a
  20-line `tjs` stub — module graph resolved, 88 api methods visible across
  the boundary. 2.2s build, 1.86MB binary. Load ≠ run: methods not yet
  exercised against a live bridge. (`spike-scriptc/probes/amp-island.ts`)
- **The Linux glibc nightmare evaporates (backend half).**
  `SCRIPTC_CC=zigcc SCRIPTC_TARGET=x86_64-linux-musl` (zig via brew)
  produced a statically-linked ELF from the Mac in 39s — island included —
  that ran correctly on Ubuntu 22.04 (glibc 2.35) AND Alpine containers,
  HTTPS fetch included. musl = libc compiled INTO the binary; no glibc
  floor exists at all, so no ubuntu-22.04 runner archaeology and no verify
  step — for the backend. The launcher stays C++/GTK, dynamically linked,
  and keeps its floor regardless. Note: the gnu targets do NOT clear our
  floor — `x86_64-linux-gnu.2.35` fails on `arc4random_buf` (glibc 2.36+);
  musl is the path (or a one-line getrandom fallback upstreamed).
  Caveats: fetch needs the distro ca-certificates bundle (bare containers
  lack it, real desktops have it); only x86_64 was container-tested;
  musl's DNS/malloc edge cases are TODO-verify.md material if this ships.

## Numbers

| | scriptc static | tjs (QuickJS) | node (V8, from TODO-txiki) |
|---|---|---|---|
| 20M-iter interpreted loop | 294ms | 548ms | ~126ms |
| JSON.stringify 3.2MB | 8ms | 32ms | — |
| JSON.parse same | 9ms | 14ms | — |
| startup | ~3ms | ~7ms | — |
| hello binary | 397KB | 5.9MB (tjs) | — |
| with island | ~1.8MB | — | — |

**Why only ~2× over an interpreter when it's "native":** the compiled code
still executes JS semantics. Numbers are f64 everywhere ("integer inference
… roadmap, not shipped" — their words), arrays are bounds-checked,
memory is reference-counted. AOT removes bytecode dispatch (~half an
interpreter's cost on arithmetic) and keeps the rest. V8 beats both
because JIT integer specialization runs the loop on int32 — that's why
node's 126ms beats scriptc's native 294ms. Mental model: **scriptc today ≈
QuickJS with the interpreter loop compiled away, not C.** The 4× JSON win
is C-vs-C where scriptc's runtime is simply better. For tinyjs this mostly
doesn't matter — the backend is IO-bound; the real wins are the correct
fetch/TLS stack, size, and the musl build.

## Parity ledger vs txiki's surface (checked against their source, not docs)

| txiki has | scriptc static tier |
|---|---|
| fetch | ✓ better (bugs absent; vendored curl+mbedtls) |
| Console, timers | ✓ |
| TCP | ✓ node:net |
| UDP | ✓ node:dgram (real impl, scr_dgram.c) |
| **Unix sockets** | ✗ — scr_net.c is AF_INET/6 only; THE structural blocker |
| HTTP server | ✓ node:http + https + http2 + tls |
| WebSocket server | ✓ upgrade handover + RFC6455 accept implemented |
| **WebSocket client** | ✗ no global; island npm `ws` plausible, untested |
| File IO | ✓ fs callbacks+promises, fs.watch incl. Linux |
| child processes | ✓ minus stdin pipes (`ignore`/`inherit` only — fence) |
| signals | partial: process.on SIGINT/SIGTERM only |
| crypto.subtle | ✗ (txiki has WebCrypto) |
| node:crypto-ish | subset: randomUUID, randomBytes, createHash sha1/sha256 |
| **Worker threads** | ✗ static (declared, no lowering); island worker_threads shim untested |
| tjs:path / uuid / zlib | ✓ node:path, randomUUID, node:zlib |
| tjs:hashing | subset (no md5) |
| **tjs:sqlite** | ✗ — node:sqlite explicitly refused today |
| **tjs:ffi** | different shape: manifest-declared C ABI, LINK-time (no dlopen), formats 1–5, NO pointer/string returns |
| SharedArrayBuffer / WebAssembly | ✗ — no engine in static tier; island is quickjs-ng (no wasm). Webview wasm unaffected |

### Adding SQLite, if needed (~600KB–1.2MB)

1. **Upstream ask, cheapest**: Node 22+ ships `node:sqlite` (DatabaseSync);
   scriptc mirrors Node's surface, refuses it today "with reasons", and
   already declares an optional `process.versions.sqlite` slot — it's on
   their radar. File an issue.
2. **FFI + amalgamation, works today**: raw sqlite3 API can't bind (FFI has
   no pointer/string RETURNS by design). Pattern: ~150-line wrapper.c —
   numeric handles for db/stmt, row values delivered INTO JS via the
   format-3/4 copy-in cstring/byte-span callback params. Link sqlite3.c
   statically (works for musl target too). Amalgamation -Os ≈ 600–800KB.
3. System libsqlite3 (macOS only, ~0KB) — not worth the platform split.

tiny.store is plain JSON files — unaffected. Grep example apps for actual
tjs:sqlite use before spending any of this.

## The architecture that would actually work

- **bridge stays ONE source.** bridge.js touches only the `tjs` global
  (~20 members; zero `tjs:` module imports — verified) plus web APIs. So
  the whole backend — bridge.js AND the app's main.js, unmodified — can
  ride the island behind a small `tjs`-compat shim mapping tjs.* onto
  node shims. No fork, no rewrite, both runtimes testable side by side.
- Later/optional: bridge rewritten as static TS for the native-speed tier
  (the spike proves its runtime surface — spawn, fs, fetch, wire — is all
  there). Apps opting into strict TS compile static too. The alpha
  de-risks this; it doesn't require it.
- `tinyjs build` would emit ONE self-contained backend binary per app
  (~2MB with island) instead of tjs (5.9MB) + bridge.js + sources.

## Optional "scriptc-alpha" plan (~1.5–2 weeks to a macOS dev-mode alpha)

Integration surface is small: `tinyjs dev` generates one entry file and
spawns `tjs run entry` at ONE site (cli.js:805 area). Pieces:

1. **Transport** (the real blocker — no AF_UNIX). Either:
   (a) teach launcher-macos a `tcp:PORT:TOKEN` sockPath variant — ~1 day C,
   bind 127.0.0.1, token checked on first line. SECURITY-ADJACENT: the
   Unix socket's per-user namespace is currently the auth; the token must
   be load-bearing. Or (b) contribute AF_UNIX to scr_net.c — ordinary BSD
   socket C, 1–2 days, zero launcher changes, benefits upstream; runtime
   ships as source in the npm package so we can vendor the patch while a
   PR is pending. Prefer (b).
2. **tjs-compat.js shim** (island JS): env/homeDir/file-ops → node:fs;
   spawn → child_process with txiki's return shape ({wait(), stdout});
   connect/listen('pipe') → chosen transport. ~2–3 days; spawn shape and
   the no-stdin-pipe fence are the fiddly parts (sh-redirect idiom covers
   stdin, same as nib's existing Bug-C workaround).
3. **cli.js**: `"runtime": "scriptc-alpha"` in tinyjs.json (or
   `tinyjs dev --scriptc`): emit static TS entry (~10 lines) importing the
   backend as an island package, emit a tsconfig.json (LOAD-BEARING, see
   foot-guns), run `scriptc build`, spawn the binary. ~1–2 days. Only devs
   who flip the flag need Node 24 + clang; default path untouched.
4. **Island gap-hunting**: bridge.js will hit quickjs-ng shim edges.
   Known already: top-level await in embedded ESM packages is unsupported
   (the generated entry's `await createApp(...)` must become an exported
   `main()` — dynamic `await import(...)` DID work in the amp probe);
   island microtask interleaving is documented-deterministic but differs
   from Node. The TINYJS_HTML self-driving pages are the parity harness.
   Budget 3–5 days of whack-a-mole.

Alpha proves deployment shape + correctness, NOT speed — island code is
QuickJS-class either way. Packaged .apps, Windows named-pipe transport,
Linux, and the zig-in-CI conversation come after.

## Gaps / risks / foot-guns (each cost real time or will)

1. **No AF_UNIX anywhere** in scriptc's net stack — static typings don't
   even accept a path. The spike bridged launcher↔backend with an
   `nc -lU`/`nc` pair (stand-in only).
2. **Child stdin is ignore/inherit only** — pipe AND fd stdin are compile
   fences ("stdin takes ignore or inherit (fd stdin has no lowering)").
3. **Static-tier strictness costs rewrites**: typed catch params rejected,
   listen-callback shapes constrained, record indexing by variable key
   unsupported, `process.argv[2]` out-of-bounds TRAPS (dense arrays, no
   undefined) — each cost one probe rewrite; none blocked, all diagnosed
   precisely at compile time.
4. **tsconfig walk foot-gun, cost an hour**: with no tsconfig.json in the
   project, scriptc's tsc pass walks UP and adopts the first one found —
   a stray `~/tsconfig.json` (2023) made every build inside this repo
   index the ENTIRE home directory: minutes of file IO, indistinguishable
   from a hang, while the same build in /tmp took 2s. Every scriptc
   project dir needs its own tsconfig (`spike-scriptc/tsconfig.json`);
   deleting the stray `~/tsconfig.json` defuses it machine-wide (saved to
   agent memory too).
5. **Experimental, 0.0.x, Vercel Labs** — pins us to their cadence.
   Impressive engineering, honest docs, but it's a bet.
6. **Toolchain**: scriptc builds need Node 24+ + clang (+ zig to cross).
   Today tinyjs needs nothing but the repo. Contained if alpha-only.
7. **Dev/release runtime divergence** if dev stays on tjs while release
   compiles — the exact class of bug TODO-verify.md exists to catch. The
   alpha's side-by-side flag is the mitigation.

## Revisit checklist (in order, when we come back)

1. Wire the island `tjs`-compat shim far enough to CALL amp's 88 api
   methods (not just load them) against a live launcher.
2. AF_UNIX patch attempt in scr_net.c + upstream issue (also ask about
   node:sqlite and WebSocket-client roadmap while there).
3. arm64 musl cross-build + container run (only x86_64 tested).
4. Windows cross-build of the spike (`x86_64-windows-gnu`) against
   launcher-win's named pipe.
5. Island long-run soak — leaks/timers over hours; all probes ran seconds.
6. Track scriptc releases: integer inference (perf ceiling), node:sqlite,
   TLA-in-island, worker story. Re-run `spike-scriptc/probes/` on each
   bump — they're cheap and each one is a single claim.

## Session artifacts

- `spike-scriptc/run-spike.sh` — the demo (verified end-to-end 2026-08-20).
- `spike-scriptc/probes/` — one measured claim each; README maps them.
- `spike-scriptc/spike-transcript.txt` — the launcher wire log.
- Machine changes that night: zig installed via brew; Docker started for
  the container runs, then quit; scriptc installs are local to
  spike-scriptc/ (gitignored).
