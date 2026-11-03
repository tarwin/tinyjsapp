---
title: "calling the backend"
---

# calling the backend

<p class="tagline">tiny.api — request/response and events</p>

The page has no file system, processes or database; the [backend](./backend) has all of them. `tiny.api` is how the two talk: the page calls functions the backend exports in `api`, and the backend sends events back. This page also covers the small calls that sit beside it on `tiny`: logging, notifications, app info, quitting, and file URLs.

```js
// backend: src/backend/main.js
export const api = {
  greet: ({ name }) => `hello, ${name}`,
};

// page
const text = await tiny.api.call('greet', { name: 'Ada' });   // 'hello, Ada'
```

`tiny.api` itself is page-only: the backend calls its own functions directly, and talks back with `app.push` (see [events to the page](./backend#push)). For the rest, the backend twin has the same name on `app` — `tiny.notify()` is `app.notify()`, `tiny.app.info()` is `app.info()`, `tiny.quit()` is `app.quit()`. The [API map](./api-map) lists both sides.

## calling a function {#call}

`tiny.api.call(method, params?)` runs `api[method]` in the backend and resolves with what it returns.

```js
const notes = await tiny.api.call('listNotes', { folder: 'inbox' });
```

- **`params`** is any JSON value; the backend function gets `{}` when you pass nothing. It goes through `JSON.stringify`, so a `Date` arrives as a string and `undefined` fields are dropped.
- **The result** must be JSON too. A function that returns nothing (or `undefined`) resolves `null`.
- **Calls run concurrently.** The backend starts each call as it arrives and doesn't wait for one to finish before the next, so two calls that touch the same state can interleave at their `await`s.
- **Names**: tinyjs's own calls (`fetch`, `log`, `notify`, `store.get`, `win.close`, and every other name the `tiny` object sends) win over a backend function with the same name. Pick names that don't collide. See [api functions](./backend#api) for the backend side, including the caller's window id and origin.

## errors {#errors}

A backend function that throws rejects the page's promise. The rejection is the error's **message as a plain string**, not an `Error`:

```js
// backend
export const api = {
  openNote: async ({ path }) => {
    if (!path.endsWith('.md')) throw new Error('not a note');
    return new TextDecoder().decode(await tjs.readFile(path));
  },
};

// page
try {
  await tiny.api.call('openNote', { path });
} catch (msg) {
  showError(msg);        // 'not a note' — msg.message is undefined
}
```

The same holds for every `tiny` call that rejects. Other reasons a call rejects:

| rejection | why |
|---|---|
| `unknown method: <name>` | no `api` function, and no built-in call, has that name |
| `"<name>" is disabled by tinyjs.json "api"` | the [`api`](./config#api) gate in tinyjs.json doesn't allow that call for this page's origin |

For errors the page should handle in code, return `{ ok: false, reason }` instead of throwing — the stack and any custom fields on a thrown error stay in the backend.

## events from the backend {#events}

The backend sends events with `app.push(name, data)` (every window), `app.win.push(name, data)` (the main window) or `app.window(id).push(name, data)` (one window). The page listens with `tiny.api.on`:

```js
// backend
setInterval(() => app.push('tick', { now: Date.now() }), 1000);

// page
const stop = tiny.api.on('tick', ({ now }) => { clock.textContent = new Date(now).toLocaleTimeString(); });
// later
stop();
```

- `tiny.api.on(event, fn)` adds a handler and returns a function that removes it. It's additive, like `addEventListener`: every handler for the event runs, in the order they were added.
- `tiny.api.off(event, fn)` removes a handler by reference.
- The `tiny.*.on` helpers (`tiny.menu.on`, `tiny.tray.on`, `tiny.theme.on`, `tiny.app.onNotificationClick`, …) wrap your function before subscribing, so `off(event, fn)` can't find it. Keep the function they return — it's the only way to unsubscribe.
- An event pushed before the page has subscribed is lost. A page that needs data at start should ask for it with `tiny.api.call` rather than wait for a push from [`init`](./backend#init).
- A handler that throws is reported in the console like any uncaught error, and the other handlers still run.

tinyjs sends its own events through the same channel, so you can listen to them by name too, and should avoid these names for your own: `menu`, `contextmenu`, `tray`, `trayclick`, `hotkey`, `theme`, `sleep`, `wake`, `locale`, `open-url`, `open-files`, `drop`, `notification-click`, `notification-action`, `media-key`, `window-state`, `window-closed`, `screens-changed`, `clipboard-change`, `audio-tap`, `update-available`, `navigate`, `download`, `popup`. The [backend hooks](./backend#hooks) table says what each one is.

## logging to the terminal {#log}

`tiny.log(msg)` prints in the terminal running the backend, tagged `[web]`, so the page's output and the backend's interleave in one place instead of one of them hiding in the webview inspector.

```js
tiny.log('loaded');
tiny.log({ notes: notes.length, ms: performance.now() });   // objects print as objects
await tiny.log('saved');   // resolves once the backend has printed it
```

`msg` can be anything JSON can carry.

Desktop notifications, `tiny.notify()`, have their own page: [notifications](./notifications).

## app info {#info}

`tiny.app.info()` resolves to what the app is and what it runs on:

```js
const { version, tinyjs, runtime } = await tiny.app.info();
// { version: '1.2.0', tinyjs: '0.62.1', runtime: 'txiki.js 26.6.0' }
```

| field | |
|---|---|
| `version` | your app's [`version`](./config#version) from tinyjs.json, or `'0.0.0'` without one |
| `tinyjs` | the tinyjs version that built the app; `'dev'` when tinyjs runs from a source checkout |
| `runtime` | the backend runtime, e.g. `'txiki.js 26.6.0'` |

In the backend it's `await app.info()`. Reading `app.info.version` without calling it still works but is deprecated.

## quitting {#quit}

`tiny.quit()` quits the app: every window closes and the backend exits. Window positions saved by [`rememberWindows`](./config#rememberWindows) are written first. In the backend it's `app.quit()`. Closing the main window from its page with `tiny.win.close()` also quits.

## local files in the page {#fileURL}

`tiny.fileURL(path)` turns a path on disk into a `file://` URL for an `<img>`, `<audio>` or `<video>` `src`, escaped correctly on every OS. It runs in the page and returns a string at once.

```js
const path = await tiny.api.call('coverArt', { track });   // '/Users/me/Music/cover.jpg'
img.src = tiny.fileURL(path);
// Windows: tiny.fileURL('C:\\Music\\cover.jpg') → 'file:///C:/Music/cover.jpg'
// a UNC share: '\\\\nas\\music\\a.mp3' → 'file://nas/music/a.mp3'
```

Building the URL by hand with `'file://' + path` breaks on Windows: the drive letter becomes the URL's host.

On macOS the page may only load files inside its own folder. For anything outside it — and a path from the backend usually is — set [`readAccess`](./config#readAccess) in tinyjs.json. Without it the element fires `error` and nothing says why. Windows and Linux don't need it.

macOS also won't let a `file://` page load files from a different disk, even with `"readAccess": "/"`: a second partition, a USB drive or a network share under `/Volumes`, or the startup disk for an app run from a mounted `.dmg`. For a path on a different disk than the page, `tiny.fileURL` returns a `tiny-media://file/…` URL, and the app serves the file itself (seeking works). It's still limited to the `readAccess` folder, and only the app's own pages can use it. Web Audio (`createMediaElementSource`) works on it the same as on a `file://` track.
