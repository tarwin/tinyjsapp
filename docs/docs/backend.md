---
title: "backend"
---

# backend

<p class="tagline">src/backend/main.js — full system access via <a href="https://txikijs.org/" target="_blank" rel="noopener">txiki.js</a></p>

Every tinyjs app is two processes: the page, in the system webview, and the backend, a JavaScript module running on [txiki.js](https://txikijs.org/). The backend has what the page doesn't — files, processes, sockets, a [sqlite](./sqlite) — through the `tjs` global and `tjs:*` modules. The page asks it for things with [`tiny.api.call`](./api), and it talks back with events. See [how it works](./how-it-works) for the wiring.

The backend is `src/backend/main.js` (or `.ts`), or wherever [`backend`](./config#backend) points. It exports up to three kinds of thing:

```js
// functions the page can call
export const api = {
  readNotes: async ({ dir }, app) => { /* … */ },
};

// runs once at startup
export async function init(app) {
  app.push('ready', { at: Date.now() });
}

// called when something happens: a menu item, a tray click, a file opened…
export function onMenu(id, app) {}
```

## api functions {#api}

Each function in `api` is a call the page can make:

```js
// backend
export const api = {
  rename: async ({ from, to }, app, { window, origin }) => {
    await tjs.rename(from, to);
    return { ok: true };
  },
};

// page
const r = await tiny.api.call('rename', { from: 'a.txt', to: 'b.txt' });
```

- **Arguments**: the params the page passed (`{}` if none), the [`app` handle](#app), and the caller — the id of the `window` it came from and the page's `origin`.
- **Returning** resolves the page's promise with the value, which must be JSON (`undefined` arrives as `null`). Functions may be `async`.
- **Throwing** rejects the page's promise with the error's message — a plain string, not an `Error`, so `catch (msg)` gets the text itself and `msg.message` is `undefined`. The stack and any custom fields stay behind; for anything richer, return `{ ok: false, … }` instead.
- **Names**: `fetch`, `log`, `notify`, `ping` and `quit` are tinyjs's own calls and win over yours, as do dotted names like `win.close`. Pick other names.

Expose named operations (`rename`, `saveNote`) rather than general ones (`exec(cmd)`, `query(sql)`): anything in `api` can be called by any page the app loads. An app that loads a site it doesn't control must limit calls with [`api` in tinyjs.json](./config#api).

## init {#init}

`init(app)` runs once, after the native window exists — the page may still be loading. Set things up here: menus, a tray icon, hotkeys, a database. A page that needs data on start should ask for it with an `api` call rather than wait for a push from `init`, which can arrive before the page has subscribed.

## events to the page {#push}

`app.push(name, data)` sends an event to every window; the page listens with `tiny.api.on(name, fn)`. `app.win.push(name, data)` reaches only the main window, and `app.window(id).push(…)` one window. With an [`api`](./config#api) block, a page from an origin the app didn't ship — a [wrapped site](./site-wrappers#events) — only gets events its rules allow; your own events need `event.<name>`.

```js
// backend
setInterval(() => app.push('tick', { now: Date.now() }), 1000);

// page
const off = tiny.api.on('tick', ({ now }) => { … });
```

## hooks {#hooks}

Export any of these and tinyjs calls it. Each also reaches the page as an event, mostly with its own `tiny` listener. A hook that throws is reported in the console and the app carries on.

| export | called when | see |
|---|---|---|
| `onMenu(id, app)` | a menu bar item is chosen | [menus](./menus) |
| `onContextMenu(id, app)` | a custom right-click menu item is chosen | [menus](./menus) |
| `onTray(id, app)` | a tray menu item is chosen; `id` is `null` for a click on the icon itself | [tray](./tray) |
| `onHotkey(id, app)` | a global hotkey is pressed | [hotkeys](./hotkeys) |
| `onSystem(kind, value, app)` | the theme changes, or the computer sleeps or wakes (`'theme'`, `'sleep'`, `'wake'`) | [system](./system) |
| `onLocale(info, app)` | the OS language or region changes (macOS) | [system](./system#locale) |
| `onOpenUrl(url, app)` | a `myapp://` link opens the app | [deep links](./deep-links) |
| `onOpenFiles(paths, app)` | files or folders are opened with the app | [deep links](./deep-links) |
| `onNotificationClick(id, app)` | a notification is clicked | [notifications](./notifications#clicks) |
| `onNotificationAction(info, app)` | a notification's button is pressed, or it's replied to | [notifications](./notifications#actions) |
| `onMediaKey(info, app)` | a media key (play/pause, next…) is pressed | [media](./media) |
| `onWindowState(info, app)` | a window goes full screen, is maximized or minimized, or gains or loses focus | [tiny.win](./window) |
| `onWindowClosed(id, app)` | a window closes | [multiple windows](./multiple-windows) |
| `onClipboardChange(info, app)` | the clipboard changes (exporting it starts watching) | [clipboard](./clipboard) |
| `onAudioTap(chunk, app)` | a chunk of tapped audio arrives | [audioTap](./audiotap) |
| `onUpdateAvailable(info, app)` | an automatic update check finds a newer version | [auto-update](./auto-update) |
| `onNavigate(info, app)` | a page navigates — and the backend may allow or block it | [site wrappers](./site-wrappers) |
| `onDownload(info, app)` | a download starts, finishes or fails — it reports; the [`downloads`](./config#downloads) setting decides | [site wrappers](./site-wrappers) |
| `onWindowOpen(info, app)` | a page opens a popup — and the backend decides where it goes | [site wrappers](./site-wrappers) |
| `onBeforeQuit(info, app)` | the user asks to quit — return `false` to cancel | [quitting](./api#quit) |
| `onQuit(app)` | the app is ending, whatever the reason — awaited, up to 5 seconds | [quitting](./api#quit) |

## the app handle {#app}

The `app` passed to `init`, hooks and `api` functions has the same calls the page has on `tiny`, under the same names: a page's `tiny.win.setTitle()` is `app.win.setTitle()` for the main window, `tiny.store.get()` is `app.store.get()`, `tiny.app.paths()` is `app.paths()`, and so on. The [API map](./api-map) lists every call on both sides. Other windows are `app.window(id)`.

[`app.dialog`](./dialogs) has the same file pickers and message boxes as `tiny.dialog`. Use it from `init` or async hooks such as `onTray`; it also works with `"mainWindow": false`, without creating a WebView.

The backend has one call of its own, `app.spawnHidden(args, opts)`: `tjs.spawn` that never flashes a console window on Windows. A console program spawned from a built Windows app otherwise opens a terminal window each time; on macOS and Linux it's the same as `tjs.spawn`.

## what the backend can use {#runtime}

- **txiki.js**: files, processes, sockets, an HTTP server, workers — the `tjs` global and `tjs:*` modules. See [txiki.js](./txiki) for what's there and links to its reference.
- **Web APIs**: `fetch`, `TextEncoder`, `crypto.subtle`, `URL`, `WebSocket` and the rest txiki.js provides — [listed here](./txiki#web).
- **npm packages**, in a TypeScript backend: it's bundled with esbuild, so anything that doesn't need Node's built-in modules works.
- **Your own files**: [resources](./resources) for data, [executables](./executables) for programs, [shared code](./shared-code) for modules the page uses too.
- **A database**: SQLite is built in — see [sqlite](./sqlite).
- **The webview's cookies**: read and change them with `app.cookies` — see [cookies](./cookies).

The page can't use any of this directly: it has no `tjs`, so expose what it needs through `api`.
