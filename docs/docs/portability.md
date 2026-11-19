---
title: "portability"
---

# portability

<p class="tagline">macOS, Windows and Linux</p>

All three run the same runtime and the same protocol; only the launcher and the transport differ.

```text
              launcher source            webview               transport
macOS         launcher-macos.cc         WebKit                unix socket
Windows       launcher-win.cc           WebView2              named pipe
Linux         launcher-linux.cc         GTK3 + WebKitGTK 4.1  unix socket
```

**Anything unported fails cleanly, so cross-platform code can feature-detect.** Capability calls reject with a specific reason, query calls resolve `null`, and fire-and-forget ones are silent no-ops. Ask before you call:

```js
const caps = await tiny.system.capabilities();
// -> { os: 'macos' | 'windows' | 'linux', badge: true, progress: true, … }
// Anything ABSENT from the answer is supported, so test !== false
if (caps.badge !== false) tiny.app.badge('3');
// the backend asks the same questions: app.system.capabilities(), .os(), …

// synchronous, safe during page setup (the webview's own UA is decisive)
tiny.system.os();  tiny.system.isMacOS();  tiny.system.isWindows();  tiny.system.isLinux();

// the chip is the one thing the page CAN'T see: WKWebView reports MacIntel
// on Apple silicon, so ask the backend
await tiny.system.architecture();   // 'arm64' | 'x86_64', the machine's chip
// the CPU this app runs as, which differs from the chip only when an x86_64
// build is emulated (Rosetta, Windows on ARM). Match a native library you
// dlopen to processArch; pick a download for the machine by arch.
(await tiny.system.info()).processArch;

// Linux assembles media codecs, speech etc. from packages, so a feature can
// be missing on one box — ask, and tell the user the fix. All ok elsewhere.
await tiny.system.requirements(['media.aac']);
// -> [{ id, ok, feature, detail, install: { manager, packages, command } }]
// ids: media.aac, media.h264, media.mp3, speech, spotlight.index, audioTap, globalHotkeys,
// mouseTracking, tray, windowPosition. { refresh: true } re-probes.
const gaps = await tiny.system.missing();  // just the ones that failed
// or: a native dialog naming what's missing, offering to copy ONE install
// command; shows nothing when all is present, so call it from a failure path
audio.addEventListener('error', () => tiny.system.promptMissing(['media.aac']));
```

## what this page can do {#capabilities}

A page can ask by the call itself instead. `tiny.capabilities` answers per `tiny.*` path, combining what the OS and the Linux session support with what tinyjs.json's [`"api"` gate](./site-wrappers#api-gate) allows this page's origin:

```js
await tiny.capabilities.ready;               // once; the rest is synchronous
if (tiny.capabilities.can('app.badge')) tiny.app.badge('3');
tiny.capabilities.can('tray.*');             // true if any tray call works
tiny.capabilities.get('app.badge');
// { ok: true }
// { ok: false, why: 'platform', feature: 'badge' }      this OS can't
// { ok: false, why: 'session', feature: 'keystroke' }   this Linux session can't (Wayland)
// { ok: false, why: 'gate', needs: ['app.badge'] }      tinyjs.json "api" denies it
tiny.capabilities.list();                    // every path that works here, sorted
tiny.capabilities.onChange(({ added, removed }) => render());
```

Paths are the ones you'd call, `tiny.` optional. `why` can also be `'unknown'` (no such path) or `'pending'` (no answer yet, when `can()` is always false). `ready` resolves within 3 seconds even if the answer hasn't arrived, so awaiting it can't hang a page; a late answer still arrives, and `onChange` fires when it does, as it does for the first answer, so a page can render from `onChange` alone. `feature` is the `system.capabilities()` key that says no.

It works on every page, gated or not. A page is only told about the names its own `tiny` uses, and the app's own `api` functions aren't covered. It's informational: the backend still checks every call. `system.capabilities()` stays the raw platform table, and the one the backend uses.

Some things are not "not ported yet" but concepts the other OSes don't have — AppleScript, Quick Look. Those live in `tiny.macos.*` and reject elsewhere (except `ai.availability()`, which answers `'unsupported'`, since it's the check you make first), so the split is visible in the call itself. Everything that *could* exist on another OS stays on `tiny.app` and answers `'unsupported'` until it does.
