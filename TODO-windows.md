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

- [ ] **`tiny.system.locale()`** — declared `false`; no locale arm in the
      launcher. `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, …)` is the
      route, with `GetUserDefaultLocaleName` for the region and
      `GetDynamicTimeZoneInformation` for the zone. This is the platform where
      it matters most: Windows has **no `LANG`**, so backend code has no
      fallback at all — and txiki has no `Intl` either. For the change event,
      `WM_SETTINGCHANGE` with lParam `"intl"` is the notification.

- [ ] **`tiny.system.wifi`** — the launcher answers `null` for a `wifi` query
      unconditionally, so `capabilities().wifi` is declared `false`. It's
      genuinely portable in principle, unlike `ocr`/`recorder`, which is why it
      stays on `tiny.system` rather than moving to `tiny.macos`.

      Route: the native WLAN API in `wlanapi.dll` — `WlanOpenHandle` →
      `WlanEnumInterfaces` → `WlanQueryInterface(wlan_intf_opcode_current_connection)`,
      giving `WLAN_CONNECTION_ATTRIBUTES.wlanAssociationAttributes`. Load it
      dynamically like the combase/WinRT paths already do rather than adding a
      link-time dependency.

      It cannot fill the macOS shape exactly, and the gaps are the design
      question, not the coding:
      * `ssid` ← `dot11Ssid`, `bssid` ← `dot11Bssid` — direct.
      * `txRate` ← `ulTxRate`, in **Kbps**; macOS reports Mbps, so divide.
      * `rssi` — **not available as dBm.** The API gives
        `wlanSignalQuality`, 0–100, documented as linear from -100 dBm (0) to
        -50 dBm (100), so `rssi ≈ quality / 2 - 100`. Derivable, lossy, and
        the value will not agree with what other Windows tools show.
      * `noise` — **not exposed at all.** `null` is the honest answer, which
        makes it the first field in this API that's per-platform nullable for
        a reason other than permissions.

      Unverified and worth checking first: Windows 11 gates SSID/BSSID behind
      the **Location** permission for some APIs, and whether that applies to
      an unpackaged Win32 process calling `WlanQueryInterface` directly is
      exactly the sort of thing that looks fine on the dev box and returns
      empty strings on a user's. All of the above is read off Microsoft's API
      docs — none of it has been run.

- [ ] **`pickColor` via the browser's `EyeDropper`** — `PICKCOLOR` reaches
      `got_unsupported`, so `capabilities().pickColor` is `false`. Windows has
      no system eyedropper to call the way macOS has `NSColorSampler` and
      Linux has the portal — but WebView2 *is* Chromium, and Chromium has
      shipped the `EyeDropper` API since 95. Worth chasing; it would close the
      gap without writing any colour-picking code at all.

      Windows-only, though. Measured 2026-07-27: `typeof window.EyeDropper` is
      **`undefined`** in the macOS WKWebView (WebKit has never implemented it),
      so this is a fallback for the Chromium webview specifically, not a
      portable "just use the browser" answer. macOS keeps NSColorSampler,
      Linux keeps the portal.

      Two routes, and the second is the better design if it works:
      * **In the page** (`runtime/tiny.js`): `if (window.EyeDropper)` →
        `new EyeDropper().open()` → `sRGBHex`, which is already the
        `'#rrggbb'` the API promises. Ten lines, and feature-detection makes
        it safe to add blind. But it only fixes the *page* half — see below.
      * **In the launcher**, via WebView2 `ExecuteScript`, answering the
        existing `PICKCOLOR` wire op. This preserves the whole architecture:
        the backend's `app.pickColor()` keeps working, `capabilities()` can
        honestly say `true`, and neither side of the API changes shape.
        Unknown: whether script run through `ExecuteScript` carries the
        **transient user activation** `EyeDropper.open()` requires.

      Three differences from the macOS contract, each needing a real Windows
      machine before any of this is claimed as done:
      1. **Screen scope.** `NSColorSampler` works *across every app and
         screen*, which is the whole point of the feature. Does Chromium's
         eyedropper inside an embedded WebView2 sample the entire desktop, or
         only the WebView2's own bounds? If it's window-only it is a
         materially weaker feature, and quietly giving it the same name would
         be exactly the over-claim this file keeps closing.
      2. **User activation.** `EyeDropper.open()` needs a transient user
         gesture: fine from a click handler, `NotAllowedError` from a timer or
         from anything the backend initiates. `NSColorSampler` has no such
         rule, so the same code would work on macOS and reject on Windows.
      3. **Page-only asymmetry.** `pickColor` exists on the backend too
         (`app.pickColor()`), and a backend has no DOM. `capabilities()` is
         *also* computed on the backend, so it cannot feature-detect the
         page's `EyeDropper` — a page-side fix would leave `pickColor: false`
         while the page-side call works, which under-claims (the safe
         direction, but still wrong). This would be the first API where the
         page can do something the backend can't.

- [ ] **scope:'app' audioTap** — system loopback shipped; per-process
      capture needs the Win10 2004+ process-loopback path. **Route proven
      2026-07-28** while probing the EQ question (TODO-audio-filters.md):
      `ActivateAudioInterfaceAsync` + process-loopback activation params,
      include-tree, aimed at `ICoreWebView2::get_BrowserProcessId()` —
      captures the audio-service child's output cleanly (a −9 dBFS sine came
      back at −9 dBFS). Two gotchas already measured: you TELL it the format
      (there is no mix format to query), and the session/capture pids differ
      (the session lives on a `--type=utility` child). What remains is
      plumbing it into `tiny_audiotap`, not research.
- [x] **native `tiny.audio.filters` — investigated 2026-07-28, and the answer
      is NO, with the reason measured rather than assumed.** Capture works,
      the biquads would port as-is, and the −60 dB attenuation trick is
      bit-clean — but process-loopback capture is post-mute AND post-volume,
      so the only way to silence the dry signal is session volume, and session
      volume (master and per-channel alike) is PERSISTED mixer state keyed on
      the shared `msedgewebview2.exe` runtime path with no host-app
      distinction. A crash while attenuated near-silences every WebView2 app
      on the machine (Teams, Widgets, …) until the key is rewritten. That is
      the CLAUDE.md per-app-only rule's exact failure shape, so
      `audioFilters: false` stays, honestly. Full numbers in
      TODO-audio-filters.md.
- [ ] **nowPlaying** — `NOWPLAYING` reaches the launcher, matches nothing in the
      dispatch chain and is dropped, so `tiny.nowPlaying.set()` did nothing
      while `capabilities()` claimed it worked (absent from the windows table =
      true). Now declared `nowPlaying: false`, which is honest but not the fix.
      Route: WinRT `SystemMediaTransportControls` (`ISystemMediaTransportControls`
      via `ISystemMediaTransportControlsInterop::GetForWindow`) — set
      DisplayUpdater properties + PlaybackStatus, and its button events are
      also the natural home for Windows media-key handling.
- [x] **haptic** — same drop, same false claim. Closed the other way on
      2026-07-26: `tiny.macos.haptic` was removed from tinyjs altogether, so
      there is no longer a capability to declare or a Windows gap to fill.
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
      The weak spot underneath is now fixed too: `create_start_menu_shortcut()`
      only ran lazily from `ensure_toast_identity()`, so an app that had never
      notified had no AUMID-carrying shortcut at all (on this box `Programs\`
      held hello / Presto / Shelf / TinyDeck / tinyjs-demo but no amp.lnk — and
      amp was exactly the app that mis-pinned). Built apps now stamp it on
      **first run**; dev spawns still skip it, having no app exe worth pointing
      at. It also rewrites when the target OR the AUMID doesn't match, so a
      plain shortcut written by an installer gets upgraded in place rather than
      blocking the real one — shelf writes exactly that, at the same canonical
      path, since WScript.Shell cannot set an AUMID (doing it from Windows
      PowerShell 5.1 was tried and abandoned: it won't cast the ShellLink
      coclass to IShellLink/IPropertyStore).
      Verified against a real installed app with the new launcher swapped in:
      first run creates target=amp.exe + aumid=tinyjs.amp with no toast; a
      fresh plain shortcut gains the AUMID on next run; an already-correct one
      is not rewritten (mtime unchanged). Watch the redo trap — WScript.Shell's
      CreateShortcut on an EXISTING file preserves its properties, so a "plain"
      shortcut planted over a good one still carries the old AUMID and the test
      silently passes for the wrong reason. Delete first.
      Caveat: shelf derives the .lnk name from the catalog title and the
      launcher from the app's own name, both stripped to [alnum . - _]. They
      match today; an app whose internal name differs from its catalog title
      would get two shortcuts.
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

## Verified 2026-07-28 (see TODO-verify.md for the measurements)

- [x] **`app.badge`** — draws via `ITaskbarList3::SetOverlayIcon`; red disc,
      white glyph, composes with `progress`, clears cleanly. `capabilities()
      .badge` is now `true` on Windows. This was the last item in the
      app-surface set that had never been seen here.
- [x] **`secrets`** against Credential Manager — full round trip, replace
      without duplicating, `null` for an unsaved key. Found and fixed a
      **2560-byte blob ceiling** that used to fail with an unexplainable
      "credential write failed" (Win32 1783). Don't reach for
      `CRED_MAX_CREDENTIAL_BLOB_SIZE` — MinGW's header has the pre-Vista 512.
- [x] **`permissions.check`**, **window-size contract** (zero ratchet drift),
      **`chrome.windowControls`** (min/max need `WS_SYSMENU`, so asking for
      minimize gets close too — and `getState` honestly reports what you got),
      **`thumbnail`** (matches macOS's breadth, not Linux's — but returns exact
      pixels, not @2x), **`build --cli`** shim + argv → `onOpenFiles` cold and
      forwarded.
- [~] **`authenticate`** — the no-Hello path only (`DeviceNotPresent`, resolves
      `false` in 13ms, no hang). A real verification still needs enrolled
      hardware.

## Not planned / no OS equivalent

`quickLook`, `ocr` (Windows.Media.Ocr someday), `applescript`,
`dock.setBadge`/`dockIcon` (could map to `ITaskbarList3` overlays),
`setAllSpaces`, `spotlight`, `tiny.app.ai`, `wifi`, `selectedText` /
`otherWindows` / `moveWindow` (UIA could do it — revisit if asked),
`share`, `nowPlaying`/media keys (SMTC via WinRT — revisit if asked). All
reject or report `'unsupported'` so apps can feature-detect.
