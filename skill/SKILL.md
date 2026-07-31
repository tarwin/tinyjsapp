---
name: tinyjs
description: Build and modify tinyjs desktop apps — tiny macOS (and beta Windows/Linux) apps with a txiki.js JavaScript backend and a native webview window. Use when working in a project with a tinyjs.json, when the user mentions tinyjs, tiny.api, or tinyjs dev/build, or when porting an Electron app to tinyjs.
---

# Building tinyjs apps

tinyjs (https://tinyjs.app, repo tarwin/tinyjsapp) makes ~6 MB desktop apps:
a **txiki.js backend** (full system access: files, sockets, processes, FFI)
+ a **native webview window** — WKWebView on macOS, WebView2 on Windows,
WebKitGTK 4.1 on Linux (both beta). They talk over a private socket — no
HTTP server, no ports. The page NEVER has system access; everything
privileged crosses `tiny.api`, which is why anything interpolated into
`innerHTML` must be escaped.

Current release: 0.34.0. App floors: macOS 14+ (universal), Windows 10/11
(WebView2), Linux glibc 2.35+ (Ubuntu 22.04 / Debian 12 / Mint 21 and up).

## Read the reference for the job at hand

| doing | read |
|---|---|
| any API beyond the basics below (windows, menus, tray, clipboard, audio, permissions, system…) | references/api.md |
| per-OS support, Windows/Linux quirks, capability gating | references/platforms.md |
| starting a tray app, desktop pet/overlay, media app, document app, site wrapper, agent app | references/recipes.md |
| shipping: build/sign/notarize, per-OS packaging, auto-update manifests | references/release.md |
| porting from Electron (API map, what won't work) | references/electron-migration.md |
| deciding where code runs, wire/binary payloads, throttling, memory | references/performance.md |

## Commands

```sh
tinyjs new <dir>    # scaffold (zero dependencies)
tinyjs new <dir> --template react-ts|vue-ts|svelte-ts|solid-ts|vanilla-ts|…
                    #   create-vite + tinyjs overlay: HMR dev server in the
                    #   native window, esbuild-bundled TS backend (npm pkgs ok)
tinyjs dev          # run with hot reload (frontend edits swap in place;
                    #   backend edits restart the process)
tinyjs build        # dist/<name> binary + dist/<Name>.app (codesigned)
                    #   --dmg installer image; --cli [name] terminal shim
tinyjs publish      # build + dist/publish/<name>-<ver>.zip|tarball + manifest
tinyjs notarize     # macOS: notarytool submit + staple (--dmg re-makes dmg)
tinyjs update       # update tinyjs itself (--check); also: uninstall, version
TINYJS_DEBUG=1 tinyjs dev   # trace every bridge message
```

## Project layout

```
tinyjs.json          { name, title, size, id, version, icon?,
                       minTinyjsVersion?,     // refuse older runtimes with a
                                              // real message — declare the
                                              // newest API family you use
                                              // (sampler → "0.34.0")
                       update?: { url: "https://…/manifest.json", auto? },
                       urlScheme?: "myapp", fileExtensions?: ["md"],
                       openFolders?: true, readAccess?: true | "/path",
                       userAgent?: "…", activation?: "accessory",
                       offscreenRescue?: false, windowPlacement?: true,
                       contextMenu?: false, audioTap?: "app" | "system",
                       permissions?: { microphone?: "why", camera?: "why",
                                       speechRecognition?: "why" },
                       chrome?: { frame, windowControls, windowControlsPos,
                                  transparent, vibrancy, squareCorners,
                                  acceptsFirstMouse, menu },
                       signIdentity?, notarize?: { profile },
                       backend?: "backend/main.ts",   // .ts → esbuild bundle
                       frontend?: { build, dist, dev, devUrl },
                       macos?/windows?/linux?: { …merged on top per OS } }
icon.png             1024×1024 app icon
src/main.js          backend
src/frontend/        index.html + assets — served as real files (file://),
                     so relative paths just work
```

## Backend (src/main.js)

```js
export const api = {
  // page calls tiny.api.call('readNotes', { dir }) — return resolves the
  // page's promise, throwing rejects it; meta.window = calling window id
  readNotes: async ({ dir }, app, meta) => { ... },
};
export function init(app) {
  app.push('event-name', data);            // page: tiny.api.on('event-name')
  // app mirrors the page surface: app.window(id).*, app.openWindow,
  // app.tray.*, app.audio.sampler.*, app.clipboard.*, app.store.*,
  // app.paths (plain object), notify, quit, …  — full tour: references/api.md
}
// other exports the scaffold wires: onMenu, onTray, onContextMenu, onHotkey,
// onSystem, onWindowState, onWindowClosed, onOpenUrl, onOpenFiles,
// onMediaKey, onNotificationClick, onNotificationAction, onUpdateAvailable,
// onClipboardChange — each (info, app)
```

Runtime is txiki.js (`tjs` global): `tjs.readFile/writeFile/readDir/stat`,
`tjs.spawn`, `tjs.watch`, `fetch`, `WebSocket`, `tjs:sqlite`, FFI. It is
NOT Node (no require, no Node builtins, no native npm modules) and it has
no JIT — compute-heavy work belongs in the page
(references/performance.md). Streams need `getReader()` (no `for await`);
`tjs.cwd` is a property; no Intl (format in the page).

## Frontend essentials

```js
await tiny.api.call('method', { params });    // -> backend api.<method>
const off = tiny.api.on('event', fn);         // <- app.push; returns unsubscribe
audio.src = tiny.fileURL(path);   // ALWAYS this, never 'file://' + path
                                  // (breaks on Windows drive letters)
const r = await tiny.fetch(url, opts);        // backend-proxied, no CORS/CSP
tiny.win.open('settings', { page: 'settings.html', size: '420x300',
                            chrome: { frame: false } });
// chrome/x/y/minSize in open() (or tinyjs.json "chrome" for main) apply
// BEFORE first paint — a late setChrome flashes the default window.
```

Everything else — windows/chrome/state events, menus (in every window),
dialogs, tray, notifications, clipboard, hotkeys, audio (sampler / filters /
audioTap / proxyURL), store/secrets, permissions, deep links, auto-update,
`tiny.macos.*` — is in references/api.md with signatures and gotchas.

## Cross-platform rules

- Gate features, don't fork code. `capabilities()` lists only the
  EXCEPTIONS: test `caps.x !== false`, never `if (caps.x)` (the truthy form
  reports "unsupported" on the OS that has it). Query calls resolve `null`
  where unsupported, capability calls reject with the reason,
  fire-and-forget ones no-op — but `tiny.macos.*` off macOS REJECTS.
  `caps.sampler` is a string ('native'|'page') — informational, don't
  branch.
- Use `app.paths` / `tiny.app.paths()` — never hardcode `~/Library` or
  `%APPDATA%`; join with '/' (works everywhere).
- `tiny.system.os()/isMacOS()/isWindows()/isLinux()` are synchronous;
  `architecture()` must be awaited (navigator lies on Apple Silicon).
- Missing system pieces (codecs, speech, tray…) →
  `tiny.system.promptMissing([ids])` puts the fix in front of the user.

## Rules of thumb

- Backend capabilities go in `api` methods; keep the frontend thin — but
  keep COMPUTE in the page and bytes off the wire (pass paths, not
  ArrayBuffers; references/performance.md).
- Escape anything interpolated into `innerHTML` — the page holds an RPC
  channel to full system access.
- Never declare a top-level `chrome` identifier in frontend code:
  `window.chrome` is a non-configurable global on WebView2, so a top-level
  `const chrome` is a PARSE-time SyntaxError that kills the whole script.
- Linux audio: never route Web Audio to `ctx.destination` (it crackles —
  measured, unfixable page-side). SFX → `tiny.audio.sampler`; EQ →
  `tiny.audio.filters`; details in references/platforms.md.
- Occluded/hidden windows are throttled (rAF stops) — continuous work lives
  in a visible window or the backend.
- Verify changes with the smoke pattern:
  `TINYJS_HTML=<tinyjs-install>/test/smoke.html tinyjs dev` — expect a
  `[web] SMOKE RESULTS {...}` line with no FAIL entries and a clean exit.
- `dist/<Name>.app` is the distributable; bare `dist/<name>` is local-only.
