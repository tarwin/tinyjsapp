# Windows port — remaining work

Tracking list for finishing the Windows port (branch `feat/windows`). The
core and most of the original list are done and verified — see the
Portability section of the README for the current support matrix. Wire op
names refer to the protocol table in the README; Windows handlers live in
`native/launcher-win.cc`.

## Done

- [x] Core port: launcher (WebView2 + named pipe), bridge, CLI, dev/build,
      dialogs, menus, tray, clipboard, hotkeys, keystroke, shell, secrets,
      power, theme/sleep/wake, window ops, custom context menus.
- [x] **Release CI + installer** — `windows-latest` job builds
      `tinyjs-windows-x86_64.zip`; `irm tinyjs.app/install.ps1 | iex`
      installs to `%LOCALAPPDATA%\tinyjs` + user PATH; `tinyjs update`
      re-runs it. **Goes live with the first tagged release after merge.**
- [x] **Multi-window** — secondary windows host their own WebView2 controller
      from the main environment; `<winid>:<seq>` call ids, targeted
      `CMD@<id>` routing, `WINCLOSED` events. (`acceptsFirstMouse`/`traffic`
      fields are no-ops; secondary fullscreen ≈ maximize.)
- [x] **Drop files IN with real paths** — `AllowExternalDrop(FALSE)` +
      `IDropTarget` on the host window → `DROP <json-paths>`.
- [x] **Drag files OUT** — `SHCreateDataObject` + `DoDragDrop` (`CF_HDROP`);
      custom drag-image png not implemented (file icons show).
- [x] **printToPDF** (`ICoreWebView2_7::PrintToPdf`).
- [x] **captureScreen** (GDI BitBlt → png) and **thumbnail**
      (`IShellItemImageFactory`).
- [x] **say / voices / stopSpeaking** (SAPI; `rate` 0..1 maps to SAPI
      -10..10, voice ids are SAPI token ids).
- [x] **Clipboard image write** (png path/base64/data-URL → `CF_DIB`);
      `color` writes as text (no native format).
- [x] **Menu accelerators** — `key:` combos fire via WebView2
      `AcceleratorKeyPressed` (Ctrl+<key>).
- [x] **launchAtLogin** — HKCU Run key for built apps (dev → 'unsupported');
      the bridge passes the app exe path on the LOGIN wire op.
- [x] **`tinyjs publish` + app auto-update** — zips `dist/` (bsdtar),
      WebCrypto sha256 manifest (no shasum spawn, both OSes); update swaps
      **file-by-file** (Windows can't rename a dir holding a running exe —
      locked exes are parked as `*.update-old` and swept on the next update)
      and relaunches. Verified end-to-end: 1.0.0 → 1.0.1 self-update.
- [x] **Chrome transparent + backdrop** — `DefaultBackgroundColor` +
      DWM system backdrops (vibrancy 'hud'/'popover'/'menu' → acrylic, other
      materials → mica; Win11 22H2+, silent no-op earlier).
- [x] **Accessory mode polish** — `WS_EX_TOOLWINDOW` (no taskbar button);
      `setDockVisible` toggles the taskbar button.

## Still open

- [ ] **Toast notifications + actions** (`NOTIFY` actions,
      `NOTIFYACTION`) — currently a tray balloon without buttons/reply.
      Proper WinRT toasts need an AppUserModelID + Start-Menu shortcut for
      unpackaged apps and COM activation for click routing. Sizeable.
- [ ] **App icon + version resources in built exes** — `dist/<name>.exe`
      ships the default icon. Generate an .ico from icon.png and embed via
      `UpdateResource` (RT_GROUP_ICON/RT_ICON) at build time.
- [ ] **Deep links / file associations / single instance** — registry
      (`HKCU\Software\Classes`) written at install/first run; single
      instance via named mutex + argv forwarding to the running pipe
      (`OPENURL`/`OPENFILES` lines already exist in the bridge).
- [ ] **authenticate** — Windows Hello (`UserConsentVerifier`, WinRT);
      returns false today.
- [ ] **audioTap** — WASAPI loopback (scope:'system'); per-process capture
      needs the Win10 2004+ process-loopback path. Sizeable.
- [ ] **Custom context-menu suppression fallback** — on WebView2 runtimes
      older than `ICoreWebView2_11`, `contextMenu: false` is a no-op; could
      fall back to `AreDefaultContextMenusEnabled`.
- [ ] **Windows CI GUI smoke** — the release job smoke-tests the build
      pipeline; running the actual window (`TINYJS_HTML=test/smoke.html`) on
      a runner would cover the launcher too.

## Not planned / no OS equivalent

`quickLook`, `ocr` (Windows.Media.Ocr someday), `applescript`, `haptic`,
`dock.setBadge`/`dockIcon` (could map to `ITaskbarList3` overlays),
`setAllSpaces`, `spotlight`, `tiny.app.ai`, `wifi`, `selectedText` /
`otherWindows` / `moveWindow` (UIA could do it — revisit if asked),
`share`, `nowPlaying`/media keys (SMTC via WinRT — revisit if asked). All
reject or report `'unsupported'` so apps can feature-detect.
