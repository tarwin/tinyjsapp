# Native audio filters — bringing `tiny.audio.filters` to macOS and Windows

`tiny.audio.filters` lands on Linux first (PipeWire). This is the note for
giving the other two platforms the same API, so an app can ask for an EQ once
and get it everywhere instead of branching.

## Why it exists at all

An app that wants to process its own output — a graphic EQ, headphone
correction, a crossover — normally reaches for Web Audio `BiquadFilterNode`.
That works on macOS and Windows. It does **not** work on Linux: WebKitGTK
renders the Web Audio graph on a normal-priority (`SCHED_OTHER`) thread while
its media threads get real-time priority, so anything reaching
`ctx.destination` misses its deadline and crackles — on an idle machine, at any
`latencyHint`, from an element source or a decoded buffer alike. Measured, not
inferred; see TODO-linux.md. There is no graph-side fix.

So on Linux the filters have to live outside the browser, and once they do, they
turn out to be *better* than the Web Audio version: they apply to whatever the
app is playing, including streams the page never gets samples for (raw radio,
native HLS), and they survive the page reloading.

That's the argument for the API existing on all three platforms rather than
being a Linux workaround: it's the only way to filter audio the page doesn't
own.

## What Linux does (the reference implementation)

- `libpipewire-module-filter-chain` sink named `tinyjs-eq-<pid>`, built from
  the builtin biquads (`bq_peaking`, `bq_lowshelf`, `bq_highshelf`,
  `bq_highpass`) plus a gain for preamp.
- The app's own stream is routed through it; its output goes to the default
  sink.
- Live parameter changes update the node's Props — no rebuild, no gap.
- Teardown mirrors `audioTap`: destroy only our own node (matched by the
  pid-stamped name), sweep any orphaned by a killed run, and tear down on
  SIGTERM/INT/HUP. See the audioTap notes in TODO-linux.md — the failure modes
  there (a lingering node, a stream left pointing at a sink that no longer
  exists) are the ones that bite hardest, because they present as "no audio at
  all" on the *next* launch.

## macOS

Candidate: an `AVAudioEngine` / `AUGraph` with `kAudioUnitSubType_NBandEQ`
inserted on the app's output. The hard part isn't the filter, it's getting the
app's audio to pass through something we own — WKWebView plays to the process's
default output directly.

Worth investigating, roughly in order of preference:

1. Whether the media output can be routed through a per-process tap
   (`AudioHardwareCreateProcessTap`, macOS 14.4+) *and* re-injected. The tap
   API is capture-oriented; check whether re-injection is possible or whether
   this only gives us the `audioTap` half.
2. A null/aggregate device we own, with the app's output moved onto it and its
   result played back — the direct analogue of the PipeWire approach, but
   creating devices is heavier on macOS and needs care not to disturb the
   user's default device.
3. Failing both: report `audioFilters: false` and let apps use Web Audio, which
   works fine there. **This is an acceptable outcome** — the point of the
   capability flag is that the honest answer is a supported answer.

## Windows

Likely the hardest. WASAPI has no builtin EQ; we'd be writing the biquads
ourselves (trivial) and finding somewhere to run them (not). Options to look
at: a loopback capture + render pair, or an APO — APOs are a system-level
install, which is far more invasive than anything tinyjs does today and
probably disqualifying.

Same fallback applies: Web Audio works on Windows, so `audioFilters: false` and
an app-side Web Audio chain is a legitimate end state.

## API contract to preserve

Whatever the backend, keep these true, because apps will rely on them:

- `filters()` replaces the whole chain and is idempotent.
- `set(i, patch)` is live — no audible gap, no restart.
- `clear()` restores unprocessed output.
- The chain applies to *everything the app plays*, not just what the page
  routed somewhere.
- `tiny.system.capabilities().audioFilters` tells the truth, and
  `tiny.system.requirements(['audioFilters'])` explains what's missing and how
  to fix it where that's installable.

## Related

- `audioTap` should capture **post-filter** where a chain is active, so
  visualisers show what the user actually hears. That falls out naturally on
  Linux (tap the filter sink's monitor) and should be honoured elsewhere.
