---
title: "multiple windows"
---

# multiple windows

<p class="tagline">tiny.win.open — settings panels, palettes, document windows</p>

An app starts with one window, `'main'`. `tiny.win.open()` opens more: each is a separate native window with its own page, named by an id you choose. Every window gets the full `tiny` API, and [`tiny.win`](./window) in a window's page controls that window. The backend opens the same windows with `app.win.open()` and reaches each one through `app.window(id)`.

```js
await tiny.win.open('settings', { page: 'settings.html', title: 'Settings', size: '420x300' });
```

## opening a window {#open}

`tiny.win.open(id, opts?)` opens a window under `id`. If a window with that id is already open, it's brought to the front and focused instead, and `opts` are ignored — so a Settings menu item can call `open('settings', …)` every time.

| option | | default |
|---|---|---|
| `page` | an HTML file in your frontend folder, like `'settings.html'`, or an `http(s)` URL | `'index.html'` |
| `title` | the window title | the id |
| `size` | `'WxH'`: the page's box, without the title bar or menu bar — the same units as [`setSize`](./window#position) | `'600x400'` |
| `minSize` | `'WxH'`: a floor under the user's resizes | none |
| `x`, `y` | the top-left, in global points, set before the window first shows | |
| `screen` | a [`screens()`](./window#screens) entry or id; `x`, `y` are then relative to that screen | |
| `units` | `'pt'` or `'px'` for `x`, `y` with `screen` | `'pt'` |
| `chrome` | the same fields as [`setChrome`](./window#chrome), applied before the first paint | |
| `parent` | `true` (the main window) or a window id — see [child windows](#parent) | |
| `show` | `false` opens the window hidden — see [opening hidden](#hidden) | `true` |

`chrome` and the position apply before the window is drawn, so a frameless panel never flashes a title bar and a positioned window doesn't jump from the centre:

```js
tiny.win.open('hud', {
  page: 'hud.html', size: '320x120', x: 40, y: 40,
  chrome: { frame: false, windowControls: false, transparent: true, vibrancy: 'hud' },
});
```

A page can only open files inside the app's frontend folder, or `http(s)` URLs; an absolute path or a `../` that leads out of the frontend rejects. The backend's `app.win.open()` also takes absolute paths.

New windows show the app menu on Windows and Linux; give one its own with [`tiny.win.menu.set()`](./menus) or none with `chrome: { menu: false }`. With [`rememberWindows`](./config#rememberWindows), a window opened without `x` and `y` goes back to where it was last time, at the size it had then.

### opening hidden {#hidden}

`show: false` creates the window, places it and loads its page, but keeps it off screen. Show it when it's ready, from its own page with `tiny.win.show()` or from the backend with `app.window(id).show()`:

```js
// opener
await tiny.win.open('report', { page: 'report.html', show: false });

// report.html
await fillTables();
tiny.win.show();
```

The window appears once, already filled in and already where it belongs. A placement saved by `rememberWindows` applies while it's hidden, and a saved maximized or fullscreen state is put back when it first shows. A `parent` relation also takes effect then. Nothing shows the window for you, so a page that never calls `show()` leaves an invisible window, which still counts in `windows()`.

Don't open at `size: '1x1'` and resize later to fake this: with `rememberWindows` the saved size replaces `1x1`, so the window shows straight away and then jumps.

## the window ids {#ids}

```js
tiny.win.id;                    // the window this page is in: 'main' or its open() id
await tiny.win.windows();       // ['main', 'settings', 'hud']
```

`windows()` lists the open windows; `'main'` is in it when there is a main window. `win.open('main')` shows and focuses the main window.

## closing {#close}

```js
tiny.win.close();               // this window
tiny.win.close('settings');     // another one
```

`close(id?)` closes the window with that id, or the calling page's own window with no id. Closing `'main'` quits the app. The close button on a secondary window closes just that window.

When a window closes, every remaining page gets a `'window-closed'` event and the backend's `onWindowClosed(id, app)` hook runs:

```js
tiny.api.on('window-closed', ({ id }) => {
  if (id === 'settings') reloadPreferences();
});
```

## child windows {#parent}

```js
tiny.win.open('about', { page: 'about.html', size: '360x420', parent: true });
```

`parent` keeps the window above another window of yours — `true` for the main window, or a window id — without floating over other apps the way [`setLevel('floating')`](./window-fx#level) does. The child minimizes and hides with its parent and closes when the parent closes. On Windows and Linux it gets no taskbar button of its own; on macOS it also moves when the parent moves. It can only be set when opening the window.

## without a main window {#no-main}

With [`"mainWindow": false`](./config#mainWindow) the app starts with no window at all — a [tray](./tray) app or a background agent — and the backend opens windows by name when it needs them. There's no `'main'`: `win.open('main')` rejects, so use another id. Closing the last window leaves the backend and tray running.

```js
// backend
export function onTray(id, app) {
  if (id === 'prefs') app.win.open('prefs', { page: 'prefs.html', size: '480x360' });
}
```

## events between windows {#events}

Each window's page is separate, with its own JavaScript and DOM, so windows talk through the backend or through events:

- `app.push(name, data)` in the backend reaches every window; `app.window(id).push(name, data)` reaches one, and `app.win.push()` only the main window. Pages listen with `tiny.api.on(name, fn)`. See [events to the page](./backend#push).
- Your `api` functions get the caller's window id as `meta.window`, so the backend knows which window asked:

```js
// backend
export const api = {
  pick: async ({ color }, app, { window }) => {
    app.window('main').push('color-picked', { color, from: window });
  },
};
```

- [`tiny.win.onState`](./window#onState), file drops ([`onDrop`](./window#files)) and `'window-closed'` reach every window, so check the window id in the event where it matters.

## from the backend {#backend}

```js
app.win.open('settings', { page: 'settings.html' });
const settings = app.window('settings');
settings.setTitle('Settings — Sync');
settings.setPosition(100, 100);
await settings.getState();
settings.close();
```

`app.window(id)` returns a handle with the same window calls as `tiny.win` — `setSize`, `setChrome`, `show`, `getState()` and the rest — plus `push(name, data)` and `eval(js)`. Calls on a handle whose window has closed do nothing. Compared with `app.win` (the main window), a handle has no `open`, `windows` or `setHideOnClose`, and its `close()` takes no id. A few calls are page-only: `onState`, `onDrop`, `find`, `stopFind`, and dragging files out.

## platform differences {#platforms}

- **Full screen** on Windows applies to the main window only; other windows are maximized instead.
- **Click-through** (`setClickThrough`) on Windows applies to the main window only.
- **`setHideOnClose`** acts on the main window's close button, whichever page calls it. A secondary window's close button always closes it.
- On macOS, `hide()` from the main window hides the whole app; from another window it hides just that window. See [showing and hiding](./window#visibility).
