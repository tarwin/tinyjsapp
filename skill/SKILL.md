---
name: tinyjs
description: Build and modify tinyjs desktop apps — tiny macOS (and beta Windows/Linux) apps with a txiki.js JavaScript backend and a native webview window. Use when working in a project with a tinyjs.json, or when the user mentions tinyjs, tiny.api, or tinyjs dev/build.
---

# Building tinyjs apps

tinyjs (https://tinyjs.app, repo tarwin/tinyjsapp) makes ~6 MB desktop apps:
a **txiki.js backend** (full system access: files, sockets, processes, FFI)
+ a **native webview window** — WKWebView/WebKit on macOS, WebView2 on
Windows, GTK3 + WebKitGTK 4.1 on Linux (both beta support). They talk
JSON-RPC over a private Unix socket (macOS, Linux) or named pipe (Windows) —
no HTTP server, no ports.

## Platforms

macOS is the primary platform; Windows and Linux support are both in beta.
The same project runs on all three — same tinyjs.json, same `tiny.*` api,
same commands.

| | macOS | Windows | Linux |
|---|---|---|---|
| toolchain to develop tinyjs ITSELF | Xcode CLT (`./setup.sh`) | MinGW-w64 g++ (`winget install BrechtSanders.WinLibs.POSIX.UCRT`) + WebView2 runtime (preinstalled on Win 11); run `setup.ps1`, use `tinyjs.cmd` | `apt install build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libayatana-appindicator3-dev` (Debian/Ubuntu); run the same `./setup.sh` as macOS |
| app users need | macOS 14+ (Apple Silicon) | Windows 10/11 with the WebView2 runtime | Ubuntu 24.04+ / current distro with `webkit2gtk-4.1`, X11 or Wayland |
| `tinyjs build` output | `dist/<Name>.app` (codesigned) + bare `dist/<name>` | portable `dist/` folder: `<name>.exe` + `launcher.exe` + `frontend/` | portable `dist/` folder: compiled backend binary (frontend + icon ride inside) + `launcher` + `icon.png` |
| `publish` / auto-update | yes | yes (update swaps files in place, then relaunches) | yes (same file-swap + relaunch) |
| `notarize` | yes | n/a (no codesigning; https+sha256 is the update trust anchor) | n/a (same https+sha256 trust anchor); a built app self-registers a `.desktop` entry on first run instead of an install step |

Works on ALL THREE platforms: the whole bridge (api calls, push events,
`tiny.fetch`/`proxyURL` streaming), dev/hot-reload, Vite `devUrl`,
multi-window (`win.open` — per-window bridge everywhere), file/folder/save
dialogs, alert/confirm/prompt, menu bar (+ `menu.update`/`get`, `key:`
accelerators — cmd on mac, Ctrl on win/linux), custom context menus +
`contextMenu: false`, clipboard (text/html/files/image, read + write +
watch), global hotkeys (`cmd` maps to Ctrl on win/linux; linux uses
XGrabKey on X11 and the GlobalShortcuts portal on pure Wayland — the
compositor prompts once to approve them), `keystroke`/`paste`
(X11/XWayland only on linux — XTest, no pure-Wayland route),
`shell.open/reveal/trash`, `secrets` (Keychain / Credential Manager / Secret
Service), `power.preventSleep`, `theme` + sleep/wake events, `store`,
`screens`/`mousePosition`/`paths`/`battery`/`idleTime` (linux: needs GNOME),
`printToPDF`, `captureScreen` (win: no permission needed; linux: X11
sessions only), `thumbnail` (linux: images only), `say`/`voices`/
`stopSpeaking` (linux: via speech-dispatcher's `spd-say` when installed),
`launchAtLogin` (built apps only on win/linux; linux = autostart
`.desktop`), auto-update (`update.check/install` — linux ships a per-arch
`"linux": { "<arch>": { url, sha256 } }` manifest block alongside mac/win),
window ops (hide/show/center/minimize/fullscreen/ontop/resizable/pos/level/
clickThrough/hideOnClose/zoom, `data-tiny-drag` regions,
`chrome.frame`/`squareCorners`; win maps `transparent` to a clear WebView2
background and vibrancy names to mica/acrylic backdrops on Win11 — BUT a
transparent MAIN window on Windows must be declared in tinyjs.json
`"chrome"` (or `win.open` options for secondaries), not just via a late
`setChrome`, and transparency + a Win32 menu bar are mutually exclusive
there; linux accepts frameless/transparent chrome too but `vibrancy` is a
no-op), `dock.bounce` (taskbar flash on win, urgency hint on linux), sqlite.

Windows-specific: drag & drop with real paths BOTH ways (`win.onDrop`,
`startDrag({ files })`); tray + `notify` work (balloon notifications, no
action buttons, no reply fields); `frontmostApp` works (rejects on Linux).

Linux-specific: tray is AppIndicator/StatusNotifier — menu-based; a bare
icon click (no menu set) is emulated via a synthetic menu entry, and
`tray.position()` returns `null`. `notify` DOES support action buttons
(`org.freedesktop.Notifications`), just no reply fields. `pickColor` works
(portal-based) — unlike Windows. Deep links / file associations / single
instance work — a built app self-registers its `.desktop` entry (app-menu
listing, icon, `urlScheme`, `fileExtensions`, single-instance) on first run,
no separate install step. `nowPlaying`/media keys work via MPRIS (shows in
the GNOME/KDE media widget + lock screen; transport routes to `onMediaKey`).
`audioTap` works for `scope:'system'` (the default sink's monitor via
`parec`/`pw-cat`); `scope:'app'` is approximated by the system mix, same as
Windows.

macOS-only (on Windows these reject or answer `'unsupported'`/null — always
feature-detect): notification action buttons, `audioTap`, `proxyURL` media
proxy, deep links / file associations / single instance, `permissions.*`
TCC flow (Windows answers 'granted'), `quickLook`, `recorder`, `pickColor`,
`ocr`, `authenticate`, `applescript`, `nowPlaying`/media keys,
`selectedText`/`otherWindows`/`moveWindow`, `share`, `haptic`,
`dock.setBadge`/`dockIcon`, `setAllSpaces`, `wifi`, `spotlight`,
`tiny.app.ai`. (Windows plans: tarwin/tinyjsapp TODO-windows.md.)

Not supported on Linux (reject or answer `'unsupported'`/`null` — always
feature-detect): `recorder`, `ocr`, `quickLook`, `applescript`,
`haptic`, Dock badge/`bounce({critical: true})`/`dockIcon`,
`share`, `wifi`, `selectedText`/`otherWindows`/`moveWindow`/`frontmostApp`,
`authenticate`, `tiny.app.ai`, `setAllSpaces` (maps to sticky windows
instead of true per-Space follow). These fail cleanly — capability calls
reject with a specific reason, query calls resolve `null`, fire-and-forget
ones no-op — so nothing hangs. (`spotlight` DOES work: name search via
`plocate`/`locate` or a bounded `find`.) (Linux plans: tarwin/tinyjsapp
TODO-linux.md.)

Cross-platform app pattern: gate features, don't fork code —
`if ((await tiny.app.permissions.check('accessibility')) !== 'unsupported')`,
`try { await tiny.win.printToPDF(p) } catch { /* fallback */ }`. Backend
paths: use `app.paths` (per-OS correct) instead of hardcoding `~/Library` or
`%APPDATA%`; join with '/' (works everywhere in tjs).

## Commands

```sh
tinyjs new <dir>    # scaffold (zero dependencies)
tinyjs new <dir> --template react-ts|vue-ts|svelte-ts|solid-ts|vanilla-ts|…
                    #   create-vite + tinyjs overlay: HMR dev server in the
                    #   native window, esbuild-bundled TS backend (npm pkgs ok)
tinyjs dev          # run with hot reload (frontend edits swap in place;
                    #   backend edits restart the process)
tinyjs build        # dist/<name> binary + dist/<Name>.app (codesigned)
                    #   --dmg: also dist/<name>-<ver>.dmg installer image
tinyjs publish      # build + dist/publish/<name>-<ver>.zip + auto-update manifest
tinyjs notarize     # notarytool submit + staple (needs Developer ID + profile)
                    #   --dmg: rebuild the dmg from the stapled .app (else its
                    #   ticket is missing; auto-rebuilt if a dmg already exists)
TINYJS_DEBUG=1 tinyjs dev   # trace every bridge message
```

## Project layout

```
tinyjs.json          { name, title, size, id, version, icon?, signIdentity?,
                       update?: { url: "https://…/manifest.json" },
                       urlScheme?: "myapp", fileExtensions?: ["md"],
                       permissions?: { microphone?: "why", camera?: "why" },
                       audioTap?: "app" | "system",   // enable tiny.audioTap
                       contextMenu?: false,           // suppress WebKit's default right-click menu (default true)
                       chrome?: { frame, trafficLights, transparent, vibrancy, squareCorners, acceptsFirstMouse },
                       backend?: "backend/main.ts",   // .ts → esbuild bundle
                       frontend?: { build: "npm run build", dist: "dist",
                                    dev: "npm run dev", devUrl: "http://127.0.0.1:5173" } }
icon.png             1024×1024 app icon
src/main.js          backend (see below)
src/frontend/        index.html + js/css/images — served as real files
                     (file:// document), so relative paths just work;
                     multi-file frontends are fine
```

## Backend (src/main.js)

```js
export const api = {
  // callable from the page as tiny.api.call('readNotes', {dir}) — return
  // value resolves the page's promise; throwing rejects it
  readNotes: async ({ dir }, app) => { ... },
};

export function init(app) {
  // runs once the window is up
  app.push('event-name', data);      // push to the page (tiny.api.on)
  app.setTitle(t); app.setSize(w, h); app.setMenu(menus); app.quit();
  // also: notify({title, body}), hide()/show()/center()/minimize()/
  // fullscreen(), setPosition(x, y), setAlwaysOnTop(v), setResizable(v),
  // setHideOnClose(v), setDockVisible(v), print(), tray.set/remove,
  // store.get/set/delete/all, hotkey.register/unregister,
  // setContextMenu(items), update.check()/update.install(),
  // clipboard.read/write/changeCount/watch/unwatch, keystroke(combo),
  // paste(), permissions.check/request, mousePosition(), screens(),
  // paths, shell.open/reveal/trash, launchAtLogin.get/set,
  // dock.setBadge/bounce, power.preventSleep/allowSleep, frontmostApp(),
  // beep()/playSound(target), window(id).share(opts), idleTime(),
  // quickLook(paths), captureScreen(screenId), pickColor(), ocr(path),
  // thumbnail(path, size), secrets.get/set/delete, authenticate(reason),
  // applescript(source), nowPlaying.set/clear, say(text, opts), voices(),
  // stopSpeaking(), show({ activate: false })
}

export function onMenu(id, app) { ... }  // optional: menu clicks, backend-side
export function onTray(id, app) { ... }  // optional: tray clicks (id null = icon)
```

Runtime is txiki.js (`tjs` global): `tjs.readFile/writeFile/readDir/stat`,
`tjs.spawn`, `tjs.watch`, `tjs.listen/connect`, `fetch`, `WebSocket`, sqlite,
FFI. Docs: https://txikijs.org. Gotchas: streams need `getReader()` (no
`for await`); `tjs.cwd` is a property; spawn stdio silencer is `'ignore'`.

Webview gotchas: (1) occluded/off-screen windows are THROTTLED (WebKit
starves rAF + timers), so a hidden window can't drive a visible one — do
continuous work in the visible window or the (un-throttled) backend. (2)
`file://` media (`<audio>`/`<img>`) only loads assets under the frontend
dir by default; widen with `createApp({ readAccess: true | '/path' })` or
`"readAccess"` in tinyjs.json — else `MEDIA_ERR_SRC_NOT_SUPPORTED`. (3) the
default UA lacks `Version/x Safari/x`, so UA-sniffing sites reject it — set
`createApp({ userAgent: '…' })` / `"userAgent"` in tinyjs.json (handy for
wrapping a hosted site via `devUrl`, though many SaaS apps also feature-detect
and refuse embedded webviews regardless).

## Frontend

The `tiny` global is injected into every page automatically (no script tag);
TypeScript definitions live in types/tiny.d.ts (TinyApiHandler, TinyApp, …):

```js
await tiny.api.call('method', { params })   // -> backend api.<method>
tiny.api.on('event-name', (data) => ...)    // <- app.push from backend

tiny.log(msg); tiny.quit();
// file:// URL for a disk path — ALWAYS use this (never 'file://' + path,
// which breaks on Windows: the drive letter becomes the URL host)
audio.src = tiny.fileURL(backendProvidedPath);
await tiny.app.info();   // { version: <app>, tinyjs: <built with>, runtime: <txiki> }

// Backend-proxied fetch — NO CORS/CSP (runs in the native process). Like
// window.fetch, returns a real Response. { stream: true } = live streaming
// body (res.body.getReader()); required for endless sources (internet radio).
const r = await tiny.fetch(url, { method, headers, body });     // buffered
const s = await tiny.fetch(streamUrl, { stream: true });        // s.body.getReader()

// Get a cross-origin stream (internet radio) INTO Web Audio — a
// MediaElementSource on a cross-origin <audio> is silent by spec; proxyURL
// streams it through the native layer with permissive CORS so it's untainted.
audio.crossOrigin = 'anonymous';
audio.src = tiny.proxyURL('https://host/stream.mp3');  // now drives EQ/analyser

// tiny.audioTap — read the app's rendered OUTPUT as PCM (VU meters, viz),
// including audio that bypasses Web Audio (native HLS, tainted streams). Needs
// "audioTap":"app"|"system" in tinyjs.json. macOS 14.4+. Read-only.
await tiny.audioTap.start({ scope: 'app', interval: 80 });  // true, or throws { code }
tiny.audioTap.on(({ pcm, sampleRate, channels, frames, t }) => {
  const bin = atob(pcm), n = bin.length >> 1;   // base64 -> interleaved LE Int16
  // sample i: ((bin.charCodeAt(2*i) | bin.charCodeAt(2*i+1)<<8) << 16 >> 16) / 32768
});                                              // tiny.audioTap.stop()
// Auth is deferred to the FIRST start() (declaring the manifest does nothing
// until then — lazy-arm friendly). That first start() prompts for "System
// Audio Recording" even for scope:'app' (WKWebView audio is in a separate
// com.apple.WebKit.GPU helper = cross-process). Under `tinyjs dev` the OWNER
// is the terminal, so it delivers only if the terminal holds the grant, else
// silent; a built .app owns its own grant. Denial surfaces as silent chunks.

tiny.win.setTitle(t); tiny.win.setSize(w, h);
await tiny.win.openFile();                  // path | null (native panel)
await tiny.win.openFiles();                 // paths[] | null
await tiny.win.pickFolder();                // path | null
await tiny.win.saveFile();                  // path | null
await tiny.win.alert(message, detail);      // native alert, resolves true
await tiny.win.confirm(message, { detail, ok, cancel });  // true | false
await tiny.win.prompt(message, { default, ok, cancel });  // string | null

tiny.menu.set([{ title: 'Actions', items: [
  { id: 'open', label: 'Open…', key: 'o' },   // key = cmd+<key>
  { id: 'mute', label: 'Mute', checked: true },     // checkmark
  { id: 'no', label: 'Nope', enabled: false },      // grayed out
  { separator: true },
  { id: 'more', label: 'More', submenu: [{ id: 'a', label: 'Sub' }] },
]}]);
tiny.menu.on((id) => ...);                  // clicks (also a 'menu' api event)
tiny.menu.update('mute', { checked: false, label: 'Unmuted' });  // patch live
await tiny.menu.get('mute');                // { exists, label, checked, enabled }
// same item shape + update/get work for tray and context menus

tiny.notify(title, body, { id, subtitle, sound });  // desktop notification
// packaged + signed (even Apple Development): native Notification Center
// banners with click routing: tiny.app.onNotificationClick((id) => ...) /
// backend export onNotificationClick(id, app). Ad-hoc/dev: osascript fallback.
tiny.win.center(); tiny.win.minimize(); tiny.win.restore();
tiny.win.fullscreen(); tiny.win.setFullscreen(bool);   // toggle / absolute
await tiny.win.getState();  // { x, y, width, height, fullscreen, minimized,
                            //   visible, focused, alwaysOnTop, resizable, screen }
tiny.win.setPosition(x, y);                 // top-left origin
tiny.win.setChrome({ frame: false, trafficLights: false,
                     transparent: false, vibrancy: 'hud' });  // frameless etc.
// squareCorners: true drops macOS's rounded corners → BORDERLESS window
// (square, no titlebar/traffic lights; no native titlebar drag — use
// data-tiny-drag; resize/shadow/focus kept). Put it in tinyjs.json "chrome"
// to apply before first paint (no rounded→square flash).
tiny.win.setChrome({ squareCorners: true });
// acceptsFirstMouse: true → the click that focuses an unfocused window also
// reaches the page (macOS swallows it by default). Good for palettes/toolbars
// and DOM drag regions on unfocused windows.
tiny.win.setChrome({ acceptsFirstMouse: true });
// drag regions: <header data-tiny-drag> — drag moves window, dblclick zooms;
// interactive children excluded (data-tiny-nodrag to opt out manually)
tiny.win.setAlwaysOnTop(v); tiny.win.setResizable(v);
tiny.win.hide(); tiny.win.show(); tiny.win.setHideOnClose(v);
// hide() hides the APP (NSApp hide) — focus returns to the previous app,
// so palettes can hide() then app.paste() with no frontmost tracking.
tiny.win.show({ activate: false });  // surface WITHOUT stealing focus (HUDs)
await tiny.app.mousePosition();      // { x, y, window: { x, y, inside },
                                     //   screen: { x, y, width, height,
                                     //   scale } } — global coords match
                                     // win.setPosition; window is relative
                                     // to this window's content area
                                     // (clientX/Y units, valid even while
                                     // the cursor is outside it)
tiny.win.onDrop((paths) => ...);            // files dropped on the window: real paths

// tray / menu-bar apps
tiny.tray.set({ title, icon, tooltip, menu: [{ id, label }, { separator: true }] });
// icon: png path OR 'sf:<name>' (SF Symbol, macOS) OR 'emoji:<glyph>' (Windows —
// drawn as a mono tray silhouette); branch per-OS for asset-free icons on both
// primaryAction: true → left click fires onClick, menu opens on right-click
// Linux: AppIndicator/StatusNotifier, menu-based — a bare icon click (no menu
// set) is emulated via a synthetic menu entry, and tray.position() -> null
tiny.tray.on((id) => ...); tiny.tray.onClick(fn); tiny.tray.remove();
tiny.app.setDockVisible(false);             // menu-bar-only app
// tray-app recipe: tinyjs.json { "activation": "accessory" } (launches with no
// Dock icon and window hidden — no flash) + tray.set + win.setHideOnClose(true);
// tiny.win.show() when needed. Without the config flag: tray.set +
// win.setHideOnClose(true) + app.setDockVisible(false) in init().

// auto-update (needs tinyjs.json "update".url; ships via `tinyjs publish`)
const { available, latest } = await tiny.api.call('update.check');
await tiny.api.call('update.install');      // verify + swap .app + relaunch

// persistent settings (~/Library/Application Support/<app id>/store.json)
await tiny.store.set('key', anyJsonValue);
await tiny.store.get('key');                // value | null
await tiny.store.delete('key'); await tiny.store.all();

// global hotkeys (system-wide, fire even when unfocused)
tiny.hotkey.register('boss', 'cmd+shift+k'); tiny.hotkey.on((id) => ...);
tiny.hotkey.unregister('boss');             // backend: export onHotkey(id, app)

// custom right-click menu (native; null restores WebKit default)
tiny.menu.setContext([{ id, label }, { separator: true }]);
tiny.menu.onContext((id) => ...);           // backend: export onContextMenu
// "contextMenu": false in tinyjs.json hides WebKit's default menu entirely; setContext still overrides

// theme + power events
await tiny.theme.get();                     // { dark } | null
tiny.theme.on((dark) => ...);               // live changes
tiny.api.on('sleep', fn); tiny.api.on('wake', fn);  // backend: export onSystem

// clipboard (native NSPasteboard in the launcher — no pbpaste/osascript spawns)
await tiny.clipboard.read();   // { kind: 'files'|'image'|'color'|'text'|'empty',
                               //   changeCount, text, html, paths, image,
                               //   imageSize ({width,height} px), color,
                               //   concealed (password-manager marker — history
                               //   apps must skip), sourceApp ({name,bundleId},
                               //   exact while watch() runs), sourceURL
                               //   (Chromium copy's page url) }
                               // image = png temp path, valid until the next
                               // clipboard change (copy the file to keep it)
tiny.clipboard.write({ text, html, paths, image, color });  // any combo;
                               // image: png path, data: URL, or base64;
                               // multiple paths all land (no flush race)
await tiny.clipboard.changeCount();         // cheap change probe
tiny.clipboard.watch(500); tiny.clipboard.unwatch();  // poll in the launcher
tiny.clipboard.onChange(({ changeCount, self }) => ...);  // self = own write
// backend: app.clipboard.* is the same api; passing onClipboardChange to
// createApp auto-starts the watcher

// drag files OUT of the app (into Finder/Slack/…): call from mousedown,
// while the button is held; image: optional custom drag-image png
el.addEventListener('mousedown', () => tiny.win.startDrag({ files: [path] }));

// native keystrokes (CGEvent from the launcher — ONE permission,
// Accessibility, and the prompt names your app, not osascript/terminal)
await tiny.app.keystroke('cmd+v');          // -> { ok, trusted }
await tiny.app.paste();                     // = keystroke('cmd+v'); hide() first
                                            // to paste into the frontmost app

// permissions — build onboarding instead of failing at first use
await tiny.app.permissions.check('accessibility');  // 'granted'|'denied'|
                                            // 'undetermined'|'unsupported'
await tiny.app.permissions.request('accessibility'); // prompts / opens Settings
// names: accessibility | screen | notifications | microphone | camera |
//        automation[:<bundle-id>]
// mic/camera: getUserMedia() works in the page (launcher auto-grants WebKit's
// per-origin prompt; only the system TCC dialog shows). Packaged apps must set
// "permissions": {"microphone": "why", "camera": "why"} in tinyjs.json —
// injected as Info.plist usage strings (required, or macOS kills the app) and,
// when signIdentity is set, as hardened-runtime device entitlements.
// dev-mode gotcha: TCC grants attach to the SHARED launcher binary
// (~/.tinyjs), not your app — all dev apps share them; packaged apps get
// their own. 'screen' never reads 'undetermined' (macOS only exposes a
// yes/no preflight for screen recording).

// shell — the NSWorkspace verbs apps otherwise spawn `open` for; each
// resolves true or rejects with the reason
await tiny.app.shell.open('https://x.com'); // URL (any scheme) or file path
await tiny.app.shell.reveal(path);          // show in Finder
await tiny.app.shell.trash(path);           // recoverable — prefer over delete

await tiny.app.screens();  // every display, same coords as win.setPosition:
                           // [{ id, name, x, y, width, height, scale,
                           //    visible: {x,y,width,height} (minus menu bar/
                           //    Dock), primary (menu-bar screen) }]

await tiny.app.paths();    // { home, data, cache, logs, temp, downloads,
                           //   desktop, documents } — data/cache/logs per
                           // app id, create on first write; backend twin
                           // app.paths is a plain object (no await)

// launch at login (packaged .app on macOS 13+; dev mode -> 'unsupported')
await tiny.app.launchAtLogin.get();         // 'enabled'|'disabled'|
await tiny.app.launchAtLogin.set(true);     //  'requires-approval'|'unsupported'
// 'requires-approval' = user must allow in System Settings > Login Items

tiny.app.dock.setBadge('3');                // '' clears
tiny.app.dock.bounce({ critical: false });  // bounce until activated

// keep the system awake (replaces caffeinate; auto-released on exit/crash)
await tiny.app.power.preventSleep('reason', { display: false });
await tiny.app.power.allowSleep();

await tiny.app.frontmostApp();  // { name, bundleId, pid } | null — the
                                // active app (focus target after hide())

await tiny.app.beep();                   // system beep
await tiny.app.playSound('Ping');        // system sound name or audio file
                                         // path -> false if it didn't load

// native share sheet — anchor at the click's clientX/clientY
tiny.win.share({ text, url, paths, x: e.clientX, y: e.clientY });

await tiny.app.idleTime();      // seconds since the user's last input
tiny.app.quickLook(pathOrArray); tiny.app.quickLook();  // preview / close
await tiny.app.captureScreen(screenId?);  // -> { path (png, yours), width,
                                // height }; needs 'screen' perm + macOS 14,
                                // rejects with the reason otherwise

await tiny.app.pickColor();     // system eyedropper (NO screen-recording
                                // perm) -> '#rrggbb' | null on cancel
await tiny.app.ocr(pngPath);    // on-device Vision OCR -> { text, blocks:
                                // [{text, confidence, box (0..1 top-left)}] }
await tiny.app.thumbnail(path, size?);  // preview png for ANY file type ->
                                // { path, width, height }
await tiny.app.secrets.get(key);        // Keychain (keytar role): tokens go
await tiny.app.secrets.set(key, value); // here, NEVER in tiny.store;
await tiny.app.secrets.delete(key);     // get -> string | null
await tiny.app.authenticate(reason);    // Touch ID / password sheet ->
                                        // true | false (false = cancel)
await tiny.app.applescript(src);        // in-process, no osascript spawn;
                                // 'automation' TCC; -> result string | null,
                                // rejects with the script error

// Now Playing (Control Center / lock screen) + hardware media keys
tiny.app.nowPlaying.set({ title, artist, album, duration, elapsed, playing });
tiny.app.onMediaKey(({ command, time }) => …);  // play|pause|toggle|next|
tiny.app.nowPlaying.clear();                    // previous|seek (time=secs)
// text-to-speech; say() resolves when playback ends (false if interrupted)
await tiny.app.voices();        // [{ id, name, lang, quality }]
await tiny.app.say(text, { voice, rate });  tiny.app.stopSpeaking();
// notifications with buttons / a reply field (packaged apps)
tiny.notify(title, body, { actions: [{ id, title, reply?, placeholder?,
                                       destructive? }] });
tiny.app.onNotificationAction(({ id, action, reply }) => …);  // reply=text
// backend exports: onMediaKey(info, app), onNotificationAction(info, app)
// record a display to .mp4 (SCStream→H.264, video only; 'screen' perm +
// macOS 14; one at a time)
await tiny.app.recorder.start({ path, screenId });  // resolves once capturing
const { path, duration } = await tiny.app.recorder.stop();

// window superpowers (overlays/HUDs/pets/cross-Space palettes)
tiny.win.setClickThrough(true);   // mouse events pass through
tiny.win.setLevel('overlay');     // 'normal'|'floating'|'overlay'|'desktop'
tiny.win.setAllSpaces(true);      // follow across Spaces + over fullscreen
// AX (Accessibility perm): grab selection anywhere + arrange other windows
await tiny.app.selectedText();    // string | null (frontmost app's selection)
await tiny.app.otherWindows();    // [{ app, pid, title, x,y,width,height }]|null
await tiny.app.moveWindow(pid, { x, y, width, height });  // resolves true/throws
await tiny.tray.position();       // { x,y,width,height }|null (anchor dropdowns)
// deep-Mac citizen
await tiny.win.printToPDF(path);  // -> { path } (vector PDF; invoices/reports)
tiny.app.haptic('generic');       // 'generic'|'alignment'|'level' (Force Touch)
tiny.app.dockIcon(pngPath);       // '' resets (render a canvas for badges)
await tiny.app.battery();         // { percent, charging, plugged, minutesRemaining }|null
await tiny.app.wifi();            // { ssid, bssid, rssi, noise, txRate }|null (ssid→Location)
await tiny.app.spotlight(query);  // find files by name/content -> up to 100 paths
// on-device LLM (Apple FoundationModels; offline, no key). Ships only in a
// TINYJS_AI build on macOS 26 — always guard on availability().
if (await tiny.app.ai.availability() === 'available')   // |'unavailable'|'unsupported'
  await tiny.app.ai.generate(prompt, { instructions }); // instructions = system prompt

tiny.win.print();                           // native print panel

// auto-update: "update": { "url": …, "auto": "launch" | "daily" } checks in
// the background (packaged apps) → 'update-available' page event /
// onUpdateAvailable(info, app) export with { current, latest, notes };
// notes come from `tinyjs publish --notes "…"` (or --notes-file FILE).
// Then: await tiny.api.call('update.install')

// multiple windows: any frontend html file can be a window
tiny.win.open('settings', { page: 'settings.html', title: 'Settings', size: '420x300' });
// chrome + x/y are applied BEFORE first paint (no titlebar flash / center-jump):
tiny.win.open('hud', { page: 'hud.html', x: 40, y: 40, chrome: { frame: false } });
tiny.win.id; tiny.win.close(); await tiny.win.windows();
// win.* calls target the caller's window; backend: app.openWindow/app.window(id)
// (eval/push/close/setTitle/setSize/chrome/getState…), app.push broadcasts,
// export onWindowClosed(id, app); api handlers get meta: (params, app, meta)
// where meta.window = calling window id

// deep links + file associations (packaged .app only; cold-start buffered;
// second `open` activates the running instance — single-instance is automatic)
tiny.app.onOpenUrl((url) => ...);           // backend: export onOpenUrl(url, app)
tiny.app.onOpenFiles((paths) => ...);       // backend: export onOpenFiles(paths, app)
```

Backend SQLite is built into txiki: `import { Database } from 'tjs:sqlite'`;
`new Database(path)`, `.exec(sql)`, `.prepare(sql).run(...)/.all()/.finalize()`,
`.close()`. Use it over tiny.store for anything query-shaped.

An app menu (About + Quit) and an Edit menu (copy/paste shortcuts) always
exist; `tiny.menu.set` adds menus after them. About shows name + version from
tinyjs.json automatically.

## Rules of thumb

- Add backend capabilities as `api` methods; keep the frontend thin.
- Escape anything interpolated into `innerHTML` — the page holds an RPC
  channel with full system access, so a filename must never become markup.
- Keep frontend asset references relative (they resolve against the page's
  directory); no external fetches at runtime unless intended.
- Never declare a top-level `chrome` identifier in frontend code.
  `window.chrome` is a non-configurable browser global on WebView2, so a
  top-level `const`/`let chrome` is a PARSE-time SyntaxError that kills the
  entire script (and a `function chrome()` silently shadows it).
- Verify changes with the smoke pattern: run
  `TINYJS_HTML=<tinyjs-install>/test/smoke.html tinyjs dev` — expect a
  `[web] SMOKE RESULTS {...}` line with no FAIL entries and a clean exit.
  A GUI window opens briefly; the page drives itself and quits.
- `tinyjs build` output: `dist/<Name>.app` is the distributable (fully
  codesigned); `dist/<name>` is a local-only single binary.
