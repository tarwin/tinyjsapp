# tinyjs

Tiny desktop apps for macOS: a [txiki.js](https://txikijs.org/) backend + a
native [webview](https://github.com/webview/webview) window.

- **~6 MB shipped**, two real files — no Electron, no Node, no bundled Chromium
- **No HTTP server, no ports** — the page and the backend talk over a Unix
  domain socket in a private temp directory
- Backend is plain JavaScript with full system access (files, sockets,
  processes, FFI) via txiki.js
- Frontend is plain HTML/CSS/JS rendered by the system WebKit
- Hot reload in dev, signed `.app` bundles out of `build`

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/tarwin/tinyjsapp/main/install.sh | sh
```

Installs to `~/.tinyjs` and symlinks `tinyjs` onto your PATH. Pin a
version with `TINYJS_VERSION=vX.Y.Z`. To install from source instead:

```sh
git clone https://github.com/tarwin/tinyjsapp && cd tinyjsapp
./setup.sh    # downloads the txiki.js runtime, compiles launcher + shim
ln -s "$(pwd)/tinyjs" /usr/local/bin/tinyjs
```

## Create and run an app

```sh
tinyjs new myapp
cd myapp
tinyjs dev
```

A window opens. `dev` hot-reloads: edit anything in `src/frontend/` and the
page is re-inlined and swapped in place (no restart, backend state survives);
edit backend sources and the process restarts automatically.
`TINYJS_DEBUG=1 tinyjs dev` traces every message crossing the bridge.

A project is just:

```
myapp/
  tinyjs.json            # { name, title, size, id, version, icon?, signIdentity? }
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
  // app also has: setTitle(t), setSize(w, h), eval(js), reload(html), quit()
}
```

Frontend — include `tiny.js` (from the template) and:

```js
const greeting = await api.call('hello', { name: 'world' });  // request/response
api.on('tick', (t) => ...);                                   // backend push
```

Built-in methods every app gets:

| method                          | returns                              |
|---------------------------------|--------------------------------------|
| `ping`                          | `"pong"`                             |
| `log {msg}`                     | logs to the backend console          |
| `quit`                          | closes the window, exits the app     |
| `win.setTitle {title}`          | `true`                               |
| `win.setSize {width, height}`   | `true`                               |
| `win.openFile` / `win.openFiles`| path / array of paths, `null` if cancelled |
| `win.pickFolder`                | path or `null`                       |
| `win.saveFile`                  | path or `null`                       |

The `win.*` dialogs are native (NSOpenPanel/NSSavePanel): the backend hands
the call id to the launcher, which runs the panel on the UI thread and
resolves the page's promise directly.

## Build for release

```sh
tinyjs build
```

produces:

- `dist/<name>` — a standalone single-file executable (`tjs app compile`;
  the whole frontend is inlined and embedded). Great for local/CLI use.
- `dist/<Name>.app` — a fully **codesigned** macOS bundle, the artifact you
  distribute.

The build inlines the frontend (scripts, stylesheets, images and CSS `url()`
refs become one self-contained HTML), generates `AppIcon.icns` from
`icon.png` via `sips` + `iconutil`, and codesigns everything (ad-hoc by
default; set `signIdentity` in tinyjs.json or `TINYJS_SIGN_IDENTITY` for a
Developer ID).

### Why the .app looks the way it does (codesigning)

tjs-compiled binaries can't be re-signed — txiki appends the bundled app
after the Mach-O, which `codesign` rejects ("main executable failed strict
validation"; fine locally, fatal for distribution). So the bundle ships the
**stock** runtime plus the app as plain data, started by a tiny signable shim:

```
MyApp.app/Contents/
  MacOS/myapp            tiny C shim (CFBundleExecutable) — execs tjs
  MacOS/tjs              stock runtime binary — signs cleanly
  MacOS/launcher         window process
  Resources/app/         entry.js, bridge.js, assets.js, src/… (plain data,
                         sealed by the bundle signature)
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
                                        │ your page (inlined)   │
                                        │ api.call('m', params) │
                                        │ api.on('event', fn)   │
                                        └───────────────────────┘
```

- The backend creates a socket in a fresh `0700` temp dir (invisible to other
  users; no ports to collide or scan), materializes the page next to it,
  listens, then spawns the launcher.
- Closing the window ends the launcher; the backend notices, cleans up, exits.
  `api.call('quit')` works the other way (backend sends `QUIT`).

### Wire protocol (newline-delimited; payloads are JSON so never contain raw \n)

| direction          | line                       | meaning                          |
|--------------------|----------------------------|----------------------------------|
| launcher → backend | `CALL <id> <json-args>`    | page invoked `api.call(...)`     |
| backend → launcher | `RET <id> <status> <json>` | resolve (0) / reject (≠0) a call |
| backend → launcher | `EVAL <js>`                | run JS in the page               |
| backend → launcher | `TITLE <text>` / `SIZE <w> <h>` | window control              |
| backend → launcher | `DLG <id> <op>`            | native dialog; launcher answers the call itself via `webview_return` |
| backend → launcher | `RELOAD`                   | re-read the page file and re-render (hot-reload) |
| backend → launcher | `QUIT`                     | close the window                 |

In the page, `window.__invoke` (injected by `webview_bind`) already returns a
promise — the shim in `tiny.js` is ~10 lines.

## Developing tinyjs itself

```
bin/tjs               txiki.js runtime (fetched by setup.sh, not committed)
native/launcher.cc    the window process (Objective-C++; webview headers vendored)
native/shim.c         .app main executable: execs tjs on Resources/app/entry.js
native/make-icon.jxa  default template icon generator (osascript -l JavaScript)
runtime/bridge.js     backend bridge library (socket, protocol, win.* methods)
runtime/inline.js     frontend asset inliner
template/             what `tinyjs new` copies
cli.js + tinyjs     the CLI
test/smoke.html       self-driving test page
install.sh            curl installer (downloads GitHub release tarballs)
setup.sh              from-source bootstrap (fetch tjs, compile natives)
.github/workflows/    release automation: tag vX.Y.Z → universal binaries →
                      per-arch tarballs + checksums → GitHub release
```

After editing the natives, re-run `./setup.sh` (or the `c++`/`cc` lines
inside it). To cut a release: `git tag v0.1.0 && git push --tags`.

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
- The launcher loads HTML via `webview_set_html` (not `file://` navigation)
  to sidestep WKWebView local-file restrictions — hence the build-time
  inliner.
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
