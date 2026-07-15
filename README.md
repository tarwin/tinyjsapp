# tinyjs

Tiny desktop apps for macOS: a [txiki.js](https://txikijs.org/) backend + a
native [webview](https://github.com/webview/webview) window.

- **~6 MB shipped**, two real files — no Electron, no Node, no bundled Chromium
- **No HTTP server, no ports** — the page and the backend talk over a Unix
  domain socket in a private temp directory
- Backend is plain JavaScript with full system access (files, sockets,
  processes, FFI) via txiki.js
- Frontend is plain HTML/CSS/JS (multi-file, WebGPU included) rendered by
  the system WebKit
- Native menus, dialogs, **tray/menu-bar apps**, notifications, drag & drop
  with real file paths, window control
- Hot reload in dev, signed `.app` bundles out of `build`, **auto-update**
  out of `publish`

## Install

```sh
curl -fsSL https://tinyjs.app/install | sh
```

Installs to `~/.tinyjs` and symlinks `tinyjs` onto your PATH. Pin a
version with `TINYJS_VERSION=vX.Y.Z`. Later, `tinyjs update` re-runs the
installer if a newer release exists (`tinyjs update --check` only reports);
`tinyjs dev` also mentions new releases, checking at most once a day.
To install from source instead:

```sh
git clone https://github.com/tarwin/tinyjsapp && cd tinyjsapp
./setup.sh    # downloads the txiki.js runtime, compiles the launcher
ln -s "$(pwd)/tinyjs" /usr/local/bin/tinyjs
```

Full API reference: [tinyjs.app/api](https://tinyjs.app/api) · release
history: [tinyjs.app/changelog](https://tinyjs.app/changelog).

## Create and run an app

```sh
tinyjs new myapp
cd myapp
tinyjs dev
```

A window opens. `dev` hot-reloads: edit anything in `src/frontend/` and the
page re-renders in place, caches bypassed (no restart, backend state
survives); edit backend sources and the process restarts automatically.
`TINYJS_DEBUG=1 tinyjs dev` traces every message crossing the bridge.

**Frameworks welcome:** `tinyjs new myapp --template react-ts` (or vue-ts,
svelte-ts, solid-ts, vanilla-ts, …) scaffolds a Vite app wired to tinyjs —
`tinyjs dev` runs Vite's dev server with HMR inside the native window, and
`tinyjs build` ships the built assets as usual. TypeScript backends are
bundled with esbuild automatically (which also makes npm packages usable in
the backend). The zero-dependency default scaffold is unchanged.

The `tiny` global is injected into every page by the launcher — no script
tag needed — and ships with full TypeScript definitions (`types/tiny.d.ts`).

A project is just:

```
myapp/
  tinyjs.json            # { name, title, size, id, version, icon?, signIdentity?,
                         #   urlScheme?, fileExtensions?, chrome?, update?, notarize?,
                         #   permissions? ({ microphone: "why", camera: "why" } for getUserMedia),
                         #   activation? ("accessory" = menu-bar agent: no Dock, starts hidden) }
  icon.png                # 1024×1024 app icon (template ships a default)
  src/main.js             # backend: export const api = {...}; export function init(app) {}
  src/frontend/           # index.html + any local js/css/images
```

### Writing the app

Backend (`src/main.js`) — every `api` function is callable from the page;
handlers receive `(params, app)`:

```js
export const api = {
  hello: async ({ name }) => `hi ${name}`,
};
export function init(app) {          // window is up
  setInterval(() => app.push('tick', Date.now()), 1000);
  // app also has: setTitle(t), setSize(w, h), setMenu(menus), eval(js),
  // reload(), quit(), notify({title, body}), hide()/show()/center()/
  // minimize()/fullscreen(), setPosition(x, y), setAlwaysOnTop(v),
  // setResizable(v), setHideOnClose(v), setDockVisible(v), print(),
  // restore(), setFullscreen(v), getWinState(), setChrome(opts),
  // startDrag(), zoom(), tray.set/remove,
  // updateMenuItem(id, patch), getMenuItem(id), info, store.get/set/delete/all,
  // hotkey.register(id, combo)/unregister(id), setContextMenu(items),
  // update.check()/update.install(),
  // clipboard.read()/write(data)/changeCount()/watch(ms)/unwatch(),
  // keystroke(combo), paste(), permissions.check(name)/request(name),
  // mousePosition(), screens(), paths, show({ activate: false }),
  // shell.open(target)/reveal(path)/trash(path),
  // launchAtLogin.get()/set(v), dock.setBadge(text)/bounce(opts),
  // power.preventSleep(reason, opts)/allowSleep(), frontmostApp(),
  // beep(), playSound(target), window(id).share(opts),
  // idleTime(), quickLook(paths), captureScreen(screenId),
  // pickColor(), ocr(path), thumbnail(path, size),
  // secrets.get/set/delete, authenticate(reason), applescript(source),
  // nowPlaying.set/clear, say(text, opts), voices(), stopSpeaking(),
  // recorder.start({ path, screenId })/stop()
}

export function onMenu(id, app) {}   // optional: handle menu clicks backend-side
export function onTray(id, app) {}   // optional: tray clicks (id null = bare icon)
export function onHotkey(id, app) {} // optional: global hotkey presses
export function onContextMenu(id, app) {}      // optional: context menu clicks
export function onSystem(kind, value, app) {}  // optional: 'theme'|'sleep'|'wake'
```

The backend runtime ships SQLite built in, handy for anything `tiny.store`
is too small for:

```js
import { Database } from 'tjs:sqlite';
const db = new Database(dataDir + '/notes.db');
db.exec('CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, text)');
const st = db.prepare('INSERT INTO notes (text) VALUES (?)');
st.run('hello'); st.finalize();
db.prepare('SELECT * FROM notes').all();   // [{ id: 1, text: 'hello' }]
```

Frontend — the `tiny` global is injected into every page automatically:

```js
const greeting = await tiny.api.call('hello', { name: 'world' });  // request/response
tiny.api.on('tick', (t) => ...);                                   // backend push

tiny.log('debug msg');  tiny.quit();
tiny.notify('Done', 'Your export finished');   // desktop notification
// packaged apps get REAL Notification Center banners (your app's icon,
// permission prompt on first use) when built with a signing identity —
// even "Apple Development" works. Ad-hoc/dev builds fall back to osascript.
tiny.notify('Ping', 'body', { id: 'x', subtitle: '…', sound: true });
tiny.app.onNotificationClick((id) => ...);  // backend: export onNotificationClick
await tiny.app.info();  // { version: <app>, tinyjs: <built with>, runtime: <txiki> }

// window control
tiny.win.setTitle('My App');  tiny.win.setSize(1200, 800);
tiny.win.center();  tiny.win.setPosition(100, 80);      // top-left origin
tiny.win.minimize();  tiny.win.restore();
tiny.win.fullscreen();  tiny.win.setFullscreen(true);   // toggle / absolute
tiny.win.setAlwaysOnTop(true);  tiny.win.setResizable(false);
tiny.win.hide();  tiny.win.show();
// hide() hides the APP (NSApp hide) — macOS returns focus to the previously
// active app on its own, so a palette can hide() then app.paste() with no
// frontmost-pid bookkeeping. show() re-activates;
tiny.win.show({ activate: false });  // …or surface WITHOUT stealing focus
                                     // (overlay/HUD panels)
tiny.win.setHideOnClose(true);  // close button hides instead of quitting

// global cursor position (same top-left coords as setPosition — handy for
// popping a palette at the mouse)
const { x, y, window, screen } = await tiny.app.mousePosition();
// window = relative to this window's content area, clientX/clientY units,
//          valid even while the cursor is OUTSIDE it: { x, y, inside }
// screen = the display the cursor is on: { x, y, width, height, scale }

// frameless / transparent / vibrancy windows (native resize + focus kept)
tiny.win.setChrome({ frame: false, trafficLights: false, vibrancy: 'hud' });
// mark your own titlebar: <header data-tiny-drag>…</header> — drag moves the
// window, double-click zooms; interactive children are excluded automatically
// (or opt out with data-tiny-nodrag). Also in tinyjs.json as "chrome": {…}
// (packaged apps apply it before first paint — no titlebar flash).
tiny.win.startDrag();  tiny.win.zoom();   // manual equivalents

// read the window back
const s = await tiny.win.getState();
// { x, y, width, height, fullscreen, minimized, visible, focused,
//   alwaysOnTop, resizable, chrome: { frame, trafficLights, transparent,
//   vibrancy }, screen: { width, height, scale } }

// files dragged onto the window arrive with REAL filesystem paths
tiny.win.onDrop((paths) => tiny.log(paths.join(', ')));
tiny.win.print();                      // native print panel for the page

// persistent settings (JSON in ~/Library/Application Support/<app id>/)
await tiny.store.set('recent', ['/tmp/a.txt']);
const recent = await tiny.store.get('recent');      // value | null
await tiny.store.delete('recent');  await tiny.store.all();

// system-wide hotkeys (work while other apps are focused)
tiny.hotkey.register('boss', 'cmd+shift+k');
tiny.hotkey.on((id) => tiny.win.show());
tiny.hotkey.unregister('boss');

// custom right-click menu (native NSMenu; null restores WebKit's default)
tiny.menu.setContext([{ id: 'copy-path', label: 'Copy Path' }, { separator: true }, { id: 'del', label: 'Delete' }]);
tiny.menu.onContext((id) => ...);

// system theme + power events
const { dark } = await tiny.theme.get();
tiny.theme.on((dark) => document.body.classList.toggle('dark', dark));
tiny.api.on('sleep', () => ...);  tiny.api.on('wake', () => ...);

// clipboard — native NSPasteboard in the launcher process (no pbpaste/
// osascript spawns, no scratch files, multi-file writes never lose the tail)
const clip = await tiny.clipboard.read();
// { kind: 'files'|'image'|'color'|'text'|'empty', changeCount, text, html,
//   paths, image, imageSize, color, concealed, sourceApp, sourceURL }
// image: png temp path, valid until the clipboard changes again (copy the
//   file to keep it); imageSize: { width, height } px
// concealed: password-manager marker (org.nspasteboard) — history apps skip
// sourceApp: { name, bundleId } frontmost when the change was noticed
//   (exact while watch() runs); sourceURL: Chromium copy's page url
tiny.clipboard.write({ text: 'hi' });                    // any combination of
tiny.clipboard.write({ paths: ['/tmp/a.png', '/tmp/b.png'] });  // text, html,
tiny.clipboard.write({ image: pngPathOrBase64, color: '#ff8800' }); // paths…
await tiny.clipboard.changeCount();          // cheap "did it change?" probe
tiny.clipboard.watch(500);                   // launcher-side change polling
tiny.clipboard.onChange(({ changeCount, self }) => ...);  // self = own write
// backend: same api as app.clipboard.*; a createApp onClipboardChange
// handler auto-starts the watcher

// drag files OUT of the app — into Finder, Slack, anywhere (real files).
// Must be called from a mousedown handler while the button is held:
row.addEventListener('mousedown', () =>
  tiny.win.startDrag({ files: ['/tmp/report.pdf'], image: undefined }));

// native keystrokes — a CGEvent posted by the launcher. ONE permission
// (Accessibility) whose prompt names your app; no osascript, no spawn.
await tiny.app.keystroke('cmd+v');           // -> { ok, trusted }
await tiny.app.paste();                      // = keystroke('cmd+v'); call
                                             // win.hide() first so the paste
                                             // lands in the frontmost app

// permissions — check before use, build onboarding instead of failing
await tiny.app.permissions.check('accessibility');
// 'granted' | 'denied' | 'undetermined' | 'unsupported'
await tiny.app.permissions.request('accessibility');  // prompt/open Settings
// names: 'accessibility', 'screen', 'notifications' (packaged apps),
//        'microphone', 'camera' (TCC layer under getUserMedia),
//        'automation' (System Events) or 'automation:<bundle-id>'
// 'screen' never reads 'undetermined' — macOS only exposes a yes/no
// preflight for screen recording ('denied' until granted in Settings).
// mic/camera: getUserMedia() just works — the launcher grants WebKit's
// per-origin prompt so users only see the system dialog naming your app.
// Packaged apps declare "permissions": {"microphone": "why", ...} in
// tinyjs.json → Info.plist usage strings + hardened-runtime entitlements.
// Dev-mode note: TCC grants attach to the SHARED launcher binary
// (~/.tinyjs), not your app — all dev apps share them, and a launcher
// update re-prompts. Packaged apps carry their own identity and grants.

// shell — the NSWorkspace verbs apps otherwise spawn `open` for
await tiny.app.shell.open('https://tinyjs.app');   // default browser
await tiny.app.shell.open('/path/to/report.pdf');  // default app for the file
await tiny.app.shell.reveal('/path/to/file');      // show in Finder
await tiny.app.shell.trash('/path/to/file');       // recoverable (prefer over
                                                   // deleting user files)
// each resolves true, or rejects with the reason ('no such file',
// 'no application registered for URL', …)

// every display, same top-left coords as win.setPosition
const screens = await tiny.app.screens();
// [{ id, name, x, y, width, height, visible: { x, y, width, height },
//    scale, primary }] — visible excludes the menu bar and Dock; primary
// is the menu-bar screen (the coordinate origin)

// standard per-app directories — prefer over hardcoding ~/Library paths
const paths = await tiny.app.paths();
// { home, data, cache, logs, temp, downloads, desktop, documents }
// data/cache/logs are per app id; create them on first write
// (backend: app.paths is a plain object, no await)

// launch at login (packaged .app on macOS 13+; dev mode -> 'unsupported')
await tiny.app.launchAtLogin.get();       // 'enabled' | 'disabled' |
await tiny.app.launchAtLogin.set(true);   //   'requires-approval' | 'unsupported'
// 'requires-approval': macOS wants the user to allow it in
// System Settings > General > Login Items

// Dock tile
tiny.app.dock.setBadge('3');  tiny.app.dock.setBadge('');  // '' clears
tiny.app.dock.bounce();                    // until the app is activated
tiny.app.dock.bounce({ critical: true });  // until the user acts

// keep the system awake — replaces spawning `caffeinate` (the assertion
// dies with the app, so a crash never wedges sleep); the reason shows in
// `pmset -g assertions`
await tiny.app.power.preventSleep('Exporting video');
await tiny.app.power.preventSleep('Playing', { display: true }); // screen too
await tiny.app.power.allowSleep();

// the active app right now — who focus returns to after win.hide()
const front = await tiny.app.frontmostApp();  // { name, bundleId, pid } | null

// sounds — system beep, a system sound by name, or an audio file
await tiny.app.beep();
await tiny.app.playSound('Ping');            // -> false if it didn't load
await tiny.app.playSound('/path/to/done.aiff');

// native share sheet — anchor it at the click
btn.addEventListener('click', (e) =>
  tiny.win.share({ url: 'https://tinyjs.app', text: 'Look at this',
                   paths: ['/tmp/report.pdf'], x: e.clientX, y: e.clientY }));

// seconds since the user's last input (pause polling when they're away)
const idle = await tiny.app.idleTime();

// Quick Look — the Finder-spacebar preview panel (no qlmanage spawn);
// an array pages with the arrow keys, no args closes it
tiny.app.quickLook('/path/to/photo.heic');
tiny.app.quickLook([a, b, c]);   tiny.app.quickLook();

// screenshot a display (id from screens(); default primary) — png in the
// temp dir, you own the file. Needs the 'screen' permission + macOS 14;
// rejects with the reason otherwise.
const { path, width, height } = await tiny.app.captureScreen();

// the system eyedropper — pick any pixel on screen, in any app; needs NO
// screen-recording permission. '#rrggbb', or null if the user cancels.
const color = await tiny.app.pickColor();

// on-device OCR (Vision) — screenshot-to-text is captureScreen + this
const { text, blocks } = await tiny.app.ocr('/path/scan.png');
// blocks: [{ text, confidence, box }] — box normalized 0..1, top-left

// a thumbnail png for ANY file type Quick Look understands (PSD, video,
// 3D models, …) — file browsers stop caring about formats
const thumb = await tiny.app.thumbnail('/path/file.psd', 256);

// Keychain secrets (the keytar/safeStorage role) — tokens go here,
// never in tiny.store; values survive reinstalls
await tiny.app.secrets.set('api-token', 'abc123');
const tok = await tiny.app.secrets.get('api-token');   // string | null
await tiny.app.secrets.delete('api-token');

// Touch ID (or the account-password sheet) — "the user proved it's them"
if (await tiny.app.authenticate('unlock the vault')) { /* … */ }

// AppleScript in-process — control Music, Spotify, Finder, any scriptable
// app; no osascript spawn, same 'automation' permission as keystrokes
await tiny.app.applescript('tell application "Music" to playpause');
const sum = await tiny.app.applescript('return 2 + 3');   // '5'

// Now Playing — your media app shows in Control Center / the lock screen
// and the hardware media keys (F7/F8/F9, AirPods) route to you
tiny.app.nowPlaying.set({ title: 'Song', artist: 'Band', album: 'LP',
                          duration: 240, elapsed: 12, playing: true });
tiny.app.onMediaKey(({ command, time }) => {  // play|pause|toggle|next|
  if (command === 'toggle') togglePlayback();  // previous|seek (time = secs)
});
tiny.app.nowPlaying.clear();

// text-to-speech with the system voices; say() resolves when playback ends
const voices = await tiny.app.voices();  // [{ id, name, lang, quality }]
await tiny.app.say('Export finished', { voice: voices[0].id, rate: 0.5 });
tiny.app.stopSpeaking();

// notifications with action buttons + a reply field (packaged apps)
tiny.notify('New message', 'from Alex', { actions: [
  { id: 'reply', title: 'Reply', reply: true, placeholder: 'Message…' },
  { id: 'mute', title: 'Mute' },
  { id: 'del', title: 'Delete', destructive: true },
]});
tiny.app.onNotificationAction(({ id, action, reply }) => {
  if (action === 'reply') sendReply(reply);   // reply = the typed text
});

// record a display to an .mp4 (SCStream → H.264; video only for now).
// Needs the 'screen' permission + macOS 14; one recording at a time.
await tiny.app.recorder.start({ path: '/tmp/demo.mp4' });   // screenId optional
// … later …
const { path, duration } = await tiny.app.recorder.stop();  // finalized file

// native file dialogs (NSOpenPanel/NSSavePanel, run by the launcher)
const file  = await tiny.win.openFile();     // path | null
const files = await tiny.win.openFiles();    // paths[] | null
const dir   = await tiny.win.pickFolder();   // path | null
const dest  = await tiny.win.saveFile();     // path | null

// native system dialogs (NSAlert)
await tiny.win.alert('Heads up', 'optional detail');
const ok   = await tiny.win.confirm('Delete everything?', { detail: '…', ok: 'Delete', cancel: 'Keep' });
const name = await tiny.win.prompt('Your name?', { default: 'world' });  // string | null
```

### Menus

Every app gets a default menu bar: an app menu (**About** — standard panel
with the app name, version from tinyjs.json, and a tinyjs credit — and
**Quit**, ⌘Q) plus an **Edit** menu so copy/paste shortcuts work. Add your
own menus after those:

```js
tiny.menu.set([
  { title: 'Actions', items: [
    { id: 'open',  label: 'Open File…', key: 'o' },   // key = ⌘+<key>
    { id: 'mute',  label: 'Mute', checked: true },    // ✓ checkmark
    { id: 'later', label: 'Not Yet', enabled: false },// grayed out
    { separator: true },
    { id: 'more',  label: 'More', submenu: [          // nests
      { id: 'a', label: 'Sub Item' },
    ]},
  ]},
]);
tiny.menu.on((id) => { ... });   // clicks; backend can also export onMenu(id, app)

// patch an item in place — no redeclaring:
tiny.menu.update('mute', { checked: false, label: 'Unmuted' });
const { exists, label, checked, enabled } = await tiny.menu.get('mute');
```

The same item shape (checked / enabled / submenu) works in tray and context
menus, and `menu.update` / `menu.get` reach those too.

The dialogs and menus are native: the backend hands the work to the launcher,
which runs panels/menus on the UI thread and answers the page's promise
directly via `webview_return`.

### Multiple windows

Any html file in your frontend dir can be its own window:

```js
tiny.win.open('settings', { page: 'settings.html', title: 'Settings', size: '420x300' });
tiny.win.id;                       // which window this page lives in
tiny.win.close();                  // close the calling window ('main' quits)
await tiny.win.windows();          // ['main', 'settings', ...]
```

Every window runs the full `tiny.*` bridge, and `tiny.win.*` calls from a
page target *its own* window. Backend side:

```js
app.openWindow('settings', { page: 'settings.html' });
app.window('settings').setTitle('…');   // eval, push, close, hide/show,
                                        // setSize/Position, chrome, getState…
app.push('event', data);                // broadcasts to every window
app.windows();
export function onWindowClosed(id, app) {}   // also a 'window-closed' event
```

API handlers can tell who's calling: `myMethod: async (params, app, meta)
=> …` — `meta.window` is the caller's window id.

### Deep links, file associations, single instance

Packaged apps (`tinyjs build`) can claim a URL scheme and file extensions in
tinyjs.json:

```json
{ "urlScheme": "myapp", "fileExtensions": ["md", "txt"] }
```

Then `open myapp://compose?to=x`, "Open With → MyApp", and dropping files on
the Dock icon all deliver events — including on cold start (launch events are
buffered until the app is ready):

```js
tiny.app.onOpenUrl((url) => ...);        // backend: export onOpenUrl(url, app)
tiny.app.onOpenFiles((paths) => ...);    // backend: export onOpenFiles(paths, app)
```

Single-instancing is automatic: a second `open` (URL, file, or Dock click)
activates the running app and delivers the event to it instead of launching
another copy. Dev mode has no bundle, so schemes/associations only work in
built apps.

### Tray (menu bar) apps

```js
tiny.tray.set({
  title: 'MyApp',            // text in the menu bar (and/or an icon)
  icon: 'sf:cup.and.saucer.fill', // SF Symbol by name — or a png path ('tray.png')
  tooltip: 'My tiny app',    // pngs are template images by default
  menu: [                    // ({ template: false } keeps their colors)
    { id: 'show', label: 'Show Window' },
    { separator: true },
    { id: 'quit', label: 'Quit' },
  ],
});
tiny.tray.on((id) => { ... });        // menu clicks; backend: export onTray(id, app)
tiny.tray.remove();

// the full tray-app recipe:
tiny.win.setHideOnClose(true);        // closing the window hides it
tiny.app.setDockVisible(false);       // no Dock icon — menu-bar-only app
```

With no `menu`, clicking the icon fires `tiny.tray.onClick(fn)` instead —
classic toggle-window behavior. Want both? `primaryAction: true` keeps the
menu on **right-click** (and ctrl-click) while a plain left click fires
`onClick` — the Caffeine-style toggle:

```js
tiny.tray.set({ icon: 'sf:cup.and.saucer.fill', primaryAction: true,
                menu: [{ id: 'quit', label: 'Quit' }] });
tiny.tray.onClick(() => toggle());    // left click; backend: onTray(null, app)
```

And to launch as a menu-bar agent from the start — no Dock icon, no window,
not even a flash of either — declare it in tinyjs.json instead of hiding
things in `init()`:

```json
{ "activation": "accessory" }
```

Packaged apps get `LSUIElement` (the system never shows them in the Dock);
dev mode behaves the same. The window exists but stays hidden until you call
`tiny.win.show()` / `app.show()`, and `setDockVisible(true)` turns the app
back into a regular one.

## Build for release

```sh
tinyjs build
```

produces:

- `dist/<name>` — a standalone executable (`tjs app compile`) that loads its
  page from `dist/frontend/` next to it. Great for local/CLI use.
- `dist/<Name>.app` — a fully **codesigned** macOS bundle, the artifact you
  distribute.

The frontend ships as real files (the launcher renders `file://` documents,
so relative scripts/styles/images just work — no bundling step). The build
generates `AppIcon.icns` from `icon.png` via `sips` + `iconutil` and
codesigns everything (ad-hoc by default; set `signIdentity` in tinyjs.json
or `TINYJS_SIGN_IDENTITY` for a Developer ID).

`tinyjs build --dmg` additionally produces `dist/<name>-<version>.dmg` — the
.app plus an /Applications shortcut, the classic installer image. With a real
Developer ID, `tinyjs notarize` submits the built .app via `notarytool`
(keychain profile from tinyjs.json `"notarize": { "profile": … }` or
`TINYJS_NOTARY_PROFILE`) and staples the ticket.

### Distributing to other people (Developer ID + notarization)

The ad-hoc-signed default runs fine on **your** Mac, but on anyone else's,
Gatekeeper blocks it: recipients must approve it in System Settings →
Privacy & Security → "Open Anyway" (on macOS 15+ the old right-click-Open
trick no longer works) or strip quarantine with
`xattr -d com.apple.quarantine`. Tolerable for friends and internal tools;
hostile for the public.

For frictionless installs you need a **Developer ID Application**
certificate plus notarization, and both require the paid
[Apple Developer Program](https://developer.apple.com/programs/) ($99/year).
A free Apple ID only issues "Apple Development" certificates, which are
valid on your own registered devices — not for distribution. One-time setup:

1. **Enroll** at [developer.apple.com](https://developer.apple.com/programs/enroll/).
2. **Create the certificate**: Xcode → Settings → Accounts → your Apple ID →
   Manage Certificates → **+** → *Developer ID Application* (or create and
   download it from the developer portal). It lands in your login keychain.
3. **Copy its exact name** into `tinyjs.json`:

   ```sh
   security find-identity -v -p codesigning
   # 1) ABC123… "Developer ID Application: Your Name (TEAMID123)"
   ```

   ```json
   { "signIdentity": "Developer ID Application: Your Name (TEAMID123)" }
   ```

   (or export `TINYJS_SIGN_IDENTITY` instead). `tinyjs build` now signs with
   the hardened runtime and a secure timestamp — notarization-ready.
4. **Store notarization credentials** (once): generate an app-specific
   password at [account.apple.com](https://account.apple.com) (Sign-In &
   Security → App-Specific Passwords), then

   ```sh
   xcrun notarytool store-credentials tinyjs-notary \
     --apple-id you@example.com --team-id TEAMID123 \
     --password <app-specific-password>
   ```

   and point `tinyjs.json` at the profile:
   `"notarize": { "profile": "tinyjs-notary" }`.
5. **Ship**: `tinyjs build && tinyjs notarize` (add `--dmg` to the build for
   an installer image). The stapled .app opens anywhere, no warnings.

### Shipping updates (auto-update)

Point `tinyjs.json` at a manifest you host anywhere (any static host —
GitHub Releases, S3, nginx):

```json
{ "name": "myapp", "version": "1.1.0",
  "update": { "url": "https://example.com/myapp/manifest.json" } }
```

Each release: `tinyjs publish --notes "What changed"` → `dist/publish/`
contains `myapp-1.1.0.zip` + `manifest.json` (version, download url, sha256,
notes) — upload both to the directory `update.url` points at
(`--notes-file CHANGES.md` for longer notes). In the app:

```js
const { available, latest, notes } = await tiny.api.call('update.check');
if (available) await tiny.api.call('update.install');   // downloads, verifies
// sha256 + code signature, swaps the .app in place, relaunches
```

Or let tinyjs check for you: `"update": { "url": …, "auto": "launch" }`
(or `"daily"`) checks in the background (packaged apps only) and fires the
`update-available` page event / `onUpdateAvailable(info, app)` backend
export with `{ current, latest, notes }` — show your own prompt, then
`update.install()`:

```js
tiny.api.on('update-available', async ({ latest, notes }) => {
  if (await tiny.win.confirm(`Update to ${latest}?`, { detail: notes ?? '' }))
    await tiny.api.call('update.install');
});
```

(Backend equivalents: `app.update.check()` / `app.update.install()`.)
Installs are refused on checksum or signature mismatch, roll back on
failure, and require the packaged .app (a quarantined/translocated app is
asked to move to /Applications first).

### Why the .app looks the way it does (codesigning)

tjs-compiled binaries can't be re-signed — txiki appends the bundled app
after the Mach-O, which `codesign` rejects ("main executable failed strict
validation"; fine locally, fatal for distribution). So the bundle ships the
**stock** runtime plus the app as plain data, with the launcher as the bundle
executable (it listens on the socket and spawns the backend — and, as the
LaunchServices-registered process, receives deep links, file opens, and
single-instance activation):

```
MyApp.app/Contents/
  MacOS/myapp            the launcher (CFBundleExecutable) — window process,
                         spawns tjs
  MacOS/tjs              stock runtime binary — signs cleanly
  Resources/app/         entry.js, bridge.js, update.js, frontend/, src/…
                         (plain data, sealed by the bundle signature)
  Resources/AppIcon.icns
```

Clean Mach-Os plus data files means the whole bundle passes
`codesign --verify --strict --deep` — and with a real Developer ID the build
signs with the hardened runtime automatically, so `tinyjs notarize` is the
only remaining step.

---

## How it works

```
┌──────────────────────┐  unix socket   ┌───────────────────────┐
│ backend (txiki.js)   │◄──────────────►│ launcher (C++, ~380 KB)│
│ your src/main.js     │  line protocol │ native/launcher.cc    │
│ + runtime/bridge.js  │                │ · WKWebView window    │
│ · owns app logic     │                │ · webview_bind bridge │
│ · fs/net/process API │                │ · native dialogs      │
│ · spawns launcher    │                │ · else: dumb forwarder│
└──────────────────────┘                └──────────┬────────────┘
                                                   │ window.__invoke / eval
                                        ┌──────────▼────────────┐
                                        │ your page (file://)   │
                                        │ api.call('m', params) │
                                        │ api.on('event', fn)   │
                                        └───────────────────────┘
```

- The backend creates a socket in a fresh `0700` temp dir (invisible to other
  users; no ports to collide or scan), listens, then spawns the launcher
  pointed at your frontend's `index.html`.
- Closing the window ends the launcher; the backend notices, cleans up, exits.
  `api.call('quit')` works the other way (backend sends `QUIT`).

### Wire protocol (newline-delimited; payloads are JSON so never contain raw \n)

| direction          | line                       | meaning                          |
|--------------------|----------------------------|----------------------------------|
| launcher → backend | `CALL <id> <json-args>`    | page invoked `api.call(...)`     |
| backend → launcher | `RET <id> <status> <json>` | resolve (0) / reject (≠0) a call |
| backend → launcher | `EVAL <js>`                | run JS in the page               |
| backend → launcher | `TITLE <text>` / `SIZE <w> <h>` | window control              |
| backend → launcher | `DLG <id> <op>[\t<arg>…]`  | native dialog (file panels, alert/confirm/prompt); launcher answers the call itself via `webview_return` |
| backend → launcher | `MENUBEGIN` … `MENU <t>` / `ITEM id\tlabel\tkey` / `SEP` … `MENUEND` | declare custom menu bar menus |
| launcher → backend | `MENU <id>`                | a custom menu item was clicked   |
| backend → launcher | `TRAYBEGIN <t>\t<icon>\t<tmpl>\t<tip>\t<primary>` … `ITEM`/`SEP` … `TRAYEND` / `TRAYREMOVE` | declare/remove the menu bar status item (icon: png path or `sf:<symbol>`; primary=1: menu on right-click only) |
| launcher → backend | `TRAY <id>` / `TRAYCLICK`  | tray menu item / tray icon clicked (no menu, or left click with primary=1) |
| backend → launcher | `WINOP <op> [args]`        | hide (main = NSApp hide: focus returns to the previous app), show [0 = don't steal focus], center, minimize, fullscreen, ontop, resizable, pos, dock, hideonclose |
| backend → launcher | `GET <qid> mouse`          | global cursor position + its screen (answered as `GOT`) |
| launcher → backend | `DROP <json-paths>`        | files dragged onto the window (real paths) |
| backend → launcher | `CTXBEGIN` … `ITEM`/`SEP` … `CTXEND` / `CTXCLEAR` | replace/restore the right-click menu |
| launcher → backend | `CTX <id>`                 | a context menu item was clicked  |
| backend → launcher | `HKREG <id>\t<combo>` / `HKUNREG <id>` | global hotkeys       |
| launcher → backend | `HOTKEY <id>`              | a global hotkey fired            |
| launcher → backend | `SYS theme dark\|light` / `SYS sleep` / `SYS wake` | system events (theme also once at startup) |
| backend → launcher | `CLIPWRITE <text>\t<html>\t<image>\t<color>\t<path>…` | write the clipboard (fields escape `\n`/`\t`; image: png path or base64) |
| backend → launcher | `GET <qid> clipboard[:count]` | read the clipboard (answered as `GOT`) |
| backend → launcher | `CLIPWATCH <ms>`           | poll changeCount in the launcher (0 = stop) |
| launcher → backend | `CLIPCHANGE <count> <self01>` | the clipboard changed (self=1: our own write) |
| backend → launcher | `DRAGOUT[@win] <image>\t<path>…` | drag real files out of the window (from a page mousedown) |
| backend → launcher | `KEYSTROKE <qid> <combo>`  | post a CGEvent keystroke; answers `GOT <qid> {ok, trusted}` |
| backend → launcher | `PERMCHK <qid> <name>` / `PERMREQ <qid> <name>` | check/request a TCC permission; answers `GOT <qid> {status}` |
| backend → launcher | `PRINT`                    | native print panel               |
| backend → launcher | `RELOAD`                   | re-render the page from disk, caches bypassed (hot-reload) |
| backend → launcher | `QUIT`                     | close the window                 |

In the page, `window.__invoke` (injected by `webview_bind`) already returns a
promise — the shim in `tiny.js` is ~10 lines.

## Developing tinyjs itself

```
bin/tjs               txiki.js runtime (fetched by setup.sh, not committed)
native/launcher.cc    the window process (Objective-C++; webview headers vendored)
native/make-icon.jxa  default template icon generator (osascript -l JavaScript)
runtime/bridge.js     backend bridge library (socket, protocol, win.* methods)
runtime/update.js     app auto-update (manifest check, verify, bundle swap)
template/             what `tinyjs new` copies
skill/SKILL.md        tinyjs reference for coding agents (copied into new
                      projects at .claude/skills/tinyjs/)
cli.js + tinyjs       the CLI
test/smoke.html       self-driving test page
docs/                 tinyjs.app site (GitHub Pages): landing page + installer
                      (docs/install is what `curl tinyjs.app/install` fetches)
                      + changelog.html (tinyjs.app/changelog)
CHANGELOG.md          release history (canonical; the site page mirrors it)
setup.sh              from-source bootstrap (fetch tjs, compile natives)
.github/workflows/    release automation: tag vX.Y.Z → universal binaries →
                      per-arch tarballs + checksums → GitHub release
```

After editing the natives, re-run `./setup.sh` (or the `c++`/`cc` lines
inside it). To cut a release: update `CHANGELOG.md` and `docs/changelog.html`
(served at tinyjs.app/changelog — retitle the "upcoming" section with the
date), then `git tag vX.Y.Z && git push --tags`.

### Testing

`test/smoke.html` exercises calls, error rejection, window control, and push,
then quits (native dialogs need a human, so they're not covered):

```sh
cd someproject
TINYJS_HTML=/path/to/tinyjs/test/smoke.html tinyjs dev
# expect: [web] SMOKE RESULTS {...} with no FAIL entries, clean exit
```

The same page also runs against a built `dist/<name>` or the `.app`'s
`Contents/MacOS/<name>` via the same env var.

### Gotchas discovered along the way (txiki.js v26.6.0)

- `tjs.spawn(..., {stdin:'pipe'})` delivered only the first write to the
  child's stdin (fixed upstream in [PR #1028](https://github.com/saghul/txiki.js/pull/1028);
  until it's in a tagged release the bridge's Unix socket design sidesteps
  it — and stays for robustness reasons regardless).
- `tjs compile` does **not** bundle imports — they resolve at runtime
  relative to cwd. Use `tjs app compile` (app dir + `app.json` manifest).
- `import.meta.url` throws inside a compiled binary; resolve shipped files
  relative to `tjs.exePath`.
- txiki streams don't support `for await`; use `getReader()`.
- `tjs.cwd` is a property, not a function; spawn's stdio silencer is
  `'ignore'` (`'null'` silently inherits).
- The launcher loads local pages as real `file://` documents
  (`loadFileURL:allowingReadAccessToURL:`), not `webview_set_html`: the
  latter's `about:blank` origin is not a secure context, and WebKit hides
  SecureContext-only APIs there — notably `navigator.gpu` (WebGPU). The
  file:// origin also makes multi-file frontends work: relative
  scripts/styles/images load straight from the page's directory.
- WebGPU is additionally gated behind a WebKit feature flag on macOS 15 and
  earlier; the launcher flips it at startup via the private
  `WKPreferences _setEnabled:forFeature:` API (no-op where it's already on,
  as on macOS 26).
- The webview headers don't compile under ARC; build without `-fobjc-arc`.

### Portability

Currently macOS-only (launcher dialogs use AppKit; releases bundle
arm64/x86_64 tjs). The design ports: webview supports WebView2/WebKitGTK,
txiki ships Windows/Linux binaries, and the socket becomes a named pipe on
Windows (`\\.\pipe\…`, which `tjs.listen('pipe', …)`/libuv already abstracts).

## Credits

Built on [txiki.js](https://github.com/saghul/txiki.js) (MIT) by Saúl Ibarra
Corretgé and [webview](https://github.com/webview/webview) (MIT). MIT
licensed.
