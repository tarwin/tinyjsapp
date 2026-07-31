# tiny.audio.sampler — one sampled-SFX mixer, native only where it must be

A cross-platform way to play *sampled* audio — game/UI sound effects with
per-voice volume, pan, and pitch, mixed into one output. Not streaming, not
music, not a sequencer. The reference workload is coo3d: a decoded bank of
short recordings, fired constantly with randomized rate/vol/pan
(`coo3d/src/frontend/app.js:372` — one AudioContext, bank decoded once,
`createBufferSource → gain → stereoPanner → destination` per play).

## Why it exists

Web Audio already IS a sampling mixer, and on macOS/Windows it's the right
tool — the browser mixes buffer sources natively on an RT-scheduled audio
thread. On Linux it is unusable: WebKitGTK renders the Web Audio graph on a
normal-priority thread (its media threads DO get RT via rtkit; the graph
thread does not), so anything reaching `ctx.destination` misses quanta and
crackles — on an idle machine, at any latencyHint, from element source or
decoded buffer alike. Measured, not theory (CLAUDE.md, TODO-linux.md); a JS
mixer in an AudioWorklet runs on the SAME thread, so "mix it ourselves"
changes nothing. Don't retry graph-side fixes.

So: **page-side Web Audio backend on macOS/Windows, native backend on
Linux** — the native code lives only where the problem lives, and the
platform with the worst hidden-page behavior (WebKitGTK suspends occluded
pages — measured, TODO-linux.md) is exactly the platform that needs no page
at all. `capabilities().sampler` reports `'native' | 'page'` so apps can
know, but they shouldn't need to care.

## API contract (shaped by the native backend, NOT Web Audio)

Everything async and handle-based, because on Linux every call crosses the
wire. If the API leaks anything Web-Audio-specific (AudioContext, `start(when)`
scheduling), the native backend can't honor it — so it doesn't exist here.
Fire-and-forget with live per-voice updates; that's what SFX needs.

    const s = tiny.audio.sampler;               // app-scoped singleton
    await s.load('coo', '/abs/path/coo-1.mp3'); // by PATH (see binary rules)
    await s.load('chime', bytes);               // ArrayBuffer accepted; see below
    const v = await s.play('coo', { vol: 0.8, pan: -0.3, rate: 1.06, loop: false });
    v.set({ pan: 0.1 });                        // live, no restart
    v.stop();                                   // short fade-out, no click
    s.master(0.5);                              // one master gain
    s.unload('coo');                            // frees the decoded PCM

- `rate` is a playbackRate-style ratio (resampling — pitch and speed together).
- `vol` linear 0..1, `pan` −1..1 **equal-power** (StereoPanner's law; the
  native mixer must match — same numbers, same sound, per the rule in
  TODO-audio-filters.md).
- Voice cap (default ~32) with oldest-voice stealing; `play()` never rejects
  for "too many", it steals. The Linux cap also protects the RT callback —
  same "never unbounded work on the audio thread" reason as the filter cap.
- Guaranteed decode formats: **WAV + MP3 + FLAC** (miniaudio decodes all
  three dependency-free; every WebKit/WebView2 decodes them too). On Linux
  the LAUNCHER decodes, so the sampler can't be broken by missing GStreamer
  plugins — unlike `<audio>` (TODO-linux.md codec note).
- App-scoped: callable identically from any window and from the backend
  (bridge api). Backend call → same mixer, same state.
- NOT in scope: sample-accurate scheduling, per-voice filters, playing MediaStreams.

## The host: the MAIN window (mac/win) — this question is already answered

The design worry was "which page hosts the mixer, and what if the app has no
window / the window closes?" The codebase answers it:

- **A main window always exists.** The wire protocol's bare `CMD` targets it;
  bridge windows are `'main'` + secondaries. Even `activation: "accessory"`
  agent apps (world-clock-style, no Dock, no visible window) have a main
  window that "exists but stays hidden" (README, bridge.js). **Never create a
  window for the sampler — the host is always there.**
- **Main cannot close, only hide** — `win.close()` is a no-op for `'main'`
  (bridge.js `winHandle`). Secondary windows closing never touches the host.
  The two-windows-one-closes problem doesn't exist.
- **Reload is the real hazard, not close.** A main-page reload loses the
  decoded bank and any playing voices. The BRIDGE owns sampler state (bank
  manifest name→path, master volume); the host page is a disposable renderer
  re-armed on client hello, same pattern as the offscreen-rescue machinery.
  Playing voices die at reload — acceptable, documented. Loads by ArrayBuffer
  are spilled to a cache file by the bridge precisely so re-arm can replay
  them by path.
- On Linux none of this applies: the launcher mixes, windows are irrelevant.

### Hidden-window behavior (why a hidden main is a safe host)

- **GPU:** a hidden window isn't composited on any platform — GPU cost ~0.
  The host page stays blank and runs no rAF/timers, so throttling of DOM
  timers is moot: it only executes when the launcher/bridge pokes it.
- **Memory:** ~0 marginal — the main window's webview process exists anyway,
  even for accessory apps.
- **Audio while hidden:** the Web Audio render thread is Core Audio
  (RT, macOS) / Chromium's audio service (MMCSS, Windows) — scheduled by
  audio infrastructure, not window visibility. Chromium explicitly exempts
  audibly-playing pages from intensive background throttling.
- **macOS App Nap:** audible playback holds a power assertion. The gap to
  verify is a *fully hidden accessory app* starting playback cold, and
  whether a silent-but-running context loses the assertion between sounds
  (if so: suspend the context when idle, resume on play — resume is ~ms).
- **Autoplay/gesture:** sampler audio starts from injected eval, never a user
  gesture. Linux launcher already sets `WEBKIT_AUTOPLAY_ALLOW`
  (launcher-linux.cc). macOS WKWebView allows it in practice (coo3d coos
  unprompted). Windows: launcher-win.cc sets no autoplay arg today — WebView2
  may need `--autoplay-policy=no-user-gesture-required` in
  AdditionalBrowserArguments. Verify before trusting.

## Binary rules (wire is text; keep bytes OFF it)

The wire is newline-delimited text; binary crosses it only as base64 (+33%
size, encode/decode CPU, full copies in JS strings — audioTap pays this per
chunk, tolerable for meter-rate PCM, wrong for sample banks).

1. **Load by path, always, internally.** Linux: launcher opens and decodes
   the file — zero wire bytes. mac/win: the host page reads the file directly
   (`fileURL` + the readAccess rules in tiny.js — the bridge must grant the
   bank's directory) then `decodeAudioData` — zero wire bytes.
2. **`load(name, bytes)` is sugar, not a wire format:** the bridge writes the
   bytes to the app cache dir once and proceeds by path. Backend-embedded
   banks (coo3d ships base64 in source) pay one disk write, not a wire
   crossing per launch — and re-arm after reload replays from the file.
3. **Decoded PCM never moves.** Decode where you mix (host page or launcher),
   keep exactly one decoded copy per sound. Decoded audio is the real memory:
   48kHz stereo float ≈ 375KB/s of material. A bank of one-second SFX is
   nothing; a 3-minute music track is ~66MB decoded — that's what `<audio>`
   streaming is for, and why this API is samples-only.
4. Compressed source bytes are droppable after decode (native keeps the path
   for re-decode; the page keeps nothing).

## Linux backend sketch

- **miniaudio for decode/resample only** (`MA_NO_DEVICE_IO` — decoders +
  resampler, no device backend): single header, MIT-0/public-domain, roughly
  +100–200KB on the binary instead of 300–400KB with its device layer.
- **Output via `pw_stream`** — the launcher already links libpipewire
  (mouseTracking, audioTap); the process callback runs on PipeWire's
  RT-scheduled data path, which is the entire point. Mixing is adds and
  multiplies; equal-power pan is two gains.
- Node name `tinyjs-sampler-<pid>`, torn down by exact-name match with the
  awk id-carry pattern, swept on SIGTERM/INT/HUP — all the per-app-node rules
  in CLAUDE.md apply. Per-app only. Never system-wide anything.
- Route the sampler stream through `tinyjs-eq-<pid>` when a filter chain is
  active, so `tiny.audio.filters` keeps its "applies to everything the app
  plays" contract and `audioTap` captures sampler output post-filter.
- Well under the ~30-node filter-chain segfault ceiling (one stream node),
  but the sampler + eq + tap all creating nodes should be counted together
  if anything grows.

## Wire ops (Linux) / host ops (mac-win)

    SAMPLER LOAD <name>\t<path>     → ok/err        (launcher decodes)
    SAMPLER PLAY <name>\t<vol>\t<pan>\t<rate>\t<loop> → voice id
    SAMPLER SET <vid>\t<vol|_>\t<pan|_>\t<rate|_>
    SAMPLER STOP <vid> | STOPALL | MASTER <v> | UNLOAD <name>

Same verbs eval'd into the main-window host on mac/win; bridge is the single
hub either way, so page-mode and backend-mode calls are one code path.
Voice `set` traffic is small text lines — fine at UI rates over the socket;
don't stream per-frame automation (no use case; sequencers are out of scope).

## Decisions already made (don't relitigate)

- **No vendored JS library.** howler (~34KB min) mostly solves problems we
  don't have (HTML5Audio fallback, codec sniffing); Tone (~180KB) is a music
  framework. The page backend is ~3–5KB bespoke in tiny.js — noise next to
  its ~52KB, and every platform carries tiny.js anyway.
- **No JS/worklet mixer.** An AudioWorklet runs on the same broken-priority
  thread on Linux; reducing node count doesn't fix scheduling. See "Why".
- **No native backend on macOS/Windows.** Web Audio is already RT-scheduled
  there; native would add binary and platform risk for nothing. The Windows
  filters post-mortem (TODO-audio-filters.md) is the standing reminder that
  per-platform native is earned by a measured need, not defaulted to.
- **No conditional inclusion / per-app stripping.** Launchers are generic
  prebuilt binaries shared by all apps — there is no per-app codegen to hang
  it on, and it would save ~4KB of JS on mac/win. Linux carries the
  miniaudio-decoders + pw_stream cost (~100–200KB) unconditionally.
- **Host = main window, never a created one.** Main always exists (even
  `activation: accessory`), cannot close, and hidden costs ~nothing. See
  "The host".
- **Bridge is the hub** for page-mode and backend-mode alike — one code
  path, one state owner, and `capabilities().sampler` stays purely
  informational rather than a fork apps must branch on.

## Build order (Linux first — that's where the need is)

1. `launcher-linux.cc`: `SAMPLER` wire ops; miniaudio with `MA_NO_DEVICE_IO`
   (decode/resample only); `pw_stream` output node with the naming/teardown/
   sweep rules above. `tinyjs dev` auto-rebuild covers the iteration loop.
2. `bridge.js`: hub state (bank manifest, master), `audio.sampler*` api
   handlers, wire forwarding.
3. `tiny.js`: the `tiny.audio.sampler` surface — thin `call()` wrappers,
   platform-blind.
4. Run the Linux verify items headlessly (`TINYJS_HTML` self-driving page +
   store.json readout, `pw-top -b` ERR, null-sink pan rig — recipes in
   CLAUDE.md).
5. mac/win page backend: same verbs eval'd into the main-window host,
   re-arm on client hello. Anything built there but not watched running goes
   in TODO-verify.md per the house rule.
6. Docs + changelog (CHANGELOG.md AND docs/changelog.html) when it ships.

## Open decisions

- [ ] Fade-out length on `stop()` (a hard stop clicks; ~10–20ms default).
- [ ] `rate` range clamp (miniaudio resampler quality vs cost at extremes).
- [ ] Idle policy on mac/win host: keep context running (steady ~0 CPU but
      possibly holds "is playing audio" state) vs suspend-when-silent
      (App Nap safe either way? — see verify list).
- [ ] Does `unload` while voices play cut them or let them finish? (Cut —
      simpler, predictable — unless a use case appears.)

## Verify (the fire-and-forget trap applies — see TODO-verify.md rules)

- [ ] Linux: sampler plays clean while a busy page renders (the whole point).
      `pw-top -b` ERR=0 under 20 concurrent voices + heavy rAF load.
- [ ] Linux: stereo pan truth via null-sink rig (built-in monitor MONO-ISES —
      CLAUDE.md), hard-panned reference first.
- [ ] Linux: filter-chain routing — sampler audible through active EQ, and
      audioTap sees it post-filter.
- [ ] mac: accessory app, window never shown, backend `play()` cold — sound
      comes out. Then: still comes out 10 minutes later (App Nap).
- [ ] win: autoplay — sampler starts with no gesture ever; add
      `--autoplay-policy=no-user-gesture-required` if not.
- [ ] win: hidden main window, sampler running — GPU/CPU quiet in Task
      Manager, audio unbroken after 5+ min (intensive throttling exemption).
- [ ] mac/win: main-window reload mid-playback — bank re-arms, next `play()`
      works without app code doing anything.
- [ ] Pan/pitch parity: same `{vol, pan, rate}` numbers on all three OSes
      sound the same (record + compare, don't eyeball by ear alone).
- [ ] Kill -9 the launcher on Linux mid-playback → relaunch → no orphaned
      `tinyjs-sampler-*` node (sweep works).
