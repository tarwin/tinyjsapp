# Where code should run — page vs backend, and what crosses the wire

The single most consequential architecture decision in a tinyjs app. Get it
backwards and the app is sluggish in ways no amount of tuning fixes.

## The engines are not equal

- **The page** runs in the OS webview: JavaScriptCore (macOS/Linux) or
  V8/Chromium (Windows). Full JIT, real WebAssembly (compiled, near-native),
  Web Workers, WebGL (WebGPU where the webview has it — WebView2 yes,
  WKWebView yes on recent macOS, WebKitGTK no), OffscreenCanvas, SIMD.
- **The backend** runs txiki.js on QuickJS: an *interpreter*, no JIT. A hot
  numeric loop that takes 50ms in the page can take a second-plus in the
  backend. There is no usable WASM story in the backend; FFI calls into a C
  library run at C speed, but the JS around them stays interpreted.

**Rule: the page computes, the backend touches the system.** Parsing a big
JSON, diffing text, image manipulation on a canvas, layout math, audio
analysis, WASM codecs — page. Files, sockets, processes, sqlite, FFI,
watching, spawning, OS APIs — backend. When a job needs both (index a
directory of files), let the backend read bytes and the page crunch them —
or better, keep the crunching backend-side ONLY if it's I/O-bound (sqlite
queries are C-fast; `tjs:sqlite` beats shipping rows to the page to filter).

## The wire is text — keep bytes off it

Backend and page talk newline-delimited text over a socket. Binary payloads
must be base64'd: +33% size, an encode, a decode, and a full copy at each
end, all on the main thread of both sides.

- **Pass paths, not bytes.** The backend writes a file and hands the page a
  path (`tiny.fileURL(path)` for media/src use, `readAccess` if it's outside
  the frontend dir). This is how the runtime itself does it —
  `tiny.audio.sampler.load(name, path)` never streams the sound over the
  wire, and byte inputs are spilled to the app cache dir once and loaded
  from disk.
- **Batch small messages.** One `api.call` returning 500 rows beats 500
  calls. Push events (`app.push`) are cheap but not free — coalesce
  per-frame state into one message per tick (coo3d drives 20+ windows from
  one 25fps backend tick, one message per window per tick).
- **Never stream media over the bridge.** Continuous PCM/frames at audio or
  video rate through base64 text will burn CPU on both sides. The sanctioned
  exception is `tiny.audioTap` at meter intervals (~80ms chunks for VU/viz),
  which is designed for exactly that rate and no more.
- Big state that both sides need: sqlite (backend queries, page renders) or
  a file the page reads once, not a giant object replayed over pushes.

## Webview throttling — hidden windows do less

WebKit throttles occluded/hidden windows: rAF stops, timers stretch. A
hidden window cannot drive a visible one, run a game loop, or keep time.
Continuous work lives in a *visible* window or in the backend (whose timers
are never throttled). Audio is the exception that proves the rule: the
sampler/Web-Audio host in a hidden main window keeps rendering because
audible pages are exempt — but don't generalize that to your own timers.

## Memory shape

- Every window is a real webview. Windows are cheap to *hide* and expensive
  to churn — pool and reuse (show/move/hide) instead of create/destroy in a
  loop; coo3d recycles its splat windows this way.
- Decoded audio is huge: 48kHz stereo float is ~375KB per second. That's why
  `tiny.audio.sampler` is for short SFX banks; music plays from an `<audio>`
  element, which streams from disk.
- The backend process is small and stays small if you don't accumulate—
  stream file reads (`getReader()`) rather than buffering whole large files.

## Quick table

| job | where | why |
|---|---|---|
| tight numeric loop, parser, differ | page | JIT |
| WASM (codecs, sqlite-wasm, etc.) | page | real compiled WASM |
| audio DSP/mixing | `tiny.audio.sampler` / `tiny.audio` | native/RT-scheduled paths exist; page Web Audio crackles on Linux |
| file I/O, watching, spawn | backend | that's what it's for |
| sqlite queries | backend (`tjs:sqlite`) | C-speed, no rows over the wire |
| per-frame animation state | visible page (rAF) or backend tick | hidden pages are throttled |
| calling a C library | backend FFI | the call is native; keep the JS loop around it thin |
| big blob page→backend or back | write a file, pass the path | wire is base64 text |
