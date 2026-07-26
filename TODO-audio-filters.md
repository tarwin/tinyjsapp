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

## macOS — BUILT (2026-07-26)

Option 1 below turned out to be possible: a process tap **can** be re-injected,
so macOS gets the same API rather than the `audioFilters: false` fallback.

The graph, all Core Audio, no driver and no system install (macOS 14.2+):

    WebKit audio processes --[process tap, MUTED]--> our IOProc --> aggregate
                                                         |           device
                                                      biquads     (wraps the real
                                                                  default output)

Muting the tap takes the page's audio off the speakers, so the only remaining
path out is our IOProc, which filters and writes to an aggregate device
wrapping the real output device. Notes worth keeping:

- **Apple ships an EQ AudioUnit** (`kAudioUnitSubType_NBandEQ` / `AVAudioUnitEQ`)
  and it could sit in the middle. It isn't used: the biquads are computed
  inline from the same RBJ cookbook PipeWire's `bq_*` builtins use, so the same
  numbers give the same curve on both platforms. An API that sounds different
  per OS would not be worth having. Verified against theory — −3.01 dB at a
  Q=0.707 cutoff, −40 dB/decade, exact peaking gain, −6.02 dB for a ×0.5
  linear (see "Debugging" below).
- **The launcher's own pid is excluded from the tap.** Our IOProc output comes
  from this process, so tapping ourselves feeds output back into input. Cost:
  `app.playSound` (NSSound, in the launcher) is not filtered.
- **Muting is earned, never assumed.** An unauthorized tap does not fail — it
  succeeds and delivers zeroed buffers. Mute on that and the app goes silent,
  which is far worse than having no EQ. So a chain starts in *probation*: tap
  unmuted, IOProc writes silence, audio plays normally down its usual path. The
  first non-zero sample promotes it to a muted, filtering graph. If that sample
  never comes, nothing is ever muted. `GET audiofilters` reports
  `state: off | waiting | active` so this is observable rather than folklore.
- **TCC.** Taking audio off the speakers needs `kTCCServiceAudioCapture`, which
  is granted per *bundle id* on first use. `tinyjs dev` has no bundle identity,
  so in dev the tap stays silent and the chain sits in `waiting` forever —
  audio unfiltered, never muted. Verify with a packaged build.

### Debugging

The subsystem is invisible from outside, so the launcher has an opt-in probe:

    c++ ... -DTINY_EQ_PROBE ...    # see setup.sh for the full command line

It logs the build (how many process objects, each pid/bundle and whether Core
Audio thinks it is `IsRunningOutput`, and the status of every step) plus a
periodic peak-in/peak-out line. That is what proved the dev-mode silence was
TCC and not a wiring bug: the tap was aimed at exactly one object, our own
`com.apple.WebKit.GPU`, with `IsRunningOutput=1`, and still read zeros.

The filter maths can be tested without any audio at all — the harness in
`/tmp` was generated by extracting `EqBand`, `eq_compile` and `eq_run` straight
out of the launcher source and sweeping sines through them, which keeps the
test from drifting from the code.

### Still unverified

- The **muted, filtering path end to end** — needs a packaged app with the
  audio-capture permission granted. Everything up to the mute is verified.
- `audioTap` capturing **post-filter** (see "Related" below). With the EQ
  muting the source and re-injecting from the launcher process — which
  `tiny_eq_process_objects` excludes but `tiny_app_process_objects` does not —
  the interaction of the two taps has not been measured.

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
