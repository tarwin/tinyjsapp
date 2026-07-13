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
                    #   --dmg: also dist/<name>-<ver>.dmg installer image
tinyjs publish      # build + dist/publish/<name>-<ver>.zip + auto-update manifest
tinyjs notarize     # notarytool submit + staple (needs Developer ID + profile)
TINYJS_DEBUG=1 tinyjs dev   # trace every bridge message
```

## Project layout

```
tinyjs.json          { name, title, size, id, version, icon?, signIdentity?,
                       update?: { url: "https://…/manifest.json" },
                       urlScheme?: "myapp", fileExtensions?: ["md"] }
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
  // setContextMenu(items), update.check()/update.install()
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
await tiny.app.info();   // { version: <app>, tinyjs: <built with>, runtime: <txiki> }

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

// theme + power events
await tiny.theme.get();                     // { dark } | null
tiny.theme.on((dark) => ...);               // live changes
tiny.api.on('sleep', fn); tiny.api.on('wake', fn);  // backend: export onSystem

tiny.win.print();                           // native print panel

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
- Verify changes with the smoke pattern: run
  `TINYJS_HTML=<tinyjs-install>/test/smoke.html tinyjs dev` — expect a
  `[web] SMOKE RESULTS {...}` line with no FAIL entries and a clean exit.
  A GUI window opens briefly; the page drives itself and quits.
- `tinyjs build` output: `dist/<Name>.app` is the distributable (fully
  codesigned); `dist/<name>` is a local-only single binary.
