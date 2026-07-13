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

A project is just:

```
myapp/
  tinyjs.json            # { name, title, size, id, version, icon?, signIdentity?,
                         #   urlScheme?, fileExtensions?, update?, notarize? }
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
  // tray.set(spec)/tray.remove(), store.get/set/delete/all,
  // hotkey.register(id, combo)/unregister(id), setContextMenu(items),
  // update.check()/update.install()
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

Frontend — include `tiny.js` (from the template); everything injected lives
under the `tiny` namespace:

```js
const greeting = await tiny.api.call('hello', { name: 'world' });  // request/response
tiny.api.on('tick', (t) => ...);                                   // backend push

tiny.log('debug msg');  tiny.quit();
tiny.notify('Done', 'Your export finished');   // desktop notification

// window control
tiny.win.setTitle('My App');  tiny.win.setSize(1200, 800);
tiny.win.center();  tiny.win.setPosition(100, 80);      // top-left origin
tiny.win.minimize();  tiny.win.fullscreen();            // fullscreen toggles
tiny.win.setAlwaysOnTop(true);  tiny.win.setResizable(false);
tiny.win.hide();  tiny.win.show();
tiny.win.setHideOnClose(true);  // close button hides instead of quitting

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
    { separator: true },
    { id: 'hello', label: 'Say Hello' },
  ]},
]);
tiny.menu.on((id) => { ... });   // clicks; backend can also export onMenu(id, app)
```

The dialogs and menus are native: the backend hands the work to the launcher,
which runs panels/menus on the UI thread and answers the page's promise
directly via `webview_return`.

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
  title: 'MyApp',            // text in the menu bar (and/or icon: png path)
  icon: 'tray.png',          // template image by default ({ template: false } to keep colors)
  tooltip: 'My tiny app',
  menu: [
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
classic toggle-window behavior.

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

### Shipping updates (auto-update)

Point `tinyjs.json` at a manifest you host anywhere (any static host —
GitHub Releases, S3, nginx):

```json
{ "name": "myapp", "version": "1.1.0",
  "update": { "url": "https://example.com/myapp/manifest.json" } }
```

Each release: `tinyjs publish` → `dist/publish/` contains
`myapp-1.1.0.zip` + `manifest.json` (version, download url, sha256) — upload
both to the directory `update.url` points at. In the app:

```js
const { available, latest } = await tiny.api.call('update.check');
if (available) await tiny.api.call('update.install');   // downloads, verifies
// sha256 + code signature, swaps the .app in place, relaunches
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
`codesign --verify --strict --deep` — and is notarizable once you use a real
Developer ID (hardened runtime + `notarytool` are the remaining steps).

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
| backend → launcher | `TRAYBEGIN <t>\t<icon>\t<tmpl>\t<tip>` … `ITEM`/`SEP` … `TRAYEND` / `TRAYREMOVE` | declare/remove the menu bar status item |
| launcher → backend | `TRAY <id>` / `TRAYCLICK`  | tray menu item / bare tray icon clicked |
| backend → launcher | `WINOP <op> [args]`        | hide, show, center, minimize, fullscreen, ontop, resizable, pos, dock, hideonclose |
| launcher → backend | `DROP <json-paths>`        | files dragged onto the window (real paths) |
| backend → launcher | `CTXBEGIN` … `ITEM`/`SEP` … `CTXEND` / `CTXCLEAR` | replace/restore the right-click menu |
| launcher → backend | `CTX <id>`                 | a context menu item was clicked  |
| backend → launcher | `HKREG <id>\t<combo>` / `HKUNREG <id>` | global hotkeys       |
| launcher → backend | `HOTKEY <id>`              | a global hotkey fired            |
| launcher → backend | `SYS theme dark\|light` / `SYS sleep` / `SYS wake` | system events (theme also once at startup) |
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
