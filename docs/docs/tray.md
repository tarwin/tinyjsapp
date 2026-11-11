---
title: "tray"
---

# tray

<p class="tagline">tiny.tray — an icon in the menu bar or notification area</p>

`tiny.tray` puts one icon in the macOS menu bar, the Windows notification area or the Linux system tray, with an optional menu. Use it for apps that live in the background — a timer, a clipboard history, a status indicator — often paired with a hidden window that the icon brings up.

```js
await tiny.tray.set({
  icon: 'sf:cup.and.saucer.fill',
  tooltip: 'Brew',
  menu: [
    { id: 'show', label: 'Show Brew' },
    { separator: true },
    { id: 'quit', label: 'Quit' },
  ],
});

tiny.tray.on((id) => {
  if (id === 'show') tiny.win.show();
  if (id === 'quit') tiny.quit();
});
```

The backend has the same calls on `app.tray` — `app.tray.set()`, `app.tray.remove()`, `app.tray.position()` — and gets clicks through the [`onTray(id, app)` hook](./backend#hooks), where `id` is the menu item's id, or `null` for a click on the icon itself. A tray app often has no page open, so the backend is the usual place for it.

## showing an icon {#set}

```js
await tiny.tray.set(spec)
await tiny.tray.remove()
```

`set` shows the icon, or replaces everything about it if it's already there. To change anything — the title, the icon, the menu — call `set` again with the whole spec. `remove` takes the icon away.

| field | | |
|---|---|---|
| `icon` | string | a png path, `'sf:<name>'` or `'emoji:<glyph>'` — see [icons](#icons) |
| `title` | string | text beside the icon on macOS, and on Linux where the desktop shows it; Windows uses it as the tooltip when there's no `tooltip` |
| `titleColor` | string \| object | macOS: the title's colour — see [title text](#title) |
| `titleFont` | string | macOS: `'monospacedDigit'` or `'monospaced'` — see [title text](#title) |
| `tooltip` | string | text shown on hover |
| `template` | boolean | macOS: `false` keeps the png's colours instead of adapting it to the menu bar; default `true` |
| `menu` | item[] | the menu, in the same [item shape](./menus#items) as the menu bar |
| `primaryAction` | boolean | `true`: a left click is a [click event](#clicks) and the menu opens on right-click |

Menu items take `id`, `label`, `checked`, `enabled`, `icon`, `submenu` and `separator`. Stock editing items (`{ role: 'copy' }`) are skipped — there's nothing to edit in a tray menu.

An item's [`icon`](./menus#icons) shows in the tray menu on macOS and Windows; Linux tray menus drop it and show the label. A menu item's `'emoji:<glyph>'` is drawn in colour on every platform, unlike the tray's own `icon` below.

## icons {#icons}

| `icon` | macOS | Windows | Linux |
|---|---|---|---|
| png path | ✓ scaled to the menu-bar height, aspect kept | ✓ | ✓ absolute path |
| `'sf:<name>'` | an SF Symbol, such as `'sf:timer'` | generic app icon | the app's icon, or a generic one |
| `'emoji:<glyph>'` | title only | the glyph as a monochrome silhouette, white or black to suit the taskbar | the app's icon, or a generic one |
| none | title only | generic app icon | the app's icon, or a generic one |

On macOS a png is a template image by default: macOS uses only its shape and colours it for a light or dark menu bar, the way system icons look. Draw it black on transparent, or set `template: false` to keep its own colours. An unknown SF Symbol name shows the title alone.

There's no single icon string that's asset-free everywhere, so branch on the OS:

```js
const os = tiny.system.os();
const icon = os === 'macos' ? 'sf:timer'
           : os === 'windows' ? 'emoji:⏱'
           : (await tiny.app.paths()).resources + '/tray.png';
await tiny.tray.set({ icon, tooltip: 'Timer', menu });
```

Pass an absolute path for a png — build it from [`paths().resources`](./resources) or `paths().data`. A relative path is resolved against the backend's working directory, which is your project folder in `tinyjs dev` but not dependably anything in a built app. Linux uses the path only if the file exists, and falls back to the app's icon otherwise.

To show changing content — a countdown, a level meter — render a png yourself, write it to `paths().data`, and call `set` with the new path. `set` redraws the icon each time, so call it only when something changed.

## title text {#title}

On macOS the title follows the menu bar's colour and font by default. Two fields change that:

- `titleColor` sets its colour: `'#rrggbb'`, `'#rrggbbaa'` or `'#rgb'`. A single colour can be hard to read on one of the menu bar's two looks, so you can pass one for each instead: `{ light: '#b00020', dark: '#ff8a80' }`. The menu bar's look follows the wallpaper behind it as well as Dark Mode, and the title switches with it.
- `titleFont: 'monospacedDigit'` gives every digit the same width. Without it a title that counts — a timer, a download percentage — shifts sideways as it changes, because `1` is narrower than `8`. `'monospaced'` makes the whole title monospace.

```js
await tiny.tray.set({
  icon: 'sf:timer',
  title: '24:59',
  titleFont: 'monospacedDigit',
  titleColor: { light: '#b00020', dark: '#ff8a80' },
});
```

Only the text changes; a template icon still takes the system's colour. Windows shows no text beside a tray icon, and on Linux the desktop draws the title, so both ignore these fields. To colour a tray icon on every OS, draw the text into a png and set `template: false`.

## clicks {#clicks}

```js
tiny.tray.on((id) => { … })      // a menu item was chosen
tiny.tray.onClick(() => { … })   // the icon itself was clicked
```

Both return an unsubscribe function. In the backend, both arrive at `onTray`: `onTray('show', app)` for an item, `onTray(null, app)` for the icon.

```js
// backend
export function onTray(id, app) {
  if (id === null || id === 'show') app.win.show();
  if (id === 'quit') app.quit();
}
```

What a click on the icon does depends on whether there's a menu and on `primaryAction`:

| | macOS | Windows | Linux |
|---|---|---|---|
| menu | any click opens the menu | left or right click opens the menu | a click opens the menu |
| no menu | left click → `onClick` | left or right click → `onClick` | a click opens a one-item menu; choosing it → `onClick` |
| menu + `primaryAction: true` | left click → `onClick`; right-click or Ctrl-click opens the menu | left click → `onClick`; right-click opens the menu | the menu, with an extra first item → `onClick` |

Linux tray icons (StatusNotifierItem) can't report a bare click — the desktop always opens a menu. So where you'd get a click on the other OSes, tinyjs adds a first menu item labelled with the `title`, else the `tooltip`, else "Open", and choosing it fires `onClick`.

The click events reach every open window's listeners, as well as the backend. If several windows subscribe, each one runs.

## changing menu items {#update}

The tray menu's items are reachable with [`tiny.menu.update()` and `tiny.menu.get()`](./menus#update), so you can tick an item or change its label without calling `set` again:

```js
await tiny.menu.update('capturing', { checked: false });
```

`update` changes every item with that id, menu bar included — give tray items ids of their own.

## where the icon is {#position}

```js
await tiny.tray.position()   // → { x, y, width, height } | null
```

The icon's rectangle on screen, in the same top-left coordinates as [`tiny.win.setPosition`](./window), for opening a dropdown window under it. It resolves `null` when there's no tray icon — and always on Linux, where the tray doesn't expose where its icons are.

```js
const r = await tiny.tray.position();
if (r) await tiny.win.setPosition(Math.round(r.x + r.width / 2 - 160), r.y + r.height);
```

## menu-bar apps {#menubar-app}

An app that lives in the tray usually has no Dock icon or taskbar button, and a window that comes and goes. Set [`"activation": "accessory"`](./config#activation) in tinyjs.json: the app launches with no Dock icon (no taskbar button on Windows and Linux), and the main window starts hidden, so it doesn't flash on screen.

```json
{ "activation": "accessory" }
```

Then make the close button hide the window rather than quit, and show it from the tray:

```js
await tiny.win.setHideOnClose(true);
tiny.tray.onClick(() => tiny.win.show());
```

`tiny.app.presence('menubar')` and `presence('normal')` switch the Dock icon or taskbar button off and on while the app runs; on Linux that works in X11 sessions only.

On Windows and Linux, hide-on-close holds only while something can bring the window back — the tray icon, accessory mode, or another open window — otherwise closing the last window quits, as it does in other apps there. With [`"mainWindow": false`](./config#mainWindow) the app starts with no window at all, and opens [windows](./multiple-windows) when it needs them.

`tinyjs wrap --menubar` sets a [site wrapper](./cli#wrap) up this way.

## Linux {#linux}

The tray needs a StatusNotifier (AppIndicator) host. KDE Plasma and most other desktops have one built in; GNOME needs the AppIndicator shell extension. Without one, `set` still succeeds, but the icon may not show. Check for it with [`tiny.system.requirements`](./portability):

```js
const [tray] = await tiny.system.requirements(['tray']);
if (!tray.ok) showHint(tray.detail, tray.install?.command);
```

Whether the `title` text shows next to the icon is up to the desktop.
