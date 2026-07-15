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
TINYJS_DEBUG=1 tinyjs dev   # trace every bridge message
```

## Project layout

```
tinyjs.json          { name, title, size, id, version, icon?, signIdentity?,
                       update?: { url: "https://…/manifest.json" },
                       urlScheme?: "myapp", fileExtensions?: ["md"],
                       permissions?: { microphone?: "why", camera?: "why" },
                       chrome?: { frame, trafficLights, transparent, vibrancy },
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
  // applescript(source), show({ activate: false })
}

export function onMenu(id, app) { ... }  // optional: menu clicks, backend-side
export function onTray(id, app) { ... }  // optional: tray clicks (id null = icon)
```

Runtime is txiki.js (`tjs` global): `tjs.readFile/writeFile/readDir/stat`,
`tjs.spawn`, `tjs.watch`, `tjs.listen/connect`, `fetch`, `WebSocket`, sqlite,
FFI. Docs: https://txikijs.org. Gotchas: streams need `getReader()` (no
`for await`); `tjs.cwd` is a property; spawn stdio silencer is `'ignore'`.

## Frontend

The `tiny` global is injected into every page automatically (no script tag);
TypeScript definitions live in types/tiny.d.ts (TinyApiHandler, TinyApp, …):

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
tiny.win.setChrome({ frame: false, trafficLights: false,
                     transparent: false, vibrancy: 'hud' });  // frameless etc.
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
// icon: png path OR 'sf:<name>' (SF Symbol, e.g. 'sf:cup.and.saucer.fill' — no assets)
// primaryAction: true → left click fires onClick, menu opens on right-click
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

tiny.win.print();                           // native print panel

// auto-update: "update": { "url": …, "auto": "launch" | "daily" } checks in
// the background (packaged apps) → 'update-available' page event /
// onUpdateAvailable(info, app) export with { current, latest, notes };
// notes come from `tinyjs publish --notes "…"` (or --notes-file FILE).
// Then: await tiny.api.call('update.install')

// multiple windows: any frontend html file can be a window
tiny.win.open('settings', { page: 'settings.html', title: 'Settings', size: '420x300' });
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
- Verify changes with the smoke pattern: run
  `TINYJS_HTML=<tinyjs-install>/test/smoke.html tinyjs dev` — expect a
  `[web] SMOKE RESULTS {...}` line with no FAIL entries and a clean exit.
  A GUI window opens briefly; the page drives itself and quits.
- `tinyjs build` output: `dist/<Name>.app` is the distributable (fully
  codesigned); `dist/<name>` is a local-only single binary.
