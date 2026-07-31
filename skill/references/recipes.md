# Recipes — proven shapes for common app kinds

Each of these is distilled from a shipped example app. Steal the shape;
the gotchas listed are the ones that actually bit.

## Tray / menu-bar app

```jsonc
// tinyjs.json
{ "activation": "accessory" }   // launches with no Dock icon, window hidden — no flash
```
```js
export function init(app) {
  app.tray.set({ title: '◎', menu: [{ id: 'show', label: 'Open' }, { separator: true }, { id: 'quit', label: 'Quit' }] });
  app.window('main').setHideOnClose(true);
}
export function onTray(id, app) {
  if (id === 'show') app.window('main').show();
  if (id === 'quit') app.quit();
}
```
- `tray.position()` anchors a dropdown window under the icon (null on Linux
  — AppIndicator hides geometry; center the window instead).
- Icons without assets: `'sf:<symbol>'` on macOS, `'emoji:<glyph>'` on
  Windows (drawn as a mono silhouette) — branch per OS.
- On win/linux `setHideOnClose` only holds while something can bring the app
  back (tray icon counts) — with a tray you're fine.
- `primaryAction: true` = left-click fires `onClick`, menu on right-click.

## Desktop pet / overlay / HUD

The coo3d/kraa3d shape: one backend brain ticking at ~25fps, many frameless
transparent windows, everything driven by `app.window(id).*`.

- **Pass `chrome` (and `x`, `y`) IN `win.open` itself** — applied before
  first paint. Calling `setChrome` from the new window's boot is too late: a
  frameless transparent pet flashes in as a white default window.
- `setClickThrough(true)` for anything that must never trap the mouse;
  `setLevel('desktop')` puts windows behind everything (wallpaper pets),
  `'overlay'` above fullscreen apps. `show({ activate: false })` surfaces a
  HUD without stealing focus.
- Occluded/hidden windows are THROTTLED (rAF stops) — run the simulation in
  the backend (never throttled) and push per-tick state; windows just render.
- Pool windows: hide/move/reuse instead of create/destroy in a loop (the
  ninth poop splat recycles the oldest window, nobody blinks).
- Pseudo-depth: re-`show()` windows back-to-front as sprites cross.
- Frameless windows on Linux get WM resize grips — declare `minSize` on
  satellites or content gets resized out of view.
- `data-tiny-drag` on a header = native window drag; `acceptsFirstMouse:
  true` lets the focusing click also reach the page (palettes).

## Media / audio app

Decision table first — audio routing is the most platform-divergent area:

| need | use |
|---|---|
| play music / long streams | `<audio>` element (streams from disk/net; never decode whole songs) |
| game/UI sound effects, pitch/pan per shot | `tiny.audio.sampler` (native mixer on Linux, Web Audio host elsewhere — same API) |
| EQ / DSP on the app's whole output | `tiny.audio.filters` (native: Linux + macOS 14.2+); Windows: `capabilities().audioFilters === false` → `tiny.audio.pageChain(ctx)` |
| VU meter / visualizer of what's playing | `tiny.audioTap` (PCM chunks at meter rate; post-filter where a chain is active) |
| cross-origin stream INTO Web Audio | `audio.crossOrigin='anonymous'; audio.src = tiny.proxyURL(url)` (macOS) |

- **Linux hard rule: no Web Audio graph into `ctx.destination`** — WebKitGTK
  renders the graph on a normal-priority thread and it crackles on an idle
  machine at any latencyHint. Play elements directly; SFX through the
  sampler (that's why it exists); EQ through the native filters. Never
  `pageChain` on Linux.
- Linux double-play: an element routed into a Web Audio graph ALSO plays
  natively on WebKitGTK — set the element's own volume to 0 on Linux if you
  must route one.
- WebKitGTK has no native HLS (vendor hls.js) and codec support depends on
  the user's GStreamer plugins — `tiny.system.requirements(['codecs'])` can
  prompt what to install.
- Chromium taps `MediaElementSource` post-volume, WebKit pre — don't use the
  element volume as a mixer knob if you analyze the tap.

## Document app

```jsonc
{ "fileExtensions": ["md", "txt"], "openFolders": true, "readAccess": true }
```
- Files arrive via `onOpenFiles(paths, app)` from every route: double-click,
  drag onto the icon, `open -a`, argv (`myapp notes.md`), and a second
  launch forwards to the running instance. Buffer-safe on cold start.
- `readAccess` widens what `file://` media/img tags may load (default: only
  under the frontend dir); `tiny.fileURL(path)` ALWAYS (never `'file://' +
  path` — breaks on Windows drive letters).
- `tiny.win.onDrop` gives real paths for files dropped onto the window;
  `startDrag({ files })` drags them out (call during mousedown).
- Claiming default-handler status: `app.setAsDefaultHandler(ext)` when the
  user asks — never on first run (competing with their editor uninvited is
  how apps get uninstalled).
- Multi-window documents: `win.open` per doc, `api` handlers get
  `meta.window` to know which window called; `win.print()`/`printToPDF`
  route to the calling window.

## Wrapping a hosted web app

```jsonc
{ "frontend": { "devUrl": "https://app.example.com" },
  "userAgent": "Mozilla/5.0 … Version/17.4 Safari/605.1.15" }
```
- The default webview UA lacks `Version/x Safari/x`; UA-sniffing sites
  reject it — set a real one. Some SaaS apps feature-detect embedded
  webviews and refuse regardless; nothing to do about that.
- `tiny` is injected into ANY origin, so `tiny.*` (menus, tray, notify,
  hotkeys) works over https too.
- Cookies/logins persist in the webview's default store per app id.

## Windowless / agent app

`{ "activation": "accessory" }` — the main window EXISTS (it always does;
it can be hidden but never closed) and quietly hosts anything page-side the
app needs (the sampler's Web Audio host on mac/win lives there). Backend
does the work; `tiny.win.show()` only when there's something to show.
Hidden windows cost ~nothing: not composited (no GPU), audio render threads
unaffected by visibility.

## Self-driving test page (headless-ish verification)

`TINYJS_HTML=/abs/path/page.html tinyjs dev` from any app dir points the
main window at a test page. The page waits for `tiny` (~poll 100ms), drives
the API, writes results with `tiny.store.set('results', {...})`; read them
from the app id's store.json in the platform data dir
(`~/Library/Application Support/<id>/` on macOS, `~/.local/share/<id>/` on
Linux). Reload-survival tests: write a phase marker to the store, call
`location.reload()`, branch on the marker at boot.
