---
name: tinyjs
description: Build and modify tinyjs desktop apps — tiny macOS apps with a txiki.js JavaScript backend and a native WebKit window. Use when working in a project with a tinyjs.json, or when the user mentions tinyjs, tiny.api, or tinyjs dev/build.
---

# Building tinyjs apps

tinyjs (https://tinyjs.app, repo tarwin/tinyjsapp) makes ~6 MB macOS desktop
apps: a **txiki.js backend** (full system access: files, sockets, processes,
FFI) + a **native WebKit window**. They talk JSON-RPC over a private Unix
socket — no HTTP server, no ports.

## Commands

```sh
tinyjs new <dir>    # scaffold
tinyjs dev          # run with hot reload (frontend edits swap in place;
                    #   backend edits restart the process)
tinyjs build        # dist/<name> binary + dist/<Name>.app (codesigned)
tinyjs publish      # build + dist/publish/<name>-<ver>.zip + auto-update manifest
TINYJS_DEBUG=1 tinyjs dev   # trace every bridge message
```

## Project layout

```
tinyjs.json          { name, title, size, id, version, icon?, signIdentity?,
                       update?: { url: "https://…/manifest.json" } }
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
  // setHideOnClose(v), setDockVisible(v), tray.set(spec)/tray.remove(),
  // update.check()/update.install()
}

export function onMenu(id, app) { ... }  // optional: menu clicks, backend-side
export function onTray(id, app) { ... }  // optional: tray clicks (id null = icon)
```

Runtime is txiki.js (`tjs` global): `tjs.readFile/writeFile/readDir/stat`,
`tjs.spawn`, `tjs.watch`, `tjs.listen/connect`, `fetch`, `WebSocket`, sqlite,
FFI. Docs: https://txikijs.org. Gotchas: streams need `getReader()` (no
`for await`); `tjs.cwd` is a property; spawn stdio silencer is `'ignore'`.

## Frontend (include tiny.js before your code)

Everything injected lives under `tiny`:

```js
await tiny.api.call('method', { params })   // -> backend api.<method>
tiny.api.on('event-name', (data) => ...)    // <- app.push from backend

tiny.log(msg); tiny.quit();

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
  { separator: true },
  { id: 'hello', label: 'Say Hello' },
]}]);
tiny.menu.on((id) => ...);                  // clicks (also a 'menu' api event)

tiny.notify(title, body);                   // desktop notification
tiny.win.center(); tiny.win.minimize(); tiny.win.fullscreen();  // toggles
tiny.win.setPosition(x, y);                 // top-left origin
tiny.win.setAlwaysOnTop(v); tiny.win.setResizable(v);
tiny.win.hide(); tiny.win.show(); tiny.win.setHideOnClose(v);
tiny.win.onDrop((paths) => ...);            // files dropped on the window: real paths

// tray / menu-bar apps
tiny.tray.set({ title, icon, tooltip, menu: [{ id, label }, { separator: true }] });
tiny.tray.on((id) => ...); tiny.tray.onClick(fn); tiny.tray.remove();
tiny.app.setDockVisible(false);             // menu-bar-only app
// tray-app recipe: tray.set + win.setHideOnClose(true) + app.setDockVisible(false)

// auto-update (needs tinyjs.json "update".url; ships via `tinyjs publish`)
const { available, latest } = await tiny.api.call('update.check');
await tiny.api.call('update.install');      // verify + swap .app + relaunch
```

An app menu (About + Quit) and an Edit menu (copy/paste shortcuts) always
exist; `tiny.menu.set` adds menus after them. About shows name + version from
tinyjs.json automatically.

## Rules of thumb

- Add backend capabilities as `api` methods; keep the frontend thin.
- Escape anything interpolated into `innerHTML` — the page holds an RPC
  channel with full system access, so a filename must never become markup.
- Keep frontend asset references relative (they resolve against the page's
  directory); no external fetches at runtime unless intended.
- Verify changes with the smoke pattern: run
  `TINYJS_HTML=<tinyjs-install>/test/smoke.html tinyjs dev` — expect a
  `[web] SMOKE RESULTS {...}` line with no FAIL entries and a clean exit.
  A GUI window opens briefly; the page drives itself and quits.
- `tinyjs build` output: `dist/<Name>.app` is the distributable (fully
  codesigned); `dist/<name>` is a local-only single binary.
