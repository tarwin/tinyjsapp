# The tiny.* / app API — full tour (current as of tinyjs 0.34.0)

The `tiny` global is injected into every page automatically (no script tag);
TypeScript definitions ship in types/tiny.d.ts. Backend handlers receive the
`app` object with a mirrored surface (`app.window(id).*`, `app.audio.*`,
`app.clipboard.*` …).

## Bridge basics

```js
await tiny.api.call('method', { params })   // -> backend api.<method>; throw rejects
tiny.api.on('event', (data) => ...)         // <- app.push from backend
const off = tiny.api.on('event', fn); off() // on() returns an unsubscribe
tiny.api.off('event', fn)                   // remove by reference
// EVERY tiny.*.on sugar wrapper (menu.on, tray.on, theme.on, win.onState …)
// passes the unsubscribe through — and for those it's the ONLY way to
// unhook (the sugar wraps your callback, off() can't match it).
// api handlers get meta: (params, app, meta) — meta.window = calling window id
```

## Windows

```js
tiny.win.setTitle(t); tiny.win.setSize(w, h); tiny.win.setPosition(x, y);
tiny.win.center(); tiny.win.minimize(); tiny.win.restore(); tiny.win.zoom();
tiny.win.fullscreen(); tiny.win.setFullscreen(bool);
tiny.win.setMinSize(w, h);   // floor under USER resizes (your own setSize can
                             // still go under it on mac/win; GTK clamps both)
tiny.win.setZoom(factor);    // native page zoom 0.25–5
tiny.win.startResize('se');  // n ne e se s sw w nw — frameless windows already
                             // get invisible edge grips
tiny.win.ensureOnScreen();   // one-shot rescue onto the nearest screen.
// Automatic version: on a boot whose SCREEN LAYOUT changed since last run,
// windows restored off-screen are clamped back (first show / first position
// once); display unplugged mid-session sweeps visible windows too. Windows
// deliberately parked half-off keep more than a sliver and are never touched.
// Opt out: "offscreenRescue": false in tinyjs.json (manual call always works).

await tiny.win.getState();
// { x, y, width, height, outer: { width, height }, fullscreen, minimized,
//   visible, focused, alwaysOnTop, resizable, screen: { width, height, scale },
//   chrome: { frame, windowControls, windowControlsPos, transparent,
//             vibrancy, squareCorners, acceptsFirstMouse } }
// width/height = the PAGE's box on all three OSes, same units setSize and
// win.open's size take (set -> get round-trips); outer = on-screen footprint,
// decorations included (window.outerWidth is 0 in a WKWebView — this is how
// you get it). windowControls reports what's actually shown (null on Linux —
// the WM owns it).

const off = tiny.win.onState(({ win, fullscreen, maximized, minimized, focused }) => …);
// Fires on EVERY transition whatever caused it — green button, menu, F11, or
// your own setFullscreen — the thing getState() polling can't distinguish
// from a manual resize to screen size. Broadcast to every window with the
// window id attached: filter on `win` if you only care about your own.
// Deduped in the launcher (drag-resize never spams). Wayland never reports
// minimized (compositor keeps it private). Backend: export onWindowState(info, app).

tiny.win.setChrome({ frame: false, windowControls: false, transparent: false,
                     vibrancy: 'hud', squareCorners: true,
                     acceptsFirstMouse: true, menu: true,
                     windowControlsPos: { x: 12, y: 24 } });
// windowControls: true | false | ['close','minimize'] | [] — mac hides each
//   button, win maps onto WS_SYSMENU/MINBOX/MAXBOX, Linux asks the WM (may
//   ignore). windowControlsPos recenters the mac traffic lights in a taller
//   custom titlebar (offset from top-left; null restores; launcher re-applies
//   across resizes/fullscreen — set once; ignored on win/linux).
// squareCorners: macOS borderless (square, no titlebar; keep data-tiny-drag).
// acceptsFirstMouse: the click that focuses an unfocused window also reaches
//   the page (palettes/toolbars).
// Startup chrome belongs in tinyjs.json "chrome" / win.open options — applied
// BEFORE first paint; a late setChrome flashes the default window.
// Windows: transparent MAIN window must be declared in tinyjs.json, and
// transparency + a Win32 menu bar are mutually exclusive; frameless/
// transparent windows never draw a menu bar there (the bar is GDI).

tiny.win.setAlwaysOnTop(v); tiny.win.setResizable(v);
tiny.win.hide(); tiny.win.show(); tiny.win.show({ activate: false });
tiny.win.hide({ app: false });   // put ONE window away without hiding the app
// (plain hide() on main is NSApp hide on macOS — right for palettes that
// paste into the previous app, wrong for a launcher window over documents)
tiny.win.setHideOnClose(v);
// macOS idea (app outlives its last window; Dock brings it back). On win/
// linux honoured only while something can bring the app back — tray icon,
// accessory mode, or another window; else closing the last window quits.

// superpowers (overlays/HUDs/pets/palettes)
tiny.win.setClickThrough(true);   // mouse passes through
tiny.win.setLevel('overlay');     // 'normal'|'floating'|'overlay'|'desktop'
tiny.win.setAllSpaces(true);      // follow Spaces + over fullscreen (mac;
                                  // linux maps to sticky windows)

// drag regions: <header data-tiny-drag> — drag moves the window, dblclick
// zooms; interactive children auto-excluded (data-tiny-nodrag to opt out)
tiny.win.onDrop((paths) => ...);  // files dropped on the window: real paths
el.addEventListener('mousedown', () => tiny.win.startDrag({ files: [path] }));
// drag OUT (Finder/Slack); call while the button is held; image: custom png

tiny.win.print();                 // native print panel — the CALLING window
await tiny.win.printToPDF(path);  // vector PDF of the calling window

// multiple windows — any frontend html file can be a window
tiny.win.open('settings', { page: 'settings.html', title: 'Settings',
                            size: '420x300', minSize: '300x200',
                            x: 40, y: 40, chrome: { frame: false } });
// chrome + x/y + minSize apply BEFORE first paint. win.* calls target the
// caller's window; backend: app.openWindow(...), app.window(id).eval/push/
// close/setTitle/…, app.push broadcasts; export onWindowClosed(id, app).
tiny.win.id; tiny.win.close(); await tiny.win.windows();
```

## Menus

```js
tiny.menu.set([
  { role: 'edit' },                          // put the standard Edit menu HERE
                                             // (omit = it goes first on macOS)
  { title: 'Actions', items: [
    { id: 'open', label: 'Open…', key: 'o' },        // ⌘O (Ctrl+O win/linux)
    { id: 'find', label: 'Find', key: 'alt+shift+f' },// ⌥⇧⌘F — extra modifiers
                                             // are prefixes; uppercase letter
                                             // alone means ⌘⇧
    { id: 'mute', label: 'Mute', checked: true },
    { id: 'no', label: 'Nope', enabled: false },
    { separator: true },
    { id: 'more', label: 'More', submenu: [{ id: 'a', label: 'Sub' }] },
  ]}]);
tiny.menu.on((id) => ...);
tiny.menu.update('mute', { checked: false, label: 'Unmuted' });
await tiny.menu.get('mute');   // { exists, label, checked, enabled }
```

`menu.set` is the APP menu: every window shows it (including windows opened
later — macOS has one bar, win/linux draw a copy per window), and
`menu.update` patches every copy at once. Per-window: `tiny.win.menu.set/
update/reset` (a window that says something else). Whether a bar SHOWS is
chrome: `setChrome({ menu: false })` — accelerators keep firing, macOS
ignores it. An app menu (About + Quit) and Edit menu always exist. Same item
shape + update/get work for tray and context menus.

```js
tiny.menu.setContext([{ id, label }, { separator: true }]);  // right-click
tiny.menu.onContext((id) => ...);       // backend: export onContextMenu
// "contextMenu": false in tinyjs.json hides WebKit's default menu entirely
```

## Dialogs (native, application-modal, all three OSes)

```js
await tiny.dialog.openFile();     // path | null       openFiles() -> paths[]
await tiny.dialog.pickFolder();   // path | null       saveFile() -> path|null
await tiny.dialog.alert(message, detail);
await tiny.dialog.confirm(message, { detail, ok, cancel });   // true | false
await tiny.dialog.prompt(message, { default, ok, cancel });   // string | null
```

## Tray

```js
tiny.tray.set({ title, icon, tooltip, menu: [{ id, label }, { separator: true }] });
// icon: png path | 'sf:<name>' (macOS SF Symbol) | 'emoji:<glyph>' (Windows,
// drawn as mono silhouette). primaryAction: true = left-click fires onClick.
tiny.tray.on((id) => ...); tiny.tray.onClick(fn); tiny.tray.remove();
await tiny.tray.position();   // { x,y,width,height } | null (Linux: null)
// Linux tray is AppIndicator/StatusNotifier — menu-based; a bare icon click
// is emulated via a synthetic menu entry.
```

## Notifications

```js
tiny.notify(title, body, { id, subtitle, sound });
tiny.app.onNotificationClick((id) => ...);   // backend: export onNotificationClick
tiny.notify(title, body, { actions: [{ id, title, reply?, placeholder?, destructive? }] });
tiny.app.onNotificationAction(({ id, action, reply }) => ...);
// Packaged+signed = native banners with click routing; ad-hoc dev falls back
// to osascript. Action buttons: macOS and Linux (freedesktop) — Windows
// balloons have no buttons/reply.
```

## Audio

```js
// ── tiny.audio.sampler — sampled SFX mixer (0.34.0). One per app: every
// window AND the backend (app.audio.sampler) drive the SAME mixer/state.
const s = tiny.audio.sampler;
await s.load('coo', '/abs/path/coo.mp3');    // or ArrayBuffer/view/Blob —
   // bytes are spilled to the app cache ONCE and read from disk, never
   // streamed over the bridge. Load by path when you can. wav/mp3/flac
   // guaranteed everywhere. Re-loading a name replaces it.
const v = await s.play('coo', { vol: 0.8, pan: -0.3, rate: 1.06, loop: false });
   // vol linear 0..1; pan −1..1 EQUAL-POWER (StereoPanner's law — same
   // numbers, same sound on all three OSes); rate = playbackRate-style ratio
   // (pitch+speed). 32 voices; past that the OLDEST is stolen — play() never
   // rejects for "too many" (it does for an unknown name).
v.set({ pan: 0.1 });          // live, no restart
v.stop();                     // short fade, no click
s.stopAll(); s.master(0.5); s.unload('coo');   // unload CUTS its voices
// Backends: capabilities().sampler is 'native' (Linux: launcher decodes with
// miniaudio, mixes on PipeWire's RT loop — immune to page load and missing
// GStreamer plugins) or 'page' (mac/win: Web Audio in the MAIN window's
// page). Apps shouldn't branch — the API is identical. Costs to know:
// main-window reload re-arms the bank by itself but playing voices die; a
// BACKEND play() on mac/win needs the main window's page loaded ("no answer
// from the main window"); decoded PCM is ~375 KB/s — short SFX, not music.
// An active tiny.audio.filters chain picks the sampler up like everything
// else. Out of scope: sample-accurate scheduling, per-voice filters,
// MediaStreams. Needs "minTinyjsVersion": "0.34.0".

// ── tiny.audio — EQ/DSP chain on the app's WHOLE output. Native (below the
// browser: reaches native HLS/tainted streams, survives reload) on Linux +
// macOS 14.2+; capabilities().audioFilters is FALSE on Windows (measured
// permanent). pageChain(ctx) is the Windows fallback: same verbs, same RBJ
// curves, but PAGE-scoped — route your source through it. NEVER pageChain on
// Linux (Web Audio to destination crackles there — that's why the native
// chain exists).
const eq = caps.audioFilters ? tiny.audio : tiny.audio.pageChain(ctx);
if (eq.input) { src.connect(eq.input); eq.output.connect(ctx.destination); }
await eq.filters([{ type: 'gain', gain: 1 },            // linear preamp
  { type: 'peaking', freq: 60, q: 1.1, gain: 4 }]);     // biquads: gain in dB
eq.filter(1, { freq: 60, gain: -3 });   // retune in place (slider drags)
await eq.balance(-0.2); await eq.clear();
// Types: peaking lowshelf highshelf lowpass highpass bandpass notch allpass
// + gain (linear). Limits: 15 filters on Linux (PipeWire truncates above),
// 32 on macOS. pageChain: shelf q and per-filter gainR ignored.

// ── tiny.audioTap — the app's rendered OUTPUT as PCM (VU meters, viz),
// including audio that bypasses Web Audio. Needs "audioTap":"app"|"system"
// in tinyjs.json. macOS 14.4+ / Windows ('system') / Linux ('system'; 'app'
// approximated by system mix). Post-filter where a chain is active.
await tiny.audioTap.start({ scope: 'app', interval: 80 });
tiny.audioTap.on(({ pcm, sampleRate, channels, frames, t }) => {
  const bin = atob(pcm), n = bin.length >> 1;   // base64 -> interleaved LE Int16
});                                              // tiny.audioTap.stop()
// macOS: auth is deferred to the FIRST start(); prompts "System Audio
// Recording" even for scope 'app'; under `tinyjs dev` the grant belongs to
// the terminal. Denial = silent chunks, not an error.

// ── cross-origin stream INTO Web Audio (mac): MediaElementSource on a
// cross-origin <audio> is silent by spec; proxyURL streams through the
// native layer with permissive CORS so it's untainted.
audio.crossOrigin = 'anonymous';
audio.src = tiny.proxyURL('https://host/stream.mp3');

await tiny.app.beep(); await tiny.app.playSound('Ping');
// playSound: system sound name, file path, or the portable meanings
// 'info'|'success'|'alert'|'error' (nearest native equivalent per OS).
```

## Fetch / files

```js
const r = await tiny.fetch(url, { method, headers, body });  // backend-proxied,
   // NO CORS/CSP; real Response. { stream: true } for endless bodies
   // (internet radio) — r.body.getReader().
audio.src = tiny.fileURL(path);  // file:// URL for a disk path — ALWAYS this,
   // never 'file://' + path (Windows drive letter becomes the URL host).
// file:// media loads only under the frontend dir by default — widen with
// "readAccess": true | "/path" in tinyjs.json.
await tiny.app.paths();   // { home, data, cache, logs, temp, downloads,
                          //   desktop, documents } — per-OS correct; backend
                          //   twin app.paths is a plain object (no await)
await tiny.app.shell.open('https://x.com');  // URL or path; resolves true/rejects
await tiny.app.shell.reveal(path); await tiny.app.shell.trash(path);
await tiny.app.thumbnail(path, size?);  // png for ANY path (macOS/Windows:
   // preview or document/folder icon — never fails on type; Linux:
   // images-only). Rejects on nonexistent path. @2x on mac/linux.
```

## Store / secrets / sqlite

```js
await tiny.store.set('key', anyJsonValue); await tiny.store.get('key');
await tiny.store.delete('key'); await tiny.store.all();
// per-app data dir; fine for settings. Query-shaped data -> sqlite (backend):
//   import { Database } from 'tjs:sqlite';
//   new Database(path).prepare(sql).run(...)/.all()
await tiny.app.secrets.set(key, value);  // Keychain / Credential Manager /
await tiny.app.secrets.get(key);         // Secret Service. Tokens go HERE,
await tiny.app.secrets.delete(key);      // never tiny.store. get -> string|null.
// dev gotcha: macOS files the ACL against the BINARY — a secret saved by
// `tinyjs dev` prompts once when the built app first reads it. Windows caps
// a value at 2560 UTF-8 bytes (rejects up front, named).
await tiny.app.authenticate(reason);  // Touch ID / Windows Hello -> true|false;
// false covers cancel AND unavailable; Linux always false (no identity check
// exists) — gate on it and the gate fails closed.
```

## System

```js
tiny.system.os();          // 'macos'|'windows'|'linux' — SYNCHRONOUS
tiny.system.isMacOS(); tiny.system.isWindows(); tiny.system.isLinux();  // sync
await tiny.system.architecture();  // 'arm64'|'x86_64' (navigator lies on mac)
await tiny.system.info();          // { os, arch, session, desktop }

const caps = await tiny.system.capabilities();
// Lists only the EXCEPTIONS — every key it doesn't name is supported. So
// test `caps.ocr !== false`, NEVER `if (caps.ocr)`. Exceptions to the
// exception rule: caps.sampler is a STRING present everywhere ('native' |
// 'page' — informational, don't branch); caps.ai reflects the BUILD (needs
// a launcher compiled against FoundationModels) plus macOS 26 at runtime.

// requirements — turn "missing system pieces" into an actionable prompt
await tiny.system.requirements(ids?, { refresh: true });
// -> [{ id, ok, feature, detail, install: { manager, packages, command } }]
await tiny.system.missing(ids?);      // unsatisfied only ([] on mac/win)
await tiny.system.promptMissing(ids?, { title, ok, cancel });
// ids: media.aac media.h264 media.mp3 speech spotlight.index audioTap tray
//      windowPosition mouseTracking. Probes cached for the app's lifetime —
//      pass { refresh: true } after the user installs. Shows nothing when
//      satisfied; where nothing installable fixes it (windowPosition on
//      Wayland) it explains instead of faking a command.

await tiny.system.locale();  // { language, languages, system, region, timeZone }
// Pages rarely need it (navigator.language/Intl work); the BACKEND does —
// txiki has no Intl and LANG is wrong/absent. languages = filtered to bundle
// localizations, system = raw user preference (differ for an English-only
// app on a French Mac). macOS only; 'locale' page event on change.
await tiny.system.battery(); await tiny.system.wifi();  // mac (wifi: ssid→Location perm)
await tiny.system.idleTime();   // seconds since last input (linux: GNOME)

await tiny.theme.get();         // { dark } | null
tiny.theme.on((dark) => ...);
tiny.api.on('sleep', fn); tiny.api.on('wake', fn);  // backend: export onSystem

await tiny.app.power.preventSleep('reason', { display: false });
await tiny.app.power.allowSleep();   // auto-released on exit/crash

tiny.hotkey.register('boss', 'cmd+shift+k'); tiny.hotkey.on((id) => ...);
tiny.hotkey.unregister('boss');   // system-wide; cmd = Ctrl on win/linux;
// linux: XGrabKey on X11, GlobalShortcuts portal on Wayland (one approval)

await tiny.app.keystroke('cmd+v');   // native CGEvent -> { ok, trusted };
await tiny.app.paste();              // = keystroke('cmd+v'); hide() first to
                                     // paste into the frontmost app.
                                     // Linux: X11/XWayland only (XTest).
await tiny.app.frontmostApp();  // { name, bundleId, pid } | null (mac/win)

await tiny.app.screens();  // [{ id, name, x, y, width, height, scale,
                           //    visible: {...}, primary }] — same coords as
                           //    win.setPosition
await tiny.app.mousePosition();  // { x, y, window: { x, y, inside }, screen }
// Linux: X11 only — pure Wayland hides the global pointer (capabilities()
// says so; "windowPlacement": true forces XWayland, or use mouseTracking):
await tiny.app.mouseTracking.start();  // true, or throws { code: 'unsupported'
// | 'denied' | 'failed' }. No-op ok where tracking is already global (mac,
// win, X11); Wayland arms a ScreenCast portal session in cursor-metadata
// mode (pixels never mapped) behind ONE consent dialog — the restore token
// rides the app's store so re-arms are dialog-free. The desktop's sharing
// indicator shows while armed. Requirements id: 'mouseTracking'.
tiny.app.mouseTracking.stop();

await tiny.app.captureScreen(screenId?);  // { path (png), width, height };
                                          // 'screen' perm + macOS 14; win: no
                                          // perm; linux: X11 sessions only
await tiny.app.pickColor();     // eyedropper -> '#rrggbb' | null (mac + linux)
await tiny.app.spotlight(q);    // file search -> paths (mac; linux via locate)
```

## App surface (icon/badge/progress/presence)

```js
tiny.app.badge('3');                     // '' clears. mac/win; Linux via the
   // Unity LauncherEntry protocol — BUILT apps only (signal addresses the
   // .desktop entry) and only docks that implement it (KDE, Ubuntu Dock,
   // Dash-to-Dock — not vanilla GNOME Shell); capabilities() reports per
   // session. app.progress rides the same protocol on Linux.
tiny.app.progress(0.4);                  // determinate bar on Dock/taskbar;
                                         // null clears; composes with icon()
tiny.app.attention({ critical: false }); // bounce/flash until activated
tiny.app.icon(pngPath);                  // '' resets (canvas -> live tile);
   // linux: X11 sessions only (Wayland has nowhere to put it) — same for
   // attention and presence there
tiny.app.presence('menubar');            // hide from Dock ('normal' back)
await tiny.app.launchAtLogin.get();      // 'enabled'|'disabled'|
await tiny.app.launchAtLogin.set(true);  // 'requires-approval'|'unsupported'
                                         // (packaged apps; dev -> unsupported)
await tiny.app.info();  // { version, tinyjs, runtime }
```

## Permissions

```js
await tiny.app.permissions.check('accessibility');   // 'granted'|'denied'|
                                                     // 'undetermined'|'unsupported'
await tiny.app.permissions.request('accessibility'); // prompts / opens Settings
// names: accessibility | screen | notifications | microphone | camera |
//        automation[:<bundle-id>]
```

- Off macOS, `granted` usually means "not gated here", not "consent given".
- mic/camera: `getUserMedia()` works in the page; PACKAGED apps must declare
  `"permissions": { "microphone": "why", "camera": "why" }` (Info.plist
  usage strings — required or macOS kills the app; hardened-runtime
  entitlements when signing). Linux: the manifest IS the gate — the launcher
  answers WebKit's permission request from it, undeclared = NotAllowedError,
  and there's no OS prompt underneath.
- speech-to-text: the page's `webkitSpeechRecognition` (WebKit AND WebView2
  have it) needs BOTH `microphone` and `speechRecognition` usage strings in
  a built app — missing the second = `service-not-allowed` with no prompt.
- dev-mode: TCC grants attach to the SHARED dev launcher binary — all dev
  apps share them; packaged apps get their own.

## Deep links, files, single instance

```js
tiny.app.onOpenUrl((url) => ...);      // "urlScheme": "myapp" in tinyjs.json
tiny.app.onOpenFiles((paths) => ...);  // "fileExtensions": ["md"]; also argv
// backend: export onOpenUrl(url, app), onOpenFiles(paths, app)
// Cold-start buffered; second launch forwards to the running copy (single
// instance is automatic). "openFolders": true adds a folder document type.
// app.setAsDefaultHandler(ext) -> 'ok'|'unsupported'|'failed' (Linux) — call
// when the user asks, never on first run.
```

## Auto-update

```js
// tinyjs.json: "update": { "url": "https://…/manifest.json", "auto": "launch"|"daily" }
tiny.api.on('update-available', ({ current, latest, notes }) => ...);
// backend: export onUpdateAvailable(info, app)
const { available, latest } = await tiny.api.call('update.check');
await tiny.api.call('update.install');   // verify sha256 + swap + relaunch
```

Full packaging/manifests: references/release.md.

## macOS-only: tiny.macos.*

`quickLook(path|paths)`, `applescript(src)` ('automation' TCC, in-process),
`ocr(png)` (Vision; normalises lookalike glyphs — never string-equal test;
give edge margin), `recorder.start/stop` (display → H.264 mp4),
`selectedText()`, `otherWindows()`, `moveWindow(pid, rect)` (Accessibility),
`ai.availability()/generate()` (on-device FoundationModels; needs macOS 26 +
Apple Intelligence ON — ALWAYS guard on availability(); tool calling is
backend-only and you must read `calls`, not the prose, to know what ran).

**Off macOS every `tiny.macos.*` call REJECTS with the reason** (not null —
an `if (!result)` branch must become a try/catch). If it's on `tiny.app`
instead, it does something on at least one other OS.

## Backend runtime (txiki.js)

`tjs.readFile/writeFile/readDir/stat`, `tjs.spawn`, `tjs.watch`,
`tjs.listen/connect`, `fetch` (bridge-wrapped: redirects + TLS1.2 hosts
handled), `WebSocket`, `tjs:sqlite`, WebCrypto, FFI. Docs: txikijs.org.
Gotchas: streams need `getReader()` (no `for await`); `tjs.cwd` is a
property; spawn stdio silencer is `'ignore'`; NO Intl (format in the page,
`system.locale()` for backend branching); no Node builtins ever — see
references/electron-migration.md.

Backend exports the scaffold wires: `api`, `init(app)`, `onMenu`, `onTray`,
`onContextMenu`, `onHotkey`, `onSystem`, `onWindowState`, `onWindowClosed`,
`onOpenUrl`, `onOpenFiles`, `onMediaKey`, `onNotificationClick`,
`onNotificationAction`, `onUpdateAvailable`, `onClipboardChange`.

## Clipboard, media keys, speech — quick reference

```js
await tiny.clipboard.read();   // { kind: 'files'|'image'|'color'|'text'|'empty',
   // changeCount, text, html, paths, image (png temp path, valid until next
   // change), imageSize, color, concealed (password managers — history apps
   // must skip), sourceApp (exact while watch() runs), sourceURL }
tiny.clipboard.write({ text, html, paths, image, color });   // any combo
await tiny.clipboard.changeCount();
tiny.clipboard.watch(500); tiny.clipboard.onChange(({ changeCount, self }) => ...);

tiny.app.nowPlaying.set({ title, artist, album, duration, elapsed, playing });
tiny.app.onMediaKey(({ command, time }) => ...);  // play|pause|toggle|next|
tiny.app.nowPlaying.clear();                      // previous|seek. mac + linux
                                                  // (MPRIS: media widget + lock screen)
await tiny.app.voices();  // [{ id, name, lang, quality }]
await tiny.app.say(text, { voice, rate });  tiny.app.stopSpeaking();
// linux: speech-dispatcher's spd-say when installed (requirements id 'speech')
```
