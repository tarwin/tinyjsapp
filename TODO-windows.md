# Windows port — remaining work

Tracking list for finishing the Windows port (branch `feat/windows`). The
core is done and verified: bridge over a named pipe, dev/build, dialogs,
menus, tray, clipboard, hotkeys, shell, theme, window ops — see the
Portability section of the README. Items below are roughly ordered by
value-for-effort; tick them off as they land. Wire-protocol op names refer to
the table in the README; the Windows handler for each lives in
`native/launcher-win.cc` (`pipe_read_loop` dispatch).

## Next up

- [ ] **Multi-window** (`WINOPEN`/`WINCLOSE`/`EVAL@<id>`/`CMD@<id>`, `WINCLOSED`)
      — the biggest missing feature; several examples use `tiny.win.open`.
      Needs per-window WebView2 hosting outside the webview library: create a
      Win32 window + `ICoreWebView2Controller` per id (mirroring the macOS
      launcher's `g_windows` map), attach the same `__invoke` bridge with
      `<winid>:<seq>` call ids, route targeted ops. The library only manages
      the main window, so this is manual COM (environment → controller →
      AddScriptToExecuteOnDocumentCreated + WebMessageReceived).
- [ ] **Drop files IN with real paths** (`DROP`) — `tiny.win.onDrop` is a
      headline feature. WebView2 hides OS paths from the page, so intercept at
      the Win32 layer: `RegisterDragDrop` on the WebView2 child HWND with an
      `IDropTarget` that reads `CF_HDROP` and forwards `DROP <json-paths>`
      (and swallows the drop so the page doesn't navigate).
- [ ] **Drag files OUT** (`DRAGOUT`) — `tiny.win.startDrag({ files })`.
      `DoDragDrop` with a `CF_HDROP` `IDataObject` from a mousedown; optional
      custom drag image via `IDragSourceHelper`.
- [ ] **Real toast notifications + actions** (`NOTIFY` actions,
      `NOTIFYACTION`/`NOTIFYCLICK`) — currently a tray balloon, no buttons/
      reply. Proper WinRT toasts need an AppUserModelID (set via
      `SetCurrentProcessExplicitAppUserModelID` + a Start-Menu shortcut for
      unpackaged apps) and the `windows.ui.notifications` WinRT APIs (or the
      C++/WinRT-free `IToastNotification` COM route). Keep the balloon as
      fallback.
- [ ] **printToPDF** (`PDF`) — `ICoreWebView2_7::PrintToPdf`; easy win.
- [ ] **Clipboard image/color WRITE** (`CLIPWRITE` fields 3/4) — read side is
      done. Write: decode png (path/base64) with GDI+ → `CF_DIB`/PNG format;
      color: no native format, write as text.
- [ ] **Menu accelerators that actually fire** — `key:` currently renders in
      the label only. webview's run loop has no `TranslateAccelerator`; use
      WebView2 `AcceleratorKeyPressed` for Ctrl+<key> combos and route to
      `MENU <id>`.

## Packaging & distribution

- [ ] **App icon + version resources in built exes** — `tinyjs build` ships
      the default exe icon. Generate an .ico from icon.png (GDI+ can write
      the frames) and stamp `dist/<name>.exe` + `launcher.exe` (embed via a
      small `windres` step in setup.ps1/build, or UpdateResource at build
      time).
- [ ] **`tinyjs publish` + auto-update on Windows** — zip `dist/`, extend
      `runtime/update.js`: platform-aware manifest (per-OS url/sha256),
      PowerShell-free sha256 (`CertUtil` or a js implementation), swap the
      install dir + relaunch (can't replace a running exe in place — rename
      old, move new, spawn, exit).
- [x] **Release CI for Windows** — done: `windows-latest` job in release.yml
      builds + smoke-tests `tinyjs-windows-x86_64.zip` (one checksums.txt
      covers all assets); `docs/install.ps1` (`irm tinyjs.app/install.ps1 |
      iex`) installs to `%LOCALAPPDATA%\tinyjs` + user PATH; `tinyjs update`
      works on Windows. Validated locally via the `TINYJS_INSTALL_ZIP` hook —
      **needs the first tagged release after merge to go live.**
- [ ] **Deep links / file associations / single instance** — registry:
      `HKCU\Software\Classes\<scheme>` + ProgID for extensions, written at
      build or first run; single instance via a named mutex + forwarding argv
      over the existing pipe (`OPENURL`/`OPENFILES` lines already exist in the
      bridge).
- [ ] **launchAtLogin** (`LOGIN`) — `HKCU\...\CurrentVersion\Run` pointing at
      the built exe; decide behavior in dev mode (probably keep
      'unsupported').

## Nice to have

- [ ] **say/voices** (`SAY`/`VOICES`/`SAYSTOP`) — SAPI (`ISpVoice`) is
      straightforward and matches the API shape.
- [ ] **captureScreen** (`CAPTURE`) — GDI `BitBlt` of the monitor → png via
      GDI+; no permission dance on Windows.
- [ ] **thumbnail** (`THUMB`) — `IShellItemImageFactory::GetImage` gives
      shell thumbnails for any registered type.
- [ ] **authenticate** (`AUTH`) — Windows Hello via
      `UserConsentVerifier` (WinRT); falls back to false today.
- [ ] **CHROME transparent** — `ICoreWebView2Controller2::put_DefaultBackgroundColor`
      + layered window; vibrancy could map to DWM acrylic/mica
      (`DwmSetWindowAttribute` DWMWA_SYSTEMBACKDROP_TYPE).
- [ ] **Accessory mode polish** — tray-only apps should also drop the taskbar
      button (`WS_EX_TOOLWINDOW`) and not flash at launch (create hidden
      instead of hide-after-create).
- [ ] **audioTap** (`AUDIOTAP`) — WASAPI loopback capture
      (`AUDCLNT_STREAMFLAGS_LOOPBACK`) covers scope:'system'; per-process
      capture needs the Win10 2004+ ActivateAudioInterfaceAsync process-loopback
      path. Sizeable.
- [ ] **Windows CI smoke test** — run the `TINYJS_HTML=test/smoke.html` flow
      headless-ish on a Windows runner (WebView2 needs a desktop session —
      GitHub's windows runners have one).
- [ ] **docs site** — tinyjs.app install page: Windows section.

## Not planned / no OS equivalent

`quickLook`, `ocr` (could use Windows.Media.Ocr someday), `applescript`,
`haptic`, `dock.*` (badge could map to `ITaskbarList3::SetOverlayIcon`),
`setAllSpaces`, vibrancy materials, `spotlight` (Windows Search exists but is
a different beast), `tiny.app.ai` (FoundationModels is Apple-only), `wifi`,
`selectedText`/`otherWindows`/`moveWindow` (UIA could do it — revisit if
asked). These all reject or report `'unsupported'` so apps can feature-detect.
