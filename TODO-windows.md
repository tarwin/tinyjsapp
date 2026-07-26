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

- [ ] **scope:'app' audioTap** — system loopback shipped; per-process
      capture needs the Win10 2004+ process-loopback path.
- [~] **Taskbar pin used to pin launcher.exe when the app was opened from
      shelf** — reported 2026-07-25 (pin was dead on relaunch); opening the
      same app's exe directly and pinning worked. **Appears fixed** by shelf's
      `openApp` now launching through `explorer.exe` instead of spawning the app
      as its own child (that change was made for a different bug — closing shelf
      closed the app). Confirmed by hand the same day: open from shelf → pin →
      close → reopens correctly. One manual check, not a regression test.
      Worth understanding before trusting it, because the obvious explanation is
      wrong: the *window* properties were never the problem. Launched both ways,
      the windows carry byte-identical, correct values —
      `AppUserModelID=tinyjs.amp`, `RelaunchCommand="…\amp\amp.exe"`, icon and
      display name all pointing at the app exe (scratchpad `pincheck.cc` reads
      them off a live window; use `%ls`, since MinGW's narrow printf truncates a
      UTF-16 string to one visible char). What differs is at the **process**
      level: shelf has its own explicit AUMID, and an app spawned as its direct
      child gets associated with it, while one launched via explorer is a child
      of the shell instead. That association, not the window property store, is
      what the pin followed.
      Still open underneath: `create_start_menu_shortcut()` only runs lazily
      from `ensure_toast_identity()`, so an app that has never shown a toast has
      no AUMID-carrying shortcut at all — on this box `Programs\` had hello /
      Presto / Shelf / TinyDeck / tinyjs-demo but no amp.lnk. Creating it at
      install or first run would be sturdier than relying on launch parentage.
      Shelf's own new shortcuts (`Programs\tinyjs\`) deliberately do NOT carry
      the AUMID: WScript.Shell can't set it, and a plain .lnk at the launcher's
      own path would make it skip its own and downgrade toasts to tray balloons.
      Setting it needs IPropertyStore on IShellLink — tried and abandoned from
      Windows PowerShell 5.1, which won't cast the ShellLink coclass to the
      interfaces. A `launcher.exe --make-shortcut` mode would reuse the real
      code, but installed apps carry the launcher they were built with, so shelf
      would need a fallback for older installs.
- [ ] **Examples: Windows builds for shelf installs** — publish per-app
      Windows zips (+ `winUrl` in catalog.json) and a zip install path in
      shelf so it's a real store on Windows, not just a filtered list.
- [x] ~~audioTap (system)~~ — WASAPI loopback, verified with a live tone.
- [x] ~~authenticate~~ — Windows Hello via WinRT UserConsentVerifier
      (clean false where Hello hardware is absent).
- [x] ~~Chromeless polish~~ — frameless windows extend to the true top edge
      (WM_NCCALCSIZE); top-edge resize traded away, sides/bottom kept.
- [x] ~~Release + installer verified end-to-end from the LIVE site~~ —
      v0.27.0/v0.27.1 CI green; `irm tinyjs.app/install.ps1 | iex` field
      bugs fixed (PS 5.1 -UseBasicParsing; checksums served as octet-stream
      → byte[] decode); installed copy scaffolds/smokes/builds.
- [x] ~~Local media un-taint~~ — --allow-file-access-from-files via a
      vendored-loader patch (v0.27.1): WebAudio hears local files.
- [x] ~~Toast notifications + actions~~ — real WinRT toasts with buttons +
      reply field, AppUserModelID + auto Start-Menu shortcut, balloon
      fallback (`destructive` styling has no ToastGeneric equivalent).
- [x] ~~App icon~~ — runtime window icon (title bar + Alt-Tab, NOT the taskbar
      button: that keeps showing the exe's own icon, measured — see
      TODO-verify.md) from icon.png (carried
      inside the TPK); embedded into launcher.exe (`--embed-icon`) AND into
      `dist/<name>.exe` — the output can't be resource-edited (UpdateResource
      destroys the appended txiki bundle), so the build stamps a clean copy
      of the runtime BEFORE `app compile` templates it. The exe also gets a
      GUI PE subsystem patch (no console flash, no REPL-on-console quirk),
      and `launcher --run` spawns console tools with CREATE_NO_WINDOW so
      tasklist/tar/reg never flash terminals.
- [x] ~~Deep links / file associations / single instance~~ — HKCU registry
      written on first run of a built app; `launcher-win.exe --open` is the
      registered handler forwarding over `\\.\pipe\tinyjs-app-<id>` (compiled
      txiki apps reject argv); second launches activate the running instance.
- [x] ~~Context-menu suppression fallback~~ (`AreDefaultContextMenusEnabled`
      on runtimes without `ContextMenuRequested`).
- [x] ~~Windows CI GUI smoke~~ — the release job now runs the smoke page in
      a real window on the runner.

## Not planned / no OS equivalent

`quickLook`, `ocr` (Windows.Media.Ocr someday), `applescript`, `haptic`,
`dock.setBadge`/`dockIcon` (could map to `ITaskbarList3` overlays),
`setAllSpaces`, `spotlight`, `tiny.app.ai`, `wifi`, `selectedText` /
`otherWindows` / `moveWindow` (UIA could do it — revisit if asked),
`share`, `nowPlaying`/media keys (SMTC via WinRT — revisit if asked). All
reject or report `'unsupported'` so apps can feature-detect.
