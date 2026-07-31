# Coming from Electron

tinyjs apps ship at ~6 MB where Electron ships ~200 MB, start instantly, and
use the OS's own webview + native menus/dialogs. The price: the backend is
NOT Node, and the renderer is NOT your pinned Chromium. Port the
architecture, not the code.

## The two mindset shifts (read these first)

1. **The backend is txiki.js on QuickJS, not Node.** No `require`, no Node
   builtins (`fs`, `path`, `crypto`, `child_process`…), no npm native
   modules, no node-gyp, ever. The runtime surface is `tjs.*`
   (readFile/writeFile/readDir/stat/spawn/watch/listen/connect), standard
   `fetch`/`WebSocket`, `tjs:sqlite`, and FFI for C libraries. Pure-JS npm
   packages CAN come along — declare `backend: "backend/main.ts"` and
   esbuild bundles them — but anything importing a Node builtin dies at
   bundle or run time. And QuickJS has no JIT: compute belongs in the page
   (see references/performance.md).
2. **The renderer is the OS webview** — WKWebView (macOS), WebView2/Chromium
   (Windows), WebKitGTK (Linux). You don't ship a browser, so you don't pick
   the browser: feature-detect. Codec support differs (Linux may lack
   codecs until GStreamer plugins are installed; WebKitGTK has no native
   HLS — vendor hls.js — and no WebGPU). CSS/JS you'd feature-gate for
   Safari applies to two of the three platforms.

## API mapping

| Electron | tinyjs |
|---|---|
| `new BrowserWindow(opts)` | `tinyjs.json` (main window) / `tiny.win.open(id, { page, size, chrome, x, y })` — chrome/x/y in `open()` apply before first paint |
| `ipcMain.handle` + `ipcRenderer.invoke` | `export const api = { method: async (params, app) => … }` + `tiny.api.call('method', params)` |
| `webContents.send` → renderer listener | `app.push('event', data)` + `tiny.api.on('event', fn)` |
| preload script / `contextBridge` | not needed — `tiny` is injected into every page automatically |
| `Tray` | `tiny.tray.set({ title, icon, menu })` (`'sf:<name>'` icons on macOS, `'emoji:<glyph>'` on Windows) |
| `Menu.setApplicationMenu` | `tiny.menu.set(spec)` — shows in EVERY window on all three OSes; `tiny.win.menu.*` for per-window |
| `globalShortcut` | `tiny.hotkey.register(id, 'cmd+shift+k')` (`cmd` = Ctrl on win/linux) |
| `dialog.showOpenDialog` etc. | `tiny.dialog.openFile/openFiles/pickFolder/saveFile/alert/confirm/prompt` |
| `shell.openExternal/showItemInFolder/trashItem` | `tiny.app.shell.open/reveal/trash` |
| `app.getPath(name)` | `tiny.app.paths()` / backend `app.paths` (plain object) |
| `safeStorage` / keytar | `tiny.app.secrets.get/set/delete` (Keychain / Credential Manager / Secret Service) |
| `autoUpdater` (+ electron-updater) | `"update": { url, auto }` in tinyjs.json + `tinyjs publish`; `update.check()` / `update.install()` |
| `nativeTheme` | `tiny.theme.get()/on()` |
| `screen.getAllDisplays` | `tiny.app.screens()` |
| `clipboard` | `tiny.clipboard.read/write/watch` (richer: sourceApp, concealed marker, files) |
| `new Notification` | `tiny.notify(title, body, { actions })` |
| `powerSaveBlocker` | `tiny.app.power.preventSleep(reason)` (auto-released on exit) |
| `app.setLoginItemSettings` | `tiny.app.launchAtLogin.set(v)` |
| `app.setBadgeCount` | `tiny.app.badge('3')` |
| `app.requestSingleInstanceLock` | automatic — second launch forwards its argv/deep-link to the running copy |
| `app.setAsDefaultProtocolClient` | `"urlScheme"` in tinyjs.json + `onOpenUrl` |
| file associations (builder config) | `"fileExtensions"` + `onOpenFiles`; `"openFolders": true` |
| `webContents.executeJavaScript` | backend `app.window(id).eval(js)` |
| `webContents.printToPDF` | `tiny.win.printToPDF(path)` (routes to the calling window) |
| `desktopCapturer` | `tiny.app.captureScreen(screenId)` / `tiny.macos.recorder` (macOS) |
| `BrowserWindow` flags: frame, transparent, alwaysOnTop, ignoreMouseEvents | `chrome: { frame, transparent }`, `setAlwaysOnTop`, `setClickThrough`, `setLevel('desktop'|'floating'|'overlay')` |
| `session.webRequest` / CORS dances | `tiny.fetch(url, opts)` — backend-proxied, no CORS/CSP; `{ stream: true }` for endless bodies; `tiny.proxyURL(url)` to get cross-origin streams into Web Audio untainted |

## No equivalent — design around

- **`nodeIntegration` in the renderer**: never. The page has zero system
  access; every privileged op crosses `tiny.api`. Consequence you must
  honor: escape anything interpolated into `innerHTML` — the page holds an
  RPC channel to full system access.
- **`BrowserView` / `<webview>` embedding**: none. Use an iframe (same-origin
  or friendly content) or a second window.
- **Chromium devtools protocol, `webContents` surgery, custom protocol
  handlers, service-worker interception**: none. `tiny.fetch`/`proxyURL`
  cover the fetch-shaped cases.
- **Buffers over IPC**: the bridge is text. Pass file paths (see
  references/performance.md).
- **Per-window process control, `process.memoryUsage()` of renderers**: the
  webview owns its processes.
- **Node crypto in backend**: WebCrypto (`crypto.subtle`) exists in txiki;
  for the page it's all there anyway.
- **Windows-only Electron niceties** (thumbar buttons, jump lists): not yet;
  `app.progress()` and `app.badge()` cover the taskbar basics.

## Porting order that works

1. Move `main.js` IPC handlers to `export const api = {}` methods 1:1;
   replace `fs`/`path` calls with `tjs.*` (join with '/', it works
   everywhere; use `app.paths` instead of `app.getPath`).
2. Point tinyjs at your existing renderer build — `"frontend": { build,
   dist, dev, devUrl }` runs your Vite/webpack dev server inside the native
   window with HMR.
3. Replace `ipcRenderer.invoke` with `tiny.api.call` (a 5-line wrapper keeps
   the call sites identical), preload goes away.
4. Re-express windows: main window in tinyjs.json, satellites as
   `win.open(...)` with chrome in the open call.
5. Feature-detect everything Chromium-specific in the renderer; test on
   WebKit early — UA-sniffing sites and `Version/x Safari/x` quirks are the
   usual surprises (`"userAgent"` in tinyjs.json when wrapping a site).
6. Wire `"update"` + `tinyjs publish` and delete your updater code.
