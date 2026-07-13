# Changelog

All notable changes to tinyjs. Versions are git tags (`vX.Y.Z`); a tag push
builds and publishes the release. The rendered version of this file lives at
https://tinyjs.app/changelog.

## 0.3.1 — 2026-07-13

- **Global hotkeys** — `tiny.hotkey.register(id, 'cmd+shift+k')` /
  `.unregister(id)` / `.on(fn)`; fire system-wide, even while other apps are
  focused (backend: `app.hotkey.*`, `onHotkey` export).
- **System events** — `tiny.theme.get()` / `tiny.theme.on(fn)` for dark/light
  (initial state included), plus `sleep` / `wake` events (`onSystem` export).
- **`tiny.store`** — persistent JSON settings in
  `~/Library/Application Support/<bundle id>/store.json`:
  `get` / `set` / `delete` / `all`, atomic writes.
- **Custom context menus** — `tiny.menu.setContext(items)` replaces the
  right-click menu with native items (`null` restores WebKit's default);
  clicks via `tiny.menu.onContext(fn)` / `onContextMenu` export.
- **`tiny.win.print()`** — native print panel for the page.
- **`tinyjs build --dmg`** — also emit `dist/<name>-<version>.dmg` (the .app
  plus an /Applications shortcut).
- **`tinyjs notarize`** — submit the built .app via `notarytool` and staple
  the ticket (needs a Developer ID `signIdentity` and a keychain profile).
- SQLite documented: txiki ships `tjs:sqlite` (`Database`) — examples in the
  README and agent skill.
- The launcher now links Carbon (for hotkeys); `setup.sh` and the release
  workflow updated accordingly.

## 0.3.0 — 2026-07-13

- **Tray / menu-bar apps** — `tiny.tray.set({ title, icon, tooltip, menu })`,
  click events, `tiny.win.setHideOnClose(true)`, and
  `tiny.app.setDockVisible(false)` for menu-bar-only apps.
- **Multi-file frontends** — the inliner is gone; `src/frontend/` ships as
  real files and relative js/css/images just work. Hot reload bypasses
  WebKit's caches (`reloadFromOrigin`), so edited subresources show up.
- **Auto-update for your apps** — `"update": { "url": … }` in tinyjs.json plus
  `tinyjs publish` (zip + manifest with sha256). `update.check()` /
  `update.install()` download, verify checksum and code signature, swap the
  .app with rollback, and relaunch. Hardened: https-only manifest/download
  (localhost exempt for testing), mandatory sha256, Team ID pinning when
  signed with a real identity, translocation detection.
- **Notifications** — `tiny.notify(title, body)`.
- **Drag & drop with real paths** — `tiny.win.onDrop((paths) => …)`; file
  drags are accepted even if the page doesn't handle HTML5 DnD.
- **Window controls** — `center`, `minimize`, `fullscreen`, `setPosition`
  (top-left origin), `setAlwaysOnTop`, `setResizable`, `hide`/`show`.
- Installer: prefers a writable bin dir already on PATH (`/usr/local/bin`,
  then `/opt/homebrew/bin`), offers to add `~/.local/bin` to your shell
  profile otherwise, and the "not on PATH" warning is now unmissable.

## 0.2.1 — 2026-07-13

- **WebGPU** — pages load as `file://` documents (secure context, so
  `navigator.gpu` exists) and the launcher enables the WebKit feature flag on
  macOS 15 and earlier. `requestAdapter()` / `requestDevice()` verified.
- **CLI self-update** — `tinyjs update` (with `--check`), plus a once-a-day
  update notice in `tinyjs dev`.

## 0.2.0 — 2026-07-12

- **`tiny.*` frontend namespace** — `tiny.api.call/on`, `tiny.win.*`,
  `tiny.menu.*`, `tiny.log`, `tiny.quit` via the `tiny.js` shim.
- **Native menus** — default app menu (About with app name/version + Quit)
  and Edit menu (copy/paste shortcuts); custom menus via `tiny.menu.set`.
- **System dialogs** — `alert` / `confirm` / `prompt` (NSAlert) alongside the
  file panels.
- **Agent skill** — `tinyjs new` scaffolds `.claude/skills/tinyjs/SKILL.md`
  so coding agents know the API.
- tinyjs.app landing page + `curl tinyjs.app/install | sh` installer.
- (v0.1.1 was superseded by this release the same day.)

## 0.1.0 — 2026-07-12

- Initial release: txiki.js backend + native WebKit launcher over a private
  Unix-domain socket (no HTTP server, no ports), `tinyjs new/dev/build`,
  hot reload, native file dialogs, codesigned `.app` bundles (~6 MB shipped)
  via the stock-runtime + shim layout, tag-triggered release workflow.
