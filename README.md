# tinyjs

![tinyjs — desktop apps ~6mb](docs/public/images/rakali-og.jpg)

Tiny desktop apps for macOS, Windows and Linux: a
[txiki.js](https://txikijs.org/) backend + a native
[webview](https://github.com/webview/webview) window.

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

Questions, help, or something you built: come say hi on
[Discord](https://discord.com/invite/NGsFQhEUD).

## Install

For a macOS Homebrew custom tap, see the [formula and tap instructions](docs/homebrew.md).

```sh
curl -fsSL https://tinyjs.app/install | sh
```

The same script now handles Linux too (it detects the OS). Installs to
`~/.tinyjs` and symlinks `tinyjs` onto your PATH. Pin a version with
`TINYJS_VERSION=vX.Y.Z`. Later, `tinyjs update` runs the copy of the
installer that came with your install if a newer release exists (`--check`
only reports, `--dry-run` shows what it would run); `tinyjs dev`
also mentions new releases, checking at most once a day. After updating,
run `tinyjs update-project` in each project to refresh its types and
coding-agent skill (see below). Linux needs the
system WebKitGTK runtime: `sudo apt install libwebkit2gtk-4.1-0` on
Debian/Ubuntu, `sudo zypper install libwebkit2gtk-4_1-0` on openSUSE.
Prebuilt binaries ship for Linux x86_64 and arm64 with the
first tagged release after Linux support merged — the installer says so
plainly against older releases.
To install from source instead:

```sh
git clone https://github.com/tarwin/tinyjsapp && cd tinyjsapp
./setup.sh    # downloads the txiki.js runtime, compiles the launcher
ln -s "$(pwd)/tinyjs" /usr/local/bin/tinyjs
```

On Linux, `setup.sh` needs the system dev packages first. Debian/Ubuntu:
`sudo apt install build-essential pkg-config libgtk-3-dev
libwebkit2gtk-4.1-dev libayatana-appindicator3-dev`. openSUSE:
`sudo zypper install gcc-c++ make pkgconf-pkg-config gtk3-devel
webkitgtk3-devel libayatana-appindicator3-devel` (WebKitGTK 4.1's dev
package is `webkitgtk3-devel` there — the "3" says GTK3, not an older
WebKit). It
downloads a prebuilt `tjs` from the tinyjsapp releases, or builds txiki.js
from source (`TJS_BUILD=1 ./setup.sh`, needs cmake + ninja).

**Windows:**

```powershell
irm https://tinyjs.app/install.ps1 | iex
```

Installs prebuilt binaries to `%LOCALAPPDATA%\tinyjs` (override with
`TINYJS_HOME`; pin with `TINYJS_VERSION`), x86_64 or native arm64 to match
the machine (Windows on ARM gets the x86_64 build, which runs emulated, from
releases before 0.62.0), and adds it to your user PATH —
open a new terminal afterwards. Needs only the WebView2 runtime
(preinstalled on Windows 11). `tinyjs update` runs the installer's bundled copy.
(Requires a release that ships Windows assets — the first one after Windows
support merged; the installer says so plainly against older releases.)

To develop tinyjs itself from source (needs MinGW-w64:
`winget install BrechtSanders.WinLibs.POSIX.UCRT`; on Windows on ARM,
`winget install MartinStorsjo.LLVM-MinGW.UCRT` for a native launcher —
setup.ps1 fetches an arm64 `tjs.exe` from the latest release):

```powershell
git clone https://github.com/tarwin/tinyjsapp; cd tinyjsapp
powershell -ExecutionPolicy Bypass -File setup.ps1
# adds the checkout to your user PATH (-SkipPath to opt out); after that,
# `tinyjs dev` auto-rebuilds the launcher whenever the native sources change
```

**Linux:**

Runs on X11 and Wayland sessions — Ubuntu 24.04+ and current distros with
`webkit2gtk-4.1`. Use the same install command above (it detects Linux) or
build from source with `./setup.sh` (see the distro deps above). Needs the
WebKitGTK 4.1 runtime — `libwebkit2gtk-4.1-0` on Debian/Ubuntu,
`libwebkit2gtk-4_1-0` on openSUSE.

**Playing media?** WebKitGTK decodes through GStreamer, and a stock desktop
install carries only some of it. Without the rest, `<audio>`/`<video>` play
MP3, Ogg/Vorbis, Opus, WAV and FLAC, but **AAC/M4A won't play at all**
(`canPlayType('audio/mp4; codecs="mp4a.40.2"')` returns `""`) and Media
Source Extensions report every type unsupported — which covers most podcasts,
most internet radio, and most adaptive streaming. Add:

```sh
# Debian/Ubuntu
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
# openSUSE
sudo zypper install gstreamer-plugins-bad gstreamer-plugins-ugly gstreamer-plugins-libav
```

WebKit hints at this itself, logging "WebKit wasn't able to find a WebVTT
encoder … unless gst-plugins-bad is installed" on startup.

**Don't play audio through Web Audio on Linux.** WebKitGTK renders the Web
Audio graph on a normal-priority (`SCHED_OTHER`) thread while its media threads
get real-time priority, so anything reaching `ctx.destination` misses its
deadline and crunches — on an idle machine, at any `latencyHint`, whether the
source is an element or a decoded buffer. A plain `<audio>` element goes
through GStreamer instead and is flawless. There is no graph-side fix; buffering
only softens it, and rtkit won't promote the thread.

So on Linux, play the element directly and get analysis from `tiny.audioTap`:

```js
if (tiny.system.isLinux()) {
  audio.volume = volume;          // straight to the speakers, no graph
  await tiny.audioTap.start({ scope: 'app' });   // PCM for your visualiser
} else {
  const src = ctx.createMediaElementSource(audio);
  src.connect(gain); gain.connect(ctx.destination);
}
```

`scope: 'app'` captures only your own output (a private PipeWire null sink fed
by your app's ports), so playback is unaffected and you don't hear the rest of
the desktop. What you lose is anything that *was* a graph node — an equalizer,
a `StereoPanner` — so tell the user rather than leaving dead controls on screen.

For **sound effects** none of this applies: `tiny.audio.sampler` mixes decoded
samples with per-voice volume/pan/pitch natively in the launcher on Linux
(PipeWire's real-time data path — clean under any page load) and via Web Audio
on macOS/Windows, same API, same equal-power pan numbers. Use it instead of
`createBufferSource` graphs.
See `examples/amp` for the whole pattern, visualisers included.

Analysis-only graphs are fine: a `MediaElementSource` feeding nothing but an
`AnalyserNode` never reaches `destination`, so a missed deadline costs a
dropped frame rather than audible crackle. Keep such elements at `volume = 0`,
since WebKitGTK also plays a graph-routed element's own output straight to the
speakers (macOS/Windows mute it).

See **Portability** below for what's supported on Windows and Linux.

If something doesn't work, run `tinyjs doctor` (from your project folder,
if you have one). It checks the install, which `tinyjs` your PATH runs, the
system webview, Node tooling and the project's `tinyjs.json`, and says how to
fix what it finds. It only reads files: see [docs/docs/doctor.md](docs/docs/doctor.md).

Full docs: [tinyjs.app/docs](https://tinyjs.app/docs) · release
history: [tinyjs.app/changelog](https://tinyjs.app/changelog).

## Create and run an app

```sh
tinyjs new myapp
cd myapp
tinyjs dev
```

A window opens. `dev` hot-reloads: edit anything in `src/frontend/` and the
page re-renders in place, caches bypassed (no restart, backend state
survives); edit backend sources (the backend entry's folder) and the
process restarts automatically. With a dev server (`devUrl`) the server's
own HMR does the frontend instead; a watch-mode tool that writes into the
frontend folder and reloads the page itself sets `"frontend": { "reload":
false }` so the page doesn't reload twice.
`TINYJS_DEBUG=1 tinyjs dev` traces every message crossing the bridge.

**Frameworks welcome:** `tinyjs new myapp --template react-ts` (or vue-ts,
svelte-ts, solid-ts, preact-ts, lit-ts, alpine-ts, vanilla-ts, …) scaffolds a
Vite app wired to tinyjs — `tinyjs dev` runs Vite's dev server with HMR inside
the native window, and `tinyjs build` ships the built assets as usual. Plain
`--template` asks which framework and language; `--pm npm|pnpm|yarn|bun|vp`
picks the package manager (otherwise you choose from the ones that work on
your machine, or get npm when there's no terminal to ask on), and
dependencies install straight away unless `--no-install`. The chosen manager
runs the project's dev and build scripts (`"dev": "pnpm run dev"` in
tinyjs.json). `--pm vp` uses [Vite+](https://viteplus.dev) 1.0 or newer
over another manager (`vp:pnpm`, `vp:bun`, …; asked if left out); an older
`vp` is refused, since its projects can't build against current Vite+
packages (`vp upgrade`). `alpine` is Vite's vanilla template with
[Alpine.js](https://alpinejs.dev) and a small component that calls the
backend. TypeScript backends are bundled with esbuild automatically (which
also makes npm packages usable in the backend). The zero-dependency default
scaffold is unchanged.

The `tiny` global is injected into every page by the launcher — no script
tag needed — and ships with full TypeScript definitions (`types/tiny.d.ts`).
New projects also get a tinyjs reference skill for coding agents
(`.claude/skills/tinyjs/` and `.agents/skills/tinyjs/`). Both are copied
in when the project is made, so after updating tinyjs, run
`tinyjs update-project` in the project to bring them up to date
(`tinyjs doctor` says when they're behind). It overwrites edits to those
files, and adds the skill to a project that doesn't have one. For any
other AI tool, the docs are at https://tinyjs.app/llms.txt (an index of
markdown pages) and https://tinyjs.app/llms-full.txt (all in one file).

A project is just:

```
myapp/
  tinyjs.json            # { name, title, size, id, version, icon?, signIdentity?,
                         #   urlScheme? (string or array), fileExtensions?, chrome?, update?, notarize?,
                         #   permissions? ({ microphone, camera, speechRecognition, location: "why" }),
                         #   audioTap? ("app" | "system"), audioTapReason? (macOS prompt text),
                         #   contextMenu? (false suppresses WebKit's default right-click menu),
                         #   debug? (true = F12 opens devtools (⌥⌘I also works on macOS),
                         #   "open" = every window auto-opens them; default off — `tinyjs dev` always has F12),
                         #   browserAccelerators? (true re-enables the engine's own keys —
                         #   Ctrl+F find, Ctrl+R reload…; default suppressed, Windows only),
                         #   activation? ("regular" default | "accessory" = menu-bar agent: no Dock, starts hidden),
                         #   mainWindow? (false = no main window/WebView at all — tray agents),
                         #   macos?/windows?/linux? (merged on top for that OS),
                         #   minTinyjsVersion? (refuse to run on an older tinyjs) }
  assets/                 # files the OS reads, by name — see "App assets"
    icon.png              #   1024×1024 app icon (template ships a default)
    Credits.html          #   optional: About panel credits (or .rtf / .rtfd)
    My App Help/          #   optional: macOS help book ("macos": { "helpBook" })
  resources/              # optional: data your code reads — see "Bundled data resources"
  executables/            # optional: programs your code spawns — see "Bundled executables"
  extras/                 # optional: README, licence… beside the app in the download — see "Download extras"
  src/backend/main.js     # backend: export const api = {...}; export function init(app) {}
  src/frontend/           # index.html + any local js/css/images
  src/shared/             # optional: code both sides import — see below
```

For `tinyjs.json` completion and type checks in your editor, see the
[configuration schema guide](docs/docs/config-schema.md).

Projects made before 0.53 keep `icon.png` (and `Credits.html`) at the
project root; both are still found there.

Both folders are conventions, not requirements. `"backend": "path/main.js"`
picks the backend entry (default: the first of `src/backend/main.js|ts`,
`src/main.js|ts` — the layout before 0.52, still supported —
`backend/main.js|ts`), and the folder it sits in is the
backend's root: a plain-JS backend ships that whole folder, and a `.ts`
entry is bundled from it. Separately, `"frontend": { "dir": "web" }`
picks the plain page folder (add `"reload": false` to leave page reloads
in `tinyjs dev` to your own watcher). A bundler project uses `"frontend": { "build",
"dist", "dev", "devUrl" }` instead, as the `--template` scaffolds do. Its
packages must be installed before `tinyjs dev` or `build` (the scaffolds do
that unless `--no-install`; otherwise the build stops and says so).

Code both the backend and the page use (constants, validation, formatting)
goes in a `shared/` folder beside the backend's folder: `src/shared/` next
to `src/backend/`, or `shared/` at the root next to the Vite templates'
`backend/`. Both sides import it with the same path, in dev and in a build:

```js
// src/shared/settings.js
export const MY_CONSTANT = 42;

// src/backend/main.js and src/frontend/app.js alike
import { MY_CONSTANT } from '../shared/settings.js';
```

It ships as plain files next to the frontend, so the page can read it: no
secrets, and nothing that imports `tjs:*` (the page has no `tjs`). Use
`.js` modules. A page loads them with `<script type="module">`, and a
bundled frontend (Vite) pulls them into its own bundle. Editing a shared
file in `tinyjs dev` restarts the app. The layout before 0.52
(`src/main.js`, frontend nested inside it) can't use `src/shared/`; move
the backend into `src/backend/` first.

Keys that genuinely differ per platform go in a `macos` / `windows` / `linux`
block, merged on top of the root ones for that OS — the block names are the
strings `tiny.system.os()` returns:

```json
{
  "name": "myapp",
  "chrome": { "frame": false },

  "macos":   { "signIdentity": "Developer ID Application: …",
               "chrome": { "vibrancy": "hud" } },
  "windows": { "icon": "icon.ico" },
  "linux":   { "icon": "icon-512.png" }
}
```

Plain objects merge (the macOS build above gets `{ frame: false, vibrancy:
'hud' }`), scalars and arrays replace. `TINYJS_SIGN_IDENTITY` and
`TINYJS_NOTARY_PROFILE` override the file — and say so when they displace a
value that was really there. Resolution order: **root → OS block → env**.

### Writing the app

Backend (`src/backend/main.js`) — every `api` function is callable from the page;
handlers receive `(params, app)`:

```js
export const api = {
  hello: async ({ name }) => `hi ${name}`,
};
export function init(app) {          // window is up
  setInterval(() => app.push('tick', Date.now()), 1000);
  // app also has: setTitle(t), setSize(w, h), menu.set(menus), eval(js),
  // reload(), quit(), notify(title, body, opts), hide()/show()/center()/
  // minimize()/fullscreen(), setPosition(x, y), setAlwaysOnTop(v),
  // setResizable(v), setHideOnClose(v), presence(mode), print(),
  // restore(), setFullscreen(v), win.getState(), win.open(id, opts), setChrome(opts),
  // startDrag(), zoom(), tray.set/remove,
  // menu.update(id, patch), menu.get(id), info(), paths(), store.get/set/delete/all,
  // hotkey.register(id, combo)/unregister(id), menu.setContext(items),
  // update.check()/update.install(),
  // clipboard.read()/write(data)/changeCount()/watch(ms)/unwatch(),
  // keystroke(combo), paste(), permissions.check(name)/request(name),
  // mousePosition(), screens(), show({ activate: false }),
  // shell.open(target)/reveal(path)/trash(path),
  // launchAtLogin.get()/set(v), badge(text), attention(opts),
  // power.preventSleep(reason, opts)/allowSleep(), frontmostApp(),
  // beep(), playSound(target), window(id).share(opts),
  // system.* (os/info/capabilities/requirements/battery/idleTime/…),
  // theme.get(), captureScreen(screenId),
  // pickColor(), ocr(path), thumbnail(path, size),
  // secrets.get/set/delete, authenticate(reason),
  // nowPlaying.set/clear, say(text, opts), voices(), stopSpeaking(),
  // recorder.start({ path, screenId })/stop(),
  // selectedText(), otherWindows(), moveWindow(pid, rect),
  // window(id).setClickThrough/setLevel/setAllSpaces, tray.position(),
  // printToPDF(path), icon(png), presence(mode),
  // macos.applescript(source)/quickLook(paths),
  // battery(), wifi(),
  // spotlight(query),
  // cookies.get(filter)/set(cookie)/remove(target)  (backend only)
}

export function onMenu(id, app) {}   // optional: handle menu clicks backend-side
export function onTray(id, app) {}   // optional: tray clicks (id null = bare icon)
export function onHotkey(id, app) {} // optional: global hotkey presses
export function onContextMenu(id, app) {}      // optional: context menu clicks
export function onSystem(kind, value, app) {}  // optional: 'theme'|'sleep'|'wake'
```

The backend runtime ships SQLite built in, handy for anything `tiny.store`
is too small for. Backend only: the page runs in the system webview, which
has no `tjs` global and can't import `tjs:*` modules (doing so throws
`ReferenceError: tjs is not defined`). Query in the backend and hand the
page results through an `api` function (example below).

```js
import { Database } from 'tjs:sqlite';
await tjs.makeDir(dataDir, { recursive: true });  // every tjs.* fs call is
// async — miss this await and the Database() below races the mkdir
const db = new Database(dataDir + '/notes.db');
db.exec('CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, text)');
const st = db.prepare('INSERT INTO notes (text) VALUES (?)');
st.run('hello'); st.finalize();
db.prepare('SELECT * FROM notes').all();   // [{ id: 1, text: 'hello' }]
```

Coming from better-sqlite3, mind the shape: `tjs:sqlite` itself is fully
synchronous (never await it), a statement has only `run()` / `all()` /
`finalize()` — no `get()`, so use `.all()[0]` — and `run()` returns
nothing. For the last insert id, ask SQLite:

```js
db.prepare('INSERT INTO notes (text) VALUES (?) RETURNING id').all('hi')[0].id;
// or: db.prepare('SELECT last_insert_rowid() AS id').all()[0].id
```

To reach the database from the page, expose what it needs from the backend:

```js
// backend
export const api = {
  notes: () => db.prepare('SELECT * FROM notes').all(),
  addNote: ({ text }) => { db.prepare('INSERT INTO notes (text) VALUES (?)').run(text); },
};
// frontend
const notes = await tiny.api.call('notes');
await tiny.api.call('addNote', { text: 'hi' });
```

Expose named operations rather than a generic `query(sql)` function, so the
page can't run arbitrary SQL.

Frontend — the `tiny` global is injected into every page automatically:

```js
const greeting = await tiny.api.call('hello', { name: 'world' });  // request/response
tiny.api.on('tick', (t) => ...);                                   // backend push
// on() is additive (addEventListener-style: N handlers, all fire) and returns
// an unsubscribe; there's also tiny.api.off('tick', fn). The tiny.*.on sugar
// (menu.on, tray.on, theme.on, …) returns the unsubscribe too — and since the
// sugar wraps your callback, that return value is the only way to unhook it:
const stop = tiny.api.on('tick', onTick);   stop();
tiny.api.off('tick', onTick);               // same thing, by reference

tiny.log('debug msg');  tiny.quit();
// tiny.log prints from the BACKEND, tagged [web] — so the page's lines and
// the backend's interleave in one terminal, in the order they happened.
// Objects arrive as objects; it resolves true once the backend has the line.
tiny.notify('Done', 'Your export finished');   // desktop notification
// packaged apps get REAL Notification Center banners (your app's icon,
// permission prompt on first use) when built with a signing identity —
// even "Apple Development" works. Ad-hoc/dev builds fall back to osascript.
tiny.notify('Ping', 'body', { id: 'x', subtitle: '…', sound: true });
tiny.app.onNotificationClick((id) => ...);  // backend: export onNotificationClick
await tiny.app.info();  // { version: <app>, tinyjs: <built with>, runtime: <txiki> }

// window control
// setSize/size are the PAGE's box (decorations excluded) — 1200x800 of
// document on every OS, whatever the title bar adds around it.
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
tiny.win.hide({ app: false });       // …or put away THIS WINDOW only, app
                                     // stays frontmost (a welcome/launcher
                                     // window stepping aside for documents)
tiny.win.setHideOnClose(true);  // close button hides instead of quitting

// global cursor position (same top-left coords as setPosition — handy for
// popping a palette at the mouse)
const { x, y, window, screen } = await tiny.app.mousePosition();
// window = relative to this window's content area, clientX/clientY units,
//          valid even while the cursor is OUTSIDE it: { x, y, inside }
// screen = the display the cursor is on: { x, y, width, height, scale }

// frameless / transparent / vibrancy windows (native resize + focus kept)
tiny.win.setChrome({ frame: false, windowControls: false, vibrancy: 'hud' });
// mark your own titlebar: <header data-tiny-drag>…</header> — drag moves the
// window, double-click zooms; interactive children are excluded automatically
// (or opt out with data-tiny-nodrag). Also in tinyjs.json as "chrome": {…}
// (packaged apps apply it before first paint — no titlebar flash).
tiny.win.startDrag();  tiny.win.zoom();   // manual equivalents

// Resizing a frameless window: only macOS keeps native edges throughout. An
// undecorated GTK window has none, and neither does a frameless SECONDARY
// window on Windows (WebView2's child HWND covers the whole rect, so the
// resize border is never hit-tested) — so on Linux, and for Windows
// satellites, the client adds invisible 5px grips around the edge
// automatically. Nothing to do per app. The Windows MAIN window is the
// exception: it keeps real left/right/bottom borders, trading the top edge.
tiny.win.startResize('se');   // 'n','ne','e','se','s','sw','w','nw' — for your own handle
// A fixed-size window (a Winamp-style deck) opts out of the grips:
//   <html data-tiny-noresize>   …or make it non-resizable:
tiny.win.setResizable(false);
// setResizable(false) means the USER can't drag the edges — your own
// setSize() still works, including shrinking to a titlebar for a shade view.
tiny.win.setMinSize(900, 640);   // a FLOOR rather than a lock (win.open takes
// the same as minSize: '900x640'). Same rule as above on macOS and Windows —
// it binds the user, not your setSize; GTK clamps both.
tiny.win.setZoom(2);   // native page zoom (0.25–5), rendered by the webview so
// it stays crisp. The page keeps laying out in CSS px and just has fewer of
// them — pair with setSize(w*2, h*2) for a real "double size" mode.

// square corners (drop macOS's rounded window corners). This makes the
// window BORDERLESS — square, no titlebar, no traffic lights — and is
// deliberately un-native: you lose the native titlebar drag (use
// data-tiny-drag) but keep resize edges, the shadow, and keyboard focus.
tiny.win.setChrome({ squareCorners: true });
// declare it in tinyjs.json "chrome": { "squareCorners": true } so it
// applies before first paint (no rounded→square flash on launch).

// acceptsFirstMouse: true delivers the click that focuses an unfocused window
// through to the page (macOS swallows it by default — "click once to focus,
// again to act"). Handy for palettes/toolbars and DOM drag regions.
tiny.win.setChrome({ acceptsFirstMouse: true });

// move the traffic lights (macOS): { x, y } from the window's top-left, for
// frameless windows whose custom titlebar is taller than the default corner
// assumes. One call — the launcher re-applies it across resizes and
// fullscreen round-trips. null restores the OS layout. Ignored on Windows
// and Linux. Also in tinyjs.json "chrome" so it applies before first paint.
tiny.win.setChrome({ windowControlsPos: { x: 12, y: 24 } });

// hide this window's menu bar (Windows/Linux draw one per window; macOS has
// one bar for the app, so it's ignored there). Also tinyjs.json
// "chrome": { "menu": false }, or win.open's chrome, to start without one.
tiny.win.setChrome({ menu: false });

// Spotlight-style palette over full-screen apps (macOS): tinyjs.json
// "chrome": { "panel": true } (or win.open's chrome — open-time only) makes a
// non-activating panel; show() gives it the keyboard WITHOUT activating the
// app, so with setAllSpaces(true) macOS doesn't switch Spaces.

// read the window back
const s = await tiny.win.getState();
// { x, y, width, height, outer: { width, height },
//   fullscreen, minimized, visible, focused,
//   alwaysOnTop, resizable, chrome: { frame, windowControls,
//   windowControlsPos, transparent, vibrancy, squareCorners,
//   acceptsFirstMouse },
//   screen: { width, height, scale } }
// width/height are the page's box — hand them straight back to setSize and
// nothing moves. outer is the footprint on screen, decorations in (the page
// can't work that out itself: window.outerWidth is 0 in a WKWebView).
// x/y are the window's top-left, the units setPosition takes.

// …or don't poll it: window-state transitions arrive as events, whatever
// the cause (green button, menu item, F11, your own setFullscreen call).
const off = tiny.win.onState(({ win, fullscreen, maximized, minimized, focused }) => {
  // same vocabulary as getState(); `win` because events are broadcast —
  // every page hears about every window and filters by id ('main' or a
  // win.open id). Wayland never reports `minimized` (compositor-private).
});
off();   // like every tiny on…, it returns its own unsubscribe

// files dragged onto the window arrive with REAL filesystem paths
tiny.win.onDrop((paths) => tiny.log(paths.join(', ')));
tiny.win.print();                      // native print panel for the page

// persistent settings (JSON in ~/Library/Application Support/<app id>/)
await tiny.store.set('recent', ['/tmp/a.txt']);   // true once on disk; false =
//   write failed: kept in memory (like localStorage) and written by the next
//   successful save, lost if the app quits first. Never rejects.
const recent = await tiny.store.get('recent');      // value | null
await tiny.store.delete('recent');  await tiny.store.all();

// fetch that runs in the BACKEND — no CORS, CSP, or mixed-content limits,
// so the page can hit any origin. Same shape as window.fetch, returns a
// real Response (res.json()/res.text()/res.headers/res.ok all work).
const r = await tiny.fetch('https://api.example.com/data', {
  method: 'POST', headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ q: 'hi' }),
});
const data = await r.json();
// { stream: true } gives a LIVE streaming body — the whole point for endless
// sources like internet radio (a buffered fetch would never resolve). The
// backend keeps the connection and the page pulls chunks on demand, with
// natural backpressure; res.body.getReader() is the endless tap.
const radio = await tiny.fetch('https://ice1.somafm.com/groovesalad-128-mp3', { stream: true });
const reader = radio.body.getReader();
for (;;) { const { value, done } = await reader.read(); if (done) break; /* feed decodeAudioData, MediaSource, … */ }
// reader.cancel() (or closing the window) tears the upstream connection down.
//
// Reachability: tiny.fetch (and plain fetch() in the backend) transparently
// hands two cases the bundled runtime can't do on its own to the system curl:
// root-path URLs like https://feeds.example.com/ (the runtime emits 'GET //',
// which strict CDNs 404) and TLS 1.2-only hosts (art19, anchor.fm — the
// runtime's TLS stack requires 1.3). Redirects are followed hop by hop so a
// tracker that bounces into either case still lands. curl is only consulted
// when the built-in path failed at the wire — a real HTTP error (404, 500)
// is returned as-is, never retried. curl ships with macOS, Linux, and
// Windows 10 1803+; without it those two cases fail as before. Details in
// TODO-txiki.md (the plan is to fix the runtime itself and drop the shim).

// tiny.proxyURL — get a cross-origin stream (internet radio) INTO Web Audio.
// A MediaElementSource on a cross-origin <audio> outputs silence by spec;
// proxyURL streams the remote through the native layer with permissive CORS,
// so the element is untainted and the full EQ/analyser graph gets real samples.
const audio = document.querySelector('audio');
audio.crossOrigin = 'anonymous';
audio.src = tiny.proxyURL('https://ice1.somafm.com/groovesalad-128-mp3');
const ctx = new AudioContext();
ctx.createMediaElementSource(audio).connect(ctx.createAnalyser()); // …→ EQ → destination
audio.play();
// The native layer does the HTTP (following redirects), http/https only.
// Only the app's own pages may use it: any other origin (a wrapped site, a
// third-party iframe) is refused unless its tinyjs.json "api".origins key
// allows "media.proxy".
// Live internet radio works too: a non-seekable stream that answers 200 with no
// Content-Length (icecast/shoutcast) is served to the element with a synthetic
// large length so CoreMedia plays it progressively and the analyser sees real
// samples. Tradeoff: audio.duration/currentTime are meaningless for such a live
// stream (huge fake timeline) — don't wire a seekbar to one.

// system-wide hotkeys (work while other apps are focused)
tiny.hotkey.register('boss', 'cmd+shift+k');
tiny.hotkey.on((id) => tiny.win.show());
tiny.hotkey.unregister('boss');

// tiny.audio.sampler — sampled sound effects (game/UI SFX): a bank of short
// decoded sounds (wav/mp3/flac) fired with per-voice vol/pan/rate, one mixed
// output, 32 voices with oldest-stealing. Works on Linux (native mix in the
// launcher — Web Audio crackles there) and macOS/Windows (Web Audio in the
// main window's page) with identical behavior; backend twin app.audio.sampler
// drives the same mixer. Not for music — that's <audio>.
const s = tiny.audio.sampler;
await s.load('coo', '/abs/path/coo.mp3');       // or an ArrayBuffer
const v = await s.play('coo', { vol: 0.8, pan: -0.3, rate: 1.06 });
v.set({ pan: 0.1 });                            // live, no restart
v.stop();                                       // short fade, no click
s.master(0.5); s.stopAll(); s.unload('coo');

// tiny.audioTap — read the app's rendered audio OUTPUT as PCM (VU meters,
// visualizers) — including audio that bypasses Web Audio (native HLS,
// CORS-tainted streams). Read-only; needs "audioTap": "app" in tinyjs.json
// ("system" for scope: 'system'; "audioTapReason" sets macOS's prompt text).
await tiny.audioTap.start({ scope: 'app', interval: 80 });
tiny.audioTap.on(({ pcm, sampleRate, channels, frames }) => {
  const bin = atob(pcm), n = bin.length >> 1;   // base64 -> interleaved LE Int16
  let peak = 0;
  for (let i = 0; i < n; i++) {
    const v = ((bin.charCodeAt(2*i) | (bin.charCodeAt(2*i+1) << 8)) << 16 >> 16) / 32768;
    if (Math.abs(v) > peak) peak = Math.abs(v);
  }
  meter.style.height = (peak * 100) + '%';       // channels/frames describe the layout
});
// tiny.audioTap.stop();  // (or the owning window closing) tears the tap down.
// Authorization is deferred to the first start() — declaring the
// manifest key does nothing until you call it — so you can lazy-arm the tap the
// first time a meter is shown. The FIRST start() prompts for "System Audio
// Recording" (even scope:'app' — WKWebView renders audio in a separate
// com.apple.WebKit.GPU helper, so the tap is a cross-process capture); the
// grant persists per app. scope:'system' also hears other apps (excludeSelf
// drops your own). NOTE: under `tinyjs dev` the audio "owner" is your terminal,
// not your app — the tap delivers real PCM only if that terminal holds the
// System Audio Recording grant, otherwise silent chunks; a built .app owns its
// own grant. Denial can't be reported synchronously — it surfaces as silence.

// custom right-click menu (native NSMenu; null restores WebKit's default)
tiny.menu.setContext([{ id: 'copy-path', label: 'Copy Path' }, { separator: true }, { id: 'del', label: 'Delete' }]);
tiny.menu.onContext((id) => ...);
// to hide WebKit's default menu entirely (Reload/Back/Inspect Element…) for an
// app-like feel, set "contextMenu": false in tinyjs.json. setContext still wins.

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
tiny.clipboard.unwatch();                    // stops delivery, not the counter:
// the OS keeps counting, so one changeCount() after a gap (a wake, a window
// coming back) says whether re-reading is worth it — without reading contents
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
//        'location' (wifi ssid/bssid; packaged apps that declare it),
//        'automation' (System Events) or 'automation:<bundle-id>'
// 'screen' never reads 'undetermined' — macOS only exposes a yes/no
// preflight for screen recording ('denied' until granted in Settings).
// mic/camera: getUserMedia() just works on the app's own pages — the
// launcher grants WebKit's per-origin prompt so users only see the system
// dialog naming your app. Any other origin (a wrapped site, a redirect, a
// third-party iframe) gets the engine's own permission prompt (macOS,
// Windows) or a denial (Linux) unless an "api.origins" key allows
// "media.microphone" / "media.camera" for it.
// Speech-to-text isn't a tiny.* call at all — the page's own
// webkitSpeechRecognition works (WebKit and WebView2 both have it), but a
// packaged app must declare BOTH "microphone" and "speechRecognition" usage
// strings or the OS refuses the service with `service-not-allowed` and no
// prompt. Pair it with app.say() and macos.ai for a hands-free loop.
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
// [{ id, uid, name, x, y, width, height, visible: { x, y, width, height },
//    scale, primary, internal, pixels: { width, height, visible } }]
// visible excludes the menu bar / Dock / taskbar; primary is the coordinate
// origin. Global x/y are logical points on every OS (one consistent space
// on mixed-DPI Windows too). `id` is a session handle (what captureScreen
// takes); persist `uid`, which survives reboots and replugs where the OS
// allows. Linux/GTK3 `scale` is an integer — fractional setups round.

// place relative to ONE screen instead of the global space
tiny.win.setPosition(40, 40, { screen: screens[1] });           // its points
tiny.win.setPosition(81, 81, { screen: screens[1], units: 'px' }); // its pixels
tiny.win.setSize(1280, 720, { units: 'px' });   // pixels of the window's screen
tiny.win.setFullscreen(true, { screen: screens[1] }); // also works on Wayland
tiny.win.open('tools', { page: 'tools.html', screen: screens[1], x: 20, y: 20 });
// (macOS snaps frames to whole points, so on a 2x screen px land on even px)

await tiny.app.screenAt(x, y);       // screen holding a global point, or null
await tiny.app.screenFor(rect);      // screen holding most of a rect, or null
await tiny.app.foregroundScreen();   // screen of the focused window of ANY app
                                     // (no permission needed); null on Wayland
await tiny.app.matchScreen(saved);   // a saved screen -> the connected screen
                                     // it most likely is now (ids lie; this
                                     // scores uid, name, size, scale, layout)
tiny.app.onScreensChanged((screens) => …);   // plug/unplug/rearrange/rescale
// getState() adds screenId, local: { x, y } and pixels: { x, y, width,
// height }; onState events carry screenId and fire when a window changes
// screen.

// standard per-app directories — prefer over hardcoding ~/Library paths
const paths = await tiny.app.paths();
// { home, data, cache, logs, temp, downloads, desktop, documents }
// data/cache/logs are per app id, created for you when paths() is called
// (backend: the same — await app.paths())

// launch at login (built apps on all three OSes; dev mode -> 'unsupported')
await tiny.app.launchAtLogin.get();       // 'enabled' | 'disabled' |
await tiny.app.launchAtLogin.set(true);   //   'requires-approval' | 'unsupported'
// 'requires-approval': macOS wants the user to allow it in
// System Settings > General > Login Items

// decorate the OS's app surface: Dock (macOS), taskbar (Windows),
// launcher (Linux) — verbs named for intent, not for furniture
tiny.app.badge('3');  tiny.app.badge('');  // '' clears
tiny.app.attention();                      // until the app is activated
tiny.app.attention({ critical: true });    // until the user acts

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
const idle = await tiny.system.idleTime();

// files named on the command line reach onOpenFiles — cold start on all
// three platforms, and forwarded to the running copy over the instance pipe
// on win/linux. `tinyjs build --cli [name]` writes dist/bin/<name>, a shim
// that's just `exec <backend> "$@"`.
export function onOpenFiles(paths, app) { /* argv, LaunchServices, drag-drop */ }

// become the default opener for an extension — ask because the USER asked
await tiny.app.setAsDefaultHandler('md');   // 'ok' | 'unsupported' | 'failed'

// the user's languages + time zone, read from the OS — NOT from LANG, which
// Windows doesn't have and which elsewhere describes the parent process
const { language, languages, system, region, timeZone } = await tiny.system.locale();
// `languages` is filtered to the localizations the app bundle declares;
// `system` is the raw preference. They differ for an English-only app on a
// French Mac — render with the first, offer a translation based on the second.
//
// A PAGE rarely needs this: navigator.language / navigator.languages / all of
// Intl / the 'languagechange' event work in the webview. The BACKEND is the
// gap — txiki has no Intl at all, so format in the page and branch on
// language in the backend.

// Quick Look — the Finder-spacebar preview panel (no qlmanage spawn);
// an array pages with the arrow keys, no args closes it
tiny.macos.quickLook('/path/to/photo.heic');
tiny.macos.quickLook([a, b, c]);   tiny.macos.quickLook();

// screenshot a display (id from screens(); default primary) — png in the
// temp dir, you own the file. Needs the 'screen' permission;
// rejects with the reason otherwise.
const { path, width, height } = await tiny.app.captureScreen();

// the system eyedropper — pick any pixel on screen, in any app; needs NO
// screen-recording permission. '#rrggbb', or null if the user cancels.
const color = await tiny.app.pickColor();

// on-device OCR (Vision) — screenshot-to-text is captureScreen + this
const { text, blocks } = await tiny.macos.ocr('/path/scan.png');
// blocks: [{ text, confidence, box }] — box normalized 0..1, top-left

// a thumbnail png for ANY path — file browsers stop caring about formats.
// A content preview where Quick Look has a renderer (PSD, video, 3D models,
// source files); the document / app / folder ICON where it doesn't, so on
// macOS this never fails on file type alone. WINDOWS behaves the same way
// via IShellItemImageFactory — images preview, and folders, .exe and plain
// text all come back as their shell icon (measured 2026-07-28). LINUX is the
// narrow one: images only, everything else rejects 'no thumbnail' (measured
// 2026-07-28) — so feature-detect per file, not per platform.
// A path that doesn't exist rejects everywhere.
// SIZE: macOS and Linux treat `size` as points and render @2x (ask 64, get
// 128); Windows treats it as pixels and returns exactly what you asked for.
// Read width/height off the result rather than assuming either.
const thumb = await tiny.app.thumbnail('/path/file.psd', 256);

// Keychain secrets (the keytar/safeStorage role) — tokens go here,
// never in tiny.store; values survive reinstalls
await tiny.app.secrets.set('api-token', 'abc123');
const tok = await tiny.app.secrets.get('api-token');   // string | null
await tiny.app.secrets.delete('api-token');
// set replaces rather than duplicating; delete of a key that was never
// there still resolves true. On macOS the keychain ACL names the binary
// that wrote the value, so a secret saved under `tinyjs dev` makes the
// BUILT app prompt once when it first reads it (a dev-only annoyance).

// Touch ID (or the account-password sheet) — "the user proved it's them".
// false covers cancel AND unavailable, and Linux has no owner check at
// all, so it answers false there — a gate on this fails closed.
if (await tiny.app.authenticate('unlock the vault')) { /* … */ }

// AppleScript in-process — control Music, Spotify, Finder, any scriptable
// app; no osascript spawn, same 'automation' permission as keystrokes
await tiny.macos.applescript('tell application "Music" to playpause');
const sum = await tiny.macos.applescript('return 2 + 3'); // '5'

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
// Needs the 'screen' permission; one recording at a time.
await tiny.macos.recorder.start({ path: '/tmp/demo.mp4' });   // screenId optional
// … later …
const { path, duration } = await tiny.macos.recorder.stop();  // finalized file

// window superpowers — overlays, HUDs, desktop pets, palettes that follow
// you across Spaces. clickThrough lets mouse events pass through the window.
tiny.win.setClickThrough(true);              // draw-on-screen / HUD overlays
tiny.win.setLevel('overlay');                // 'normal'|'floating'|'overlay'
                                             //  |'desktop' (behind windows)
tiny.win.setAllSpaces(true);                 // follow across every Space
// (backend: app.setClickThrough/setLevel/setAllSpaces + app.window(id).*)

// grab the text selected in ANY app (PopClip-style popovers; Accessibility)
const sel = await tiny.macos.selectedText();   // string | null

// arrange other apps' windows (Rectangle/Magnet; Accessibility)
const wins = await tiny.macos.otherWindows();  // [{ app, pid, title, x,y,w,h }]
await tiny.macos.moveWindow(wins[0].pid, { x: 0, y: 0, width: 1280, height: 800 });

// anchor a dropdown under the tray icon
const spot = await tiny.tray.position();     // { x, y, width, height } | null

// render the page to a PDF (vector, WKWebView) — invoices, reports
const { path } = await tiny.win.printToPDF('/tmp/report.pdf');
// From another origin (a wrapped site), only Downloads, the app's data dir or
// the temp dir are written directly; any other path opens a save panel.

// a live app icon (render a canvas → progress rings)
tiny.app.icon(canvasPngPath);                // '' resets to the bundle icon

// battery + Wi-Fi for menu-bar monitors
const bat = await tiny.system.battery();        // { percent, charging, plugged,
                                             //   minutesRemaining } | null
const net = await tiny.system.wifi();           // { ssid, bssid, rssi, txRate }
                                             //   | null — ssid needs
                                             //   permissions.request('location')

// find files by name or content (Spotlight, no mdfind spawn) — 100 paths max
const docs = await tiny.app.spotlight('quarterly report');

// on-device LLM — Apple's FoundationModels (offline, no API key, private).
// Needs macOS 26 AND Apple Intelligence switched on, so ALWAYS check
// availability() first — three states, and only one of them can generate.
// TOOL CALLING (backend only — a tool's run() is a real function, so it
// can't cross the bridge from a page). The model decides what to call:
const { text, calls } = await app.macos.ai.generate('Move the window to 200, 120', {
  instructions: 'You control a desktop app window. Use the tools.',
  tools: [{
    name: 'moveWindow',
    description: 'Move the application window to a position on screen.',
    parameters: { x: { type: 'integer', description: 'x in points' },
                  y: { type: 'integer', description: 'y in points' } },
    run: ({ x, y }) => { app.window('main').setPosition(x, y); return 'moved'; },
  }],
});
// `calls` is the RECORD; `text` is a summary that may be fiction. Measured on
// macOS 26.5: asked for three tool calls in one turn, this model made all
// three in one run out of four — and claimed all three in its prose every
// time, including the runs where it silently skipped one. Put anything
// irreversible behind a confirmation, not behind a hopeful read of `text`.

if (await tiny.macos.ai.availability() === 'available') {
  const reply = await tiny.macos.ai.generate('Summarise this in one line: ' + text,
    { instructions: 'You are terse.' });   // instructions = a system prompt
}

// native file dialogs (NSOpenPanel/NSSavePanel, run by the launcher)
const file  = await tiny.dialog.openFile();     // path | null
const files = await tiny.dialog.openFiles();    // paths[] | null
const dir   = await tiny.dialog.pickFolder();   // path | null
const dest  = await tiny.dialog.saveFile();     // path | null
// openFile/openFiles/saveFile take { types: ['md', 'txt'] } — extensions,
// no dots — to limit what's choosable (allowedContentTypes on macOS,
// COMDLG_FILTERSPEC on Windows, GtkFileFilter on Linux; the latter two add
// an "All files" escape hatch). Omit types and everything is selectable.
const md = await tiny.dialog.openFile({ types: ['md', 'markdown', 'txt'] });
// saveFile({ name }) pre-fills the file name: a name, not a path (any
// folder part is dropped). With types, the extension goes on as usual.
const doc = await tiny.dialog.saveFile({ types: ['docx'], name: 'report' });  // …/report.docx

// native system dialogs (NSAlert)
await tiny.dialog.alert('Heads up', 'optional detail');
const ok   = await tiny.dialog.confirm('Delete everything?', { detail: '…', ok: 'Delete', cancel: 'Keep' });
const name = await tiny.dialog.prompt('Your name?', { default: 'world' });  // string | null
// Backend: app.dialog has all seven methods with the same arguments and
// results, including in mainWindow:false tray apps without a WebView.
// The page's bare confirm() is the browser's own: one argument, no custom
// title or buttons.
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
    { id: 'find',  label: 'Replace…', key: 'alt+f' }, // ⌥⌘F ('F' alone = ⌘⇧F)
    { id: 'mute',  label: 'Mute', checked: true },    // ✓ checkmark
    { id: 'later', label: 'Not Yet', enabled: false },// grayed out
    { id: 'wave',  label: 'Say Hi', icon: 'emoji:👋' }, // or a png path / 'sf:<symbol>'
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

The same item shape (checked / enabled / icon / submenu) works in tray and
context menus, and `menu.update` / `menu.get` reach those too. Icons show in
every menu on macOS and Windows; Linux tray menus drop them (see
[menus › icons](https://tinyjs.app/docs/menus#icons)).

On macOS you get that Edit menu by default, because the webview's ⌘C/⌘V ride
its key equivalents. Where it sits is up to you. `{ role: 'edit' }` in the
array gives it that slot instead of the first one, which is how a **File**
menu gets to come before it, the way every Mac app has it:

```js
tiny.menu.set([
  { title: 'File', items: [...] },
  { role: 'edit' },                 // Undo / Cut / Copy / Paste / Select All
  { title: 'View', items: [...] },
]);
```

Your own editing commands can go in that menu too. Give the role `items` and
macOS appends them under Select All, after a separator. They work like any
other item: ids, keys, ticks, `menu.update` / `menu.get`, clicks through
`tiny.menu.on`:

```js
tiny.menu.set([
  { title: 'File', items: [...] },
  { role: 'edit', items: [
    { id: 'find', label: 'Find…', key: 'f' },
    { id: 'dup',  label: 'Duplicate', key: 'd' },
  ] },
]);
```

To control the stock items too, place them by role inside `items`: `undo`,
`redo`, `cut`, `copy`, `paste`, `selectAll`, or `standard` for the whole group.
Once any of them appears, you set the whole order and nothing is added for you:

```js
{ role: 'edit', items: [                // your items first, stock group below
  { id: 'find', label: 'Find…', key: 'f' },
  { separator: true },
  { role: 'standard' },
] }

{ role: 'edit', items: [                // just the stock items you want
  { role: 'undo' }, { role: 'redo' },
  { separator: true },
  { role: 'copy' }, { role: 'paste' },
] }
```

`standard: false` leaves the stock items out entirely. With no `items` either,
there is no Edit menu:

```js
{ role: 'edit', standard: false, items: [{ id: 'find', label: 'Find…', key: 'f' }] }
{ role: 'edit', standard: false }       // no Edit menu at all
```

⌘C, ⌘V, ⌘X, ⌘A, ⌘Z and ⌘⇧Z keep working in text fields either way: the
launcher handles any of those shortcuts that no menu item claims. If one of
your own items takes ⌘C, it gets ⌘C everywhere, text fields included.

On Windows and Linux the webview handles Ctrl+C/V on its own, so nothing is
added for you there. A bare `{ role: 'edit' }` is skipped and the bar keeps
the order you declared. With `items`, the Edit menu in that slot is exactly
those items, **stock roles included**. So to get the same Edit menu on all
three platforms, place the stock items yourself:

```js
{ role: 'edit', items: [                // identical on macOS, Windows, Linux
  { role: 'standard' },
  { separator: true },
  { id: 'find', label: 'Find…', key: 'f' },
] }
```

There, stock items show their shortcut (Ctrl+C) but never claim it, so the
webview's own handling of the keys in text fields is untouched. On Linux they
run WebKitGTK's editing commands and grey out when they don't apply; on
Windows a click replays the shortcut into the webview, and they're always
enabled. `standard: false` only matters on macOS, the one place anything is
added implicitly. Stock roles work in `tiny.menu.setContext` too, and are
skipped in the tray. Only the first `edit` block counts.

`{ role: 'app', items: [...] }` is the other slot the launcher owns: on macOS
those items go **inside the application menu**, between About and Quit, which
is where the platform keeps **Settings…** (⌘,) and the one part of the bar
`setMenu` could not reach before. They behave like any other item — ids,
key equivalents, ticks, `menu.update` / `menu.get` — and clicks arrive through
the same `tiny.menu.on`:

```js
const mac = tjs.env.OS !== 'Windows_NT'
  && !/linux/i.test(navigator.platform ?? '');

tiny.menu.set([
  ...(mac ? [{ role: 'app', items: [
    { id: 'settings', label: 'Settings…', key: ',' },
  ] }] : []),
  { title: 'File', items: [
    ...(mac ? [] : [{ id: 'settings', label: 'Settings…', key: ',' }]),
  ] },
  { role: 'edit' },
]);
```

Windows and Linux have no application menu at all, so the role is unknown
there and its items are dropped rather than landing somewhere arbitrary —
which is why the example declares Settings twice and shows it once. Only the
first `app` block is used; a second is a mistake, and appending it would
silently double the items.

The macOS **About** item normally shows the standard panel (app name, version,
a tinyjs credit) for free. To draw your own instead, set `"about": "menu"` in
tinyjs.json — the click then arrives like any other menu item, with the
reserved id `'about'`, in `tiny.menu.on` / the backend's `onMenu`. Apps that
don't opt in keep the free panel; Windows and Linux have no default About
item, so there the flag changes nothing. Two ways to shape that free panel:
an `assets/Credits.html` (or `Credits.rtf` / `Credits.rtfd`) is bundled to
`Resources/` and shown in place of the tinyjs credit line (the macOS
convention; start the HTML with `<meta charset="utf-8">` and give it a font,
or AppKit reads it as Latin-1 and sets it in Times), and
`"attribution": "…"` changes the one-liner itself — precedence:
Credits file → `attribution` → the tinyjs default. Credits only show in a
built app; `tinyjs dev` has no bundle to carry them.

`{ role: 'help' }` is the **Help** menu. On macOS it always goes last in the
bar, and macOS puts its menu-search field at the top. When the built app
ships a help book (see [App assets](#app-assets)) the menu starts with
"<App> Help" (⌘?), which opens the book in Help Viewer, and the search field
searches the book too. A help book gets this menu even if you never declare
the role; declare it to add your own items below that one, or with
`standard: false` to leave the stock item out. Windows and Linux draw a plain
"Help" menu of your items, in the slot where you declared it:

```js
app.menu.set([
  { title: 'File', items: [/* … */] },
  { role: 'help', items: [
    { id: 'site', label: 'Visit Website' },
    { id: 'issue', label: 'Report an Issue…' },
  ] },
]);
```

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

// chrome + position are applied BEFORE the window paints — a frameless
// panel never flashes its titlebar, and it opens where you asked (not
// center-then-jump):
tiny.win.open('hud', { page: 'hud.html', size: '300x120', x: 40, y: 40,
                       chrome: { frame: false, windowControls: false, vibrancy: 'hud' } });

// parent keeps a window above another of YOUR windows — an about box or tool
// panel that can't get lost behind the window it belongs to, without floating
// over other apps the way setLevel('floating') would. true = 'main', or name
// a win.open id. It also minimizes/hides with its parent, closes when the
// parent closes, and on Windows/Linux gets no taskbar entry of its own.
// Open-time only (Windows ownership can't be set later). One cross-OS
// difference: on macOS the window also MOVES with its parent.
tiny.win.open('about', { page: 'about.html', size: '360x420', parent: true });
```

Every window runs the full `tiny.*` bridge, and `tiny.win.*` calls from a
page target *its own* window. Backend side:

```js
app.win.open('settings', { page: 'settings.html' });
app.window('settings').setTitle('…');   // eval, push, close, hide/show,
                                        // setSize/Position, chrome, getState…
app.push('event', data);                // broadcasts to every window
app.windows();
export function onWindowClosed(id, app) {}   // also a 'window-closed' event
export function onWindowState(info, app) {}  // { win, fullscreen, maximized,
                                             // minimized, focused } on every
                                             // transition — the backend twin
                                             // of tiny.win.onState
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
tiny.app.presence('menubar');         // no Dock icon — menu-bar-only app
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
`tiny.win.show()` / `app.show()`, and `app.presence('normal')` turns the app
back into a regular one.

### Windowless tray agents

`"mainWindow": false` starts the launcher and backend without a main window
or WebView. `"activation": "accessory"` only hides the main window, so WebKit
still runs; with this flag there is no `main` window at all, and WebKit's
processes don't start until you open a window. On macOS that took a tray
agent's idle footprint from ~65 MiB to ~17 MiB; on Linux, from ~330 MiB to
~110 MiB (launcher plus WebKit processes); on Windows, from ~110 MiB private
(launcher plus six WebView2 processes) to ~12 MiB with none. Combine the
two flags for an agent with a menu-bar icon and no Dock icon:

```json
{ "mainWindow": false, "activation": "accessory" }
```

```js
export function init(app) {
  app.tray.set({ icon: 'sf:camera', menu: [
    { id: 'settings', label: 'Settings…' },
    { id: 'quit', label: 'Quit' },
  ] });
}

export function onTray(id, app) {
  if (id === 'settings') {
    app.win.open('settings', { page: 'settings.html', size: '600x400' });
    app.window('settings').show();
  }
  if (id === 'quit') app.quit();
}
```

You don't need a frontend or `index.html` at startup; add pages only for
the windows you open later. `app.windows()` starts as `[]`. Closing the last
named window leaves the backend and tray running, and you can open it again.
Main-window operations don't create a window — use `app.win.open` and
`app.window(id)`. `app.hide()` / `app.show()` hide and show the whole app.
`app.reload()` reloads the named windows that are open. Dev watches the
frontend only if that directory exists. `"url"` can't be combined with
`"mainWindow": false`, because there is no main window to load it into.

On Windows and Linux, which have no app-level hide, `app.hide()` hides every visible
named window and `app.show()` brings exactly those back.

**macOS and Linux so far.** Windows accepts the flag but keeps the main
window, hidden, as `"activation": "accessory"` does: the same tinyjs.json
works, `app.windows()` starts as `["main"]`, and you don't get the memory
saving. The CLI prints a notice there.

This mode still needs a logged-in GUI session for the tray; it isn't a
server or SSH mode. Closing a window may not free all of WebKit's memory,
because WebKit can keep its worker processes cached. If the editor must
release everything when it closes, ship it as a separate app that the agent
launches.

### Remembering where windows were

```json
{ "rememberWindows": true }
```

Every window goes back where the user left it: position, size, the screen it
was on, and maximized/fullscreen. Placements are kept per **screen layout**
(the set of plugged-in screens), the way the OS keeps its own windows', so
docked and laptop-only each get their own arrangement back. A screen is
recognised by more than its id — uid, name, resolution, scale, built-in-ness
and its place in the layout all count — so a replugged monitor (new id, same
panel) still matches. If the window's screen is gone, it lands on the screen
under the cursor at the same relative spot, clamped to fit.

The main window is built hidden and shown once it's been placed, so it never
flashes at the default position (it shows anyway after 3s if something goes
wrong). A `win.open()` without `x`/`y`/`screen` reopens where that window id
last was; an explicit position always wins. The state lives in
`window-state.json` next to `store.json`. Off by default. Apps with their own
policy can build it from `getState()` (`screenId`, `local`, `pixels`),
`matchScreen()` and `setPosition(x, y, { screen })` instead. On Wayland only
size and fullscreen come back — the compositor owns position.

### Apps that place their own windows

If your app moves its own windows — `win.setPosition`, `win.center`, snapping,
docking, anything that decides where a window goes — say so:

```json
{ "windowPlacement": true }
```

It's a no-op on macOS and Windows, which always let a window place itself.
It matters on Linux: **Wayland forbids a client from placing its own
toplevels**, so `setPosition` and `center` do nothing on a Wayland session,
and `mousePosition` reads 0,0 because the global pointer is hidden too. With
this key set, the launcher asks GTK for the X11 backend (XWayland on a
Wayland desktop), where placement, the global cursor, `captureScreen` and
`keystroke` all work — so a window-choreographing app behaves the same as it
does on macOS.

Don't set it if you don't need it: the Wayland backend is the better one when
you don't (native fractional scaling, no XWayland translation layer).

To branch at runtime instead, ask what the machine can do:

```js
tiny.system.os()                     // 'macos' | 'windows' | 'linux'   (sync)
tiny.system.isMacOS()                // sync — as are isWindows() / isLinux(),
                                     // so a page can branch before first paint
await tiny.system.architecture()     // 'arm64' | 'x86_64' — must be awaited:
                                     // WKWebView says MacIntel on Apple silicon
await tiny.system.info()             // { os, arch, session, desktop }
const can = await tiny.system.capabilities();
if (!can.windowPosition) useDragInstead();   // e.g. tiny.win.startDrag()
```

`capabilities()` reports what this machine can actually do, so an app can
degrade on purpose instead of calling something that quietly does nothing.

### Filtering your own audio

`tiny.audio.filters` runs a DSP chain on **this app's own output** — a graphic
EQ, headphone correction, a crossover — below the browser:

```js
const can = await tiny.system.capabilities();
// One backend decision, then identical code: pageChain speaks the same verbs.
const eq = can.audioFilters ? tiny.audio : tiny.audio.pageChain(ctx);
if (eq.input) {                          // the page chain needs routing once
  source.connect(eq.input);
  eq.output.connect(ctx.destination);
}
await eq.filters([
  { type: 'gain', gain: 1.0 },                          // preamp (linear)
  { type: 'peaking', freq: 60, q: 1.1, gain: 4 },       // dB
  { type: 'highshelf', freq: 8000, q: 0.7, gain: -2 },
]);
eq.filter(1, { gain: -3 });   // retune one, live, no gap
await eq.clear();             // remove
```

Types: `peaking`, `lowshelf`, `highshelf`, `lowpass`, `highpass`, `bandpass`,
`notch`, `allpass` (`freq`/`q`/`gain`, gain in dB) and `gain` (a linear
multiplier). Filters apply in order.

Why this exists rather than "just use Web Audio": on Linux you can't. WebKitGTK
renders the Web Audio graph on a normal-priority thread while its media threads
get real-time priority, so **anything reaching `ctx.destination` crackles** —
on an idle machine, at any `latencyHint`, from an element or a decoded buffer.
Filtering below the browser sidesteps that, and picks up two things Web Audio
never had: it applies to audio the page doesn't own (raw radio streams, native
HLS, a CORS-tainted `<audio>`), and it survives a page reload.

How it's built, per platform:

- **Linux** — a `libpipewire-module-filter-chain` sink the app's stream is
  routed through.
- **macOS 14.2+** — a Core Audio process tap over the app's own WebKit audio
  processes, *muted*, fed back through an aggregate device wrapping the real
  output, with the biquads applied in between. No driver, no system install.
  The maths is the same RBJ cookbook PipeWire's builtins use, so the same
  numbers give the same curve on both.
- **Windows** — reports `false`, permanently, and the reason is measured
  rather than pending: taking the direct signal off the speakers there means
  attenuating the audio session, and Windows *persists* session volume on a
  key every WebView2 app on the machine shares — a crash while filtering
  would near-silence all of them (see `TODO-audio-filters.md` for the
  numbers). Instead, `tiny.audio.pageChain(ctx)` builds the same chain from
  Web Audio nodes in the page — same band vocabulary, same RBJ curves
  (verified: ask +12 dB at the centre, measure 12.00), same four verbs, so
  the snippet above needs no second code path. Two honest differences: it
  filters only what you route through it (not native HLS or CORS-tainted
  media the page never gets samples for), and Web Audio's shelves ignore `q`
  / per-filter `gainR` (use `balance()`). Don't reach for it on Linux — see
  the crackle above; that's what the native chain is for.

Three limits worth knowing:

- **Filter count.** 15 on Linux (PipeWire's filter-chain crashes above that, so
  tinyjs truncates rather than letting a page take the audio server down), 32
  on macOS.
- **macOS needs the audio-capture permission**, which is granted per bundle id
  on first use — so this only engages in a **packaged** app, not `tinyjs dev`.
  Until it does, the chain sits armed but idle.
- **A chain that can't engage leaves audio alone.** It never half-applies and
  never mutes: on macOS the tap starts unmuted and silent, and only takes over
  once it has proven it can actually hear the app. An unauthorized tap returns
  success and silence rather than an error, so trusting it would mute the app
  outright — the one failure mode genuinely worth designing against.

Replacing the chain rebuilds it (a brief gap); changing only *values* retunes
in place. Keep the shape stable if you're driving it from a slider — pass the
same list of types every time and just vary the numbers.

### Missing system packages

Linux ships its media stack in pieces — AAC and H.264 live in optional
GStreamer plugin sets — so a feature can be absent on one machine and present
on the next. `requirements()` answers what's missing and how to fix it:

```js
const [aac] = await tiny.system.requirements(['media.aac']);
// -> { id, ok, feature, detail, install: { manager, packages, command } }

// the same thing with the satisfied ones dropped — the common case, and
// empty on macOS/Windows, where all of it ships with the OS
const gaps = await tiny.system.missing();
```

`promptMissing()` is the presentable version: it shows a native dialog naming
the feature, explaining why it's absent, and offering a **Copy install
command** button — nobody retypes `gstreamer1.0-plugins-bad` correctly from a
toast. It shows **nothing at all** when the requirements are satisfied, so
call it straight from a failure path:

```js
audio.addEventListener('error', () => tiny.system.promptMissing(['media.aac']));
```

Ask for several at once and their packages merge into a single command, so the
user runs one line. Where nothing installable would fix it (`windowPosition`
on Wayland is the session, not a package) it explains instead of offering a
command that doesn't exist.

Probes are cached for the life of the app, so a package the user just
installed would still read as missing — pass `{ refresh: true }` to re-probe
when they retry:

```js
const [aac] = await tiny.system.requirements(['media.aac'], { refresh: true });
```

Ids: `media.aac`, `media.h264`, `media.mp3`, `speech`, `spotlight.index`,
`audioTap`, `tray`, `windowPosition`. Everything reports `ok` on macOS and
Windows, so this costs nothing to call cross-platform.

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
generates `AppIcon.icns` from the app icon (`assets/icon.png`) via `sips` + `iconutil` and
codesigns everything (ad-hoc by default; set `signIdentity` in tinyjs.json
or `TINYJS_SIGN_IDENTITY` for a Developer ID).

By default the .app runs only on the build Mac's CPU type: the bundled `tjs`
runtime is single-arch, so an app built on Apple Silicon won't open on an
Intel Mac (the build says so: `runs on: Apple Silicon (arm64)`). Two opt-ins,
both from any Mac:

- `tinyjs build --arch x86_64` (or `arm64`; `TINYJS_ARCH` does the same) builds
  the .app for that CPU only. The other arch's `tjs` comes from the same
  txiki.js release, downloaded once and cached in `~/Library/Caches/tinyjs`.
  Artifacts are named `<name>-<ver>-macos-<arch>.dmg` / `.zip`, so an
  Apple Silicon and an Intel build can ship side by side — the usual way to
  release for both. Apple Silicon Macs can also run the Intel build under
  Rosetta.
- `tinyjs build --universal` (or `TINYJS_UNIVERSAL=1`) makes one .app with
  both slices, `lipo`'d together — about 6 MB bigger, and it needs the Xcode
  Command Line Tools.

In a source checkout either one rebuilds the launcher for arm64 + x86_64 first
(`TINYJS_UNIVERSAL=1 ./setup.sh` does it by hand); release installs already
ship it universal. The bare `dist/<name>` binary stays host-only.

`tinyjs build --dmg` additionally produces `dist/<name>-<version>.dmg` — the
.app plus an /Applications shortcut, the classic installer image. With a real
Developer ID, `tinyjs notarize` submits the built .app via `notarytool`
(keychain profile from tinyjs.json `"notarize": { "profile": … }` or
`TINYJS_NOTARY_PROFILE`) and staples the ticket. Pass `tinyjs notarize --dmg`
to also rebuild the dmg from the **stapled** .app — a dmg made at build time
holds the pre-staple bundle, so its ticket is missing and offline Gatekeeper
rejects it. (If a dmg already exists on disk, `notarize` refreshes it
automatically, since that copy is guaranteed stale.)

### Bundled data resources

Data your own code reads at runtime — a dictionary, a database seed, fonts,
sample files — goes in a `resources/` folder in the project. The whole folder
ships with the app, and `app.paths()`'s `resources` points at it:

```
my-app/
  resources/
    dictionary.json
    fonts/
```

```js
export async function init(app) {
  const root = (await app.paths()).resources;
  if (root === null) return;
  const bytes = await tjs.readFile(root + '/dictionary.json');
  console.log(new TextDecoder().decode(bytes));
}
// Frontend: the existing RPC returns the same root.
const paths = await tiny.app.paths();
console.log(paths.resources); // string | null
```

In dev, the root is the project's `resources/` folder itself, read in place.
Builds copy it under `Contents/Resources/app/resources/` in a macOS .app, or
`resources/` beside the executable in bare/Windows/Linux portable builds, so
the same relative paths work in both. These are real external files, not TPK
modules: distribute the whole folder. The root is independent of the launch
directory and follows a relocated build. Local `tinyjs publish` archives
include the resources for updates. Without a `resources/` folder (or with
`"resources": []`) nothing is copied and both APIs return `null`.

To ship only part of the folder, or something different per OS, list paths
inside it in tinyjs.json; platform blocks replace the list for that OS:

```json
{
  "resources": ["dictionary.json", "fonts/"],
  "windows": { "resources": ["dictionary.json", "fonts/", "win-only.dat"] }
}
```

This follows the same rule as every other key in a platform block: the OS
block's list replaces the root one for that OS, it doesn't add to it, and an
OS without its own list uses the root value. So a list in one OS block alone
narrows only that OS — the others still ship the whole folder. To give one
OS a file the others don't get, empty the root list:

```json
{
  "resources": [],                                  // Windows, Linux: nothing
  "macos": { "resources": ["mac-only.png"] }        // macOS: just this
}
```

A list narrows the build only — in dev the whole folder is still readable, so
check a narrowed build before you ship it. Listed paths are normalized
(including backslash separators); duplicates and children of a listed
directory are copied once. Missing files, absolute/drive/UNC paths and `..`
fail with a CLI error, and so do symlinks anywhere in what ships, including
`resources/` itself. Windows components ending in a dot or space are also
rejected.

Resources are for data. They don't change `readAccess` or what the page may
read: read the files in the backend and expose only what your page needs
through your API. Files macOS itself reads (Credits, a help book) go in
`assets/` instead, and programs your app launches in `executables/`, both
below.

### Bundled executables (sidecars)

Programs your backend launches — a CLI built with Go, Rust, Bun, Deno, pkg or
PyInstaller, an ffmpeg — go in an `executables/` folder. It works like
`resources/`: the whole folder ships by default, `"executables": [...]`
narrows it, and `app.paths()`'s `executables` points at what shipped. The one
difference is that a build only ships its own OS's binaries, so the
binaries for every OS can sit in the folder together:

```
my-app/
  executables/
    my-cli.exe            # Windows (PE): only Windows builds ship it
    my-cli                # macOS (Mach-O, universal)
    linux/my-cli          # Linux's own my-cli, since the name clashes
    linux-arm64/my-cli    # …and the arm64 one, for arm64 builds
```

```js
export async function init(app) {
  const dir = (await app.paths()).executables;   // string | null
  const exe = dir + '/my-cli' + (tjs.env.OS === 'Windows_NT' ? '.exe' : '');
  const p = tjs.spawn([exe, '--version'], { stdout: 'pipe' });
  // …read p.stdout, then await p.wait()
}
// Frontend: (await tiny.app.paths()).executables is the same folder.
```

With no list, a build takes every file at the top of `executables/`, then
the files in `executables/<os>/` (`macos`, `windows` or `linux`), then those
in `executables/<os>-arm64/` or `executables/<os>-x86_64/` for a build for
that CPU — a later file replaces an earlier one with the same name. A macOS
`--universal` build skips the CPU folders. Files at the top that aren't this
OS's binaries (another OS's, a README) are left out with a note; in an OS
folder, where everything is meant for that OS, they fail the build.

To pick the files yourself, list paths inside `executables/` — a folder
means the files in it. Platform blocks replace the list for their OS, as
with `resources`:

```json
{
  "executables": ["my-cli.exe"],
  "macos": { "executables": ["mac/my-cli", "mac/helper"] },
  "linux": { "executables": ["linux/"] }
}
```

Everything ships flat — `my-cli` lands at `(await app.paths()).executables + '/my-cli'`
whichever folder it came from — so two files with the same name in one list
fail the build. A listed file must be a native binary for that OS. `[]`
ships nothing. Paths follow the `resources` rules (no `..`, absolute paths
or symlinks).

In dev, `executables` is `.build/executables/`, holding links to
the files a build would ship (copies on Windows), so a sidecar you rebuild
is picked up on the next spawn. Run `chmod +x` on a file that isn't
executable — dev says so; builds mark them. Builds copy the files to
`Contents/Helpers/` in a macOS .app, or `executables/` beside the
executable in bare/Windows/Linux portable builds, and `tinyjs publish`
archives (so auto-updates) carry them. Without an `executables/` folder, or
with nothing in it for this OS, both APIs return `null`.

Only this OS's native binaries ship, plus the libraries they load (`.dll`,
`.dylib`, `.so`), and `#!` scripts on Linux. A script can't go in a macOS
.app's code folder (it can't carry a signature there): put it in
`resources/` and run it with its interpreter. The same goes for data the
program reads.

**macOS signing and notarization.** A .app's executables all need a valid
signature, so the build signs each sidecar with the app's identity
(`signIdentity`, ad-hoc by default) before the app itself, with the
identifier `<id>.<file>`. With a Developer ID it adds the hardened runtime
and a secure timestamp, which notarization requires of every executable in
the bundle. The hardened runtime kills a JIT runtime — Bun, Deno, Node/pkg
— on its first compiled function, unless the binary has the entitlement. A
sidecar keeps the entitlements it arrived with; to set them, list them per
shipped file name (they replace the ones it had):

```json
{
  "macos": {
    "executableEntitlements": {
      "my-cli": ["com.apple.security.cs.allow-jit",
                 "com.apple.security.cs.allow-unsigned-executable-memory",
                 "com.apple.security.cs.disable-library-validation"]
    }
  }
}
```

The app's own `permissions` entitlements (mic, camera) don't extend to
sidecars. The build warns when the app runs on Intel Macs but a sidecar has
no x86_64 slice — the app would open there and then fail to spawn it. An
x86_64-only sidecar still runs on Apple Silicon, under Rosetta 2 (the build
notes it). A binary you downloaded
may be quarantined, which blocks it in dev (the build's copy is clean):
`xattr -d com.apple.quarantine executables/macos/my-cli`.

### App assets

`assets/` holds the files the OS and the build read, each by its name. None
of them ship as data, and the build notes anything else it finds there,
since that's usually a page or data file in the wrong folder (or a README
meant for [`extras/`](#download-extras-readme-licence)).

| File | Used for |
|---|---|
| `icon.png` | The app icon on every OS (1024×1024; `tinyjs new` puts the default here). `"icon"` in tinyjs.json overrides the path. |
| `Credits.html`, `Credits.rtf` or `Credits.rtfd` | macOS About panel credits, in place of the tinyjs line (see [Menus](#menus)). |
| a help book folder | macOS Help menu, named by `"macos": { "helpBook": "<folder>" }`. |

Projects from before `assets/` keep working: `icon.png` and `Credits.html`
are still found at the project root.

**Help books (macOS).** A help book is a folder of HTML pages that Help
Viewer shows, with its own `Contents/Info.plist` (Apple's [help book
guide](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/ProvidingUserAssitAppleHelp/authoring_help/authoring_help_book.html)
covers the layout). Put the folder in `assets/` and name it:

```json
{ "macos": { "helpBook": "My App Help" } }
```

The build copies it to the .app's `Contents/Resources/`, writes
`CFBundleHelpBookFolder` and `CFBundleHelpBookName` (the book's own
`CFBundleIdentifier`) into the app's Info.plist, and rebuilds the book's
search index with `hiutil` for each `.lproj` when the book names one
(`HPDBookIndexPath`). The app then gets a Help menu with "<App> Help" (⌘?)
and a working search field, with no code; add items with
`{ role: 'help' }` (see [Menus](#menus)). Help Viewer only knows a built app,
so `tinyjs dev` shows the Help menu without the book. Help Viewer caches
books by version: rebuild with an edited book at the same `version` and it
keeps showing the old copy (even from a fresh build in another folder).
Bump `version` to see your changes.

### Download extras (README, licence)

Files for the person downloading your app (a README, a licence, install
notes) go in an `extras/` folder. They ship next to the app, never inside
it:

| OS | Where they land |
|---|---|
| macOS | the root of the dmg, beside the .app and the Applications shortcut |
| Windows | the app folder, beside `<name>.exe` (`dist/` and the publish zip) |
| Linux | the app folder, beside `<name>` (`dist/` and the publish tarball) |

The four folders, by who reads them:

| Folder | Read by | Ships |
|---|---|---|
| `resources/` | your code | inside the app |
| `executables/` | your backend, which spawns them | inside the app |
| `assets/` | the OS (icon, Credits, help book) | inside the app, by name |
| `extras/` | the person downloading | beside the app |

With no list, a build ships everything at the top of `extras/`, then what's
in `extras/<os>/` (`macos`, `windows` or `linux`), which replaces a file of
the same name. A folder ships whole; `.DS_Store` and friends are skipped,
and exec bits are kept, so an `install.sh` in a Linux tarball runs:

```
my-app/
  extras/
    LICENSE.txt           # every OS
    README.txt            # Windows and Linux…
    macos/README.txt      # …and the Mac's own, which mentions Gatekeeper
```

To pick the files yourself, list paths inside `extras/`. Each lands by its
own name, wherever it sits in the folder; platform blocks replace the list
for their OS, and `[]` ships nothing:

```json
{
  "extras": ["LICENSE.txt", "README.txt"],
  "macos": { "extras": ["LICENSE.txt", "macos/README.txt"] }
}
```

Every build prints what it shipped (`==> extras: LICENSE.txt, README.txt → dmg`).
Extras sit beside files the build writes, so a name the build uses fails it
(`launcher.exe`, `<name>.exe`, `resources`, the `.app`, `Applications`, …),
compared case-insensitively. So do two extras with the same name. Paths
follow the `resources` rules (no `..`, absolute paths or symlinks).

Some things to know:

- **macOS: only the dmg carries them.** Build with `--dmg` (or let
  `tinyjs notarize --dmg` rebuild it, which re-reads `extras/`); a build
  without it notes that the extras went nowhere. The .app and the
  `tinyjs publish` zip (the update payload) never carry them, since nobody
  looks inside a bundle once it's in /Applications.
- **Linux: updates replace them.** An update swaps the whole app folder, so
  each version's extras replace the last one's (and anything a user saved
  in that folder goes too).
- **Windows: updates overwrite them by name.** A running app's folder can't
  be swapped, so an update copies the new version's files over the old
  ones. An extra with the same name is replaced; one you drop or rename
  stays in the folder, so renaming `README.txt` to `README.md` leaves both.
  Files a user saved there stay too. To retire an extra, ship a file of
  the same name that says so rather than deleting it.
- **Needed after install?** Once the dmg is ejected, a Mac user no longer
  has its README. For help people will need later, use something in the
  app: Credits or a help book (see [App assets](#app-assets)), or a file in
  `resources/` that a Help menu item opens.
- `tinyjs dev` ignores `extras/`. A README-like file put in `assets/` gets a
  note pointing here.

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
5. **Ship**: `tinyjs build && tinyjs notarize` (for a dmg installer, use
   `tinyjs build --dmg && tinyjs notarize --dmg` so the image is rebuilt from
   the stapled .app). The stapled .app opens anywhere, no warnings.

### Shipping updates (auto-update)

To authenticate the update manifest with a key pinned in your app, opt in to
[signed updates](docs/docs/signed-updates.md): same url, same manifest plus a
signed block, so existing installs keep updating. That guide covers keys,
expiry and multi-platform publishing.

Point `tinyjs.json` at a manifest you host anywhere (any static host —
GitHub Releases, S3, nginx):

```json
{ "name": "myapp", "version": "1.1.0",
  "update": { "url": "https://example.com/myapp/manifest.json" } }
```

Each release: `tinyjs publish --notes "What changed"` → `dist/publish/`
contains `myapp-1.1.0.zip` + `manifest.json` (version, download url, sha256,
notes) — upload both to the directory `update.url` points at
(`--notes-file CHANGES.md` for longer notes). Shipping separate Apple
Silicon and Intel builds? Run `tinyjs publish --arch arm64` then
`tinyjs publish --arch x86_64`: the second run keeps the first's zip and
merges both into one manifest (a `"mac": { "arm64": …, "x86_64": … }` block,
with the arm64 build also in the top-level `url` for apps that predate the
block). Each installed app downloads the build for its own Mac. Windows
works the same way without a flag: an app's build matches the tinyjs that
made it, so publish once with the x64 tinyjs and once with the arm64 one
(both can live on one Windows on ARM machine; x64 runs emulated) from the
same project. The manifest gets a `"win": { "x86_64": …, "arm64": … }`
block, with the x64 build also in `win.url` for apps that predate it; an
x64 app on an ARM64 machine moves to the arm64 build. In the app:

```js
const { available, latest, notes } = await tiny.app.update.check();
if (available) await tiny.app.update.install();   // downloads, verifies
// sha256 + code signature, swaps the .app in place, relaunches
```

Or let tinyjs check for you: `"update": { "url": …, "auto": "launch" }`
(or `"daily"`) checks in the background (packaged apps only) and fires the
`update-available` page event / `onUpdateAvailable(info, app)` backend
export with `{ current, latest, notes }` — show your own prompt, then
`tiny.app.update.install()`:

```js
tiny.api.on('update-available', async ({ latest, notes }) => {
  if (await tiny.dialog.confirm(`Update to ${latest}?`, { detail: notes ?? '' }))
    await tiny.app.update.install();
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
┌──────────────────────┐  unix socket   ┌──────────────────────────┐
│ backend (txiki.js)   │◄──────────────►│ launcher (C++, ~1–2 MB)  │
│ your src/main.js     │  line protocol │ native/launcher-macos.cc │
│ + runtime/bridge.js  │                │ · WKWebView window       │
│ · owns app logic     │                │ · webview_bind bridge    │
│ · fs/net/process API │                │ · native dialogs         │
│ · spawns launcher    │                │ · else: dumb forwarder   │
└──────────────────────┘                └──────────┬───────────────┘
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
| backend → launcher | `DLG <id> <op>[\t<arg>…]`  | native dialog (file panels, alert/confirm/prompt); page ids resolve in the page, `q<id>` replies to the backend via `GOT <id> <json>` |
| backend → launcher | `MENUBEGIN` … `MENU <t>` / `ITEM id\tlabel\tkey\tflags\ticon` / `SEP` … `MENUEND` | declare custom menu bar menus |
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
| backend → launcher | `AUDIOTAP <qid> <scope>\t<excludeSelf>\t<interval>` / `AUDIOTAP STOP` | start/stop an output PCM tap (start answered as `GOT`) |
| launcher → backend | `AUDIOTAP <b64>\t<sr>\t<ch>\t<frames>\t<t>` | a tap PCM chunk (base64 interleaved Int16) |
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
bin/tjs               txiki.js runtime (fetched by setup.sh, not committed;
                      bin/tjs.exe on Windows via setup.ps1)
native/launcher-macos.cc  the macOS window process (Objective-C++; webview
                      headers vendored)
native/launcher-win.cc  the Windows window process (WebView2 via the same
                      vendored webview; named pipe instead of a Unix socket)
native/launcher-linux.cc  the Linux window process (GTK3 + WebKitGTK 4.1
                      directly, no vendored webview; Unix socket, same as macOS)
native/make-icon.jxa  default template icon generator (osascript -l JavaScript)
runtime/bridge.js     backend bridge library (socket, protocol, win.* methods)
runtime/update.js     app auto-update (manifest check, verify, bundle swap)
template/             what `tinyjs new` copies
skill/SKILL.md        tinyjs reference for coding agents (copied into new
                      projects at .claude/skills/tinyjs/ and, for other
                      agents, .agents/skills/tinyjs/)
cli.js + tinyjs       the CLI (tinyjs.cmd is the Windows wrapper; Git Bash
                      on Windows uses the sh wrapper tinyjs)
test/smoke.html       self-driving test page
docs/                 tinyjs.app site (VitePress, deployed to GitHub Pages by
                      .github/workflows/site.yml): docs/docs/*.md are the
                      docs pages; docs/public/ is copied as-is — landing
                      page, installers (docs/public/install is what
                      `curl tinyjs.app/install` fetches) and the schema.
                      docs/docs/api-map.md is generated by
                      scripts/api-map/run.sh.
                      Preview: cd docs && pnpm install && pnpm dev
CHANGELOG.md          release history (the site's /changelog includes it)
setup.sh              from-source bootstrap (fetch tjs, compile natives) —
                      handles both macOS and Linux, branching on `uname`
setup.ps1             the same for Windows (also fetches the WebView2 header)
TODO-windows.md       remaining Windows-port work, ordered (tick items off
                      as they land)
TODO-linux.md         remaining Linux-port work, ordered (tick items off
                      as they land)
.github/workflows/    release automation: tag vX.Y.Z → universal binaries →
                      per-arch tarballs + checksums → GitHub release
```

After editing the natives, re-run `./setup.sh` (or the `c++`/`cc` lines
inside it); on Windows, re-run `setup.ps1` (it regenerates `tiny_client.h`
and recompiles `launcher-win.exe`). To cut a release: update `CHANGELOG.md` (tinyjs.app/changelog renders it
— retitle the "upcoming" section with the date), then `git tag vX.Y.Z && git push --tags`.

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
- Every `tjs.*` fs call is async while `tjs:sqlite` is fully synchronous —
  an unawaited `tjs.makeDir` before `new Database(dir + '/x.db')` is a race
  that surfaces as "database not initialized", but only sometimes.
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
- **Occluded windows are throttled.** WebKit starves `requestAnimationFrame`
  and timers in a window that's covered or off-screen, so a hidden window
  can't reliably drive a visible one (e.g. a background renderer feeding a
  foreground viz). Do time-critical/continuous work in the *visible* window,
  or in the backend (which isn't a webview and isn't throttled) and push
  results to the page.
- **`file://` media is sandboxed to the page's own directory.** By default
  `<audio>`/`<video>`/`<img>` can only load assets under the frontend dir;
  a file elsewhere fails with `MEDIA_ERR_SRC_NOT_SUPPORTED`. Widen the read
  root with the **`readAccess`** option — `createApp({ readAccess: true })`
  (the user's home dir) or `readAccess: '/abs/media/root'`, or
  `"readAccess"` in tinyjs.json (`true` / a path). Then `file://` assets
  anywhere under that root load directly — no base64 → Blob round-trip.
  (It's opt-in because it widens what the page can read from disk.)
- **Custom User-Agent.** WKWebView's default UA omits the trailing
  `Version/x Safari/x`, so UA-sniffing sites see an unrecognized browser.
  Override it with `createApp({ userAgent: '…' })` or `"userAgent"` in
  tinyjs.json (packaged apps read it from the `TinyjsUserAgent` plist key the
  build writes; in dev it rides in on `TINYJS_UA`). Useful when pointing a
  `devUrl` at a real hosted site. Note that a UA alone often isn't enough:
  many SaaS apps (Slack, etc.) *also* feature-detect at boot and deliberately
  refuse embedded/non-blessed browsers — an embedded WKWebView genuinely lacks
  some of what they require (e.g. Web Push), so spoofing the UA gets you
  recognized as a browser but not necessarily *in*.
- **`"inject"` (document-start JS).** `"inject": "src/shim.js"` in tinyjs.json
  injects that file (bundled at build; `.ts` goes through esbuild) into
  **every** page at document-start, on any origin — the tool for shimming a
  wrapped third-party site before it boots. Dev carries it in the
  `TINYJS_INJECT` env var; packaged .apps ship it as `Resources/app/inject.js`.
  It runs your code inside someone else's origin with full page privileges —
  pair it with an `"api"` capability gate when the origin isn't yours.
- **Site wrappers.** `"url": "https://…"` makes the main window a remote page
  (no local frontend needed). JS dialogs (`alert`/`confirm`/`prompt`),
  downloads (`"downloads"`: `auto` | `ask` | `deny`, with progress events),
  `window.open` (`"popups"`: `external` | `window` | `deny`, refinable
  per-popup from `onWindowOpen`'s `kind: 'policy'` call), navigation events +
  policy (`onNavigate` — return `'deny'`/`'external'` from a `kind: 'policy'`
  call; other URL schemes such as `mailto:` are blocked unless it returns
  `'external'`), and `tiny.win.find` make a wrapped site behave like an app. The
  `"api"` capability gate makes it shippable: `"api": "wrapper"` preset,
  `{ disable, enable }` lists of wire method names (enable wins), or
  per-origin keyholes — `"api": { "origins": { "file://*": "all",
  "https://app.example.com": ["notify", "store.*"] } }` — keyed off the
  calling frame's origin as WebKit reports it, not as the page claims it.
  In an origin key `*` matches any characters, dots included, against the
  whole `scheme://host[:port]` — so never end a key with `*` after the host
  (`https://app.example.com*` also matches `https://app.example.com.evil.com`).
  `capabilities().api.denied` tells a page what it can't have. All three
  platforms; the per-engine caveats (a denied navigation still made its
  request on Linux, an asked POST re-issues as a GET on Windows, popups keep
  default chrome everywhere) are in TODO-site-wrapper.md.
- **Cookies (`app.cookies`, backend only).** The webview's own jar — the one
  every window and a wrapped site share — including the HttpOnly cookies
  `document.cookie` can't see. The use it was built for: a site logs in,
  the backend keeps the session, and later pages skip the login.

  ```js
  // after the user signs in on the wrapped site
  const [sid] = await app.cookies.get({ url: 'https://app.example.com', name: 'sid' });
  await app.secrets.set('sid', JSON.stringify(sid));       // not tiny.store
  // next launch, before loading a page that needs it
  await app.cookies.set({ url: 'https://app.example.com', ...JSON.parse(await app.secrets.get('sid')) });
  await app.cookies.remove({ url: 'https://app.example.com' });  // log out
  ```

  `get({ url?, name?, domain?, path? })` → `[{ name, value, domain, path,
  expires (ms | null = session), secure, httpOnly, sameSite }]`; with `url`
  you get what a request to it would carry. `set({ url | domain, name,
  value, path = '/', expires?, secure?, httpOnly?, sameSite = 'lax' })`
  replaces a cookie with the same name/domain/path; `url` alone makes it
  host-only, a `domain` of `'.example.com'` covers subdomains. `remove()`
  takes a cookie from `get()` (name + domain + path) or `{ url, name? }`,
  and resolves the number removed. Names and values with control
  characters or `;` are refused. Linux needs WebKitGTK 2.42+ for `get()`
  without a `url`.

  **SameSite.** `set()` defaults to `'lax'` on every OS, because the engines
  disagree about a cookie with none. `get()` reports the policy the engine
  enforces, so a cookie the *site* set without SameSite reads as that
  engine's default: `'none'` on macOS (WebKit has no lax-by-default),
  `'lax'` on Windows (Chromium's default), and either on Linux depending on
  the WebKitGTK version. Passing a `get()` result back to `set()` keeps it
  working the same way, which is what the example above relies on.
  `'none'` generally needs `secure: true` to be sent cross-site.

  Pages get no cookie API: a page that could read the jar could read every
  site's session — if one needs a cookie, hand it over from your own `api`
  handler.

### On-device AI (`tiny.macos.ai`)

`tiny.macos.ai.generate()` runs Apple's FoundationModels LLM locally — offline,
no API key, fully private.

**Released macOS tarballs include it.** The release workflow builds on a
`macos-26` runner with the Swift shim linked in, so `tiny.macos.ai` is a
feature you can ship, not one users have to build for themselves.

`./setup.sh` does the same for a source build: it links the shim in when the
SDK it's compiling against carries FoundationModels, and quietly builds
without when it doesn't, so an older toolchain still works. It says which it
did.

Either way the launcher keeps its **macOS 14 floor** and weak-links
FoundationModels, so FoundationModels never raises what the launcher needs.
A built app still needs **macOS 15+**, because the bundled txiki.js runtime
is built for 15. `ai.availability()`
just returns `'unsupported'` below macOS 26, as does a launcher compiled
without the shim. So app code that guards on `availability()` is always safe, and
guarding is not optional: the honest states are *available*, *unavailable*
(Apple Intelligence off or still downloading) and *unsupported*, and only
the first one can generate anything.

```js
if (await tiny.macos.ai.availability() === 'available')
  await tiny.macos.ai.generate(prompt, { instructions });
```

The release build asserts what makes that safe, per architecture, because
each of these failures is silent: the deployment floor is still 14.0, the
FoundationModels link is still **weak** (a strong one would stop the
launcher loading at all below macOS 26 — the AI feature taking the whole app
down with it), and the shim is actually present rather than a broken Swift
step having shipped a launcher that just answers `'unsupported'`.

### Portability

macOS is the primary platform, and everything is tested on Windows and
Linux too, with per-OS handling where a system needs it. All three run the
same runtime and speak the identical wire protocol — only the launcher
and the transport differ:

|         | launcher source            | webview              | transport                 | bootstrap    | CLI          |
| ------- | -------------------------- | -------------------- | ------------------------- | ------------ | ------------ |
| macOS   | `native/launcher-macos.cc` | WebKit               | Unix socket               | `./setup.sh` | `tinyjs`     |
| Windows | `native/launcher-win.cc`   | WebView2             | named pipe (`\\.\pipe\…`) | `setup.ps1`  | `tinyjs.cmd` |
| Linux   | `native/launcher-linux.cc` | GTK3 + WebKitGTK 4.1 | Unix socket               | `./setup.sh` | `tinyjs`     |

Windows and Linux use the same vendored webview library as macOS;
`tjs.listen('pipe', …)`/libuv abstracts the socket-vs-named-pipe split.

**Anything unported fails cleanly, so cross-platform code can
feature-detect.** Capability calls reject with a specific reason, query
calls (`wifi`, `frontmostApp`, `system.locale`, `tray.position`) resolve
`null`, and fire-and-forget ones are silent no-ops. `tiny.macos.*` /
`app.macos.*` throw off macOS, except `ai.availability()`, which answers
`'unsupported'`. `tiny.system.capabilities()` (or `app.system.capabilities()`
on the backend) lists what this machine can't do.

#### Windows

Works:

- **Bridge & dev loop** — the full page↔backend bridge (api calls, push
  events, `tiny.fetch`), dev mode with hot reload and backend restart, Vite
  `devUrl` frontends.
- **Windows & chrome** — **multi-window** (`win.open` — each window gets its
  own WebView2 + bridge), the window ops
  (fullscreen/ontop/click-through/level/…), `transparent` chrome, and
  vibrancy→mica/acrylic backdrops (Win11).
- **Native UI** — menu bar with working `key:` accelerators (Ctrl+<key>),
  tray (+ balloon notifications), custom context menus, file/folder/save
  dialogs, alert/confirm/prompt.
- **Data in & out** — **drag & drop with real paths both ways**
  (`win.onDrop` in, `startDrag({ files })` out), clipboard
  (text/html/files/image, both directions).
- **System** — global hotkeys, `keystroke` (`cmd` maps to Ctrl),
  `shell.open/reveal/trash`, `secrets` (Credential Manager),
  `power.preventSleep`, theme + sleep/wake events, `printToPDF`,
  `captureScreen`, `thumbnail`, `say`/`voices` (SAPI), `launchAtLogin`
  (built apps; HKCU Run).
- **Shipping** — `tinyjs build` (a portable `dist/` folder: `<name>.exe` +
  `launcher.exe` + `frontend/`), `tinyjs publish` + **app auto-update**
  (file-by-file swap — Windows can't replace a whole running folder — then
  relaunch).

Not yet ported:

- `nowPlaying` / media keys — wants the WinRT `SystemMediaTransportControls`
- `pickColor`, `spotlight`, `system.locale`, `wifi`, `share`, the `proxyURL`
  media proxy
- the native `audio.filters` chain — use `tiny.audio.pageChain(ctx)`, the
  same chain in Web Audio (`capabilities().audioFilters` is false)
- the macOS-only `tiny.macos.*` family (Quick Look, OCR, AppleScript,
  recorder, selectedText, otherWindows/moveWindow, on-device AI) and
  Spaces

Burn-down list with implementation notes:
[TODO-windows.md](TODO-windows.md).

#### Linux

Works:

- **Windows & chrome** — window ops (hide/show/center/minimize/fullscreen/
  ontop/resizable/position/zoom/level/click-through/sticky), frameless +
  transparent chrome (`vibrancy` is a no-op), multi-window, hot reload.
- **Native UI** — menus + accelerators (Ctrl+key), native dialogs
  (open/save/folder/alert/confirm/prompt), custom context menus +
  suppression, notifications with action buttons
  (`org.freedesktop.Notifications`; no reply fields), the print dialog +
  `printToPDF`, and tray via AppIndicator/StatusNotifier (menu-based — a
  bare icon-click is emulated through a menu entry, and `tray.position()`
  returns `null`).
- **Data & storage** — clipboard (text/html/image/files + watch),
  `store`/`paths` (XDG dirs), `tiny.fetch`/`proxyURL` streaming, `secrets`
  (Secret Service/GNOME Keyring), `shell.open/reveal/trash`, `spotlight`
  (name search — indexed `plocate`/`locate` when present, else a bounded
  `find`).
- **System** — theme dark/light (+ live changes), sleep/wake events,
  `power.preventSleep` (a logind inhibitor), `launchAtLogin` (autostart
  `.desktop`, built apps only), `screens`/`mousePosition`/`win.getState`,
  `battery`, `idleTime` (GNOME), `pickColor` (portal), `thumbnail` (images
  only), `captureScreen` (X11 sessions only), `app.attention` (a taskbar
  urgency hint), `app.icon` (the window icon), `app.presence` (the
  skip-taskbar hint) — those last three are **X11 sessions only**, since GTK's
  Wayland backend has no protocol to carry any of them and drops all three;
  `capabilities()` reports them per session. `app.badge`/`app.progress` go out
  over the Unity LauncherEntry DBus protocol instead, so they work on either
  session but only where the dock implements it (KDE Plasma, Ubuntu Dock,
  Dash-to-Dock — not vanilla GNOME Shell), and only for a **built** app, whose
  `.desktop` entry is what the signal is addressed to.
- **Input** — global hotkeys (X11 via XGrabKey; pure Wayland via the
  GlobalShortcuts portal — the compositor prompts to approve them once),
  `keystroke` synthesis (X11/XWayland via XTest — not pure Wayland).
- **Audio & media** — `playSound`/`beep`, `say`/`voices` (via
  speech-dispatcher's `spd-say` when installed), `nowPlaying` + media keys
  (a real MPRIS player — shows in the GNOME/KDE media widget and lock
  screen, and Play/Pause/Next/Previous/Seek route back via `onMediaKey`),
  `audioTap` (system scope — the default sink's monitor via `parec`/
  `pw-cat`; like Windows, `scope:'app'` is approximated by the system mix).
- **Shipping** — a built app registers its own `.desktop` entry on first run
  (app-menu listing, icon, deep links via `urlScheme`, file associations via
  `fileExtensions`, and single-instance) with no separate install step.
  `tinyjs publish` emits `<name>-<version>-linux-<arch>.tar.gz` plus a
  manifest carrying a per-arch `"linux": { "<arch>": { url, sha256 } }`
  block alongside the mac and win ones; auto-update works (swap + relaunch).

Not (yet) supported:

- the macOS-only `tiny.macos.*` family (recorder, OCR, Quick Look,
  AppleScript, selectedText, otherWindows/moveWindow, on-device AI)
- `attention({critical: true})`, `share`
- `wifi`, `frontmostApp`, `system.locale`, `authenticate`
- `setAllSpaces` — maps onto sticky windows rather than true per-Space
  follow

Burn-down list with implementation notes: [TODO-linux.md](TODO-linux.md).

## Future explorations

Nothing here is planned work — these are directions worth measuring, mostly
aimed at the same thing: the ~6 MB a shipped app costs today, of which
`bin/tjs` is 5.6 MB and the launcher 1–2 MB (1.6 MB on macOS, 2.0 MB on
Windows, 1.2 MB on Linux).

### A slimmer txiki.js

txiki.js already exposes build flags for the parts we don't all need:
`BUILD_WITH_WASM=OFF` (~0.4 MB) and `BUILD_WITH_SQLITE=OFF` (~1.5 MB),
plus `BUILD_WITH_STRIP`/`BUILD_WITH_LTO`/`BUILD_WITH_GC_SECTIONS` and
`BUILDTYPE=MinSizeRel`, which cut size without dropping features
([docs](https://txikijs.org/docs/building/#optional-features)). The
[tjs-lite discussion](https://github.com/saghul/txiki.js/discussions/954)
reports 6.4 MB → 3.31 MB with WASM/SQLite/TLS out, and ~2.15 MB with the
optimization flags stacked on top, for a 7–8% faster start.

`setup.sh` can already build from source (`TJS_BUILD=1`), so this is
mostly a matter of picking flags and re-measuring. The catches: we
document `tjs:sqlite` as the backend database story, so dropping it is an
API break rather than a size win; dropping TLS would take `tiny.fetch` to
https, `proxyURL`, and auto-update with it; and `MinSizeRel` trades
compute-heavy JS throughput for bytes. A likely first cut is
WASM-off + strip/LTO/gc-sections, keeping SQLite and TLS — worth building
and timing against the numbers above rather than assuming they transfer.

### scriptc as a backend

[scriptc](https://github.com/vercel-labs/scriptc) (Vercel Labs) compiles
TypeScript straight to a native binary with no JS engine bundled: ~170–200 KB
static, ~2.4 ms startup, 1–4 MB RSS, with an embedded QuickJS (~620 KB)
available under `--dynamic` for the parts that need real dynamic
evaluation. It covers a useful slice of Node's surface (fs, path, process,
crypto, child_process, net/http/https/tls, dgram, dns, fetch).

For tinyjs that would mean a `bridge.js` rewritten in TypeScript against
scriptc's APIs, shipping a launcher plus a sub-megabyte backend. The
unknowns are whether tjs-specific pieces (`tjs.listen('pipe', …)`, `tjs.spawn`
stdio, FFI, `tjs app compile` as the packaging step) have equivalents that
hold up, and what a user's own `src/main.js` becomes — today it's plain JS
run by a real engine, and static compilation is a different contract. It's a
research spike, not a port.

## Credits

Built on [txiki.js](https://github.com/saghul/txiki.js) (MIT) by Saúl Ibarra
Corretgé and [webview](https://github.com/webview/webview) (MIT). MIT
licensed.
