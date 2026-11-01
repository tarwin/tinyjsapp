# Changelog

All notable changes to tinyjs. Versions are git tags (`vX.Y.Z`); a tag push
builds and publishes the release. The rendered version of this file lives at
https://tinyjs.app/changelog.

## Unreleased

- **Auto-update compares pre-release versions using SemVer precedence.**
  `1.2.0-beta.2` can now update to `1.2.0-beta.10` and then `1.2.0`;
  build metadata does not affect ordering. Both the installed and published
  versions must now be valid SemVer (an optional leading `v` is accepted);
  a non-SemVer suffix on either version now prevents an update. See
  [auto-update](docs/docs/auto-update.md#versions).

## 0.68.0 — 2026-10-29

- **Menu item icons.** Menu bar, right-click and tray items take an
  optional `icon`: a png path (absolute or project-relative),
  `'sf:<name>'` for an SF Symbol on macOS, or `'emoji:<glyph>'` for a
  colour emoji drawn at menu size (no asset to ship). macOS and Windows
  show them in every menu; Linux in the menu bar and right-click menu
  (AppIndicator tray menus carry no images). An icon that can't be drawn
  leaves the item label-only. macOS 27 hides menu item images unless
  the item opts in; tinyjs opts in every item that has one. On Windows, relabelling an item with
  `menu.update` now keeps the rest of the item intact, and a checked
  item with an icon shows a tick badge on the icon (Win32 draws no
  checkmark beside one).
- **Linux: a custom right-click menu (`menu.setContext`) is now a GTK menu
  of the launcher's own** instead of WebKitGTK's, which is what lets it
  carry icons, and `checked: true` now shows its ✓ there too. Stock roles
  (copy, paste, undo, …) run the webview's editing commands and grey out
  per click, as in the menu bar.

## 0.67.1 — 2026-10-28

- **Linux X11: backend dialogs from a tray action now come up in front
  (#95).** A windowless app's `app.dialog` panel was mapped without a
  timestamp, so GNOME's focus-stealing prevention showed an "is ready"
  notice and placed the panel beside the focused window instead of over it.
  The launcher now presents confirm/alert/prompt panels and the file chooser
  with an X server timestamp, as it already does for windows (#89). Wayland
  is unchanged; it already raised them.

## 0.67.0 — 2026-10-28

- **Backend native dialogs: `app.dialog` (#95).** All seven `tiny.dialog` methods
  are now available from the backend with the same arguments and results,
  including tray/menu hooks and `"mainWindow": false` apps. The existing
  launcher panels and reply protocol are reused; no WebView is needed.
  Page calls retain their existing origin and capability gates.

## 0.66.3 — 2026-10-28

- **macOS: `navigator.mediaDevices` is no longer `undefined` under `tinyjs
  dev` on macOS 26+ (#96).** WebKit hides the whole API unless its
  `MediaDevicesEnabled` preference is on, and that default demands a TCC
  grant or a bundle usage string — dev mode has neither, because the bridge
  spawns the launcher through a title-named symlink, so WebKit sees no
  Info.plist at all (wrapping the launcher in a real `.app` can't help for
  the same reason). The launcher now flips the private-but-stable
  `_setMediaDevicesEnabled:` preference (WebKit SPI since macOS 10.13) with
  the launcher's other private WKPreferences, before the first navigation —
  dev/spawn mode only, main and secondary windows; packaged apps keep
  WebKit's declare-usage-strings-or-die check. Actual capture is unchanged:
  the per-origin media gate and TCC still decide everything.

## 0.66.2 — 2026-10-25

- **macOS: `data-tiny-drag` drags engage late or never on a real trackpad
  (#92).** `win.startDrag()` lands on the main thread via an async
  dispatch, and a trackpad press is followed by a dense stream of
  pressure/touch/gesture events — so `[NSApp currentEvent]` was often no
  longer the mouse-down when `do_dragwin` checked it, and the drag silently
  no-oped (up to 15% of gestures never moved; another ~45% engaged
  110–165 ms late). Scripted `CGEvent` gestures never show it — they carry
  no interleaved events. The launcher now caches the last left mouse-down
  from a `sendEvent:` hook and hands it to the drag APIs whenever the
  button is still held (`[NSEvent pressedMouseButtons]` — the guard
  `do_resizewin` already uses), which is what AppKit asks for: "the
  original mouseDown event". `tiny.win.dragOut()` (`startDrag({ files })`)
  had the same race with the same symptom and now shares the cache. Retries
  landing while a drag is already engaged stay harmless. The cached press
  is dropped on its mouse-up and only drags the window it landed in, so a
  `startDrag()` can't glue the window to a drag in another app (the button
  mask is system-wide). Regression harness: `test/drag-race/` reproduces
  the race synthetically (scrollwheel burst between press and dispatch).
  ([#92](https://github.com/tarwin/tinyjsapp/issues/92),
  [#93](https://github.com/tarwin/tinyjsapp/pull/93), thanks to
  [@skuridin](https://github.com/skuridin) for the diagnosis, the fix and
  the regression harness.)

## 0.66.1 — 2026-10-25

- **`tiny.capabilities.ready` can't hang a page.** It resolves within 3
  seconds even if the answer hasn't arrived — on Windows a window-mode
  popup's calls never return (a known WebView2 issue), and on Linux the
  first answer waits on D-Bus probes. A late answer still lands and fires
  `onChange`. The Linux probes now start when the app does, and windows
  opening together share one probe instead of each starting their own.
- **Linux Wayland: `setLevel`, `setAlwaysOnTop` and `setAllSpaces` are
  reported as unsupported.** GTK has no way to do them on Wayland, so they
  did nothing while `system.capabilities()` (and so
  `tiny.capabilities.can()`) claimed them. New key `windowLevel`;
  `setAllSpaces` is now `false` on Wayland too. X11 and XWayland
  (`windowPlacement`) are unchanged.
- **Linux Wayland: `setClickThrough(true)` works.** GTK rewrote the
  window's input region on every relayout, so the empty one the launcher
  set never reached the compositor and the window kept taking clicks.

## 0.66.0 — 2026-10-24

- **`tiny.capabilities`: ask whether a call will work, by the call.** Every
  `tiny` function exists on every page whether or not it works there, so a
  page couldn't feature-detect without knowing two vocabularies:
  `system.capabilities()`'s platform keys (`badge`) and the `"api"` gate's
  wire names (`app.badge`, `clip.write`) in `api.denied`. Now
  `tiny.capabilities.can('app.badge')` answers for the path you'd call,
  folding in the OS, the Linux session and the gate for this page's origin.
  `await tiny.capabilities.ready` once; after that `can()`, `get()` and
  `list()` are synchronous. `get(path)` says why not: `gate` (with `needs`,
  the names to add to `"enable"`), `platform`, or `session` (Wayland).
  `can('tray.*')` is true if any tray call works, and `onChange` fires when
  the answer arrives. It works under any gate, even `"none"`, and a page is
  told only about the names its own `tiny` uses: unlike `api.denied`, it
  never lists the app's own api methods to a site that can't call them.
  Docs: [portability](https://tinyjs.app/docs/portability#capabilities).

## 0.65.0 — 2026-10-23

- **macOS: `tinyjs wrap` no longer fails with `command failed (osascript)`
  (#91).** The release didn't include `native/pad-icon.jxa`, the script that
  pads a site's icon to the Apple icon grid, so with an installed tinyjs
  every wrap of a site with an icon stopped right after fetching it. The
  macOS download ships it again, and the padding step is now best-effort
  as intended: if it fails, the wrap finishes with the icon as fetched.
- **macOS: panel windows, for palettes over full-screen apps (#89).** A
  menu-bar app couldn't open a window that both appears over a full-screen
  app and takes the keyboard: `show()` activated the app, which made macOS
  switch to a desktop Space, and `show({ activate: false })` stayed put but
  never got keyboard focus. `"chrome": { "panel": true }` (tinyjs.json for
  the main window, or `win.open`'s chrome) makes a non-activating panel,
  the way Spotlight-style launchers do it: `show()` gives it the keyboard
  without activating the app, so with `setAllSpaces(true)` it opens over
  the full-screen app. Edit shortcuts (⌘C, ⌘V, ⌘X, ⌘Z, ⌘A) work in the page,
  and `focus`/`blur` fire. Open-time only; Windows and Linux ignore it.
- **Windows: a tray app's first `show()` now reports focus.** For an
  `"activation": "accessory"` app, `onWindowState` / `tiny.win.onState`
  missed `focused: true` on the first show and then the `focused: false`
  when the user clicked away, so a dropdown that hides when it loses focus
  stayed open the first time.
- **Linux X11: `show()` takes the keyboard every time, not just the first
  (#89).** A tray or hotkey app's window shown over another app — a
  full-screen one included — took focus on its first show only; after a
  hide, GNOME's focus-stealing prevention left it on top but marked "needs
  attention", with the keyboard still in the other app. `win.open` of a
  new or hidden window did the same. Showing now carries an X server
  timestamp, as `restore()` already did.
- **Linux X11: `setLevel()` and `setAlwaysOnTop()` survive hide and show.**
  A window given a level while it was on screen came back as an ordinary
  window after `hide()` → `show()`; only a level set before the first show
  stuck. The launcher now re-applies it each time the window is mapped.

## 0.64.0 — 2026-10-23

- **macOS: apps can ask for Location, so `tiny.system.wifi()` can read the
  network name (#90).** On macOS 14+ `ssid` and `bssid` are `null` without
  the Location permission, and there was no way to get it: the app never
  asked, and macOS only lists an app under Location Services once it has.
  Declare `"permissions": { "location": "why" }` in tinyjs.json (it writes
  the Info.plist usage string, and signed builds get the
  `personal-information.location` entitlement), then call
  `tiny.app.permissions.request('location')` — `check` works too. The
  prompt only appears in a packaged app; in `tinyjs dev`, or without the
  declaration, `request` resolves `'undetermined'` without asking. Windows
  and Linux answer `'unsupported'`.

## 0.63.0 — 2026-10-21

- **Security: a wrapped site can no longer open local files or programs.**
  `tinyjs wrap` gives the wrapped site the `"wrapper"` preset, which
  includes `shell.open`, and `shell.open` handed any target to the OS — a
  local path, a `file://` URL, any URL scheme (on Windows, straight to
  ShellExecuteW). A site could open a program it had just downloaded. A
  page the app didn't ship now gets `http(s)` and `mailto` links only;
  the app's own pages (`file://`, the dev server) can still open anything.
  `test/shell-open-gate/` has the headless check and the hostile-page run.
- **Auto-update no longer installs the wrong build.** Without signed
  updates, `install()` didn't check that an update had been found. When a
  release had no build for this OS and CPU — an arm64-only Mac release on
  an Intel Mac, a Windows arm64 app with no `win.arm64` entry — it installed
  the manifest's top-level download anyway (on Windows arm64, the mac zip,
  whose swap then deleted the app's `resources/` and `executables/`), and
  with nothing newer it reinstalled or downgraded. It now installs only
  what `check()` calls an update for this machine, and rejects with
  `no newer update available for this platform` otherwise. Signed updates
  already worked this way. `test/update-install-check/check.js` covers it.
- **Security: `"api"` gate values that looked restrictive no longer allow
  everything.** `"api": false` and `""` meant no gate at all, the same as
  leaving `api` out; they now mean `"none"`. An object with only an
  `enable` list — `{ "enable": ["notify"] }`, at the top level or inside
  `origins` — disabled nothing and so allowed every call; it now means
  "only these", like the list form. Any other value that isn't a string,
  list or object denies everything and logs why. Apps that wrote these get
  stricter, never looser. `test/api-gate/check.js` covers every form.
- **Hotkeys and keystrokes: a combo is never mistaken for a bare key.**
  On macOS `super`/`win` weren't modifiers, so `'super+k'` registered a
  bare K as a global hotkey — every K typed in every app went to your app.
  On Linux any unknown word did the same (`'hyper+k'`), and on Windows
  `'a+b'` kept the last key. The three launchers now share one parser:
  modifiers in any order (Windows used to need them first), exactly one
  key, and anything else is refused and logged
  (`tinyjs: hotkey "…" not registered: …`). On macOS `win`/`super` mean ⌘,
  as they already did in menus. A combo the OS won't register (taken by
  another app) is logged too, instead of failing silently.
- **One throwing `tiny.api.on` listener no longer silences the rest.** If
  a page listener threw, the listeners registered after it never ran for
  that event — for tinyjs's own events too (`menu`, `tray`, `drop`…). Each
  listener now runs regardless; the error is still reported in the
  console. The backend's hooks already worked this way.
- **A `'gain'` audio filter without a `gain` no longer mutes the app.**
  The native filter chain defaulted every filter's `gain` to 0 — right for
  the EQ shapes (dB), but for type `'gain'` it's a linear multiplier, so
  `{ type: 'gain' }` (say, with only `gainR` to pan) silenced the output
  on macOS and Linux while the page chain left it unchanged. It now
  defaults to 1 there, as it does in `pageChain`.
- **`tiny.d.ts` says what the code does.** Editor hovers were wrong in
  about a dozen places: `notify()` resolves `true` once the notification
  is handed to the OS (not `false` on failure); `proxyURL` does nothing on
  Windows; `audioTap` needs macOS 14.2, not 14.4, and never rejects with
  `'denied'`; `TinyBattery.percent` can be `null`; `tray.position()` is
  always `null` on Linux; `win.share()` is macOS-only and resolves `true`;
  `log()` takes any JSON value; plus the filter limit per OS, `attention()`
  per OS, the `captureScreen` default, `spotlight` per OS, tray icon paths,
  `win.open`'s `page`, and the call origin (stamped on all three OSes).
- **Security: update redirects are checked at every hop.** The update
  manifest and download had to end at an `https://` address, but the hops
  in between weren't checked, so https → http → https passed — and a
  network attacker on the plain-http hop could hand the app a manifest of
  their choosing. Each hop is now checked before it's requested.
  `test/update-redirects/check.js` covers it.
- **A failed update can't leave a half-copied app (macOS, Linux).** The
  current app was moved aside before the new one was in place; when the
  new one had to be copied (always on Linux, where the temp folder is
  another filesystem) and the copy failed partway, the app folder was left
  half-copied and the rollback silently failed. The new version is now
  staged beside the app first, and the current app is only touched once
  it's complete. The copy also keeps symbolic links. `test/update-swap/`
  covers it.
- **Editor hints for `tinyjs.json` match the docs.** The schema's
  descriptions — what editors show on hover — now say which OSes each key
  works on and what it really does (`activation` works on all three,
  `permissions` differ per OS, `downloads`/`popups` aren't wrapper-only,
  `frontend.dev` needs `devUrl`, the `api` forms…). `title` and `id` no
  longer accept `null`, which replaced the default and built
  `dist/null.app`.
- **Windows: clipboard reads stop leaving files behind.** Every
  `clipboard.read()` of an image wrote a new temp PNG, plus an empty
  placeholder file, and nothing removed them. There's now one file per
  clipboard change, deleted when the clipboard changes again (as on
  macOS). `paths` is `[]` rather than `null` when no files are copied, as
  on macOS and Linux. Screenshots stop leaving an empty placeholder too.
- **Windows: an interrupted `say()` resolves `false`.** One cut off by a
  newer `say()` or by `stopSpeaking()` resolved `true`, as if it had
  finished; it now resolves `false`, as on macOS.
- **Linux under Wayland: keystrokes and permissions answer honestly.**
  `keystroke()` and `paste()` posted keys into XWayland and answered
  `ok: true`, though the Wayland app in front never saw them; they now
  send nothing and answer `{ ok: false, trusted: false }`, matching
  `capabilities().keystroke`. `permissions.check('accessibility')` and
  `check('screen')` answer `'unsupported'` there instead of `'granted'`.
- **macOS: every popup goes through `onWindowOpen`.** A link with
  `target="_blank" rel="noopener"` (which WebKit sends as a navigation with
  no target frame) opened the browser directly — `onWindowOpen` never heard
  of it and the page got no `popup` event. It now gets the same ask and
  event as `window.open`.
- **Windows: `userAgent` applies to every window.** It was set on the
  main window only, so a wrapped site's sign-in popup — and any other
  window — went out with WebView2's stock user agent. macOS and Linux
  already applied it everywhere.
- **Security: events to pages go through the `"api"` gate.** Every event
  the backend sent reached every window, so a wrapped site heard deep
  links (with their OAuth codes), dropped and downloaded file paths,
  navigation, the audio tap and every event the app pushed. With an
  `"api"` block, a page the app didn't ship now gets an event only if its
  rules allow it: tinyjs's own events count as their calls (`menu.*`,
  `win.*`, `theme.get`…, so the `"wrapper"` preset keeps menu, tray,
  window, theme, locale, Now Playing and notification events), and the
  rest — including every event your app pushes — need `event.<name>`.
  The check is made against the page actually in the window when it's
  delivered. Apps without an `"api"` block are unchanged. An app that
  pushed its own events to a wrapped site now has to grant them.
  `test/event-gate/` has the headless check and the wrapped-site run.
- **`hotkey.register()` says whether it worked.** It resolves `true` once
  the system has the hotkey and `false` when the combo isn't valid or
  another app holds it (the reason is logged), on the page and in the
  backend. It used to resolve the same either way. On Linux under X11 a
  combo another client has grabbed is now detected too; under Wayland
  `true` means it was handed to the desktop, which asks the user.
- **Hotkeys and keystrokes: the same key names on every OS.** Each
  launcher knew a different set: Windows had no `minus`/`slash`/`quote`…,
  macOS no `home`/`end`/`pageup`/`pagedown`/`backspace` or keys above
  F12, Linux no `quote`, and only Windows took punctuation typed as
  itself. All of them now take the full set, punctuation characters
  (`'cmd+,'`) and the aliases (`enter`, `esc`, `apostrophe`…).
  **Windows: `delete` now means Backspace,** as it always did on macOS and
  Linux; use `forwarddelete` for the Delete key.
- **Linux: `capabilities().globalHotkeys` is honest under Wayland.** It
  said `true` everywhere; on a Wayland desktop without the GlobalShortcuts
  portal hotkeys never fire, and it now says `false` there. A new
  requirement, `tiny.system.requirements(['globalHotkeys'])`, explains why
  and what to install.
- **Linux: file associations join the existing file type.** For each
  `fileExtensions` entry the app defined a type of its own
  (`application/x-<id>-md`) with the same `*.md` pattern as the real one,
  which could change how file managers and other apps classify those
  files. It now lists the app under the type the system already has
  (`text/markdown`), and only an extension nothing claims gets its own.
  The type file an earlier version wrote is removed on the next launch.
  Types other tinyjs apps invented are ignored too (one still installed
  from an older version would otherwise be joined instead of
  `text/markdown`). `setAsDefaultHandler()` uses the same type.
- **Full txiki.js types for the backend.** `types/tjs.d.ts` was a stub
  that typed `tjs` as `any`; it's now txiki.js's own definitions
  (`@txikijs/types`, matching the bundled 26.6.0) in one file, so editors
  complete and check `tjs.*` calls and every `tjs:*` module import —
  without npm. New projects get it, and `tinyjs update-project` upgrades
  existing ones. Vite projects get it in `backend/` only: `src/` is the
  page, which has no `tjs`, so it no longer gets a `tjs.d.ts`.
- **New docs site.** The docs are now pages at
  [tinyjs.app/docs](https://tinyjs.app/docs/), one per topic, with search.
  Old `docs.html#…` and `api-map.html#…` links still land on the right
  page.

## 0.62.1 — 2026-10-21

- **macOS: `contextMenu: false` also covers dialogs.** The title and
  message of an alert, confirm or prompt (`tiny.dialog.*` and the page's
  own `alert()`/`confirm()`/`prompt()`) could be selected, and
  right-clicking them opened the system text menu (Look Up, Translate,
  Services…). With `contextMenu: false` that text is now plain and shows no
  menu; a prompt's input keeps its menu. Apps without the setting keep the
  standard macOS behaviour.
  ([#83](https://github.com/tarwin/tinyjsapp/issues/83), thanks to
  [@Blatman](https://github.com/Blatman) for the report.)

## 0.62.0 — 2026-10-21

- **Windows on ARM: native arm64 builds.** Releases now include
  `tinyjs-windows-arm64.zip`, and the Windows installer picks it on an
  ARM64 machine; before, Windows on ARM ran the x64 build under emulation.
  txiki.js publishes no Windows ARM64 runtime, so release CI builds
  `tjs.exe` from source the way txiki builds its x64 one, and compiles the
  launcher with llvm-mingw. WebAssembly's WASI calls needed two patches to
  txiki's WebAssembly engine (WAMR) on Windows ARM64, in
  `native/patches/`: its upstream ARM64 call routine, and a fix to the
  argument layout it uses there; txiki's own WebAssembly tests run
  against every build. Apps build for the CPU of the tinyjs that
  builds them, so an arm64 tinyjs makes arm64 apps. `tinyjs publish` names
  that zip `<name>-<ver>-win-arm64.zip`. Publishing the same project with
  an x64 and an arm64 tinyjs merges both into the manifest's `"win"` block;
  the x64 build also stays in `win.url`, where existing installs look. An
  x64 app on an ARM64 machine updates to the arm64 build when there is one,
  and an arm64 app never falls back to an x64 build. In a source checkout,
  `setup.ps1` fetches the arm64 `tjs.exe` from the latest release and
  compiles with llvm-mingw when it finds it.
  ([#82](https://github.com/tarwin/tinyjsapp/issues/82), thanks to
  [@zhuyuanmin](https://github.com/zhuyuanmin) for the request.)
- **`tiny.system.architecture()` says `arm64` on Windows on ARM.** It
  returned `x86_64` on every Windows machine, an ARM64 one included, and
  so did `info().arch`. It now reports the machine, the way `tinyjs
  doctor` does, so an x64 build running emulated on ARM64 says `arm64` as
  well.

## 0.61.0 — 2026-10-20

- **Signed app updates (opt in).** The sha256 in an update manifest comes
  from the same host as the download, so anyone who controls that host
  (domain, DNS, bucket, release) could ship their own build to every
  installed copy. Set `"update": { "publicKey": [...] }` to one or more
  Ed25519 public keys and the app only installs a manifest signed by a
  matching private key, which `tinyjs publish` reads from the file named by
  `TINYJS_UPDATE_PRIVATE_KEY_FILE` and never copies into the project. Pin a
  signing key plus an offline backup, so a lost or leaked key can be
  replaced without reinstalling. The signed manifest keeps its plain fields,
  so installs without a key keep updating from the same url;
  `--signed-only` cuts them off. `--expires 30d` (or a date) makes a
  manifest expire, and `publish --sign-manifest FILE` signs a manifest
  merged from several platforms or extends an expiry. Apps without
  `publicKey` behave as before. Guide:
  [docs/signed-updates.md](https://tinyjs.app/docs/signed-updates).
  ([#40](https://github.com/tarwin/tinyjsapp/issues/40),
  [#63](https://github.com/tarwin/tinyjsapp/pull/63), thanks to
  [@Zulut30](https://github.com/Zulut30) for the implementation.)

## 0.60.0 — 2026-10-20

- **`extras/`: a README or licence beside the app in the download.** Files
  in a project's `extras/` folder ship next to the app, never inside it: at
  the root of the macOS dmg beside the .app and the Applications shortcut,
  and in the Windows/Linux app folder beside the executable (`dist/` and
  the publish archive). `extras/macos/`, `extras/windows/` and
  `extras/linux/` replace a file of the same name for that OS, and
  `"extras": [...]` in tinyjs.json picks paths instead. Folders ship whole
  and exec bits are kept, so an `install.sh` in a Linux tarball runs. A
  name the build already uses there (`launcher.exe`, the .app,
  `Applications`, …) fails the build. On macOS only the dmg carries them
  (`build --dmg`, or `notarize --dmg`, which re-reads the folder); the
  update zip doesn't. A Linux update replaces the extras with the new
  version's; a Windows update overwrites them by name, so one you drop or
  rename stays behind. A README or licence put in `assets/` gets a note
  pointing at `extras/`.
- **`onScreensChanged` / `onSystem('screens')` no longer miss a real
  change.** Since 0.59.0 a screen being plugged in, unplugged or rescaled
  went unreported if the app called `screens()`, `screenAt()`,
  `screenFor()`, `foregroundScreen()` or `matchScreen()` around the same
  moment. Polling `screens()`, as a screen map does, could swallow every
  change. Reports where nothing changed, such as macOS's around fullscreen,
  are still dropped.

## 0.59.2 — 2026-10-20

- **Windows: `tiny.dialog` button labels.** `alert`, `confirm` and
  `prompt` now show the `ok` and `cancel` labels you pass, as macOS and
  Linux already did; before, Windows always showed OK/Cancel. Buttons grow
  to fit a long label, and the box widens if they need the room. A `&` in a
  label shows as a `&` rather than underlining the next letter. The page's
  own `prompt()` keeps OK/Cancel.
  ([#81](https://github.com/tarwin/tinyjsapp/issues/81))

## 0.59.1 — 2026-10-20

- **`tiny.dialog.confirm(message, 'detail')` shows the detail.** A string
  second argument was spread into the options as `{ 0: 'D', 1: 'e', … }`,
  so the detail and any button labels were silently dropped. `confirm` and
  `alert` now take either the detail string or `{ detail, ok, cancel }`,
  and `alert` can relabel its button (`{ ok: 'Got it' }`). `prompt` rejects
  a non-object second argument with a `TypeError` instead of ignoring it.
  Button labels apply on macOS and Linux; Windows still shows its own
  OK/Cancel. The docs now point out that the page's bare `confirm()` is the
  browser's own: one argument, the app name (or the site's origin) as the
  headline, OK/Cancel only. Use `tiny.dialog.confirm` for anything more.
  ([#81](https://github.com/tarwin/tinyjsapp/issues/81), thanks to
  [@Blatman](https://github.com/Blatman) for the report.)

## 0.59.0 — 2026-10-20

- **Windows: one coordinate space across monitors with different
  scaling.** `screens()`, `setPosition`, `getState()` and `mousePosition()`
  each divided by a different monitor's scale, so on a mixed-DPI setup they
  disagreed. A 125% laptop to the right of a 100% 1920-wide monitor reported
  `x: 1536`, a point inside the left monitor. They now share one layout,
  built the way Chromium builds its own: the laptop sits at `x: 1920`, and
  `setPosition(getState().x, getState().y)` stays put on every monitor.
  **If you worked around this** by multiplying by `scale` before
  `setPosition` (the workaround in #70), remove it. Single-monitor and
  same-scale setups are unaffected.
- **Place windows relative to one screen.** `setPosition(x, y, { screen })`
  takes coordinates relative to that screen's top-left, in its points, or in
  its pixels with `units: 'px'`. `setSize(w, h, { units: 'px' })`,
  `setFullscreen(true, { screen })` (which also works on Wayland) and
  `win.open(id, { screen, x, y })` work the same way. There is no global
  pixel space, because macOS doesn't have one, so `px` needs a `screen`.
- **More about each screen.** `screens()` entries gain `uid` (the stable
  identity to persist: display UUID on macOS, monitor device path on
  Windows, connector + model on Linux), `internal` (the built-in panel) and
  `pixels`. `getState()` gains `screenId`, `local` and `pixels`; `onState`
  events carry `screenId` and fire when a window moves to another screen;
  `mousePosition().screen` gains `id`. New: `screenAt(x, y)`,
  `screenFor(rect)`, `foregroundScreen()` (the screen of the focused window
  of any app, no permission needed; `null` on Wayland), `matchScreen(saved)`
  and `onScreensChanged(fn)` (backend: `onSystem('screens')`).
- **`"rememberWindows": true`** in tinyjs.json puts every window back where
  the user left it: position, size, screen and maximized/fullscreen, kept
  per set of plugged-in screens and matched across replugs. A window that
  was maximized comes back maximized on the screen it was on, and a move
  made just before `quit()` is saved. The main window is shown once it has
  been placed. Off by default.
  ([#70](https://github.com/tarwin/tinyjsapp/issues/70), thanks to
  [@lionel-rowe](https://github.com/lionel-rowe) for the report.)
- **Windows: windows keep their size when moved to a screen with a different
  scale.** A secondary window kept its pixel size, so a 600×400 window
  dragged from a 100% screen to a 150% one came out 400×267, and one opened
  on a scaled screen started a few points short. A main window with a menu
  bar lost the bar's height on every crossing (720 → 700 → 680).
- **Windows: no more crash on quit.** An app that was still sending updates
  to its windows as it quit could make the launcher crash during teardown
  (amp did on every quit). Since 0.58.1 that showed up as
  `webview launcher exited with status -1073741819` and a non-zero exit code.
- **Linux X11: `getState()` x/y is the frame's top-left, as documented.**
  For a window with a title bar it reported the top-left of the page area
  instead, one title bar lower than the spot `setPosition` moves the frame
  to, so `setPosition(getState().x, getState().y)` moved the window down by
  one title bar each time. Frameless windows and Wayland are unaffected.

## 0.58.1 — 2026-10-19

- **Linux: apps start on NVIDIA + Wayland.** With NVIDIA's proprietary
  driver on a Wayland session, the window opened and the app died within a
  second, before the page loaded. The cause was the driver's explicit-sync
  path: the compositor rejected the window's surface, and GTK exited. The
  stock WebKitGTK MiniBrowser dies the same way. The launcher now sets the
  driver's own opt-out, `__NV_DISABLE_EXPLICIT_SYNC=1`, when it finds the
  NVIDIA driver on Wayland. GPU acceleration stays on and fullscreen video
  works, which the `WEBKIT_DISABLE_*` workarounds broke. If you have already
  set the variable yourself, including to `0`, your value is kept.
- **A crashed webview no longer looks like a normal quit.** When the
  launcher died, `tinyjs dev` and built apps still exited with code 0 and
  printed nothing. They now print `tinyjs: webview launcher exited with
  status N` (or `killed by <signal>`) and exit with a non-zero code. Thanks
  to [@AbrLis](https://github.com/AbrLis) for the report and for tracking
  down the cause ([#78](https://github.com/tarwin/tinyjsapp/issues/78)).

## 0.58.0 — 2026-10-19

- **`app.cookies`: read and write the webview's cookies from the
  backend.** The backend can now see the same cookie jar the app's windows
  and a wrapped site use, including HttpOnly cookies that `document.cookie`
  can't reach. `get({ url?, name?, domain?, path? })` lists them (with a
  `url`, the ones a request to it would carry), `set({ url | domain, name,
  value, … })` adds or replaces one, and `remove()` deletes one cookie or
  everything for a url. The case it was built for: a site logs in, the app
  keeps the session (in `app.secrets`), and a later launch restores it so
  the user skips the login. `set()` defaults `sameSite` to `'lax'` on every
  OS. A cookie a site set without SameSite reads as that engine's own
  default (`'none'` on macOS, `'lax'` on Windows, either on Linux by
  WebKitGTK version). Pages get no cookie API: a page that could read the
  jar could read every site's session. All three OSes. Thanks to
  [@zhuyuanmin](https://github.com/zhuyuanmin) for the request and
  [@slabbdev](https://github.com/slabbdev) for helping scope it
  ([#46](https://github.com/tarwin/tinyjsapp/issues/46)).

## 0.57.10 — 2026-10-19

- **`app.paths()` creates the `data`, `cache` and `logs` folders.** The
  docs said these are created on first write, but nothing created them,
  so writing a log file into `paths.logs` failed until the app made the
  folder itself. They now exist by the time `paths()` resolves, from the
  page (`tiny.app.paths()`) and the backend alike, on all three OSes.
  Thanks to [@sheffieldnikki](https://github.com/sheffieldnikki) for the
  report ([#71](https://github.com/tarwin/tinyjsapp/issues/71)).
- **`tinyjs build` and `publish` say when the app is still running.**
  Windows can't delete a running `.exe`, so rebuilding with the app open
  failed with a bare `Error: EEXIST: file already exists`. The build now
  stops with `couldn't clear dist/ — dist/<name>.exe is in use`, and asks
  whether the app is still running. Thanks to
  [@sheffieldnikki](https://github.com/sheffieldnikki) for the report
  ([#72](https://github.com/tarwin/tinyjsapp/issues/72)).

## 0.57.9 — 2026-10-19

- **Linux: `win.onDrop` fires once, when the file is released.** It fired
  while a file was still being dragged over the window, every time the drag
  entered it, and not at the release itself, so a page couldn't tell a
  held file from a dropped one. Now it fires exactly once, on release, and
  a drag that leaves the window without dropping sends nothing. macOS and
  Windows already behaved this way. Thanks to
  [@AbrLis](https://github.com/AbrLis) for the detailed report
  ([#77](https://github.com/tarwin/tinyjsapp/issues/77)).

## 0.57.8 — 2026-10-19

- **macOS: clicking the Dock icon brings back a window hidden by
  `setHideOnClose(true)`.** The red close button hid the main window as
  expected, but nothing brought it back: clicking the app's Dock icon did
  nothing. Now it shows the same window again, with its page state intact.
  A visible or minimized window, and an app with `"mainWindow": false`,
  behave as before. Windows and Linux already showed the window when the
  app is launched again. Thanks to [@9wick](https://github.com/9wick) for
  the fix ([#73](https://github.com/tarwin/tinyjsapp/pull/73)).

## 0.57.7 — 2026-10-16

- **New: `tinyjs update-project` brings a project's types and agent skill
  up to date.** `tinyjs new` copies the TypeScript types (`tiny.d.ts`,
  `tjs.d.ts`) and the coding-agent skill (`.claude/skills/tinyjs/`,
  `.agents/skills/tinyjs/`) into a project once, and nothing updated them
  after that. Run it in a project after `tinyjs update`. It goes file by
  file and names every file it updates, adds or removes, so the skill
  folders end up identical to your tinyjs's. Types are only updated where
  the project has them, and a project without the skill gets one. Edits to
  those files are overwritten (git has the old versions). It never writes
  through a symbolic link: a linked folder or file is skipped by name, so a
  cloned project can't point it at files elsewhere. A file it can't change,
  such as one another program has open, is named and the rest still
  updates (exit 1). `tinyjs doctor` now says, in a project, when these
  files are behind.
- **Docs: what `store.set` resolving `false` means.** As with
  `localStorage`, memory is the source of truth: a failed disk write keeps
  the value in memory, the next successful save writes it, and it's lost if
  the app quits first. To retry, set the same value again rather than
  redoing a read-modify-write. In the docs, the types and the agent skill.

## 0.57.6 — 2026-10-16

- **New: `tinyjs doctor` finds what's stopping tinyjs or your project.**
  Run it (from your project folder, if you have one) when something doesn't
  work. It checks the bundled runtime and launcher; which `tinyjs` your PATH
  actually runs, and any second install behind it (the usual cause of "I
  updated but nothing changed"); WebView2 on Windows and WebKitGTK on
  Linux; which Node tools are on PATH; and the project's `tinyjs.json`:
  that it parses and has a `name`, that `minTinyjsVersion` isn't newer than
  your tinyjs, that the tool your frontend build runs is installed, and that
  `node_modules` exists. Each problem comes with how to fix it, and the
  exit status is 1 when there are any. It only reads files: it runs
  nothing, opens no windows and doesn't use the network. In a terminal the
  labels are coloured (`NO_COLOR` turns that off). See docs/doctor.md.
  Thanks to @Zulut30 for the first version (#61).

## 0.57.5 — 2026-10-15

- **Editor completion and checks for `tinyjs.json`.** A JSON Schema for
  the config now lives at https://tinyjs.app/schema/tinyjs.schema.json, and
  `tinyjs new` and `tinyjs wrap` put it in the first line of a new project's
  `tinyjs.json` as `"$schema"`. VS Code, JetBrains IDEs, Zed and most other
  editors then complete keys, show what each one does, and flag wrong
  types or values (such as `"popups": "tab"`) with no setup. tinyjs ignores
  the key, and the editor fetches the schema, never tinyjs. For an existing
  project, add the same line. See docs/config-schema.md. Thanks to
  @Zulut30 for the schema (#64).

## 0.57.4 — 2026-10-15

- **Fix (macOS): `vibrancy: false` no longer paints a transparent secondary
  window.** A window opened with `{ transparent: true, vibrancy: false }`
  came up solid white or black (following light/dark mode) instead of
  see-through, because `vibrancy: false` turned the page background back on
  for every window except main. Leaving `vibrancy` out was the workaround.
  Thanks to @Blatman for the report (#68).

## 0.57.3 — 2026-10-15

- **Fix: `tinyjs update` gets through on networks where it kept failing.**
  On some connections txiki's fetch drops the request to GitHub ("closed
  before established") while curl and browsers get through, and the update
  failed with "could not reach GitHub to check the latest release". It now
  tries twice, then asks the system curl (macOS, Linux, Windows 10 1803+).
  If everything fails, the message says what each attempt hit instead of
  only "could not reach GitHub". A redirect to a non-http(s) URL is refused
  instead of crashing the CLI. Thanks to @lionel-rowe for the report and for
  tracking down the underlying error (#67).

## 0.57.2 — 2026-10-15

- **Fix: `win.open('main')` behaves the same on every platform.** It now
  shows and focuses the main window, the way `win.open` on any open window
  focuses it, and ignores the options. Under `mainWindow: false` it throws.
  Before, macOS focused main, Linux ignored the call, Windows built a
  half-wired second window, and a windowless app got silence everywhere.

## 0.57.1 — 2026-10-15

- **Fix (macOS): `setChrome({ squareCorners })` no longer re-enables
  resizing.** Switching square corners on or off rebuilt the window's style
  with the resize bit always set, so a window that had called
  `setResizable(false)` became resizable again. It hit `app.window(id)` and
  `tiny.win` in named windows, and turning square corners off on the main
  window. Windows and Linux weren't affected.

## 0.57.0 — 2026-10-14

The backend `app` and the page's `tiny` had drifted into two dialects of
the same API: `app.setMenu` against `tiny.menu.set`, `app.notify({ title,
body })` against `tiny.notify(title, body)`, `app.paths.data` against
`await tiny.app.paths()`, and a handful of calls that quietly did something
different on one side. This release makes the backend speak the page's
API. Code moves between the two without translating, and one set of docs
covers both. The [API map](https://tinyjs.app/api-map.html) lists every
backend call beside its page twin.

> Tarwin: An apology for the big change. I hope not to do this kind of big change again, but wanted to get it right now. 25 years ago I did a lot of PHP, and I'd always have to look at the docs to know if it was `func(needle, haystack)` or `func(haystack, needle)` because it was different per function. So I know the pain of inconsistent APIs.

- **Behaviour change: `app.mouseTracking.start()` throws on failure**, as
  the page's always did: an Error with `.code` `'unsupported'`, `'denied'`
  or `'failed'`. It used to resolve `{ ok: false, … }`. Both sides resolve
  `{ ok: true }` on success, so `if (!r.ok)` code still works when tracking
  starts, but a failure now lands in your `catch`. Only Linux-Wayland can
  fail. This one can't warn, so check your backend if it calls `start()`.
- **Behaviour change: `app.window('main').close()` quits**, as
  `tiny.win.close()` does from the main page. It used to do nothing.
- **Behaviour change: `app.macos.*` throws off macOS**, as the page's
  `tiny.macos.*` does. It used to answer `true` (`quickLook`) or `null`
  (`selectedText`, `otherWindows`, which looked exactly like "nothing
  selected"). `macos.ai.availability()` is the exception, on both sides: it
  resolves `'unsupported'` off macOS. The page's version used to throw,
  which made the check you're told to make first fail.
- **Behaviour change: `audio.filter(i, patch)` patches.** A field you leave
  out keeps the value `filters()` gave that band. It used to fall back to
  the default, so `filter(0, { gain: -3 })` also reset the frequency and Q.
  A patch that changes `type` or `gainR` now rebuilds the chain; it used to
  be dropped. `audio.clear()` also re-centres the balance, as
  `pageChain(ctx).clear()` always did. Same on both sides, and in
  `pageChain`.
- **The backend uses the page's names.** The old ones still work, log a
  one-line warning the first time each is used, are marked `@deprecated` in
  `tiny.d.ts`, and will be removed in a future release:
  - `app.setMenu` / `updateMenuItem` / `getMenuItem` / `setContextMenu` →
    `app.menu.set` / `update` / `get` / `setContext`
  - `app.window(id).setMenu` / `resetMenu` / `updateMenuItem` /
    `getMenuItem` → `app.window(id).menu.set` / `reset` / `update` / `get`
  - `app.openWindow(id, opts)` → `app.win.open(id, opts)`;
    `app.getWinState()` → `app.win.getState()`
  - `app.setAudioFilters` / `setAudioFilter` / `setAudioBalance` →
    `app.audio.filters` / `filter` / `balance` (and `clear`)
  - `app.notify({ title, body, … })` → `app.notify(title, body, opts)`
  - `app.info.version`, `app.paths.data` → `(await app.info()).version`,
    `(await app.paths()).data`. `JSON.stringify(app.paths)` still gives the
    fields.
- **`app.win` is the main window**, under the names a page uses for its own
  (`tiny.win.*`), plus `open()`, `close(id?)`, `windows()` and
  `setHideOnClose()`. `close(id)` closes that window; no id closes main,
  which quits. Every `app.window(id)` handle now has its `id`. The top-level
  `app.setTitle()` and friends still target main and aren't deprecated.
- **`app.system` has everything `tiny.system` has**: `os()`, `isMacOS()` /
  `isWindows()` / `isLinux()`, `info()`, `architecture()`,
  `capabilities()`, `requirements()` and `missing()`, alongside battery,
  wifi, idleTime and locale.
- **`app.theme.get()`** → `{ dark } | null`, like `tiny.theme.get()`.
- **`tiny.app.update.check()` / `install()`** in the page. They used to need
  `tiny.api.call('update.check')`, which still works.
- **The clipboard watcher has one owner per side.** The backend and each
  window keep their own `watch()` interval, and the launcher polls at the
  fastest. A page calling `tiny.clipboard.unwatch()` no longer stops the
  backend's `onClipboardChange` (or another window's watch).
  `tiny.clipboard.onChange()` starts a watch if the page hasn't, as the
  backend's export always did; a handler on its own used to hear nothing.
- **`app.presence()` warns on anything but `'normal'` / `'menubar'`.** A
  typo like `'menuBar'` used to give a normal app without a word. It will
  throw in a future release, as the page call already does.
- **Calls that ignored part of what you passed now say so:**
  `app.startDrag({ files })` drags the window (dragging files out only
  works from the page, inside a mousedown), and `tiny.macos.ai.generate`
  can't use `tools` (their `run()` can't leave the backend).
- Fix: `tiny.system.promptMissing()` rejected with "unknown method:
  win.alert" whenever something was actually missing. It shows its dialog
  again.
- **The docs have been gone through end to end.** Every page now matches
  the code as it is, checked call by call rather than by eye:
  - **A new [API map](https://tinyjs.app/api-map.html)** lists every backend
    `app` call beside its page `tiny` twin, with full signatures. It's
    generated from `tiny.d.ts` and checked against the live objects, so it
    can't drift (`scripts/api-map/run.sh` rebuilds it).
  - **README, docs site and the agent skill** use the new names throughout,
    including `await app.paths()` / `app.info()` and `tiny.app.update`.
  - **The tinyjs.json reference is complete.** `mainWindow`,
    `audioTapReason`, `"activation": "regular"`, `urlScheme` arrays, the
    other `api` forms and every `chrome` key were missing; the
    `contextMenu` comment had its default backwards.
  - **Portability is accurate again**: what's unported on Windows and Linux
    is checked against each platform's capability table (Linux does have
    `app.badge`; Windows is missing `wifi`, `share` and the native audio
    chain). **Windows and Linux are no longer called beta.** Everything is
    tested on all three, with per-OS handling where a system needs it.
- Fix: `"chrome": { "vibrancy": false }` in tinyjs.json means no vibrancy in a
  built macOS app, as it does everywhere else. The build passed it on as a
  material named "false".
- Fix: `app.say(text, null)` threw; `app.playSound('')` sent an empty sound
  name where the page beeps. Backend `clipboard.watch()` / `unwatch()`
  return `true`, like the page's.
- `tiny.d.ts`, removed: `app.setZoom`, `app.battery`, `app.wifi` and
  `app.idleTime`, which were declared but never existed, so calling them
  threw. Use `app.window(id).setZoom` and `app.system.*`.
- `tiny.d.ts`, corrected: every `on…()` helper returns its unsubscribe
  function (they were typed `void`); `locale()` can be `null` (Windows and
  Linux have no locale reader yet); `promptMissing` resolves `{ missing,
  copied }`; page `store.set` / `delete` resolve `boolean`; `app.hide()`
  takes `{ app: false }`; the `mouseTracking` types match the runtime; a
  duplicate `onNotificationClick` is gone. Calls that only acknowledge
  (window setters, menus, tray, audio, …) resolve `Promise<void>` instead
  of `Promise<any>`; `win.getState()` can be `null`; `windowControlsPos` is
  in the chrome options and window state; `playSound()`'s argument is
  optional; `app.done` is never `null`; `app.clipboard.watch()` returns
  `true`.
- `tiny.d.ts`, added: `tiny.system.battery` / `wifi` / `idleTime`,
  `tiny.api.off`, `tiny.win.setMinSize` / `ensureOnScreen` /
  `startResize` / `onState`, the window handle's `setMinSize` /
  `ensureOnScreen` / `startDrag` / `startResize`, `app.audio.sampler`,
  `app.spawnHidden`, `app.setAsDefaultHandler`, `app.system.locale`,
  `minSize` in window options, `primaryAction` on tray specs, `role` on
  menu items, `canPosition` on window state, and typed results for
  `requirements()` and `system.info()`.

## 0.56.0 — 2026-10-14

- **`tiny.dialog.saveFile({ name })` pre-fills the file name.**
  `saveFile({ types: ['docx'], name: 'report' })` opens the save panel with
  `report.docx` in the name field instead of "Untitled" (macOS) or an empty
  field (Windows, Linux), so an app can suggest the name the user would
  otherwise type every time. It is a file name, not a path: any folder part
  is dropped, so a page can't use it to open the panel somewhere else. The
  panel opens in its usual folder. Verified on all three platforms. Thanks
  to Blatman for the request (#59).

## 0.55.1 — 2026-10-14

- **Windows: `"activation": "accessory"` keeps a normal title bar.** Hiding
  the taskbar button also turned the window into a tool window, with a small
  caption, no icon, no minimize or maximize, and an odd close button. The
  window now keeps its full title bar and still has no taskbar button; the
  same goes for `app.presence('menubar')`. With nothing on the taskbar to
  minimize to, minimizing hides the window, and the tray (or `show()`)
  brings it back. A showing accessory window is listed in Alt+Tab. Thanks to
  lionel-rowe for the report (#56).
- **Windows: `setResizable(false)` no longer grows the page.** The page came
  out 10px wider and taller than declared, because the thinner frame's
  spare pixels went to the page. The page now keeps its size and the
  window's frame shrinks around it. Thanks to sheffieldnikki for the report
  (#57).

## 0.55.0 — 2026-10-14

- **`src/shared/`: code the backend and the page both import** (#58). Put
  it beside `src/backend/` (or `shared/` beside the Vite templates'
  `backend/`) and both sides use the same path,
  `import { X } from '../shared/settings.js'`, in `tinyjs dev` and in a
  built app. It ships next to the frontend, the macOS page may read it,
  and editing it in dev restarts the app. The layout before 0.52
  (`src/main.js`) gets a hint to move the backend into `src/backend/`
  instead.
- **Linux: `tinyjs dev` sees edits in subfolders.** Linux file watching
  only reports a folder's own files, so editing `src/backend/lib/util.js`
  never restarted the app and editing `src/frontend/css/app.css` never
  reloaded the page. You had to save a top-level file or restart dev by
  hand. Dev now watches every subfolder of the backend, `shared/` and
  frontend folders, including folders you add while it runs. macOS and
  Windows already watched the whole tree.

## 0.54.0 — 2026-10-14

- **`executables/`: sidecar programs that ship with the app.** Put the
  programs your backend launches (a Go or Rust CLI, a `bun build --compile`
  binary, ffmpeg) in an `executables/` folder. It works like `resources/`:
  the whole folder ships, `"executables": [...]` (per OS through the
  `macos` / `windows` / `linux` blocks) narrows it, and
  `app.paths.executables` (`(await tiny.app.paths()).executables` in the
  page) is where to `tjs.spawn` them from, or `null` without any. A build
  ships only its own OS's binaries, so every OS's can live there together;
  `executables/<os>/` and `executables/<os>-arm64/` or `-x86_64/` override
  files by name for that OS and CPU. On macOS they go in
  `Contents/Helpers/` and are signed with the app's identity, with the
  hardened runtime under a Developer ID, so a notarized app can carry them;
  `"macos": { "executableEntitlements": { "my-cli": [...] } }` gives a JIT
  sidecar (Bun, Deno, Node/pkg) the `allow-jit` it needs to run there. The
  build warns when a macOS sidecar can't run on every Mac the app runs on.
  Thanks to Luiz Américo, Sam LABBE and Blatman for the design discussion
  (#8).
- **A backend at the project root no longer copies `resources/` and
  `assets/` into the app's sources** (`"backend": "main.js"`); they ship
  only through their own paths.
- **Windows: an update removes the files the new version dropped** from
  `executables/` and `resources/`; until now they stayed in the install
  folder (macOS and Linux replace the whole app, so they never had this).
  The rest of the folder is left alone, and a failed update still restores
  every file. It takes effect from the update after an app built with this
  one, since the installed version's updater does the swap.

## 0.53.2 — 2026-10-14

- **Windows: the first window no longer sometimes stays blank in
  `tinyjs dev`.** The fix in 0.48.0 made the first page load wait until
  WebView2 had finished an internal step before sending it, but a
  freshly started WebView2 could still silently drop it, leaving an empty
  window. The launcher now checks a second later that the load actually
  started, and sends it again if it didn't (up to 4 tries). Wrapped sites
  benefit too. Thanks to lionel-rowe for the report and the trace (#32).

## 0.53.1 — 2026-10-14

- **`tinyjs new` no longer tells you to run `npm install` and `npm run dev`.**
  That advice was create-vite's own closing message, printed just before
  ours; it is now hidden, so the only next steps shown are tinyjs's (`cd`,
  then `tinyjs dev`). Thanks to Raymond Camden for the report (#43).
- **Docs: the devtools key on macOS.** `"debug": true` opens devtools with
  F12 on every OS, and ⌥⌘I also works on macOS, where F12 needs fn on most
  keyboards. `"debug": "open"` keeps the inspector open through page
  reloads and `tinyjs dev` backend restarts (#44).

## 0.53.0 — 2026-10-13

- **`resources/`: data files that ship with the app.** Put files your code
  reads at runtime (a dictionary, a database seed, fonts) in a `resources/`
  folder and the whole folder ships with the app. `app.paths.resources` in
  the backend, and `(await tiny.app.paths()).resources` in the page, point at
  it: the folder itself in dev, the bundled copy in builds, `null` when there
  is none. `"resources": [...]` in tinyjs.json ships only the listed paths,
  and the `macos` / `windows` / `linux` blocks can list different ones per
  OS. Thanks to Manacost Dev for the implementation (#49) and Luiz Américo
  and Sam LABBE for the design discussion (#8).
- **`assets/`: the icon, About credits and help book in one place.** New
  projects keep `icon.png` in `assets/`, next to the other files the OS
  reads. Existing projects with `icon.png` and `Credits.html` at the project
  root keep working unchanged. The build notes any file in `assets/` it
  doesn't use, since nothing else there ships.
- **macOS: help books.** Put an Apple help book folder in `assets/` and name
  it with `"macos": { "helpBook": "My App Help" }`. The build bundles it,
  registers it in Info.plist and rebuilds its search index, and the app gets
  a Help menu with "\<App> Help" (⌘?) and a search field that searches the
  book. Thanks to Blatman for the request and a sample book (#47).
- **`{ role: 'help' }` in `setMenu`.** Your own Help menu items: on macOS the
  menu always goes last and gets the system search field (plus "\<App> Help"
  when there's a help book); on Windows and Linux it's a plain "Help" menu.
- **macOS: `Credits.rtf` and `Credits.rtfd` work as About panel credits,** as
  well as `Credits.html`.
- **macOS: built apps carry `CFBundleShortVersionString`,** set from
  `version`, the key Finder and the rest of macOS read the version from.
- **Linux: clicking a menu item no longer gives it a check mark.** GTK
  toggled every menu bar and tray item on click, so a plain item showed ✓
  after one use and a checked one lost its ✓, whatever the app had set.
  `checked` now only changes when the app changes it, as on macOS and
  Windows.
- **Windows: `tinyjs publish` from Git Bash writes a real zip.** Git Bash
  puts GNU tar first on PATH, which ignores `-a` and wrote a tar archive
  named `-win.zip`, and an app started from Git Bash couldn't unpack
  its update zip. Both now use Windows' own `tar` from System32.

## 0.52.0 — 2026-10-13

- **`tinyjs dev` no longer restarts the app on frontend edits in the Vite
  templates.** The dev watcher watched `src/`, which in the Vite templates
  is the frontend, so every save restarted the whole app after Vite's HMR
  had already applied it. Backend edits in `backend/` were never watched,
  so they didn't restart at all. The watcher now follows the backend
  entry's own folder: frontend edits are left to HMR and backend edits
  restart. A backend at the project root (`"backend": "main.js"`) skips
  `.build/`, `node_modules/`, `.git/` and the build output. Thanks to
  Anton Tsvilikhovskiy for the report (#53) and Sam LABBE for the fix (#54).
- **New vanilla apps put the backend in `src/backend/`,** next to
  `src/frontend/` instead of above it. `tinyjs new` scaffolds
  `src/backend/main.js`, and it is the first place the backend is looked
  for. Existing apps with `src/main.js` keep working unchanged.
- **`"frontend": { "reload": false }`** stops `tinyjs dev` from reloading
  the page on frontend changes. Use it when your own tool already does (a
  watch-mode bundler writing into the frontend folder plus a livereload
  script), so the page doesn't reload twice per save. Dev servers set with
  `devUrl` never had tinyjs reloads.

## 0.51.1 — 2026-10-12

- **The installers no longer get rate limited on CI.** `install`,
  `install.ps1`, `tinyjs update` and the daily update notice looked up the
  latest release through GitHub's REST API. Without a token, GitHub allows
  60 of those calls an hour per IP, and shared CI runners use that up, so
  installs failed with a 403 ("could not determine latest release") or
  "API rate limit exceeded". They now read the tag from the
  `github.com/tarwin/tinyjsapp/releases/latest` redirect, which isn't
  limited this way and needs no token. Pinning with `TINYJS_VERSION` works
  as before. Thanks to Tulio Calil for the report (#51).

## 0.51.0 — 2026-10-12

- **`"mainWindow": false`: tray agents with no window until they need one.**
  An app with `"activation": "accessory"` only hides its main window; the
  web engine still runs behind it. With `"mainWindow": false` there is no
  main window or WebView at all. The backend, tray, menus, hotkeys and
  clipboard run on their own, and the engine starts when the app opens a
  named window with `app.openWindow`. Idle memory for a tray agent drops
  from ~65 to ~17 MiB on macOS, ~111 to ~12 MiB (private bytes) on Windows
  and ~329 to ~111 MiB (RSS) on Linux. `app.windows()` starts as `[]`, no
  `index.html` is needed, and closing the last window leaves the app
  running. `app.hide()` / `app.show()` hide and restore the open windows,
  and `app.reload()` reloads them. Can't be combined with `"url"`. Combine
  it with `"activation": "accessory"` for a menu-bar / tray-only app. Thanks
  to Mikhail Angelov for the macOS implementation (#50).
- **Linux: named windows open with the current `app.icon()`.** They always
  took the startup icon, so a window opened after `app.icon()` didn't match
  the rest.

## 0.50.1 — 2026-10-07

Security fix.

- **macOS: an iframe can no longer call your app.** WebKit gives the page's
  message handler to every frame, not only the top one, so any iframe in
  an app's page (a CodePen embed, an ad, a wrapped site's widgets) could
  post a hand-built call. Without an `"api"` block every call was allowed,
  so that iframe could call the app's own backend methods and every
  built-in one, AppleScript, keychain secrets and the clipboard included.
  Its replies, including errors, also went to the top frame and settled the top frame's own pending calls with the same
  number. The launcher now marks calls from subframes, and the backend
  runs one only when an `"api".origins` key names the iframe's origin, under
  that key's lists. No `"api"`, a preset, or top-level lists alone give
  subframes nothing. A subframe's replies no longer reach the top frame.
  Top-level pages, satellite windows and wrapped sites are unchanged. Linux
  already dropped subframe calls (0.46.0), and WebView2 on Windows only
  delivers messages from the top-level document. **macOS apps that show
  third-party iframes should update.**

## 0.50.0 — 2026-10-05

- **`tinyjs new --template` asks, and works with your package manager.**
  `--template` on its own asks for the framework (vanilla, react, vue,
  svelte, solid, preact, lit, alpine) and the language, then which package
  manager to use. Only the ones that actually run on this machine are
  offered: npm, pnpm, yarn, bun, or Vite+ (`vp`) over one of them. It then
  installs the dependencies. Flags answer each question without a prompt:
  `--template react-ts`, `--pm pnpm` (or `--pm vp:pnpm`), `--no-install`.
  With no terminal it uses npm, and a bare `--template` stops with the list.
  tinyjs.json runs the project's dev and build scripts through the chosen
  manager (`pnpm run dev`).
- **Alpine.js starter:** `--template alpine` / `alpine-ts` is Vite's vanilla
  template plus [Alpine.js](https://alpinejs.dev), with a small component
  that calls the backend.
- **Vite+ (`--pm vp`) needs vp 1.0 or newer.** A 0.1.x `vp` scaffolds
  projects that mix 0.1 and 1.0 Vite+ packages and fail every build with a
  misleading "Cannot find native binding", so tinyjs refuses it and says to
  run `vp upgrade`.
- **TypeScript backends build in projects that pin another package
  manager.** npm 11 refuses `npx` in a project whose package.json sets
  `devEngines.packageManager` to something else (Vite+ 1.0 writes pnpm's),
  so the esbuild step failed with `EBADDEVENGINES`. esbuild now runs from
  the project's own `node_modules` when installed, otherwise through `npx`
  from a temp folder.
- **CLI prompts show up.** The terminal check always failed, so no prompt
  ever appeared: `tinyjs wrap` silently took the exact origin instead of
  asking whether to include subdomains. It asks now.

## 0.49.0 — 2026-10-05

Thanks to [@slabbdev](https://github.com/slabbdev) for the About-panel
change ([#37](https://github.com/tarwin/tinyjsapp/pull/37)) and to
[@cfjedimaster](https://github.com/cfjedimaster) for the report
([#41](https://github.com/tarwin/tinyjsapp/issues/41)).

- **macOS: your own credits in the About panel.** A `Credits.html` in the
  project root is bundled into the app by `tinyjs build`, and the standard
  About panel shows it (links work) instead of "Made with tinyjs". Start it
  with `<meta charset="utf-8">` and set a font, or AppKit reads it as
  Latin-1 and sets it in Times. Without a `Credits.html`, `"attribution":
  "…"` in tinyjs.json replaces the credit line with your own
  ([#37](https://github.com/tarwin/tinyjsapp/pull/37)).
- **`tinyjs build` / `tinyjs dev` say "run `npm install` first"** when a
  bundler project (`--template …`) has no `node_modules`, instead of
  failing inside the project's own tools. That was a TypeScript
  `Cannot find type definition file for 'vite/client'` error with a global
  `tsc` installed. A `node_modules` in a parent folder (workspaces) and
  Yarn PnP both count
  ([#41](https://github.com/tarwin/tinyjsapp/issues/41)).
- **A backend entry at the project root builds.** With `"backend":
  "main.js"` the build copied the whole project, including its own build
  folder, into the app until it failed with `ENAMETOOLONG`. It now skips
  `.build`, `node_modules`, `dist`, `.git` and the frontend folders.
- **Docs:** the project folders are conventions, now documented:
  `"backend"` picks the backend entry (its folder is the backend's root),
  and `"frontend": { "dir": "…" }` picks the plain page folder (default
  `src/frontend`).

## 0.48.0 — 2026-10-05

Thanks to [@slabbdev](https://github.com/slabbdev) for the report, triage
and fix ([#36](https://github.com/tarwin/tinyjsapp/issues/36),
[#38](https://github.com/tarwin/tinyjsapp/pull/38)) and to
[@zmila](https://github.com/zmila) for tracking down the Windows blank
window ([#32](https://github.com/tarwin/tinyjsapp/issues/32)).

- **A page's `tiny.win.printToPDF(path)` can't write just anywhere.**
  `win.*` is part of the `"wrapper"` preset, so a wrapped site could write
  PDF bytes over any file the user can write. From the app's own pages
  nothing changes. **From any other origin, the PDF is written directly
  only into Downloads (a bare file name lands there), the app's data folder
  or the temp folder; any other path opens a save panel pre-filled with the
  name, and cancelling rejects.** Backend `app.printToPDF` is unchanged.
  All platforms ([#36](https://github.com/tarwin/tinyjsapp/issues/36)).
- **Windows: an app whose first page is `http` no longer opens blank.**
  The launcher cancels a page's first load to ask `onNavigate`, then
  retries it. On a cold start WebView2 takes ~800ms to finish the cancel,
  and the retry, sent before that, was silently dropped. That hit every
  dev-server app (`--template react-ts`, `svelte-ts`) and wrapped sites;
  adding a menu only hid it by changing the timing. The retry now waits
  for the cancel to finish
  ([#32](https://github.com/tarwin/tinyjsapp/issues/32)).
- **`tinyjs dev` no longer reports its own entry file as an opened
  document.** Every dev launch sent an `open-files` event naming
  `.build/app/entry.js`; parsing the command line for documents now only
  happens in built apps
  ([#38](https://github.com/tarwin/tinyjsapp/pull/38)).
- **Linux: `app.paths` Downloads, Desktop and Documents follow the user's
  real folders** (`user-dirs.dirs`, so a translated `~/Téléchargements`
  works), the same ones the launcher already used.

## 0.47.1 — 2026-10-04

Security fix. Thanks to [@slabbdev](https://github.com/slabbdev) for the
report ([#30](https://github.com/tarwin/tinyjsapp/issues/30)).

- **A page can no longer get around the `"api"` gate with malformed
  messages.** The launchers passed part of each page message on to the
  backend unchecked, so a page that `"api"` restricts, such as a wrapped
  site or an iframe inside one, could send a message that ran with the
  app's own permissions, or trigger the app's own event handlers. Each
  launcher now drops malformed messages before they reach the backend.
  All three platforms. **Apps that wrap a site or use `"api".origins`
  should update.**

## 0.47.0 — 2026-10-04

Security fixes from the rest of #30's audit. Thanks to
[@slabbdev](https://github.com/slabbdev) for the report
([#30](https://github.com/tarwin/tinyjsapp/issues/30)).

- **`tiny.proxyURL` serves only the app's own pages.** The `tiny-media://`
  proxy fetches any http(s) URL and answers with CORS open, so any page
  that could reach it could read any site or local-network URL the machine
  can, including a wrapped site or a third-party iframe. The proxy now
  serves only the app's own pages and the dev server. **Wrapped sites that
  use `tiny.proxyURL` need `"media.proxy"` in their `"api".origins` key.**
  It also refuses to serve pages, which closes a trick where a frame loaded
  its own page through the proxy and then read anything same-origin. macOS
  and Linux (Windows has no proxy).
- **Other URL schemes go to `onNavigate`, blocked by default.** Links and
  navigations to `mailto:`, `ms-settings:`, `someapp://` and the like, in
  any frame, never reached `onNavigate`. On Windows, WebView2 offered to
  open them with its own prompt. They now get a `kind: 'policy'` call on
  all three platforms. Return `'external'` to hand the URL to the OS;
  anything else, or no answer, blocks it. **A `mailto:` link in a wrapped
  site now does nothing unless `onNavigate` returns `'external'`.**
  `isMainFrame` is `null` where the engine can't tell (Linux and Windows).
- **`tinyjs update` runs the installer that came with your install**
  instead of downloading it from tinyjs.app each time, so the website no
  longer runs code on your machine at every update. It downloads from
  GitHub and checks the release checksums as before. An install in a custom
  `TINYJS_HOME` now updates in place instead of moving to the default
  location. `tinyjs update --dry-run` shows what it would run.
- **`TINYJS_DEBUG` no longer prints clipboard contents or keychain
  values.** The message trace shows `[redacted]` for those calls and their
  replies.
- **Docs:** what `*` matches in `"api".origins` keys (any characters,
  dots included: never end a key with `*` after the host), and the
  `onNavigate` example compares origins exactly.

## 0.46.0 — 2026-10-02

Security fixes. Thanks to [@slabbdev](https://github.com/slabbdev)
for the reports ([#18](https://github.com/tarwin/tinyjsapp/issues/18),
[#29](https://github.com/tarwin/tinyjsapp/issues/29)).

- **A page's `win.open` stays inside the app.** `tiny.win.open` took any
  absolute path for `page`, and the `"wrapper"` preset hands `win.*` to the
  wrapped site. Every launcher lets `file://` pages read other files, so a
  hostile page could download an HTML file and then open it as a privileged
  `file://` window that reads the disk. From a page, `page` must now be an
  http(s) URL or a file inside the frontend directory. `../` walks, other
  absolute paths, UNC paths and `file:`/`javascript:` URLs are refused with
  an error. Backend `app.openWindow` is unchanged. All platforms.
- **Windows: the single-instance pipe belongs to the user.** It was
  `\\.\pipe\tinyjs-app-<id>`, the same name for every user on the machine,
  with Windows' default permissions. Other users could connect to read it,
  and a user who created the name first would receive a second launch's
  file paths and deep links (OAuth callbacks included). The pipe is now
  per user (`tinyjs-app-<id>-<user>`), locked to that user and SYSTEM with
  network access denied. A second launch or a registered URL/file handler
  hands over only to a pipe whose server runs as the same user. If another
  user holds the name, single instance turns off, matching Linux since
  0.42.2 (#12). URL schemes and file types re-register with the new name
  on first launch.
- **Windows: the app only talks to its own window process.** The private
  pipe between an app and its window has a random name, but anyone on the
  machine can list pipe names and connect to read. The app took the first
  connection to arrive as its window and sent it everything, so another
  user who connected first received the app's traffic. The window process
  now proves itself with a one-time secret before the app sends anything,
  and connections without it are hung up.
- **Linux: an iframe can no longer borrow its page's `"api"` access.**
  WebKitGTK doesn't say which frame a message came from, so the launcher
  relies on a per-page token that only the top frame holds. Messages
  without a token were still accepted (a fallback for pages that never got
  one) and stamped with the top frame's origin. So a cross-origin iframe
  inside a trusted wrapped site could post a hand-built message and be
  gated as that site. Untokened messages are now dropped once the window
  has a token, which leaves the fallback only for a window whose first
  page hasn't loaded yet
  ([#18](https://github.com/tarwin/tinyjsapp/issues/18)).

## 0.45.0 — 2026-10-02

Security and robustness fixes. Thanks to [@slabbdev](https://github.com/slabbdev)
for the reports ([#26](https://github.com/tarwin/tinyjsapp/issues/26),
[#29](https://github.com/tarwin/tinyjsapp/issues/29),
[#30](https://github.com/tarwin/tinyjsapp/issues/30)).

- **Windows: each app has its own browser profile.** WebView2 names its
  data folder after the executable, and every tinyjs app's window belongs
  to `launcher.exe`, so all tinyjs apps on a machine shared one profile:
  cookies, IndexedDB, site permissions and `localStorage`. Because an
  app's own pages all count as one origin there, two apps saving
  `localStorage.setItem('settings', …)` overwrote each other. Each app now
  keeps its profile in `%APPDATA%\<app-id>\WebView2`. **Nothing is
  carried over:** after updating, Windows apps start with an empty
  profile, so wrapped sites ask you to log in again and page
  `localStorage` starts empty. `tiny.store` is unaffected.
- **Built Windows and Linux apps ignore tinyjs's dev environment
  variables.** A built app passed its whole environment on to the
  launcher, so whatever started it could replace its page
  (`TINYJS_HTML`), inject script, widen camera/microphone or file access,
  or add WebView2 browser flags such as a remote-debugging port. Built
  apps now drop every inherited `TINYJS_*` and `WEBVIEW2_*` variable and
  use only their manifest. `tinyjs dev` and macOS are unchanged.
- **Streaming `tiny.fetch` in two windows at once works.** Each page
  numbers its streams from 1, so two windows streaming at the same time
  used the same id: the first window's body stopped after its first
  chunk, and any window could read or cancel another's stream. Streams
  now belong to the window that started them.
- **`tiny.store` treats every key as a plain key.** `set('__proto__', …)`
  changed what other keys read back instead of storing a value, and
  backend `get('constructor')` returned a built-in function rather than
  `null`. A `store.json` holding `null` or an array now loads as empty
  instead of making every call fail.
- **Window titles flatten carriage returns and tabs**, as they already
  did newlines.
- **txiki.js downloads are checked against pinned hashes.** Cross-arch
  and universal macOS builds bundle a txiki runtime from a cache in
  `~/Library/Caches/tinyjs`, which any program running as you can write
  to, and it was then signed with your identity. Builds now check it
  against a hash shipped with tinyjs, after download and every time the
  cache is reused, and stop if it doesn't match. `setup.sh`, `setup.ps1`
  and the release workflow check their downloads the same way.
- **`setup.sh` uses a private temp folder** instead of predictable
  `/tmp` names another local user could set up in advance.

## 0.44.1 — 2026-10-02

Thanks to [@slabbdev](https://github.com/slabbdev)
([#16](https://github.com/tarwin/tinyjsapp/pull/16),
[#21](https://github.com/tarwin/tinyjsapp/pull/21)) and
[@samcharles93](https://github.com/samcharles93)
([#31](https://github.com/tarwin/tinyjsapp/pull/31)).

- **Windows: a failed self-update no longer leaves the app half-updated.**
  The update replaces the app's files one by one, and a file locked
  partway through (Defender, the indexer) stopped it there, leaving some
  files old and some new ([#13](https://github.com/tarwin/tinyjsapp/issues/13)).
  Renames now retry a briefly locked file, and if one still can't be
  replaced, every file already swapped is put back and the update reports
  that the current version is intact.
- **The app comes back after a self-update.** On Windows the relaunched
  app was killed along with the old one as it quit, and on every platform
  the new instance could find the old one still running and hand off to
  it, so no app was left open. The relaunch now starts outside the old
  process on Windows and waits up to 5 seconds for the old instance to
  exit. An app updating from an older version still runs the old update
  code that one time, so it won't reopen on its own after that update.
- **macOS: `tinyjs dev` shows the app's name in the menu bar and the app
  switcher**, not "launcher-macos". Built `.app`s already did.
- **openSUSE support.** `setup.sh` prints the zypper packages to install,
  the README covers openSUSE's package names, and the tray requirement no
  longer suggests a package openSUSE doesn't have. The installer's
  WebKitGTK check also stopped failing on systems where `ldconfig` isn't
  on the user's `PATH` (openSUSE, and others that keep it in `/usr/sbin`)
  even though WebKitGTK was installed.

## 0.44.0 — 2026-10-02

- **`tinyjs wrap <url>` turns a website into a desktop app.** One command
  scaffolds a site wrapper: the site is the app's main window, there's no
  frontend to write, and everything it generates is ordinary `tinyjs.json`
  and backend code you can edit afterwards. It fetches the page's title and
  best icon (apple-touch-icon, then the page's icon links, then
  `/favicon.ico`, with the PNG pulled out of ICO files), and on macOS pads
  the icon to Apple's icon grid. The page title is cut at a tagline
  separator ("GitHub · Change is constant…" becomes "GitHub") and loses any
  characters a filename can't hold, since it becomes the `.app` name.
  The `"api"` gate gives the wrapped site's exact origin the `wrapper`
  preset and every other origin only the window controls (close, minimize,
  zoom, drag). `--origins subdomains` widens that to the site's registrable
  domain, using the public suffix list so it never stops at a shared suffix
  like `co.uk` or `github.io`; `--origins` also takes an explicit list.
  Popups open as windows (OAuth keeps working) and downloads ask where to
  save. `--menubar` makes a menu-bar app with no Dock icon, `--panel` (with
  `--menubar`) a dropdown panel under the menu-bar icon, `--top` keeps the
  window above others, `--external a.com,b.com` opens those sites in the
  default browser, and `--ua` sets the user agent. `--force` re-wraps an
  existing project in place: settings you don't pass flags for carry over,
  `--no-menubar`, `--no-panel` and `--no-top` turn a mode off, and an
  edited `src/main.js` is saved as `src/main.js.bak` first. Thanks to
  [@slabbdev](https://github.com/slabbdev) for the feature
  ([#20](https://github.com/tarwin/tinyjsapp/pull/20)).

## 0.43.0 — 2026-10-02

Security fixes. Thanks to [@slabbdev](https://github.com/slabbdev) for the
reports, each with a runtime repro
([#24](https://github.com/tarwin/tinyjsapp/issues/24),
[#27](https://github.com/tarwin/tinyjsapp/issues/27),
[#28](https://github.com/tarwin/tinyjsapp/issues/28)).

- **Only the app's own pages get the microphone and camera without
  asking.** On macOS the launcher granted every `getUserMedia` request, so
  in a site wrapper the wrapped site, any page it redirected to, or a
  third-party iframe got the device with only the one system prompt, which
  names the app and not the site. On Linux the `"permissions"` block
  entitled every page in the app. Now the app's own `file://` pages (and the
  dev server in `tinyjs dev`) are granted as before; any other origin gets
  WebKit's own permission prompt on macOS (for a third-party iframe inside
  the app's page, that prompt names the app, not the iframe's site) and is
  refused on Linux. An app can trust a site explicitly with an `"api"`
  `origins` key that allows `media.microphone` / `media.camera`, e.g.
  `"https://meet.example.com": ["media.*", "notify"]`. Presets and
  top-level lists never grant it. Windows is unchanged: WebView2 already
  asks per site.
- **`tiny.fetch` no longer puts request headers on curl's command line.**
  curl is only in the picture as a stopgap: txiki.js 26.6.0's own fetch
  sends a root-path URL as `GET //` and can't complete a handshake with
  TLS 1.2-only servers, so until fixed txiki releases ship, those two kinds
  of request go through the system curl instead. On those requests, headers
  such as `Authorization` and `Cookie` were readable by any local user with
  `ps` while the request ran. They now go through a temp file in a folder
  only the current user can open, deleted when curl exits. Every other
  request never touched curl.
- **`tiny.fetch` drops credentials on cross-origin redirects.**
  `Authorization`, `Cookie` and `Proxy-Authorization` were sent again to
  whatever origin a redirect pointed at. They're now dropped when a
  redirect changes origin, as browsers do, and a redirect that turns a POST
  into a GET also drops the body's `Content-*` headers.
- **`tiny.audio.sampler.load` only accepts audio files.** A page could load
  any path and read the file back through the internal `sampler.bytes`
  call, so any page in an app without an `"api"` gate could read any file
  the user can. `load` now refuses a path unless it has an audio extension
  AND starts like an audio file (WAV, AIFF, CAF, Ogg, FLAC, MP3/AAC,
  M4A/MP4, WebM). The contents check alone let UTF-16 text files and HEIC
  photos through. `sampler.bytes` answers only the main window on macOS and
  Windows.

## 0.42.3 — 2026-09-29

Security fix. Thanks to [@slabbdev](https://github.com/slabbdev) for the
report, which came with a repro
([#22](https://github.com/tarwin/tinyjsapp/issues/22)), and the fix
([#23](https://github.com/tarwin/tinyjsapp/pull/23)).

- **A window id from the page can no longer add launcher commands.** Some
  window calls put the id the page passed straight onto the line the
  runtime sends to the launcher: `tiny.win.close(id)`, `win.open` with
  `minSize` or with `x`/`y`, and the menu item lookups. A newline in the id
  split that line in two, and the launcher ran the second half as a
  command of the page's choosing, which the `"api"` gate never saw. On
  macOS that included running AppleScript, so a page could run shell
  commands as the user. The line splitting worked on every platform. Any
  page that could call `win.*` could do it: apps without an `"api"` gate,
  and apps using the `wrapper` preset. Ids are now cleaned of tabs and
  newlines at these calls, as they already were everywhere else. Real
  window ids never contain them, so apps don't need to change anything.

## 0.42.2 — 2026-09-28

Thanks to [@slabbdev](https://github.com/slabbdev) for reporting the
single-instance socket issue ([#12](https://github.com/tarwin/tinyjsapp/issues/12)).

- **Fetch with a body no longer hangs when it goes through curl.** tinyjs
  sends two kinds of request through the system curl instead of txiki.js's
  own fetch: root-path URLs such as `POST https://api.example.com/`, and
  requests where the native fetch fails outright. The body used to reach
  curl over its stdin, and txiki.js 26.6.0 never finishes a small write to
  a child's stdin
  ([saghul/txiki.js#1027](https://github.com/saghul/txiki.js/issues/1027),
  fixed upstream but not yet released). The server got the request and
  answered it, but `await fetch(...)` never returned and no error was
  thrown. Bodies now go to curl through a temp file in a private folder,
  which is deleted once curl exits. This hit macOS and Linux; Windows
  wasn't affected.
- **Linux: the single-instance socket stays private without
  `XDG_RUNTIME_DIR`.** A built app finds its running copy through a socket
  named after the app id. When `XDG_RUNTIME_DIR` wasn't set (some services
  and containers), that socket went straight into the shared temp folder.
  Another user on the machine could create it first, so the app thought it
  was already running and quit, or connect to it and send the app URLs and
  file paths to open. It now goes in a per-user `tinyjs-<uid>` folder that
  only you can access. If that folder exists and isn't yours alone, the app
  prints a warning and runs with single instance and URL/file handoff off.
  The socket itself is also owner-only now. Desktop sessions, which set
  `XDG_RUNTIME_DIR`, work as before.

## 0.42.1 — 2026-09-28

Security fixes from an outside review of the runtime. Thanks to
[@slabbdev](https://github.com/slabbdev) for the review, which came with
repros ([#11](https://github.com/tarwin/tinyjsapp/issues/11)), and for the
fixes ([#14](https://github.com/tarwin/tinyjsapp/pull/14),
[#15](https://github.com/tarwin/tinyjsapp/pull/15)).

- **`debug.get` no longer gets past the `"api"` gate.** It was always
  allowed, listed as part of the client's startup, but the client never
  calls it. It reads raw values back from the launcher, including the
  clipboard, wifi, the frontmost app, other windows and selected text. So
  under `"api": "wrapper"` a hosted page could read the clipboard, which the
  preset says it excludes. It's now gated like every other method. If an
  app with an `"api"` gate calls it from the page, add `"debug.get"` to
  `enable`. Apps without a gate aren't affected, and neither is the
  backend's `app.debug()`.
- **Fetch rejects malformed headers.** A header value containing CR or LF
  went onto the wire as extra header lines, so a page could add headers of
  its choosing to a `tiny.fetch` request. That happened on both routes: the
  curl fallback, and txiki.js's own fetch, which doesn't check either. A
  `:` in a header name let curl send a different header, `Host` included.
  Names must now be valid header names and values can't contain CR, LF or
  NUL. Anything else throws a `TypeError` before the request is sent, as in
  a browser. This covers the backend's `fetch()` too. Two smaller fixes on
  the curl route: headers passed as `[[name, value]]` arrays were misread,
  and a header with an empty value was dropped instead of sent.
- **`tinyjs build` escapes tinyjs.json values in Info.plist.** Only `url`
  and `userAgent` were escaped. A `&` or `<` in anything else, such as
  `"title": "Tom & Jerry"`, made the plist invalid: the app was signed
  without complaint, but macOS refused to open it ("executable is
  missing"). All values are now escaped, and control characters, which
  XML can't hold at all, are dropped.

## 0.42.0 — 2026-09-24

- **`tinyjs` works in Git Bash on Windows.** The Windows download shipped
  only `tinyjs.cmd`. cmd and PowerShell find it when you type `tinyjs`, but
  Git Bash doesn't try `.cmd` when it looks up a command, so there you got
  "command not found" and had to type `tinyjs.cmd`. The zip now also has
  the sh wrapper that macOS and Linux use, next to `tinyjs.cmd`: Git Bash
  runs the wrapper, cmd and PowerShell keep running `tinyjs.cmd`. To get it,
  run `tinyjs update` (from Git Bash: `tinyjs.cmd update`). A source
  checkout set up with `setup.ps1` already had the wrapper. The release
  build now runs it in Git Bash before publishing, so it can't go missing
  unnoticed again. Thanks to [@nudopnu](https://github.com/nudopnu) for the
  report and the fix ([#9](https://github.com/tarwin/tinyjsapp/issues/9),
  [#10](https://github.com/tarwin/tinyjsapp/pull/10)).
- **macOS apps for Intel Macs too.** A macOS build only carried the build
  Mac's CPU, so an app built on Apple Silicon was refused on Intel Macs
  ("not supported on this type of Mac"), and the build didn't warn you
  ([#2](https://github.com/tarwin/tinyjsapp/issues/2)). Three changes:
  - `tinyjs build` now prints which Macs the .app opens on, e.g.
    `runs on: Apple Silicon (arm64)`, and says how to reach the others.
  - `tinyjs build --arch x86_64` (or `arm64`) builds the .app for that CPU
    from any Mac. The other CPU's txiki.js comes from the same release and
    is cached in `~/Library/Caches/tinyjs`. The dmg and zip are named
    `<name>-<ver>-macos-<arch>`, so both builds can sit in one release.
    `tinyjs publish --arch arm64` followed by `--arch x86_64` merges both
    into one update manifest: a new `mac` block keyed by CPU. The arm64 build
    also stays in the top-level `url`, so apps already out there keep
    updating. The updater picks the build for the Mac it's running on. An
    Intel build running on Apple Silicon under Rosetta moves to the native
    one.
  - `tinyjs build --universal` makes one .app with both CPUs instead. It's
    about 6 MB bigger and needs the Command Line Tools for `lipo`.
  Per-CPU builds are opt-in: without `--arch` or `--universal` a build
  targets the Mac it's built on, as before. Thanks to
  [@akii09](https://github.com/akii09) for the build report
  ([#6](https://github.com/tarwin/tinyjsapp/pull/6)) and `--universal`
  ([#7](https://github.com/tarwin/tinyjsapp/pull/7)), and for finding that
  the bundled txiki.js needs macOS 15, not the documented 14. The docs now
  say macOS 15+.

- **`onLocale` works.** The backend hook existed, but the generated app
  entry never passed it on, so exporting `onLocale` did nothing.
- **Windows `capabilities()` owns up to `proxyURL` and `setAllSpaces`.**
  Both quietly do nothing there, but the capabilities table left them out,
  so `caps.x !== false` read them as supported.
- **The agent skill's release reference ships again.** A case-insensitive
  `.gitignore` rule meant for the root `RELEASE.md` also ignored
  `skill/references/release.md`, so SKILL.md linked to a file new projects
  never got. The skill is also brought up to date: current version, the
  full list of backend hooks, the page-to-backend name map, Windows'
  real toast notifications, and several wrong examples fixed.
- **Docs refresh.** The docs page gains the tinyjs.json keys, CLI commands
  and APIs it was missing (`debug`, `readAccess`, `windowPlacement`, dialog
  `types`, `role: 'app'`, `tiny.fileURL`, `mouseTracking`, `requirements`,
  `tinyjs version`/`uninstall`, and more), and drops per-feature macOS
  version notes that no longer mean anything now that apps need macOS 15.
  pickColor and spotlight are correctly marked as not on Windows yet, and
  the launcher's size is corrected to 1–2 MB.

## 0.41.1 — 2026-09-23

- **`tinyjs new --template` finishes again with create-vite 9.** create-vite
  now asks "Install with npm and start now?" after scaffolding. npm's `--yes`
  doesn't answer it, since that flag only covers npm's own prompts. Saying yes
  started a Vite dev server that never exits, so tinyjs never got to add its
  own files: the project had no `tinyjs.json`, `backend/`, types, icon or
  agent skill. tinyjs now passes `--no-interactive --no-immediate`, and
  create-vite scaffolds and exits without asking. Thanks to
  [@favasconcelos](https://github.com/favasconcelos) for the report, which
  arrived with the root cause and the verified fix
  ([#5](https://github.com/tarwin/tinyjsapp/issues/5)).
- **Fetch ids wrap instead of saturating.** The client's per-fetch counter is
  now a `Uint32Array`, so it wraps after 2³² fetches. A plain number stops
  increasing at `Number.MAX_SAFE_INTEGER`, after which every fetch would get
  the same id. No app will reach either limit, but the wrap is the safer
  behavior. Thanks to [@WebReflection](https://github.com/WebReflection)
  ([#4](https://github.com/tarwin/tinyjsapp/pull/4)), the first outside
  contribution to tinyjs.

## 0.41.0 — 2026-09-22

- **Your own items in the Edit menu.** `{ role: 'edit', items: [...] }` now
  appends those items to the standard macOS Edit menu, below Select All and
  after a separator, where Find… and friends belong. Before this the role
  ignored its items, and declaring a separate `{ title: 'Edit' }` gave the bar
  two Edit menus. The items behave like any other: ids, key equivalents,
  ticks, `menu.update` / `menu.get`, clicks through `tiny.menu.on`. Windows and
  Linux have no stock Edit menu (their webviews handle Ctrl+C/V themselves),
  so there the items alone become an **Edit** menu in the declared slot. A
  bare `{ role: 'edit' }` still draws nothing on those platforms. Only the
  first `edit` block counts, as with `app`.
- **Choose which stock Edit items appear, and where, on every platform.**
  Put `{ role: 'undo' }`, `redo`, `cut`, `copy`, `paste`, `selectAll`, or
  `{ role: 'standard' }` for the whole group, inside `items`. Once any of them
  is there, you set the order and nothing is added for you. For example,
  `[{ id: 'find', … }, { separator: true }, { role: 'standard' }]` puts your
  items above the stock ones. Stock roles render on all three platforms, so
  one declaration gives the same Edit menu on macOS, Windows and Linux. On
  Windows and Linux the items show their shortcut (Ctrl+C) without claiming
  it, so the webview keeps handling the keys in text fields. Linux runs
  WebKitGTK's editing commands and greys items out when they don't apply;
  Windows replays the shortcut into WebView2, and its items are always
  enabled. Roles work in `tiny.menu.setContext` too, and the tray skips them.
  This is the Electron model (Tauri's `PredefinedMenuItem` is the same idea).
- **`standard: false` (macOS) drops the implicit stock items**, and with no
  `items` either there is no Edit menu. Unlike Tauri, dropping Copy/Paste no
  longer breaks ⌘C/⌘V in text fields: the launcher handles
  ⌘C/⌘V/⌘X/⌘A/⌘Z/⌘⇧Z for any shortcut the menu bar doesn't claim, and an
  app item bound to one of them still wins. A menu holding stock items greys
  Copy out when nothing is selected, and an app item's `enabled: false` still
  holds in it. Windows and Linux add nothing implicitly, so the flag has
  nothing to remove there.

## 0.40.0 — 2026-08-24

- **No display now fails fast, loud, and diagnosably — on all three
  platforms.** An agent (or CI job) running where no window can exist —
  headless Linux, an SSH'd or sandboxed Mac, a Windows service session —
  used to see the launcher die with an opaque message or nothing at all,
  indistinguishable from an app bug (one tester burned real time blaming
  their npm cache). Each launcher now checks its environment before
  connecting to the backend: Linux for `DISPLAY`/`WAYLAND_DISPLAY` +
  `gtk_init_check`, macOS for a window-server session (SSH/CI/sandboxes
  that block WindowServer), Windows for a visible window station. All
  print a `tinyjs:`-prefixed explanation — saying the app code is likely
  fine and what kind of environment can show the window — and exit 3
  before the socket connect, so the bridge reports "launcher exited
  before connecting" instead of hanging on a dead peer.
- **Docs: the `tjs.*`-async / `tjs:sqlite`-sync footguns are now written
  down.** Every `tjs.*` fs call returns a Promise while sqlite is fully
  synchronous — an unawaited `makeDir` before `new Database(...)` is a
  race. The sqlite examples (README, docs, agent skill) now show the
  `await`, and the better-sqlite3 differences are spelled out: statements
  have only `run()`/`all()`/`finalize()` (no `get()` — use `.all()[0]`),
  `run()` returns void, and the last-insert-id idioms
  (`INSERT … RETURNING id` / `last_insert_rowid()`) are documented and
  verified against the shipped runtime.

## 0.39.1 — 2026-08-18

- **macOS: a frameless secondary window is really frameless.** `windowControls`
  rides the wire as a TOKEN — `all`, `none`, or a comma list — so the
  `['close','minimize']` form survives, but the titlebar-hiding branch tested it
  as a bit (`== "0"`), which is never true. So every window opened by
  `app.openWindow` with `chrome: { frame: false, windowControls: false }` kept a
  live titlebar container: invisible, but its rounded top edge drew over the
  page and it swallowed the top strip's mouse events. The main window was never
  affected — it derives the same decision from its own tracked state.

## 0.39.0 — 2026-08-06

- **`{ role: 'app' }` puts items in the macOS application menu.** The one slot
  in the bar `setMenu` could not reach — and the one macOS reserves for
  **Settings… (⌘,)**, which every app is expected to put there and none built
  on tinyjs could. Items declared in an `app` role block land between About and
  Quit and behave like any other: ids, key equivalents, ticks, `menu.update` /
  `menu.get`, and clicks through `tiny.menu.on`. Windows and Linux have no
  application menu, so the role is unknown there and its items are dropped
  rather than landing somewhere arbitrary — declare the same item in a menu of
  your own for those platforms. Only the first `app` block is used.

  The id→item registry now resets at the top of the bar build rather than just
  before the custom menus, because the application menu is built first and the
  old order threw its registrations away — an item that clicked correctly (the
  id rides on `representedObject`) but that `menu.get` and `menu.update` could
  not see.

## 0.38.0 — 2026-08-06

- **Wrap a hosted web app as a real desktop app.** `"url": "https://…"` in
  tinyjs.json makes the main window a remote page (no local frontend
  needed), and the things a *browser* does — which a local-page app never
  missed — now exist on all three platforms: `alert`/`confirm`/`prompt` get
  native panels headlined by the page's origin (`confirm()` used to return
  `false` silently, a permanent "Cancel" for any site guarding a
  destructive action); downloads actually download (`"downloads"`: `auto`
  to ~/Downloads with de-duplicated names, `ask` for a save panel, or
  `deny`) with start/progress/done events; `window.open` is configurable
  (`"popups"`: `external` to the real browser, `window`, or `deny`);
  navigation start/commit/finish/fail/crash reach `onNavigate`, which can
  also *veto* a main-frame navigation or send it to the browser; and
  `tiny.win.find()` searches the page.
- **`"api"` — a capability gate, so a wrapped site isn't handed the
  machine.** `tiny` is injected into every origin, so without this a
  third-party page holds an RPC channel to a backend with full filesystem
  and process access. Now: `"api": "wrapper"` for the obvious posture,
  `{disable, enable}` name lists (enable wins), or **per-origin keyholes** —
  `{"origins": {"file://*": "all", "https://app.example.com": ["notify",
  "store.*"]}}` — keyed off the calling frame's origin as the ENGINE
  reports it, not as the page claims it. Denied calls reject with a
  readable reason; `capabilities().api.denied` lets a page hide UI it can't
  use. Unknown preset names fail closed.
- **`"inject"`** runs a script at document-start in every page, bundled at
  build (`.ts` via esbuild) — the supported way to shim a wrapped site,
  replacing the undocumented `TINYJS_INJECT` env var, which had a silent
  8KB cap on Windows that dropped bigger bundles.
- **`prompt()` crashed the app on macOS** — the result string was released
  before WebKit read it, so any site using a prompt died. Fixed.
- Windows: `beforeunload` is no longer auto-accepted (a wrapped app with
  unsaved work navigated away silently); a download's reported `filename`
  now names the file that exists on disk rather than the engine's
  suggestion; Back/Forward survive a navigation policy ask; and
  `menu.update` no longer applies stale specs.
- Linux: `tiny.win.id` inside a popup is the popup's own id (it read
  `main`, so `app.window(tiny.win.id)` from a popup targeted the main
  window); find state no longer survives a navigation; and a download whose
  filename isn't valid UTF-8 — legal on Linux — can't put an unparseable
  line on the wire.

## 0.37.1 — 2026-08-04

- **`tinyjs dev` is quiet again.** 0.37.0's always-has-devtools change
  seeded `TINYJS_DEBUG=1` into every dev run — the same env the bridge
  reads as its message-trace gate, so dev spewed the full bridge log on
  every launch, and (`!!'0'` being truthy) `TINYJS_DEBUG=0` couldn't hush
  it. Dev now seeds a sentinel the trace ignores: devtools still arm on
  all three platforms, an explicit `TINYJS_DEBUG=1` (or `open`) traces
  exactly as documented, and `0`/`false` mean silence.

## 0.37.0 — 2026-08-02

- **Devtools are off by default — and yours to turn on.** Every shipped app
  had an end-user-reachable inspector: right-click "Inspect Element" on
  macOS and Linux, F12 devtools on Windows. Now nothing ships one unless
  tinyjs.json says `"debug": true` — which buys a launcher-owned **F12 on
  all three platforms** (plus ⌥⌘I and a Develop menu on macOS) opening the
  inspector **in its own window**, where it can't crush a small app window.
  `"debug": "open"` auto-opens one for every window at creation, child
  windows included — which were never inspectable before, on any OS.
  `tinyjs dev` always has F12, whatever the manifest says.
- **The webview's own shortcuts can't fire under your app's nose.**
  WebView2 ships a whole browser key set — Ctrl+P print preview, Ctrl+F
  find bar, Ctrl+R/F5 reload, Ctrl+O — that no native app has, and it's now
  suppressed by default (`"browserAccelerators": true` opts back in). Menu
  accelerators are untouched either way. WKWebView and WebKitGTK have no
  such set, so the flag is a no-op there.
- **A greyed-out menu item swallows its shortcut, like AppKit always has.**
  On Windows a disabled item's combo fell through to the webview — nib with
  no folder open turned Ctrl+P (Open Quickly, disabled) into a print sheet.
  The accelerator now belongs to the menu whether or not the item can fire;
  disabled just means it fires nothing.
- **Uppercase menu keys mean Shift on Linux too.** `{key:'P'}` is
  Ctrl+Shift+P everywhere else (macOS gets it free from AppKit; Windows
  already parsed it), but GTK lowercases the keyval and keeps the modifiers
  as passed — so `'P'` registered plain Ctrl+P, collided with a `'p'` item
  elsewhere in the bar, and Ctrl+Shift+P was bound to nothing. Every
  uppercase accelerator on Linux was affected; nib's Print… was eating Open
  Quickly's combo this way.
- **`app.keystroke` can send function keys on Linux.** `'F12'` parsed to
  NoSymbol (the combo parser lowercases and XStringToKeysym is
  case-sensitive) and answered `ok:false`; F-keys now map to their keysyms
  directly, matching macOS and Windows.

## 0.36.0 — 2026-08-01

- **The About menu item can be yours.** `"about": "menu"` in tinyjs.json
  turns macOS's About item into an ordinary menu click — it arrives in
  `tiny.menu.on` / the backend's `onMenu` with the reserved id `about`
  instead of showing the standard panel, so an app can draw its own. Opt-in:
  apps that don't declare it keep the free panel. Windows and Linux have no
  default About item, so the flag changes nothing there.
- **`win.open({ parent })` keeps a window above your own.** `parent: true`
  (= main) or another window's id uses the native relation on each OS —
  child window on macOS, Win32 owner, GTK transient — so an about box or
  tool panel stays over the window it belongs to *without* floating over
  other apps the way `setLevel('floating')` does. It also minimizes and
  hides with its parent, closes when the parent closes, and gets no taskbar
  button on Windows (GTK sets no skip-taskbar hint, so Linux taskbars may
  still list it). Open-time only, like `transparent` — Win32 ownership can't
  be set after creation. One cross-OS difference, documented rather than
  fought: macOS child windows also move with their parent. Verified on all
  three OSes, including that closing a parent destroys its children and
  `WINCLOSED` arrives for every one of them.
- **A throw in an app's own handler no longer takes the whole app down.**
  An exception (or an async rejection) from any exported handler —
  `onWindowClosed`, `onMenu`, … — propagated out of the bridge's read loop
  and killed it: no further events, no api answers, and nothing on screen to
  say why. Every handler call site now logs the failing handler by name and
  carries on; verified with handlers that throw and reject on purpose.
- **`saveFile({ types })` appends the extension on Linux too.** 0.35.0 shipped
  that as a Windows-only nicety — `SetDefaultExtension` there, nothing on
  Linux, because GTK has no equivalent — so the same call answered
  `…/untitled-note` on one OS and `…/untitled-note.md` on the other, and an
  app that trusted the extension wrote a file the picker had just filtered
  out. The Linux chooser now appends the first declared extension by hand,
  and only where it can't surprise anyone: the name must carry no extension
  at all (`notes.md` is left alone, never doubled), the type filter rather
  than "All files" must be the one selected, and a call with no `types`
  appends nothing. One difference from Windows remains and is unavoidable
  without reimplementing the dialog: GTK's overwrite confirmation runs before
  the extension goes on, so it prompts about the name as typed.
- **`tiny.win.restore()` really un-minimizes on Linux.** It was a bare
  `gtk_window_deiconify()`, which Mutter drops on the floor when the client
  has no user timestamp to prove the request came from a person — the window
  stayed minimized and merely started demanding attention, while the call
  resolved `true`. Nothing in an app's own logs could tell that apart from a
  working restore. It now deiconifies and presents, with a real server
  timestamp on X11, and the `win.onState` listener sees the `minimized:false`
  it should always have seen.

## 0.35.0 — 2026-07-31

- **File pickers take a type filter.** `tiny.dialog.openFile({ types:
  ['md', 'markdown', 'txt'] })` — and the same on `openFiles` and
  `saveFile` — limits what's choosable, mapped natively on each OS:
  `allowedContentTypes` on macOS, a `COMDLG_FILTERSPEC` on Windows,
  `GtkFileFilter` on Linux. Windows and Linux keep an explicit "All files"
  entry so a filter never hard-hides the disk; `saveFile` on Windows also
  appends the first extension by default. Extensions only, dots and case
  forgiven; omit `types` and nothing changes. Filtering is always explicit
  and per-call — deliberately not defaulted from the manifest's
  `fileExtensions`, so a picker never silently narrows because of an
  unrelated file-association declaration.

## 0.34.1 — 2026-07-31

- **Packaged macOS apps keep their declared chrome.** A built app with
  `windowControls: false` or `squareCorners` booted titled, traffic lights
  and all, while `tinyjs dev` and the bare binary looked right — amp
  shipped that way twice before anyone caught it. Two independent bugs,
  either one sufficient: the build wrote the plist chrome from
  `trafficLights`, the pre-0.30 name of `windowControls`, so the
  buttons-off request never reached any packaged build; and bundle startup
  applied the plist chrome and then called a set-size that rewrites the
  styleMask wholesale, snapping the window straight back to titled. Dev
  never showed either, because the bridge re-sends chrome over the socket
  after boot — packaged apps have only the plist, so the wipe stuck. The
  plist now carries the same tokens the socket does, and startup re-applies
  it after the size call in the same runloop turn, so nothing ever paints
  titled.

## 0.34.0 — 2026-07-31

- **`tiny.audio.sampler` — sampled sound effects that work everywhere,
  including Linux.** A bank of short decoded sounds (wav/mp3/flac guaranteed)
  fired with per-voice volume, pan and pitch, mixed into one output — game and
  UI SFX, the coo3d workload. `await s.load('coo', path)` (or an ArrayBuffer —
  spilled to the app cache once, never streamed over the wire), `const v =
  await s.play('coo', { vol, pan, rate, loop })`, then `v.set({ pan })` live
  and `v.stop()` with a click-free fade; `s.master(v)`, `s.unload(name)`. 32
  voices, oldest-stolen — `play()` never fails for "too many". Backend twin
  `app.audio.sampler` drives the SAME mixer, one state owner (the bridge), so
  every window and `main.js` agree. On macOS/Windows it mixes in the main
  window's page via Web Audio (already RT-scheduled there; the main window
  always exists and can't close, and a page reload re-arms the bank by itself
  — playing voices die, which is the documented cost). On Linux, where Web
  Audio reaching `ctx.destination` crackles under WebKitGTK (measured, the
  reason `tiny.audio.filters` is native too), the LAUNCHER decodes with
  miniaudio and mixes in a PipeWire `pw_stream` on the RT data loop — the
  page can be melting under rAF load and the audio doesn't care (measured:
  20 live-automated voices, ERR 0, ~240µs/quantum). Same equal-power pan law
  as `StereoPanner` on all three, verified against a null-sink capture to
  four decimal places on Linux. Launcher decode also means SFX can't be
  broken by missing GStreamer plugins, and an active `tiny.audio.filters`
  chain picks the sampler up like everything else the app plays.
  `capabilities().sampler` says `'native'` or `'page'`, but apps shouldn't
  care — the API is identical. Out of scope by design: sample-accurate
  scheduling, per-voice filters, music (that's `<audio>`).

- **The agent skill is rebuilt: a thin core plus on-demand references.**
  `tinyjs new` used to drop a single 635-line SKILL.md into the project;
  a coding agent loaded all of it for every task. It's now a 156-line core
  with a routing table and six reference files the agent reads only when
  the job calls for them — the full API tour, per-OS truths, app-kind
  recipes (tray, desktop pet, media, document, wrapper), shipping/notarize/
  auto-update, an Electron porting map, and page-vs-backend performance
  rules. The content was re-audited against the docs, which caught real
  drift: the old skill still said Linux needed Ubuntu 24.04+ (the floor is
  22.04/Debian 12), called builds Apple-Silicon-only (they're universal),
  and had `app.badge` unsupported on Linux (it works on LauncherEntry
  docks). Scaffolded projects get the whole skill dir now, not one file.

## 0.33.0 — 2026-07-31

- **Linux downloads load on Ubuntu 22.04 and Debian 12 again.** A linked
  binary requires the newest glibc symbol versions its build machine offers,
  and the Linux release builds ran on Ubuntu 24.04 — so every download
  quietly demanded glibc 2.38 and refused to load on anything older
  (``version `GLIBC_2.38' not found``: Ubuntu 22.04, Debian 12, Mint 21,
  Pop!_OS 22.04). CI builds on Ubuntu 22.04 runners now; the floor is glibc
  2.35, which those distros and everything newer all clear. And because a
  raised floor is not a build error — the build stays green and the break
  only ever shows on a user's machine — the release now asserts the ABI
  floor, the architecture, and every optionally-linked feature on both
  arches before anything is packaged. That last check is earning its keep
  already: amp 0.8.0's arm64 launcher linked no AppIndicator, so its tray
  was silently dead on arm64 while working on x86_64. Both arches ship
  complete from here on, and none of this can regress without turning the
  build red.

- **Wayland mouseTracking is actually in the Linux downloads now.** Unlike
  the EQ, which spawns PipeWire's command-line tools and can therefore ask
  the user to install what's missing, mouseTracking consumes the ScreenCast
  portal's stream in-process — the PipeWire library is compiled into the
  launcher, or the feature doesn't exist in that binary at all. setup.sh
  treats the dev package as optional and skips it quietly, the CI machines
  didn't have it, and so every released launcher shipped with
  `mouseTracking.start()` answering `unsupported`. The build machines
  install it now, the release check refuses a launcher without PipeWire
  linked in, and the one piece a user's machine can genuinely be missing —
  a ScreenCast-capable portal — joined `tiny.system.requirements` as
  `'mouseTracking'`, so `promptMissing` can put the fix in front of the
  user the same way it does for codecs.

- **Linux: opening a file into an already-running app can't silently drop
  the payload.** The single-instance handoff wrote the deep-link / file-open
  line to the socket once and ignored the result, so a partial or failed
  write reported success while the running app never got the whole message.
  It now loops until the line is fully sent and falls back to spawning the
  app when it can't be.

## 0.32.0 — 2026-07-30

- **Lost windows come back.** Apps restore saved positions blindly, and
  coordinates from an unplugged external display land a window in empty space
  nobody can see or grab. The launchers now know how to rescue one — clamp it
  onto the nearest screen, titlebar first — and the bridge decides when: it
  fingerprints the screen layout per app, and only a boot whose layout
  CHANGED since the last run arms the rescue, chasing each window's first
  show / first position once. Unchanged screens (every ordinary day) mean
  zero interference — apps that fling windows off-screen on purpose (coo3d's
  pigeons) never feel a thing. A display departing mid-session sweeps the
  visible windows too. Automatic parts opt out with `"offscreenRescue":
  false` in tinyjs.json; `tiny.win.ensureOnScreen()` (backend:
  `app.window(id).ensureOnScreen()`) is the manual one-shot that always
  works. Windows deliberately parked half-off-screen keep more than a sliver
  visible and are never touched.

- **Mission Control shows your windows now (macOS).** A window without the
  Titled style bit is silently excluded from Mission Control / Exposé, so
  every `squareCorners` app was invisible to the four-finger swipe.
  Membership now follows what the window IS: normal and floating windows
  participate like documents; desktop-level windows (wallpaper pets),
  overlay-level ones (draw-on-screen HUDs) and click-through windows — which
  couldn't take the click Exposé would hand them — stay out. Re-derived
  whenever level, click-through or squareness changes, so call order doesn't
  matter, and nothing to change in any app.

## 0.31.0 — 2026-07-30

- **Window-state transitions are events now.** `tiny.win.onState(fn)` fires
  with `{ win, fullscreen, maximized, minimized, focused }` on every
  transition, whatever caused it — the green button, a menu item, F11, or a
  programmatic `setFullscreen` — so nobody has to fake it by debouncing
  `resize` and re-reading `getState()` (which cannot tell a fullscreen
  transition from a manual resize to screen size). Same vocabulary as
  `getState()`, deduped in the launcher so live-resize spam never crosses the
  wire, broadcast to every window with the id attached. Backend twin:
  `export function onWindowState(info, app)`. Like every `on…` since
  removable events landed, it returns its own unsubscribe. Wayland never
  reports `minimized` — the compositor keeps minimize private, same honesty
  rule as `mousePosition`.

- **The traffic lights can move (macOS).** `chrome.windowControlsPos:
  { x, y }` — offset from the window's top-left — recenters the
  close/minimize/zoom group in a taller custom titlebar, the thing every
  frameless app with its own header bar eventually wants. Works in
  `tinyjs.json "chrome"` (applied before first paint), `setChrome`, and
  `win.open`'s per-window chrome; `null` restores the OS layout, and
  `getState().chrome` reports it back. AppKit re-lays-out the buttons on
  resizes, fullscreen round-trips and titlebar rebuilds, so the launcher
  re-applies the offset on those transitions — set it once. Ignored on
  Windows and Linux, whose window buttons live in a real titlebar.

## 0.30.0 — 2026-07-30

- **`setHideOnClose` no longer strands the app on Windows and Linux.** It is a
  macOS idea — there an app outlives its last window and the Dock icon brings
  it back. Neither other platform has anywhere to put that: a hidden window
  takes its taskbar button with it, so closing the last window of an app that
  set the flag left a process the user could neither see nor quit (nib did
  exactly this; `tinyjs dev` never returned). The flag is now honoured only
  when there IS a way back — a tray icon, accessory mode, or another window
  still on screen. Otherwise the close means what it means everywhere else on
  that OS and the app exits. macOS is unchanged, tray apps are unchanged, a
  programmatic `win.hide()` still hides whatever it is told to, and closing
  the last *secondary* is left alone so an app that answers it by re-showing
  its main window (nib's Welcome screen) still can.

- **The menu bar shows in EVERY window.** It always did on macOS, which has
  one bar for the whole app — but Windows and Linux draw the bar inside a
  window, and the launchers only ever put it on `main`. A multi-window app
  therefore lost its menu in exactly the windows that needed it: on Windows
  nib's document windows had no File menu at all, while the Welcome screen
  carried one it had no use for. `tiny.menu.set` is now the APP menu — every
  window shows a copy, including windows opened later — and `menu.update`
  moves every copy of an id at once, which is what one shared macOS bar
  already did. Nothing to change in an existing app; the menu simply appears
  where it was missing.

  Two new ways to differ per window:

  - **`tiny.win.menu.set/update/reset`** (backend: `app.window(id).setMenu`,
    `.updateMenuItem`, `.resetMenu`) — this window says something else, and
    only this window. Clicks arrive as the same `menu` event either way, so a
    handler doesn't have to care which bar sent them. macOS swaps the bar in
    while that window is key and puts the app menu back when it isn't, so a
    per-window menu means the same thing on all three platforms.
  - **`chrome.menu: false`** — this window shows no bar; the app menu carries
    on everywhere else. Whether a bar shows is chrome, what's in it is menu.
    Declare it in tinyjs.json `"chrome"` or `win.open`'s chrome to apply
    before first paint, or flip it live with `setChrome({ menu: false })`.
    Accelerators keep firing with no bar showing. macOS ignores it — a
    bar-less mac app isn't a thing — and frameless or transparent windows
    never draw one on Windows, where the bar is GDI.

  Four Windows fixes rode along. **A page never had keyboard focus until the
  user clicked in it.** WebView2's child window doesn't take focus when the
  host window does, so `document.hasFocus()` answered `false` from launch —
  and an app that gates on it (nib ignores menu events unless its own window
  is the focused one, the standard multi-window pattern) dropped every
  page-side menu action until you happened to click the document first. Menu
  items answered by the backend worked, ones answered by the page did nothing,
  which is a maddening thing to debug. The launcher now moves focus into the
  page when a window is created and on every `WM_SETFOCUS`, matching macOS,
  where a new window's webview is first responder immediately. **A menu
  declared immediately before
  `{ role: 'edit' }` came out empty** — the launcher flushed its items into
  place at the role line and then, because the role slot claimed no entry of
  its own, flushed an empty list back over them at the next menu. nib's File
  menu is exactly that shape and had no items at all on Windows. `size` now
  means the page's box whether or not the window has a menu bar: attaching one
  used to silently shorten the page by its row (Linux had already made the
  same repair for its in-window bar). And secondary windows no longer drop the
  GDI redirection bitmap unconditionally — only transparent and frameless ones
  do, since a window without one cannot draw a menu bar at all.

**Breaking.** The Dock/taskbar/launcher calls are renamed, two macOS-only
calls move to their own namespace, and `haptic` is removed outright. Old names
are gone rather than deprecated.

- **The app surface is verbs named for intent, not macOS furniture.** The Dock,
  the Windows taskbar and the Linux launcher are surfaces the OS owns and an
  app decorates — there is no shared object to hang a namespace off:

  | before | after |
  | --- | --- |
  | `app.dock.setBadge(t)` | `app.badge(t)` |
  | `app.dock.bounce(o)` | `app.attention(o)` |
  | `app.dockIcon(p)` | `app.icon(p)` |
  | `app.setDockVisible(v)` | `app.presence('normal' \| 'menubar')` |

  Same names on both sides of the bridge, so `main.js` and the page agree.
- **`app.progress(0..1)`** — new. A determinate bar on the app icon; `null`
  clears. macOS draws it into the Dock tile, Windows uses `ITaskbarList3`,
  Linux emits the Unity `LauncherEntry` signal (KDE Plasma, Ubuntu Dock and
  Dash-to-Dock listen; vanilla GNOME Shell doesn't). On macOS it composes with
  `app.icon()` — a custom Dock tile keeps its icon while the bar is up.
- **`tiny.macos.*` holds everything macOS-only** (and `app.macos.*` on the
  backend). `applescript` and `quickLook` were joined by `ocr`, `recorder`,
  `ai`, `selectedText`, `otherWindows` and `moveWindow` — every `tiny.app`
  call that answers `'unsupported'` on both other platforms. The namespace is
  now the honest signal it looked like: if it's on `tiny.app`, it does
  something somewhere other than macOS.

  Calling `tiny.macos.*` off macOS **rejects** with the reason instead of
  resolving something empty. That's a behaviour change for the moved calls, not
  just a rename: `otherWindows()` used to answer `null` on Windows and Linux
  and now throws, so a `if (!wins)` branch becomes a `try`. Deliberate — a call
  that cannot work on the platform you're running is a bug in the app, and a
  `null` that means "wrong OS" is indistinguishable from one that means
  "nothing selected".

  If any of these grows a Windows or Linux implementation it moves back to
  `tiny.app` with the `tiny.macos` name kept working alongside it for a
  release or two, so that direction won't break anyone overnight.

  Three stayed put on purpose. `win.share` is **window-scoped** — it anchors
  the sheet to a specific window, and window-scoped calls belong on `tiny.win`
  even when they're macOS-only. `authenticate` also works on Windows, and
  `spotlight`, `pickColor` and `nowPlaying`/media keys also work on Linux —
  none of those are macOS-only at all, whatever the deck's old grouping
  implied.
- **`haptic` is removed** (was `app.haptic`, briefly `tiny.macos.haptic`).
  `NSHapticFeedbackManager` only fires while a finger is resting on a Force
  Touch trackpad and the "Force Click and haptic feedback" setting is on, so
  the honest description of the call is that most presses of it — every one
  driven by a mouse, and every one on a desktop Mac — do nothing at all. An API
  whose normal outcome is silence is indistinguishable from an unimplemented
  one, which is the exact confusion this release spent its time removing
  elsewhere. Nothing replaces it; `tiny.app.playSound()` is the portable way to
  acknowledge an action.

- **`battery`, `wifi` and `idleTime` move to `tiny.system.*`.** They're facts
  about the machine, not things the app does — the same line that decided the
  app surface belongs on `tiny.app`. Mirrored on the backend as
  `app.system.*`. (`tiny.system` also gained TypeScript definitions, which it
  never had at all.)
- **Native dialogs move to `tiny.dialog.*`** — `openFile`, `openFiles`,
  `pickFolder`, `saveFile`, `alert`, `confirm`, `prompt`. They were on
  `tiny.win.*`, but nothing about them is window-scoped: the launcher runs
  them application-modally, so a dialog from a satellite behaved exactly like
  one from main. All three platforms use their real native dialogs.
- **Breaking: `chrome.trafficLights` → `chrome.windowControls`, and it's
  cross-platform now.** The macOS nickname described a group every OS has, so
  the key is named for the thing rather than the Mac's word for it. It takes
  `true`, `false`, a subset array like `['close', 'minimize']`, or `[]` for
  none — macOS hides each button individually, Windows maps them onto
  `WS_SYSMENU`/`WS_MINIMIZEBOX`/`WS_MAXIMIZEBOX`, Linux uses
  `gtk_window_set_deletable` plus an `_MOTIF_WM_HINTS` request the WM may
  ignore. `getState().chrome.windowControls` reports the array that is
  actually shown (read off the buttons on macOS and the styles on Windows),
  and `null` on Linux, where the WM owns the decision and won't say.
  Previously the bit was macOS-only in effect but stored and echoed back on
  Linux, so an app that set it and read it back was told it had worked.
- **Breaking: a window's size is the page's box, everywhere.** `win.open({
  size })`, `win.setSize()`, `setMinSize()` and `getState().width/height` used
  to be up to three different units depending on the platform and the window.
  Creation took a content size on all three, but `setSize` and `getState`
  spoke *frame* units on macOS and Windows and content units on Linux. What
  that looked like: an app declaring `1100x720` got a **688-tall page on
  macOS** and a 720-tall one on Windows and Linux, the title bar eaten out of
  the document; `open({ size: '150x150', chrome: { frame: false } })` produced
  a **150x182** page, because dropping the title bar handed those points to
  the page; and on Linux the first menu bar to appear took its own height out
  of the document the same way. All of them now mean the page's own box on all
  three platforms, so reading a window's size and handing it straight back to
  `setSize` is a no-op — which is what the old frame-units workaround was
  chasing, having fixed the round-trip by moving both ends away from the unit
  creation used.
- **`getState().outer`** — new: `{ width, height }`, the window's footprint on
  screen with decorations included. `width`/`height` are the page; `outer` is
  what the screen sees, and for a frameless window the two are equal. A window
  that keeps itself inside a screen rect needs the outer number, and the page
  cannot work it out alone — `window.outerWidth`/`outerHeight` are `0` in a
  WKWebView. On Linux it comes from the WM's frame extents, so under Wayland,
  which has no server-side frame to ask about, expect it to equal the page box.
- **`{ role: 'edit' }` places the standard Edit menu.** macOS installs an Edit
  menu whether an app declares one or not — the webview needs its key
  equivalents for ⌘C/⌘V to work at all — and it always went first, so a custom
  File menu was stuck sitting *after* Edit, which no Mac app does. Put
  `{ role: 'edit' }` in the `setMenu` array and the standard menu takes that
  slot instead:

  ```js
  app.setMenu([{ title: 'File', items: [...] }, { role: 'edit' },
               { title: 'View', items: [...] }]);
  ```

  Omit it and nothing changes. Windows and Linux have no launcher-owned menu,
  so they skip the entry and keep the order the app declared.
- **`minTinyjsVersion` in `tinyjs.json`.** Optional: the oldest tinyjs an app
  works with. Running it on an older one now fails with
  `myapp needs tinyjs 0.30.0 or newer — you have 0.28.3` instead of the app
  half-working, which is what an app calling an API its runtime predates
  actually looks like — an unexplained TypeError in the page with nothing
  naming the cause. `tinyjs new` stamps the version it scaffolded with; a
  source checkout reports `dev` and is never blocked.
- **Per-OS config blocks.** Root keys in `tinyjs.json` apply everywhere; an
  optional `macos` / `windows` / `linux` block merges on top for that platform,
  so only the keys that genuinely differ get repeated. Plain objects merge,
  scalars and arrays replace. Block names match `tiny.system.os()`.
- **`TINYJS_SIGN_IDENTITY` / `TINYJS_NOTARY_PROFILE` now override
  `tinyjs.json`** rather than only filling a gap — but never silently: if one
  displaces a value that was really in the file, the build says so. Resolution
  order is root → OS block → env.
- **`tiny.win.print()` and `tiny.win.printToPDF()` print the window that
  called them.** Both went straight to the main window, whichever page asked —
  so in a multi-window app the document window's ⌘P printed the launcher
  screen, and Save as PDF wrote a PDF of it. They now route by calling window
  like every other `win.*` call (`PRINT@<id>` / `PDF@<id>` on the wire), on all
  three platforms, and `app.window(id).print()` / `.printToPDF(path)` expose
  the same thing to the backend. Single-window apps see no change.
- **`capabilities()` stopped over-claiming**, on two platforms. Windows
  declared none of the app-surface keys while silently no-opping them, which
  under the table's "absent = true" rule claimed support that never existed.
  On Linux, `app.icon`, `app.attention` and `app.presence` emit *zero bytes* of
  Wayland protocol — GTK3's Wayland backend has nowhere to put a window icon,
  an urgency bit or a skip-taskbar hint — so all three are now gated on X11 and
  report honestly per session. Badge/progress detection is a bus-name probe
  rather than an `XDG_CURRENT_DESKTOP` guess, which was wrong in both
  directions.

  A second pass (2026-07-27) found six more of exactly the same shape, by
  checking every capability name against what the launcher actually dispatches:
  **Windows** claimed `otherWindows`, `moveOtherWindows`, `pickColor`,
  `spotlight` and `mediaKeys` — the first falls through the launcher's `GET` to
  `null`, the next three reach `got_unsupported`, and nothing in
  `launcher-win.cc` has ever written a media-key line, so `onMediaKey` could
  not fire. **Linux** claimed `otherWindows`, `selectedText` and
  `moveOtherWindows` on X11; the first two have no `GET` arm at all and
  `WINCTRL` already said "moving other apps' windows isn't supported on Linux".
  X11 could support all three — nobody has written it, which is a different
  statement from the one the table was making.

- **`"permissions": { "speechRecognition": "why" }`** — a new `tinyjs.json`
  key, injecting `NSSpeechRecognitionUsageDescription`. Speech-to-text isn't a
  `tiny.*` call: the page's own `webkitSpeechRecognition` already works (WebKit
  has it, and so does WebView2), but a bundled app needs that usage string as
  well as the microphone one, and without it the OS refuses the *service*
  rather than the mic — the page sees `service-not-allowed` with no prompt to
  accept and nothing to click. Measured: adding the key alone turned that error
  into `start` + `audiostart` on an otherwise identical build, and a human
  accepting the prompt confirmed real transcription. It also implies the
  `com.apple.security.device.audio-input` entitlement under the hardened
  runtime, same as `microphone`.

  Pairs with `macos.ai` and `app.say()` for a hands-free loop — listen,
  generate, speak — which is what the deck's AI card now demonstrates.

- **`macos.ai.generate` can call your functions.** Pass
  `tools: [{ name, description, parameters, run }]` and the on-device model
  decides which to invoke; `run(args)` is ordinary JavaScript and whatever it
  returns goes back to the model. Backend only — a tool's `run` is a real
  function, so it can't cross the bridge from a page.

  Tools are declared in JS, not Swift, which needed the schema to be built at
  *runtime*: `DynamicGenerationSchema` + an untyped `GeneratedContent`
  argument, rather than the compile-time `@Generable` struct the framework's
  examples use. The call round-trips Swift → a blocking C hop → the socket →
  your handler → back, with a 20s deadline so a handler that never answers
  can't wedge generation.

  **With tools it resolves `{ text, calls }`, and `calls` is the record.**
  This matters more than the plumbing: asked for three tool calls in one turn,
  the model made all three in **one run out of four** — and its prose claimed
  all three **every** time, including the runs where it silently skipped one.
  It passed a wrong argument once too. Never read completion off `text`, and
  put anything irreversible behind a confirmation rather than a hopeful read
  of a sentence.

- **`win.hide({ app: false })`** — put one window away without hiding the app.
  A plain `hide()` on the main window is `[NSApp hide:]`, which is right for a
  palette (focus goes back to whatever you were in) and wrong for an app whose
  main window is a launcher screen: putting it away while its document windows
  are up deactivated the app around them. Secondary windows always ordered out,
  so this only changes what `'main'` can do, and only when asked. Windows and
  Linux hides were already window-scoped; the flag is a no-op there.

- **Menu keys can carry other modifiers** — a menu `key` was ⌘ plus the
  character, with an uppercase letter meaning ⌘⇧, and no way at all to say ⌥⌘P
  or ⌃⌘K. Now anything extra is a prefix: `key: 'alt+p'` is ⌥⌘P, `'alt+shift+f'`
  is ⌥⇧⌘F, and `ctrl+`/`shift+` spell themselves (`cmd+` is accepted and
  redundant — ⌘ is always in, since a menu bar without it isn't one). Same
  grammar on all three platforms, with ⌘ reading as Ctrl on Windows and Linux;
  Windows also stopped firing an accelerator when Shift was held but not
  declared, which made `'F'` and `'f'` the same key there.

- **Files on the command line reach `onOpenFiles`.** Nothing read process argv
  before, so `myapp notes.md` from a terminal was a launch with a document
  silently dropped. Now argv is parsed on every platform at startup — flags
  skipped, relative paths resolved against the cwd, and anything that isn't an
  existing path dropped rather than guessed at, so `myapp --verbose` doesn't
  hand the app a document called `--verbose`. On Windows and Linux a second
  launch forwards its documents over the instance pipe to the running copy
  instead of only waking it.

  The backend hook is the reliable half: `onOpenFiles` fires immediately. The
  page event is best-effort, exactly as `OPENFILES` from LaunchServices always
  was — there is no page-loaded signal to wait on.

- **Fixed: every built macOS app was told to open its own plumbing.** AppKit
  hands a process's whole command line to `application:openFiles:` during
  launch, so an app with an `onOpenFiles` handler received its own
  `frontend/index.html` and `app.sock` — and, once those were filtered, its own
  title and `"960x640"` — as documents the user had opened. On every launch,
  since the handler existed. The launcher now remembers its own argv and drops
  it. Found while testing the argv work, not by looking for it.

- **`"openFolders": true`** — a folder document type: `public.folder` in
  `CFBundleDocumentTypes`, `inode/directory` in the `.desktop` `MimeType=`. A
  folder has no extension to match, so it needed saying separately rather than
  being another `fileExtensions` entry.

- **`tinyjs build --cli [name]`** — writes a shim beside the app, since only
  the build knows where the executable lands. It goes in `dist/bin/`, not
  `dist/`, because the bare executable is already `dist/<name>` and a
  same-named shim overwrites it. On macOS it targets that bare binary, NOT the
  `.app`: the bundle's main executable is the launcher, which parses argv as
  `<html> <socket>` and dies on a file path. Caveat: the bare binary has no
  single-instance pipe on macOS, so running the shim while the app is open
  starts a second copy — cold start is what this covers well.

- **`app.setAsDefaultHandler(ext)`** — `'ok' | 'unsupported' | 'failed'`.
  Linux runs `xdg-mime default` for the app's own mime (or `inode/directory`
  for `'folder'`); macOS and Windows answer `'unsupported'` rather than
  no-oping, since one wants a LaunchServices call and the other guards the
  setting deliberately. Deliberately NOT automatic at registration the way
  `urlScheme` is: claiming `tinydeck://` competes with nobody, claiming `.md`
  competes with the user's editor, and an app that does that on first run is
  an app people uninstall.

- **`tiny.system.locale()`** — `{ language, languages, system, region,
  timeZone }`, read from the OS. Plus a **`locale` event** when the user
  changes their language or region, so an app can re-render instead of going
  stale until relaunch.

  A page mostly doesn't need it: `navigator.language`, `navigator.languages`,
  the whole of `Intl` and the `languagechange` event all work in the webview
  (measured — `Intl.DateTimeFormat().resolvedOptions()` gives locale, time
  zone, calendar and numbering, and formatting is correct). **The backend is
  the gap**: txiki has no `Intl` at all, and reading `LANG` is the wrong
  instinct — Windows has no such variable, and elsewhere it describes the
  parent process rather than the user.

  Two lists because they answer different questions. `languages` is
  `NSLocale.preferredLanguages`, filtered to the localizations the app bundle
  declares — what to render. `system` is the raw preference — what the user
  actually reads. They agree until they matter: an English-only app on a
  French Mac gets `['en']` and `['fr-FR', …]`. Rendering from the wrong one
  either produces text nobody asked for or hides that a translation is worth
  offering.

  macOS only for now; `capabilities().locale` is `false` on Windows and Linux,
  with the route for each in their TODOs.

- **On-device AI just ships now.** `tiny.app.ai` was previously reachable only
  by people who built tinyjs themselves with an opt-in env var, because the
  release pipeline ran on a `macos-14` runner without the macOS 26 SDK — so
  for anyone who installed a release, `availability()` was permanently
  `'unsupported'`. The release job now builds on `macos-26` with the Swift
  shim linked in, and `setup.sh` detects the toolchain instead of asking: it
  links the shim when the SDK it compiles against carries FoundationModels,
  builds without when it doesn't, and says which it did. It checks the *SDK*,
  not the running OS — a Mac can run macOS 26 while its Command Line Tools
  still ship an older SDK, and that combination compiles fine right up until
  the import. (`TINYJS_AI=0` forces it off, for bisecting the Swift path.)

  Runtime requirements are unchanged and still worth guarding: macOS 26 with
  Apple Intelligence switched on. `availability()` has three answers and only
  one of them can generate.

  Two things made this more than a runner-label change. The workflow never
  passed `-mmacosx-version-min`, so the deployment floor came from the host
  SDK — invisible on `macos-14`, and on `macos-26` it would have published a
  universal binary refusing to launch below macOS 26 (measured: `minos 26.0`
  without the flag, `14.0` with it). And `swiftc` takes a single `-target`
  where clang takes `-arch a -arch b`; passing two targets doesn't error, the
  last silently wins, so the universal build is now per-arch plus `lipo`.
  Both slices cross-compile from one arm64 runner.

  The job asserts the three properties whose failures are silent, per slice:
  the floor is still 14.0, FoundationModels is still **weak**-linked (a strong
  link would stop the launcher loading at all below macOS 26 — the AI feature
  taking the whole app down), and the shim is actually present rather than a
  broken Swift step having quietly shipped a launcher that answers
  `'unsupported'`. Cost: ~62 KB on a universal binary.

  Also fixes a latent hazard unrelated to AI: `macos-14` is deprecated in
  GitHub's runner images, so that job had to move regardless — and moving it
  without pinning the floor was the trap waiting there.

- **`capabilities().ai` reflects the build, not the OS.** `tiny.app.ai` needs
  the macOS 26 SDK and `swiftc` to compile at all, so whether a given launcher
  has it is a property of the build — and one built without it rejects
  `generate()` with "not built in" while the table, by omitting the key,
  called it supported. It now asks the
  launcher (the only party that knows how the binary was compiled) and answers
  `false` on a stock build. The worst-placed over-claim of the set: the
  fallback for "no model here" is usually a whole different feature.

- **`app.thumbnail` works for *any* path now** — folders, `.app` bundles,
  `.css`, `.wasm`, executables. It asked Quick Look for a content preview only
  (`representationTypes: …Thumbnail`), which exists just for types Quick Look
  has a generator for, so those all failed with "QLThumbnailErrorDomain error
  0" while `.js`, `.md`, `.html` and images worked — indistinguishable from
  random, from outside. It now asks for the best available representation and
  falls back to the document icon, which every file has. A path that doesn't
  exist still rejects, and now does so explicitly: the icon fallback would
  otherwise have turned a typo into a successful-looking generic page icon.

- **`tiny.audio.filters` works on macOS** (14.2+), not just Linux — so an app
  can ask for an EQ once instead of branching. There's no driver and no system
  install: a Core Audio process tap over the app's own WebKit audio processes
  is *muted* and fed back through an aggregate device wrapping the real output,
  with our IOProc filtering in between. The biquads are the same RBJ cookbook
  PipeWire's `bq_*` builtins use, so the same numbers give the same curve on
  both platforms (Apple's own N-Band EQ AudioUnit would not have). Windows
  still reports `false`.

  Muting is **earned, never assumed**: an unauthorized tap doesn't fail, it
  succeeds and returns silence, and muting on that would take the app silent
  entirely. So a chain starts unmuted and inaudible, and only promotes itself
  to muted-and-filtering once it has seen a real sample. If that never happens
  — `tinyjs dev` has no bundle identity to hold the audio-capture permission —
  the app keeps playing unfiltered rather than going quiet.
- **`tinyjs dev` shows your app's icon on macOS too.** Windows and Linux
  already passed the project's icon to the launcher in dev; macOS didn't, so a
  dev run wore the terminal's icon and every app you had open looked identical
  in the Dock and the ⌘-tab switcher. It now uses `icon` from `tinyjs.json`
  (or `icon.png`), and `app.icon('')` resets to that rather than to nothing —
  the closest equivalent to a packaged app resetting to its bundle icon.
- **`app.playSound` gained four portable names** — `'info'`, `'success'`,
  `'alert'`, `'error'`. Every OS ships alert sounds and none agree on what
  they're called, so these ask for a meaning and get the platform's nearest
  equivalent (Ping/Glass/Funk/Basso on macOS, the `SystemAsterisk` family on
  Windows, the freedesktop sound theme on Linux). Platform names and file
  paths still pass through untouched; a name from the wrong platform resolves
  `false`, as before.
- **Feeds and hosts txiki's fetch couldn't reach now load.** The runtime's
  `fetch` (txiki v26.6.0) has two wire bugs, repro'd in TODO-txiki.md: any
  root-path URL goes out as `GET //` — S3/CloudFront-backed hosts answer 404 —
  and mbedtls never completes a handshake with a TLS 1.2-only host
  (rss.art19.com, anchor.fm). Eight of amp's ~60 baked-in podcast feeds hit one
  or the other. bridge.js now wraps `globalThis.fetch`: redirects are followed
  hop by hop, and exactly the broken cases route through the system curl — a
  real HTTP error is never retried, bodies stream, and every hop is
  re-validated as http(s) so a hostile `Location: file:///…` throws instead of
  reaching curl. Both the backend's `fetch()` and every page's `tiny.fetch`
  inherit it; a machine with no curl gets native behavior, bugs and all.
  Verified end-to-end on all three platforms; the shim dies the day a patched
  tjs ships (the plan is TODO-txiki.md).
- **Event handlers can be removed.** `tiny.api.on(event, fn)` was push-only —
  additive like `addEventListener` (N handlers per event, all fire), but
  nothing could ever unhook one. It now returns an unsubscribe function, and
  `tiny.api.off(event, fn)` removes by reference. Every `tiny.*.on` sugar
  wrapper (`menu.on`, `tray.on`, `theme.on`, …) passes the unsubscribe through
  — and for those it's the *only* way to unhook, since the sugar wraps your
  callback and `off` can't match the wrapper by reference. A handler that
  unsubscribes mid-dispatch no longer makes the one after it get skipped.
- **Linux: `getUserMedia` works**, gated on the app's declared permissions.
  WebKitGTK's default for an unhandled permission-request signal is deny, so
  every camera/mic app failed with `NotAllowedError` while `permissions.check`
  said `granted`. The launcher now answers the signal — but desktop Linux has
  no consent layer underneath (no TCC, no WebView2 prompt; `/dev/video*` is
  simply open to the session), so the grant is manifest-gated: only what an
  app declares in tinyjs.json's `"permissions"` block is allowed.
  `enumerateDevices` labels ride the same gate; `getDisplayMedia` stays
  denied.
- **Linux: `mousePosition().inside` is honest on Wayland, and outside tracking
  is opt-in.** Wayland (and XWayland) hides the pointer once it leaves the
  app, and the stale bounds check left `inside` stuck true. The
  surface-under-pointer probe now vetoes it, so it goes honest-false instead.
  New `tiny.app.mouseTracking.start()/stop()` for apps that need the pointer
  outside their windows: a no-op `ok` where tracking is already global (macOS,
  Windows, real X11); on Wayland sessions it arms a ScreenCast portal session
  in cursor-metadata mode — pixels are never mapped — behind one consent
  dialog, with the restore token round-tripping through the app's store so a
  re-arm is dialog-free (24 ms measured). The desktop's sharing indicator
  shows while armed, as it should.

**Fixed**

- **`readAccess` could stop the app loading at all** (macOS). WebKit refuses to
  load a page unless the page itself sits under the directory passed to
  `allowingReadAccessToURL:`, so a `readAccess` that didn't contain the page
  produced a blank window with nothing said. That's every app declaring
  `readAccess` under the documented headless-test recipe — `TINYJS_HTML` points
  at a page in a temp dir while `readAccess` names the home directory. The read
  root is now the nearest common ancestor of the two.
- **`app.playSound` crashed the app on macOS**, intermittently, usually on the
  second play. `[NSSound soundNamed:]` returns an autoreleased sound and the
  launcher stored it in a global without retaining it, so it was freed as soon
  as the handler's pool drained; the next play's `[g_sound stop]` messaged dead
  memory and aborted once that memory had been reused. Whether it died was a
  race, which is why it looked like a bad sound name rather than a leak.
- **Linux `app.icon` had never worked, on any session.** GDK only publishes
  `_NET_WM_ICON` — the property shells actually read — while the image fits
  X11's per-request limit; 256×256 lands, 512×512 is dropped. Every tinyjs app
  ships a 1024×1024 `icon.png`, so this silently swallowed both `app.icon()`
  *and the startup window icon* for every app ever shipped, while returning
  success. The launcher now scales to an icon list (256/128/64/48/32, never
  upscaling).
- **Linux `attention()` latched forever** — nothing cleared the urgency hint
  and Mutter doesn't clear it on focus, so one call left the window demanding
  attention for the rest of the run. A focus handler now drops it.
- **Linux: every frameless window wore a full title bar** on X11 sessions.
  `windowControls: false` replaced `_MOTIF_WM_HINTS` with a property declaring
  only its functions section, and a property that doesn't declare decorations
  means "WM default" — decorated — so it erased the `frame: false` GDK had
  just written and Mutter wrapped the window in a frame. Frameless apps always
  declare both, so the second request undid the first, every time. The
  property now carries both sections. Startup chrome also rides the spawn env
  now so it applies before first show — the socket line landed after the
  window was mapped, which flashed a decorated frame at every frameless
  launch.
- **macOS `secrets.get` leaked its result.** The launcher builds without ARC,
  so the `__bridge_transfer` on `SecItemCopyMatching`'s +1 return was a no-op
  and the NSData leaked on every read. Autoreleased explicitly now.
- **`win.setSize` → `getState` round-trips.** Both the Windows main window and
  macOS *titled satellites* reported a frame size while setting a content size,
  so any read-modify-write ratcheted up by the title bar every pass (measured on
  macOS: 600×400 → 432 → 464 → 496). Borderless windows never showed it, which
  is why it hid for so long. Window creation still takes a content size — no
  feedback loop there.
- **Windows `app.icon` accepts png** (the one format every tinyjs app actually
  ships — it silently no-op'd on its own `icon.png` before), and `icon('')`
  restores the startup icon instead of falling through to the window class.
- **Windows `setZoom`, `setMinSize` and `startResize` implemented**;
  `startDrag` now honours the per-window `DRAGWIN@<id>` form, so a satellite's
  drag handle works. Frameless windows get the size they asked for.
- **Windows: frameless satellite windows can be resized again.** They had no
  grabbable edge at all. A frameless secondary answers `WM_NCCALCSIZE` with no
  non-client area and then hands WebView2 the entire window rect, so its child
  HWND — another process — owns every edge pixel: `WindowFromPoint` never
  resolves to us and `WM_NCHITTEST` is never asked, leaving `WS_THICKFRAME`
  inert for grabbing. The grips that cover for exactly this on Linux were gated
  on `__TINY_FRAMELESS`, which only the Linux launcher injected. Windows now
  injects it for frameless secondaries too, so they get the same automatic 5px
  edges (`data-tiny-noresize` and `setResizable(false)` still opt out). The
  main window is deliberately unmarked — it keeps real left/right/bottom
  borders and trades away the top edge, as before.
- **Windows `startDrag`/`startResize` no longer glue a window to the cursor.**
  `DefWindowProc`'s `SC_MOVE`/`SC_SIZE` loop entered with the mouse button
  already up tracks until the *next* click, and the trip from a page's
  `mousedown` to the launcher is two process hops — so a click near an edge,
  rather than a drag, wedged the window to the pointer. Now guarded on a held
  button (as macOS already was), on `setResizable(false)` read live, and
  against a second gesture nesting a modal loop inside the first. The live
  cursor position is passed as the message point, matching macOS and GTK.
- **macOS `setZoom`, `setMinSize` and `startResize` implemented** — the same
  three, missing on the other side. All three reached the launcher's op chain,
  matched nothing and were dropped, so each resolved `true` and did nothing;
  fire-and-forget window ops look identical whether they're unimplemented or
  merely ineffective, which is how they stayed hidden while shipping as
  cross-platform. `setMinSize` also pulls a window that's already under the new
  floor back up to it, since AppKit otherwise applies `contentMinSize` only to
  the *next* resize. `startResize` has no AppKit equivalent to hand off to
  (Windows has `WM_NCLBUTTONDOWN`, GTK `begin_resize_drag`), so the launcher
  runs the drag itself and refuses to start unless a button is really held —
  the loop waits on mouse events, and one started without a live gesture would
  hang the app. macOS `startDrag` likewise honours `DRAGWIN@<id>` now, and
  checks for a live mouse-down first: `performWindowDragWithEvent:` raises on
  anything else rather than no-oping.
- **Windows stopped claiming `nowPlaying` and `haptic`**, which were no-ops.
- **`tinyjs build` refreshes a stale launcher** the way `dev` already did, so a
  dev checkout can't ship a launcher predating its own sources — or silently
  skip the app icon, since `--embed-icon` is run *by* that binary.

- **`tiny.audio.pageChain(ctx)`** — the filter chain as Web Audio nodes in the
  page, for where the native chain doesn't exist. Same band vocabulary, same
  RBJ curves (measured in an OfflineAudioContext: ask +12 dB at the centre,
  get 12.00), and the same four verbs as `tiny.audio` — so an app picks its
  backend once and the code after is identical. Page-scoped, honestly: it
  filters what you route through `input`→`output`, not audio the page never
  gets samples for; shelf `q` and per-filter `gainR` are ignored (use
  `balance()`); and it's not for Linux, where Web Audio crackles and the
  native chain exists precisely because of that.

  It exists because Windows measured out: process-loopback capture is
  post-mute *and* post-volume, so the only way to silence the direct signal
  is session volume — which Windows **persists**, keyed on the EdgeWebView
  runtime path that every WebView2 app on the machine shares. A crash while
  filtering would have near-silenced Teams and every other WebView2 app until
  something rewrote the key. `capabilities().audioFilters` therefore stays
  `false` there, permanently and with the numbers attached
  (TODO-audio-filters.md); the deck's EQ card now runs this chain on Windows
  instead of a dead panel.
- **`app.badge` works on Windows**, and `capabilities().badge` says so. It was
  written but never run there, so the capability deliberately reported `false`;
  it has now been seen drawing on the taskbar button (and composing with
  `app.progress`), so the claim is true rather than cautious.
- **Fixed: `app.spotlight()` answered `[]` on Windows instead of rejecting.**
  The launcher correctly reports the op as unsupported; the bridge threw that
  away and returned an empty array, so a caller was told "no files matched"
  where the truth was "there is no search backend here". Those are opposite
  answers. It now rejects, like the neighbouring `pickColor` already did.
- **Fixed: a secret larger than 2560 bytes failed unexplainably on Windows.**
  Credential Manager caps a credential blob there — smaller than the macOS
  keychain, which takes 4 KB without complaint — and over the cap `CredWriteW`
  fails with a Win32 error (1783) that names nothing, which surfaced as a bare
  "credential write failed". `tiny.app.secrets.set` now rejects oversized
  values up front, naming the byte count and the limit. Note the limit counts
  BYTES of UTF-8: 4-byte emoji reach it four times faster than ASCII.
- **`app.thumbnail` on Windows is broader than the docs claimed, and sized
  differently.** Folders, executables and plain text all render as their shell
  icon, the same breadth macOS has — it's Linux that is images-only. And where
  macOS and Linux treat `size` as points and render @2x, Windows treats it as
  pixels and returns exactly what was asked. Read `width`/`height` off the
  result rather than assuming either. Docs corrected.
- **Fixed: `tinyjs build --cli` printed a Unix hint on Windows**, suggesting an
  `ln -sf` into `/usr/local/bin` — neither of which exists there. It now prints
  a `setx PATH` line.
- **Fixed: the curl fallback flashed a terminal window on Windows**, once per
  hop. The fetch repair shim routes exactly two cases through the system curl
  — root-path URLs and TLS 1.2-only hosts — which is most podcast feeds
  (art19, anchor.fm), and a console tool spawned from a GUI-subsystem exe pops
  a console window for the life of the call. So loading a feed strobed one to
  three of them, and amp made it look like the app was shelling out to
  something. The curl hops and the `curl --version` probe now go through
  `launcher --run` (`CREATE_NO_WINDOW`), the same route `reg add` and the
  updater's `tar` already took; it was only ever missed here because the fetch
  shim lives outside `createApp`, where the launcher path was resolved.
  `--run` also puts the tool in a job object with `KILL_ON_JOB_CLOSE` now, so
  killing the wrapper — what aborting a curl-backed stream does — takes curl
  with it instead of leaving it downloading into a pipe nobody reads. Measured
  against a built app with a console-window watcher: 19 curl children, not one
  window; the same app spawning curl directly popped one per call.
- **Fixed: an app's backend could run before the fetch repair was installed.**
  `update.js` probed for its bundle with a top-level `await`, which makes it —
  and `bridge.js`, which imports it — an *async* module, and ES module
  evaluation lets a synchronous sibling run while an async one is still
  pending. So a backend whose module body fetched before its own first `await`
  got txiki's unrepaired `fetch`: measured, a top-level
  `fetch('https://rss.art19.com/')` failed with `mbedtls connect -1 5 0` while
  the identical call one `await` later returned 200. The probe is lazy now
  (`bundlePath()` is async), nothing in bridge.js's import graph awaits at the
  top level, and the first line of a backend gets the same `fetch` as the
  hundredth.

**Also**

- `test/appsurface.html` — a self-driving page that steps through the whole app
  surface, holding each state long enough to see, and prints what
  `capabilities()` claims for the machine. It now minimizes itself before
  `attention()` and holds after `presence('normal')`: Windows defines
  `FLASHW_TIMERNOFG` as a no-op while the window is already foreground, and
  quitting straight after restoring a taskbar button makes it indistinguishable
  from the app simply leaving. Both checks were unfalsifiable on Windows before.
- `TODO-verify.md` — what's been built on one OS but not yet watched run on
  another. A fire-and-forget op reaching a shell that ignores it looks exactly
  like one that was never implemented, so "nothing appeared" needs a written
  home.
- The docs site is now one page at [tinyjs.app/docs](https://tinyjs.app/docs)
  (was /api): install, the full API, and how-it-works / portability / gotchas,
  with a section rail on wide screens.

## 0.29.0 — 2026-07-23

- **Linux support (beta).** tinyjs apps now run on Linux: a third native
  launcher (`native/launcher-linux.cc`, GTK3 + WebKitGTK 4.1 directly)
  speaks the identical wire protocol over the same
  Unix socket macOS uses, and the CLI, bridge, and build work cross-platform.
  Runs on X11 and Wayland sessions, Ubuntu 24.04+ and current distros with
  `webkit2gtk-4.1`. `tinyjs dev`/`build`/`publish` and auto-update all work:
  `build` produces a portable `dist/` folder (compiled backend binary with
  the frontend and icon riding inside it, plus `launcher` and `icon.png`)
  with no install step — a built app registers its own `.desktop` entry
  (app-menu listing, icon, deep links via `urlScheme`, file associations via
  `fileExtensions`, single instance) on first run. `publish` emits
  `<name>-<version>-linux-<arch>.tar.gz` plus a manifest carrying a per-arch
  `"linux": { "<arch>": { url, sha256 } }` block alongside the existing mac
  and win blocks; installs verify and swap in place, then relaunch.
  Working: window ops, frameless/transparent chrome (`vibrancy` is a no-op),
  menus + `key:` accelerators, tray via AppIndicator/StatusNotifier
  (menu-based — a bare icon click is emulated via a menu entry,
  `tray.position()` returns `null`), native dialogs, clipboard (+ watch),
  notifications with action buttons (no reply fields), theme + sleep/wake,
  `power.preventSleep` (logind), `secrets` (Secret Service/GNOME Keyring),
  `shell.open/reveal/trash`, `launchAtLogin` (built apps, autostart
  `.desktop`), multi-window, custom context menus + suppression, hot reload,
  `tiny.fetch`/`proxyURL` streaming, `printToPDF`, the print dialog,
  `screens`/`mousePosition`/`getWinState`, `captureScreen` (X11 sessions
  only), `thumbnail` (images only), global hotkeys (XGrabKey on X11; the
  GlobalShortcuts portal on pure Wayland — a one-time approval dialog),
  `keystroke` synthesis (X11/XWayland via XTest only), `playSound`/`beep`,
  `say`/`voices` (via speech-dispatcher's `spd-say` when installed),
  `pickColor` (portal), `idleTime` (GNOME), `battery`, `dock.bounce`
  (urgency hint), `nowPlaying` + media keys (a real MPRIS player — metadata
  in the GNOME/KDE media widget and lock screen, transport back via
  `onMediaKey`), and `audioTap` — `scope:'app'` is REAL per-app capture on
  Linux (a private PipeWire null sink fed by the app's own output ports, so
  it hears itself and nothing else; playback untouched), with system scope
  falling back to the default sink's monitor — plus `spotlight` (name search via indexed `plocate`/`locate`, else a
  bounded `find`). Not (yet) supported: `recorder`, `ocr`, `quickLook`,
  `applescript`, `haptic`, Dock badge/`bounce({critical: true})`/`dockIcon`,
  `share`, `wifi`, `selectedText`/`otherWindows`/`moveWindow`/
  `frontmostApp`, `authenticate`, `tiny.app.ai`, and `setAllSpaces` (maps to
  sticky windows) — all fail cleanly so cross-platform code can
  feature-detect: capability calls reject with a specific reason, query
  calls resolve `null`, fire-and-forget ones are silent no-ops. Burn-down
  list: [TODO-linux.md](https://github.com/tarwin/tinyjsapp/blob/main/TODO-linux.md).

- **`tiny.system` — know where you are, and what's missing.**
  `os()`/`isMacOS()`/`isWindows()`/`isLinux()` answer synchronously;
  `info()` adds arch and the Linux session/desktop; `capabilities()` reports
  what this machine can actually do so apps degrade deliberately. On Linux,
  where the media stack ships in optional pieces, `requirements(ids)` says
  what's missing and how to fix it (per-distro package lists + the exact
  install command), `requirements(ids, { refresh: true })` re-probes after
  the user installs, and `promptMissing(ids)` puts a native dialog in front
  of them with a **Copy install command** button — showing nothing at all
  when everything's present, so it's safe to call from a failure path.
  Dialog `detail` text keeps its line breaks on Linux.

- **`tiny.audio` — native DSP on the app's own output (Linux).**
  Web Audio is the right tool for an EQ on macOS/Windows and unusable on
  Linux: WebKitGTK renders the graph on a normal-priority thread, so
  anything reaching `ctx.destination` crackles. `audio.filters(list)` runs
  the chain in PipeWire instead (peaking/shelves/passes/notch/allpass +
  linear gain, up to 28 filters, per-channel gains via `gainR`), applies to
  audio the page never sees (raw radio streams, native HLS), retunes live
  with no gap, and survives reloads. `audio.balance(v)` pans on the chain's
  output stream — no filter slot, no rebuild. `capabilities().audioFilters`
  gates it; the chain dies with the launcher, so nothing can be left behind.

- **Frameless windows resize on Linux — and windowing grew up generally.**
  Undecorated GTK windows have no WM resize edges, so the injected client
  mounts invisible grips on every frameless window (opt out per page with
  `data-tiny-noresize`, or via `setResizable(false)`);
  `tiny.win.startResize(edge)` starts a native resize from your own handle.
  `setResizable(false)` now means what it means elsewhere — the USER can't
  drag the edges — without blocking the app's own `setSize` (GTK conflated
  the two, so a fixed-size deck couldn't collapse into its own shade view).
  `startDrag`/`startResize` are correctly per-window (a satellite's grip
  used to move the main window). New: `tiny.win.setMinSize(w, h)` (+
  `minSize` in `win.open`) puts a WM-enforced floor under user resizes, and
  `tiny.win.setZoom(factor)` renders native page zoom — crisp double-size
  modes on hi-dpi screens with no page changes (Linux launcher today;
  no-ops elsewhere until the mac/win launchers grow the op).

- **`windowPlacement` manifest key.** Wayland forbids clients placing their
  own toplevels; apps whose whole design is windows that place each other
  (amp's docked satellites) can declare `"windowPlacement": true` and run
  on X11/XWayland where all of it works — everything else stays Wayland.

- **Linux installer + prebuilt binaries.**
  `curl -fsSL https://tinyjs.app/install | sh` now detects Linux too,
  installing to `~/.tinyjs` the same as macOS (needs the system
  `libwebkit2gtk-4.1-0` runtime). From source, `./setup.sh` now also builds
  on Linux — needs `build-essential pkg-config libgtk-3-dev
  libwebkit2gtk-4.1-dev libayatana-appindicator3-dev` — and downloads a
  prebuilt `tjs`, or builds txiki.js from source with `TJS_BUILD=1`
  (needs cmake + ninja). The release workflow gained `linux-x86_64` and
  `linux-arm64` jobs that build txiki.js from source and ship it as
  `tjs-linux-<arch>.gz`, checksummed alongside the macOS and Windows assets.

## 0.28.3 — 2026-07-22

- **Windows: taskbar pins finally work.** The visible window belongs to
  `launcher.exe` (the WebView2 host), so pinning it pinned a binary that
  can't start on its own — the pin was dead on next launch. Every tinyjs
  window now carries `System.AppUserModel` relaunch properties
  (command/name/icon pointing at the app exe, plus a stable per-app
  AppUserModelID), so pins and Start-Menu launches start the actual app.
  The toast-notification Start-Menu shortcut targets the app exe too.

## 0.28.2 — 2026-07-22

The Windows polish release: no stray terminal windows, a real exe icon, and
a two-file dist.

- **Windows: console tools no longer flash terminal windows.** txiki's
  `tjs.spawn` can't pass `CREATE_NO_WINDOW`, so every console child
  (`tasklist`, `tar`, `reg`, …) spawned from a GUI-subsystem app popped a
  visible console. New `launcher-win.exe --run <cmd…>` mode runs any console
  tool hidden with stdio and exit code passed through; the bridge routes its
  own `reg add` calls (deep-link registration) and the updater's zip extract
  through it, and apps get `app.spawnHidden(args, opts)` — plain `tjs.spawn`
  on macOS, hidden on Windows.
- **Windows: built exes carry the app icon.** `tinyjs build` now stamps the
  icon into a clean copy of the runtime *before* `app compile` appends the
  TPK bundle (the output itself can't be resource-edited), so
  `dist/<name>.exe` shows the real icon in Explorer, the taskbar, and
  registry `DefaultIcon` entries.
- **Windows: dist is just two files.** The frontend and icon already ride
  inside the compiled exe's TPK bundle (extracted to tmp at launch), so the
  build stops shipping the redundant `dist/frontend/` and `dist/icon.png` —
  a built app is `<name>.exe` + `launcher.exe`, nothing else. Bundle
  detection (updates, single-instance, deep links) no longer requires a
  `frontend/` folder next to the exe; older dists keep working.
- **Per-platform release notes.** A `win.notes` field in the update manifest
  overrides the top-level `notes` on Windows, for releases where the two
  platforms ship different things.

## 0.28.1 — 2026-07-22

- **Per-platform update manifests.** `url`/`sha256` remain the macOS zip;
  a `win: { url, sha256 }` block carries the Windows build. Windows apps
  overlay it on check and report "no update" when a release has no Windows
  zip — never a mac download. `tinyjs publish` on Windows names the zip
  `<name>-<version>-win.zip` and emits the `win` block; publishing both
  platforms means merging the two manifests (mac owns the top-level fields).

## 0.28.0 — 2026-07-22

The whole example fleet now runs on Windows — this release is everything a
one-by-one sweep of all 25 apps shook out of the framework.

- **`tiny.fileURL(path)`** — one correct `file://` builder for both OSes
  (drive letters, backslashes, UNC hosts, percent-encoding). Hand-rolled
  `'file://' + path` was the single most common porting bug; stop writing it.
- **Windows: UNC media plays untainted.** `file://server/share` URLs
  (Parallels shared folders, network drives) are host-bearing and therefore
  cross-origin to Chromium — a WebResourceRequested interceptor now serves
  them with CORS headers and full Range/206 support, so seeking and WebAudio
  analysis work like local files.
- **Windows: secondary windows reached parity with main.** Every
  `win.open` webview now gets the identical wiring: context menus, the UNC
  interceptor, drag-and-drop forwarding, menu accelerators — and Chromium's
  autoplay gesture-gate is off, so satellite windows (visualizers analysing
  a silent twin stream) start without ever being clicked.
- **Windows: transparency, the full story.** Transparent windows composite
  correctly in every combination we could construct: WebGL content in
  secondaries (`WS_EX_NOREDIRECTIONBITMAP` at creation), `setChrome`
  transparency applied at any time, and transparent MAIN windows — declare
  those in tinyjs.json `"chrome"`; a Windows window can host transparency
  or a Win32 menu bar, never both.
- **Windows: `emoji:` tray icons** — the asset-free twin of macOS `sf:`
  symbols. `tray.set({ icon: 'emoji:🍵' })` renders the glyph as a
  monochrome silhouette that follows the taskbar theme; branch per-OS and
  neither platform ships an icon file.
- **Windows: `audioTap` scope `'app'` rides the system loopback** rather
  than rejecting — honest degradation until true per-process capture lands.
- **Store hardening (both OSes):** `store.set` bursts are serialized and
  renames retry — Defender/indexers transiently lock fresh files on
  Windows, which surfaced as EPERM under load.
- **Bridge survives `window.chrome` shadowing.** A page declaring its own
  `chrome` no longer severs the RPC channel (the post hook is stashed at
  document-start). Related rule now in the skill: never declare a top-level
  `chrome` identifier — on WebView2 a `const chrome` is a parse-time
  SyntaxError that kills the entire script.
- Plus: window moves no longer steal focus (`SWP_NOACTIVATE`), new windows
  clamp onto the virtual screen (stale saved positions can't open apps in
  the void), and `win.open` position/size honor DPI scaling everywhere.

## 0.27.2 — 2026-07-21

- **Windows: logical coordinates everywhere + WebGPU.** The launcher is
  per-monitor-DPI-aware, so Win32 hands it PHYSICAL pixels — but the tinyjs
  coordinate contract is logical CSS-pixel units (macOS points). On scaled
  displays secondary windows opened at half size and app-driven window
  drags ran at half the cursor speed (amp). Every wire coordinate now
  converts at the boundary: setPosition, win.open size/position, SIZE,
  getState, mousePosition, screens, tray.position. Also: WebView2 now gets
  `--ignore-gpu-blocklist --enable-unsafe-webgpu`, so `navigator.gpu`
  yields a real adapter even on virtualized GPUs — WebGPU parity with the
  macOS launcher, which force-enables the WebKit feature flag.

## 0.27.1 — 2026-07-21

- **Windows: local media now drives WebAudio.** Chromium gives every
  `file://` URL an opaque origin, so `createMediaElementSource` on a local
  track was CORS-tainted and output pure zeros — record players, EQs, and
  visualizers ran silent while the element "played". The bridge now passes
  `--allow-file-access-from-files` to WebView2 (via a small patch to the
  vendored webview loader, which ignored `WEBVIEW2_ADDITIONAL_BROWSER_
  ARGUMENTS`), making `file://` same-origin like WKWebView treats page-dir
  files on macOS. Verified end-to-end with platter: art + audio both work.

## 0.27.0 — 2026-07-21

- **Windows: second wave — near feature parity.** Multi-window (`win.open`
  — each window hosts its own WebView2 with the full bridge; `<winid>:<seq>`
  call routing like macOS), drag & drop with real paths both directions
  (`IDropTarget` in, `DoDragDrop`/`CF_HDROP` out), `printToPDF`,
  `captureScreen` (no permission dance), `thumbnail` (shell image factory),
  `say`/`voices` (SAPI), clipboard image write (`CF_DIB`), menu `key:`
  accelerators (Ctrl+\<key> via WebView2 AcceleratorKeyPressed),
  `launchAtLogin` for built apps (HKCU Run), chrome `transparent` +
  vibrancy→mica/acrylic backdrops (Win11 22H2+), accessory apps drop the
  taskbar button, and `tinyjs publish` + app auto-update: manifest sha256 via
  WebCrypto (no shasum spawn on either OS), bsdtar zip, and an in-place
  file-by-file swap on install (Windows can't rename a folder holding a
  running exe; locked exes park as `*.update-old`, swept next update) —
  verified end-to-end with a real 1.0.0→1.0.1 self-update. Launchers now
  chdir out of the app folder so updates never block on a cwd handle.

- **Windows installer + prebuilt binaries.** `irm https://tinyjs.app/install.ps1 | iex`
  installs a prebuilt `tinyjs-windows-x86_64.zip` (built by the release
  workflow's new `windows-latest` job, checksummed alongside the macOS
  tarballs) into `%LOCALAPPDATA%\tinyjs` and adds it to the user PATH — no
  git, no compiler. `tinyjs update` re-runs the installer on Windows too.
  Dev nicety: `tinyjs dev` from a source checkout rebuilds
  `launcher-win.exe` automatically when `native/launcher-win.cc` or
  `runtime/tiny.js` changed.

- **Windows support (beta).** tinyjs apps now run on Windows: a new native
  launcher (`native/launcher-win.cc`, WebView2 via the same vendored webview
  library) speaks the identical wire protocol over a named pipe, and the CLI,
  bridge, and build work cross-platform. From a source checkout:
  `powershell -ExecutionPolicy Bypass -File setup.ps1` (downloads the Windows
  txiki.js runtime + WebView2 SDK header, compiles the launcher — needs
  MinGW-w64, e.g. `winget install BrechtSanders.WinLibs.POSIX.UCRT` — and adds
  the checkout to your user PATH so `tinyjs` works from any new terminal;
  `-SkipPath` opts out). Working: the full page↔backend bridge (calls,
  events, tiny.fetch), dev mode with hot reload, Vite `devUrl` frontends,
  `tinyjs build` (portable `dist/` folder with `<name>.exe`), native file/save/
  folder dialogs, alert/confirm/prompt, menu bar + tray (+ balloon
  notifications), custom context menus, clipboard (text/html/files/image read),
  global hotkeys, keystroke synthesis (`cmd` maps to Ctrl), shell
  open/reveal/trash, secrets (Credential Manager), power.preventSleep, theme +
  sleep/wake events, window ops (chrome/fullscreen/ontop/click-through/…).
  Not (yet) ported: multi-window, drag-out/drop-in with real paths,
  notifications with actions, printToPDF, audioTap, and the macOS-specific
  APIs (Quick Look, OCR, AppleScript, Touch ID, vibrancy, Dock, Spaces…),
  which reject or report `'unsupported'` cleanly so cross-platform app code
  can feature-detect.

- **`tiny.app.ai`** (on-device LLM) — landed behind an opt-in build, not yet
  in the released tarballs. `ai.generate(prompt, { instructions })` runs
  Apple's FoundationModels locally (offline, no API key, private);
  `ai.availability()` → `'available' | 'unavailable' | 'unsupported'`.
  Implemented as a small Swift shim linked into the launcher with
  `TINYJS_AI=1 ./setup.sh` (needs the macOS 26 SDK); the binary keeps the
  macOS 14 floor and weak-links FoundationModels, so stock builds and older
  systems just report `'unsupported'`. Shipping it in the release tarballs
  needs the CI runner bumped to a macOS 26 image — held for a deliberate
  pipeline change so the default `macos-14` build never depends on the newer
  SDK. Proven working end-to-end locally (real generation, ~250ms).

## 0.26.0 — 2026-07-20

- **Custom User-Agent.** `createApp({ userAgent })` / `"userAgent"` in
  tinyjs.json overrides the webview's User-Agent string (packaged apps read
  the `TinyjsUserAgent` Info.plist key the build writes; dev passes it via
  `TINYJS_UA`). WKWebView's default UA omits the trailing `Version/x Safari/x`,
  so UA-sniffing sites reject it as an unknown browser — mainly useful when
  pointing a `devUrl` at a real hosted site. Caveat: a UA alone often isn't
  enough. Many SaaS apps (Slack, etc.) also feature-detect at boot and refuse
  embedded browsers, and an embedded WKWebView genuinely lacks some of what
  they require (e.g. Web Push) — so this gets you recognized as a browser, not
  necessarily *in*.
- **`TINYJS_INJECT` (footgun, dev-only).** An env var that injects arbitrary
  JS into every page at document-start on any origin, for shimming a
  third-party site wrapped via `devUrl`. Deliberately undocumented in the
  public API with no packaged-app equivalent — it runs your code inside
  someone else's origin with full page privileges. A debugging tool, not for
  shipping.

## 0.25.3 — 2026-07-18

- **`tiny.proxyURL` now plays live internet radio through the Web Audio / EQ
  graph.** A live (icecast/shoutcast) stream answers `200` with no
  `Content-Length` and no byte-range support, so WKWebView's custom-scheme media
  loader rejected the `<audio>` with error 4 (`SRC_NOT_SUPPORTED`) — the
  limitation noted in 0.25.0. The proxy now detects a length-less
  `audio/*`/`video/*` response and serves it with a synthetic large
  `Content-Length` (dropping the contradictory range/chunked headers), so the
  element plays it progressively as one long non-seekable resource and
  `MediaElementSource` taps it for real. Verified end-to-end on live
  StreamTheWorld MP3, both direct and via the redirect: error 4 → playing with
  live analyser data. Redirects were never the issue — they always worked.
  Tradeoff: for a live stream `audio.duration`/`currentTime` are meaningless
  (the fake length maps to a huge timeline) — don't wire a seekbar to one. The
  synthetic length is gated to media MIME types, so ordinary `fetch()` through
  `proxyURL` is unaffected.
- **Friendly apology on Intel Macs instead of a cryptic hang.** The app launcher
  is a universal binary but the bundled `tjs` runtime is Apple-Silicon-only, so
  a distributed app used to launch on an Intel Mac and then hang on a backend
  that could never start. The launcher now detects this (`posix_spawn` returns
  `EBADARCH`) and shows a plain "This app needs an Apple Silicon Mac" apology,
  then exits cleanly. The `tinyjs` CLI also prints a non-fatal heads-up when run
  on an Intel Mac. tinyjs remains Apple-Silicon-first.

## 0.25.2 — 2026-07-17

- **`"contextMenu": false`** in tinyjs.json suppresses WebKit's default
  right-click menu (Reload / Back / Forward / Inspect Element…) for an app-like
  feel. Off by default — the browser menu shows unless you opt out. A custom
  `tiny.menu.setContext(...)` still overrides, and `null`/clearing it falls back
  to *suppressed* (not the WebKit default) while the option is on.

## 0.25.1 — 2026-07-17

- **docs:** corrected `tiny.audioTap` TCC behavior. The first `start()` **does**
  prompt for "System Audio Recording" — even `scope:'app'`, because WKWebView
  renders audio in a separate `com.apple.WebKit.GPU` helper, so the tap is a
  cross-process capture (the grant then persists per app, which is why repeat
  runs don't re-prompt). Authorization is deferred to the first `start()`
  (declaring the manifest key does nothing at runtime), so the tap can be
  lazy-armed. Under `tinyjs dev` the audio *owner* is the terminal, so the tap
  delivers real PCM only if that terminal holds the grant (else silent chunks).
  No code change — the implementation already defers to `start()`.

## 0.25.0 — 2026-07-17

- **`tiny.audioTap`** — read the app's (or the system's) *rendered* audio
  output as PCM, for VU meters and visualizers — including audio that never
  touches Web Audio (native HLS, CORS-tainted streams, other apps). Read-only:
  it observes the mix in sync with what's audible, it can't process it (EQ
  still needs the signal in the graph — that's `proxyURL`; the two compose).
  `await tiny.audioTap.start({ scope: 'app' | 'system', interval })` then
  `tiny.audioTap.on(({ pcm, sampleRate, channels, frames, t }) => …)` where
  `pcm` is base64 of interleaved little-endian Int16; `stop()` (or the owning
  window closing) tears it down. Gated by an `"audioTap": "app" | "system"`
  key in tinyjs.json (the build bakes `NSAudioCaptureUsageDescription`).
  Implemented with Core Audio process taps (macOS 14.2+): a `CATapDescription`
  → `AudioHardwareCreateProcessTap` → an aggregate device → an IOProc that
  downmixes to stereo Int16, chunked out to the page over the existing bridge.
  `scope:'app'` selects exactly this app's audio process objects by matching
  the *responsible pid* (WKWebView renders audio in a `com.apple.WebKit.GPU`
  XPC helper that isn't a child process). Authorization is deferred to the
  first `start()` (declaring the manifest key does nothing until then), so the
  tap can be lazy-armed; that first `start()` prompts for "System Audio
  Recording" — even `scope:'app'`, since the GPU-helper tap is a cross-process
  capture — and once granted yields real audio (verified in a real bundle: RMS
  matched a test tone to 3 decimals). `scope:'system'` also hears other apps.
  Survives output-device changes (headphones) by re-arming. Cross-platform by
  design (Windows process-loopback / WASAPI, Linux PipeWire), macOS-only for
  now. Note: under `tinyjs dev` the audio *owner* is the terminal, not the app,
  so the tap delivers only if that terminal holds the grant (else silence);
  a built .app owns its own grant.
- **docs:** corrected the `tiny.proxyURL` docs — driving `<audio>` through the
  proxy needs a byte-range-capable (HTTP `206`) stream; a non-seekable live
  stream that answers `200` can't play through the proxy element (fall back to
  the raw URL). Redirects were never the issue.

## 0.24.0 — 2026-07-16

- **`tiny.proxyURL(url)`** — get a cross-origin stream (internet radio) into
  Web Audio. A `MediaElementSource` on a cross-origin `<audio>` outputs silence
  by spec, so radio can't drive an EQ or analyser. `proxyURL` returns a same-app
  `tiny-media://…` URL that streams the remote http(s) resource through the
  native layer (NSURLSession — redirects, byte-range/seek, native buffering)
  and injects `Access-Control-Allow-Origin: *`; with `<audio crossorigin=
  "anonymous">` the element is CORS-approved and untainted, so the full Web
  Audio graph gets real samples. Implemented as a `WKURLSchemeHandler`
  registered on every webview via a swizzle of `-[WKWebView initWith
  Frame:configuration:]` — no backend hop, no base64, no TCP port. Verified
  end-to-end: a `MediaElementSource` on a proxied SomaFM stream drove a live
  `AnalyserNode` with real (non-silent) waveform data. macOS only for now; the
  design (custom scheme + streamed upstream + injected CORS) maps to WebView2
  (`WebResourceRequested`) and WebKitGTK (`register_uri_scheme`) when those
  backends land.

## 0.23.0 — 2026-07-16

- **`tiny.fetch(url, init)`** — a `fetch` that runs in the backend (a native
  process), so it has **no CORS, CSP, or mixed-content limits**: the page can
  reach any origin. Same shape as `window.fetch` and resolves to a real
  `Response` (`res.json()` / `res.text()` / `res.headers` / `res.ok` all work);
  request bodies can be strings, `ArrayBuffer`/typed arrays, `Blob`, or
  `URLSearchParams`. Pass `{ stream: true }` for a **live streaming body** —
  the backend keeps the connection open and the page pulls chunks on demand
  (`res.body.getReader()`) with natural backpressure. That's what an endless
  source like internet radio needs, where a buffered fetch would never resolve;
  `reader.cancel()` (or closing the window) tears the upstream connection down.
  Transport reuses the existing request/response bridge — chunks travel base64
  over the CALL→RET channel, so there's no new wire frame and no launcher
  change. Note: a page with CORS-free network reach has the app's full network
  reach — not a new trust boundary for tinyjs (every page already has an RPC
  channel to a backend with full system access), but worth knowing.

## 0.22.6 — 2026-07-16

- **`tinyjs notarize --dmg`** — rebuild the installer dmg from the *stapled*
  .app. `stapler` mutates the bundle in place after Apple's verdict, so a dmg
  made at `build --dmg` time holds the pre-staple .app with no ticket embedded —
  online Gatekeeper hides this (it fetches the ticket), but offline / first
  launch before network it blocks the app. `notarize` now rebuilds the dmg from
  the stapled bundle when `--dmg` is passed, and refreshes an existing dmg on
  disk automatically (that copy is guaranteed stale). Ship a dmg with
  `tinyjs build --dmg && tinyjs notarize --dmg`.

## 0.22.5 — 2026-07-16

- **`chrome: { acceptsFirstMouse: true }`** — opt a window into delivering the
  click that focuses it straight through to the page. macOS normally swallows
  that first click into web content ("click once to focus, again to act"),
  which also bites between an app's own windows and makes an unfocused window's
  DOM drag region need an extra click. On per window (main or secondary), via
  `tiny.win.setChrome`, `app.openWindow`'s `chrome`, or `tinyjs.json` `"chrome"`
  (applied before first paint); reported back in `getState().chrome`. Off by
  default — matches the platform, no change for existing apps.

## 0.22.4 — 2026-07-16

- **Multi-line JavaScript sent to the page keeps its newlines.** Script pushed
  across the bridge — `app.eval`, a window handle's `eval`, and the event
  pushes behind `app.push` / `push` — was flattened onto a single line before
  it crossed the socket, so a `//` line comment silently commented out
  everything after it (a whole multi-line snippet could vanish after its first
  comment). Snippets now travel wire-escaped and are unescaped in the launcher,
  so real newlines survive intact; event payloads containing newlines or
  backslashes round-trip unchanged.

## 0.22.3 — 2026-07-15

- **`tinyjs notarize` fails fast on the wrong signature.** It now inspects what
  the built `.app` is actually signed with (via `codesign`) and stops
  immediately unless that's a *Developer ID Application* certificate — naming
  what it found (ad-hoc, unsigned, or e.g. an "Apple Development" cert) instead
  of uploading, waiting minutes, and coming back with Apple's opaque "Invalid".
  It checks the artifact on disk, not just the configured identity, so a build
  that predates a config change is caught too.
- The notarize "needs a real Developer ID" error now mentions the
  `TINYJS_SIGN_IDENTITY` env var, not just `signIdentity` in tinyjs.json — both
  have always worked (so does `TINYJS_NOTARY_PROFILE` for the notary profile);
  the message just never said so.

## 0.22.2 — 2026-07-15

- **`tinyjs uninstall`.** Cleanly removes the install (`~/.tinyjs`, or
  `$TINYJS_HOME`) and the `tinyjs` PATH symlink — the exact inverse of the
  installer. It removes only a symlink that points back into the install it's
  running from (so an unrelated `tinyjs` on your PATH is left alone), prints
  what it'll delete, and prompts before doing it (`--yes` skips the prompt; a
  non-interactive run without `--yes` refuses rather than deleting silently).
  Safe by construction: it won't touch a source checkout, `/`, or a directory
  containing `.git`. The shell-profile PATH line is left in place with a note,
  since editing dotfiles on uninstall is too invasive to do silently.
- **Friendlier installer finish.** The install script now explains that it
  linked into a dir *because it's already on your PATH*, and warns that a
  running shell caches command lookups — so if this same terminal still says
  `tinyjs: command not found`, run `hash -r` or open a new one.

## 0.22.1 — 2026-07-15

- **Tray icons keep their aspect ratio.** A PNG tray icon was force-scaled to
  a hardcoded 18×18, squishing wide "pill"/wordmark icons into a square and
  throwing away the PNG's DPI. Custom icons now scale to the 18pt menu-bar
  height while preserving width (a 58×22pt icon becomes 47.5×18pt, not
  18×18), and the variable-length status item widens to fit. Square icons are
  unchanged; `sf:` SF Symbol icons were never affected.

## 0.22.0 — 2026-07-15

- **`chrome.squareCorners`** — drop macOS's rounded window corners.
  `setChrome({ squareCorners: true })` (or `"chrome": { "squareCorners":
  true }` in tinyjs.json, applied before first paint) makes the window
  **borderless**: square, no titlebar, no traffic lights. It's a deliberate,
  explicit choice — you lose the native titlebar drag (use `data-tiny-drag`)
  and it reads as un-native — but resize edges, the drop shadow, and
  keyboard focus are all kept (a `canBecomeKeyWindow` override restores focus
  that borderless windows normally can't take). Works on the main window and
  secondary `openWindow` windows; `getState().chrome.squareCorners` reports
  it, and it survives `setSize`.

## 0.21.0 — 2026-07-15

Fixes and window ergonomics from real-app usage.

- **Fix: `store.set()`/`delete()` can no longer crash the backend.** A write
  failure (bad path, full disk) used to surface as an unhandled rejection
  and take the process down; the store now swallows its own write errors
  (resolving `false`, keeping the in-memory value) and uses a unique temp
  file per write so a burst of un-awaited `set()`s can't race on the rename.
- **`openWindow` gains `chrome` and `{ x, y }`** — applied *before* the
  window first paints, so a frameless/vibrancy panel never flashes its
  titlebar and a positioned window opens where you asked instead of
  center-then-jumping. `getState().chrome` is now per-window (secondary
  windows report their own frame/trafficLights/transparent, not the main
  window's).
- **`readAccess` option** — `<audio>`/`<video>`/`<img>` can only load
  `file://` assets under the frontend dir by default (`MEDIA_ERR_SRC_NOT_
  SUPPORTED` otherwise). `createApp({ readAccess: true })` (home) or
  `readAccess: '/path'` (also `"readAccess"` in tinyjs.json) widens the
  read root so media anywhere under it loads directly — no base64 → Blob
  round-trip.
- Docs: noted that WebKit throttles `requestAnimationFrame`/timers in
  occluded or off-screen windows (do continuous work in the visible window
  or the un-throttled backend).

## 0.20.0 — 2026-07-14

Deep-Mac citizen — the small native niceties apps otherwise shell out (or
give up) for.

- **`win.printToPDF(path)`** — render the page to a vector PDF via
  WKWebView → `{ path }`. Invoices, reports, receipts without a print
  dialog.
- **`app.haptic(pattern)`** — trackpad haptic feedback (`'generic'` |
  `'alignment'` | `'level'`), a no-op on Macs without a Force Touch pad.
- **`app.dockIcon(pngPath)`** — replace the Dock icon (render a canvas for
  progress rings or unread badges); `''` resets to the bundle icon.
- **`app.battery()`** — `{ percent, charging, plugged, minutesRemaining }`
  (or `null` on a desktop) from IOPowerSources.
- **`app.wifi()`** — `{ ssid, bssid, rssi, noise, txRate }` from CoreWLAN
  (`ssid`/`bssid` need the Location permission on macOS 14+).
- **`app.spotlight(query)`** — find files by name or content via
  NSMetadataQuery (home scope, up to 100 paths) — no `mdfind` spawn.

## 0.19.0 — 2026-07-14

Window superpowers — the primitives behind overlays, HUDs, desktop pets,
and window managers.

- **Window stacking & behaviour** — `win.setClickThrough(bool)` (mouse
  events pass straight through — draw-on-screen overlays), `win.setLevel(
  'normal'|'floating'|'overlay'|'desktop')` (`overlay` floats above almost
  everything incl. most fullscreen apps; `desktop` pins behind normal
  windows for wallpaper/pets), `win.setAllSpaces(bool)` (follow the user
  onto every Space). All three round-trip through `getState()` and have
  backend twins on `app` and `app.window(id)`.
- **`app.selectedText()`** — the text selected in the frontmost app, for
  PopClip-style popovers (Accessibility permission; `null` if none).
- **`app.otherWindows()` / `app.moveWindow(pid, rect)`** — enumerate other
  apps' on-screen windows and move/resize their frontmost window: a
  Rectangle/Magnet "snap the active window" primitive (Accessibility).
- **`tray.position()`** — the tray icon's on-screen rect, so a dropdown
  window can anchor under it (the Tauri `positioner` gap).

## 0.18.0 — 2026-07-14

- **`app.recorder`** — record a display to an .mp4: `start({ path,
  screenId? })` resolves once capture is running, `stop()` resolves
  `{ path, duration }` once the file is finalized. ScreenCaptureKit's
  `SCStream` feeds H.264 frames into an `AVAssetWriter` on a dedicated
  serial queue. Video only for now (no audio track); one recording at a
  time. Needs the `'screen'` permission and macOS 14+ — the permission is
  preflighted so an ungranted `start()` rejects immediately with a clear
  message instead of hanging.

## 0.17.0 — 2026-07-14

Media tier — apps that behave like real media citizens. (Screen recording
to .mp4 is next, in its own release.)

- **`app.nowPlaying`** — `set({ title, artist, album, duration, elapsed,
  playing })` puts your app in Control Center and on the lock screen, and
  arms the hardware media keys; presses (F7/F8/F9, AirPods taps, Control
  Center transport) arrive as the `media-key` page event /
  `onMediaKey(info, app)` export with `{ command: 'play'|'pause'|'toggle'|
  'next'|'previous'|'seek', time? }`. `clear()` tears it down. (MediaPlayer
  framework: MPNowPlayingInfoCenter + MPRemoteCommandCenter.)
- **`app.say(text, { voice, rate })`** — text-to-speech via
  AVSpeechSynthesizer; resolves when playback *finishes* (false if
  interrupted). `app.voices()` lists the installed voices (`{ id, name,
  lang, quality }`), `app.stopSpeaking()` interrupts.
- **Notification actions & reply** — `tiny.notify(title, body, { actions:
  [{ id, title, reply?, placeholder?, destructive? }] })` adds buttons or a
  text-reply field to a banner (packaged apps); taps come back as the
  `notification-action` event / `onNotificationAction(info, app)` export
  with `{ id, action, reply }` (reply = the typed text).

## 0.16.0 — 2026-07-14

The Mac's superpowers — the frameworks macOS ships that JS can't normally
touch. All on-device, plus auto-update quality-of-life.

- **`app.pickColor()`** — the system eyedropper (NSColorSampler): pick any
  pixel in any app, **no screen-recording permission needed**. Resolves
  `'#rrggbb'`, or `null` when the user cancels.
- **`app.ocr(path)`** — on-device OCR via Vision (accurate mode) →
  `{ text, blocks: [{ text, confidence, box }] }` with normalized top-left
  boxes. `captureScreen()` + `ocr()` = screenshot-to-text in two calls.
- **`app.thumbnail(path, size?)`** — a preview png for *any* file type
  Quick Look understands (PSD, video, 3D models, …) → `{ path, width,
  height }`, rendered @2x.
- **`app.secrets`** — Keychain-backed `get`/`set`/`delete` (generic
  passwords under the app id) — the keytar/safeStorage role. Tokens go
  here, never in `tiny.store`; values survive reinstalls.
- **`app.authenticate(reason)`** — Touch ID, falling back to the account
  password sheet → `true`/`false` (false covers cancel).
- **`app.applescript(source)`** — run AppleScript in-process (NSAppleScript,
  no osascript spawn) under the same `'automation'` permission the
  framework already manages. Control Music, Spotify, Finder — anything
  scriptable.
- **Auto-update polish** — `"update": { "auto": "launch" | "daily" }`
  checks in the background (packaged apps only) and fires the
  `update-available` page event / `onUpdateAvailable(info, app)` backend
  export with `{ current, latest, notes }`; `tinyjs publish --notes "…"`
  (or `--notes-file FILE`) puts release notes in the manifest, and
  `update.check()` now returns them.

## 0.15.0 — 2026-07-14

Round three: preview, capture, and idle.

- **`app.quickLook(paths)`** — the real Finder-spacebar preview panel
  (QLPreviewPanel — no `qlmanage` spawn) for any file(s) tinyjs apps
  manage; an array pages with the arrow keys, `quickLook()` closes.
- **`app.captureScreen(screenId?)`** — screenshot a display via
  ScreenCaptureKit → `{ path, width, height }` (a png in the temp dir the
  caller owns). Takes a display id from `screens()`, defaults to the
  primary. Needs the `'screen'` permission and macOS 14+ — rejects with
  the reason otherwise (the framework is weak-linked, so older systems
  still launch).
- **`app.idleTime()`** — seconds since the user's last input, session-wide
  (pause pollers / dim UI when they walk away).

## 0.14.0 — 2026-07-14

More stop-shelling-out, round two.

- **`app.power`** — `preventSleep(reason, { display? })` / `allowSleep()`
  hold a single IOPMAssertion instead of spawning `caffeinate`: the
  assertion dies with the process, so a crashed app can never wedge the
  Mac's sleep. The reason string shows up in `pmset -g assertions`.
- **`app.frontmostApp()`** — `{ name, bundleId, pid }` of the active app
  (who focus returns to after `hide()` — pairs with `paste()`).
- **`app.beep()` / `app.playSound(target)`** — system beep, a system sound
  by name (`'Ping'`, `'Glass'`, …), or an audio file path; resolves `false`
  if the sound didn't load. No more `afplay` spawns.
- **`win.share({ text?, url?, paths?, x?, y? })`** — the native share sheet
  (NSSharingServicePicker), anchored at page coordinates — pass the click's
  `clientX`/`clientY`. Backend twin: `app.window(id).share(opts)`.

## 0.13.0 — 2026-07-14

Theme: stop shelling out. Five small APIs replacing the `open`/osascript
spawns and hardcoded paths tinyjs apps still needed.

- **`app.shell`** — `open(target)` (URL with any scheme, or a file path,
  in its default app), `reveal(path)` (show in Finder), `trash(path)`
  (move to the Trash — recoverable, prefer it over deleting user files).
  Each resolves `true` or rejects with the reason (`'no such file'`,
  `'no application registered for URL'`).
- **`app.launchAtLogin`** — `get()`/`set(v)` →
  `'enabled' | 'disabled' | 'requires-approval' | 'unsupported'` via
  SMAppService. Packaged .apps on macOS 13+; `'requires-approval'` means
  the user must allow it in System Settings → General → Login Items. Dev
  mode reports `'unsupported'` (no bundle identity to register).
- **`app.screens()`** — every display in the same top-left coordinates as
  `setPosition`: `{ id, name, x, y, width, height, visible, scale,
  primary }` — `visible` excludes the menu bar and Dock, so multi-monitor
  palette placement is one call.
- **`app.paths`** — `{ home, data, cache, logs, temp, downloads, desktop,
  documents }`, per-app-id where it matters. Prefer these over hardcoding
  `~/Library` paths (they're what a future non-macOS backend would remap).
- **`app.dock`** — `setBadge('3')` / `setBadge('')`, `bounce()` /
  `bounce({ critical: true })`.
- Docs: noted that `'screen'` permission never reads `'undetermined'`
  (macOS only exposes a yes/no preflight), and that dev-mode TCC grants
  attach to the shared launcher binary rather than your app.

## 0.12.1 — 2026-07-14

- **Clipboard metadata** — `clipboard.read()` now also returns `concealed`
  (the org.nspasteboard Concealed/Transient markers password managers set —
  clipboard-history apps must skip these), `sourceApp` (`{ name, bundleId }`
  of the app the content came from: the frontmost app when the change was
  first noticed — exact while `watch()` runs, best-effort on a later read;
  your own `write()`s are attributed to your app), `sourceURL` (the page a
  Chromium-browser copy came from), and `imageSize` (`{ width, height }` in
  pixels, so no more shelling out to `sips` for dimensions).
- Fix: a backend `export function onClipboardChange(e)` was silently dead in
  `tinyjs dev`/`build` apps — the generated entry didn't forward it to
  `createApp` (direct `createApp({ onClipboardChange })` users were fine).

## 0.12.0 — 2026-07-14

- **Microphone + camera** — `getUserMedia()` now just works in tinyjs pages:
  the launcher answers WebKit's per-origin media-capture prompt itself, so
  users see a single system dialog naming your app instead of a double prompt
  naming `file://` or localhost. `tiny.app.permissions.check/request` gain
  `'microphone'` and `'camera'` (the TCC layer underneath, via
  AVCaptureDevice) for onboarding flows.
- **`"permissions"` in tinyjs.json** — packaged apps declare
  `{ "microphone": "why", "camera": "why" }`: the strings are injected as
  `NSMicrophoneUsageDescription`/`NSCameraUsageDescription` (required — macOS
  kills a bundled app that captures without them), and builds signed with
  `signIdentity` get the matching hardened-runtime device entitlements
  (`com.apple.security.device.audio-input`/`.camera`) — without which the
  hardened runtime denies capture outright even after the TCC grant.

## 0.11.0 — 2026-07-14

- **Native clipboard** — `tiny.clipboard` / `app.clipboard`: NSPasteboard in
  the long-lived launcher process. `read()` returns
  `{ kind, changeCount, text, html, paths, image, color }` (image lands as a
  png temp file), `write()` takes any combination of the same (image: png
  path, data: URL, or base64), `changeCount()` is a cheap change probe, and
  `watch(ms)`/`onChange` push `clipboard-change` events with a `self` flag
  for your own writes (backend: `onClipboardChange` auto-starts the
  watcher). No more per-second `osascript`/`pbpaste` spawns or scratch-file
  plumbing — and multi-file writes can't lose their tail to the short-lived
  writer flush race.
- **Drag files out of the app** — `tiny.win.startDrag({ files, image? })`
  (alias `tiny.win.dragOut`) starts a native NSDraggingSession from a page
  mousedown, so real files can be dragged into Finder, Slack, or any other
  app. Optional custom drag-image png; file icons (stacked for multi-file
  drags) otherwise.
- **Native keystrokes** — `tiny.app.keystroke('cmd+v')` / `tiny.app.paste()`
  post a CGEvent from the launcher: one permission (Accessibility) whose
  prompt names *your app*, instead of osascript→System Events needing
  Automation + Accessibility with prompts naming osascript or the terminal.
  Resolves `{ ok, trusted }` so apps can detect the missing grant.
- **Permission checks** — `tiny.app.permissions.check(name)` / `request(name)`
  for `accessibility`, `screen`, `notifications`, and
  `automation[:<bundle-id>]` → `granted` / `denied` / `undetermined` /
  `unsupported`, so apps can build a proper onboarding flow ("needs
  Accessibility to paste — Open Settings") instead of discovering failure at
  first use. `request` prompts (Accessibility opens System Settings pointed
  at the app).
- **hide() now hides the app, not just the window** — `tiny.win.hide()` /
  `app.hide()` use `NSApp hide`, so macOS deactivates the app and returns
  focus to the previously active app automatically. Palettes can
  `hide()` → `paste()` with no frontmost-pid bookkeeping. Secondary windows
  (`app.window(id).hide()`) still just order out. `show()` unhides +
  activates as before.
- **`show({ activate: false })`** — surface the window without stealing
  focus, for overlay/HUD panels that must appear over the active app
  (clicking the window still activates normally).
- **`tiny.app.mousePosition()`** — cursor position three ways:
  global (`x`/`y`, same top-left coordinates as `win.setPosition` — open a
  palette at the mouse), `window` (relative to the calling window's content
  area in `clientX`/`clientY` units, with an `inside` flag — valid even
  while the cursor is outside the window), and `screen` (the display the
  cursor is on: `{ x, y, width, height, scale }`).
- Fix: builds with a real `signIdentity` now sign with the **hardened
  runtime** and a secure timestamp (`codesign --options runtime
  --timestamp`) — both required by notarization, which previously rejected
  the .app. Ad-hoc builds are unchanged. codesign stderr is no longer
  swallowed when a real identity is used, so keychain errors surface.
- Docs: README gains a "Distributing to other people" walkthrough — why
  ad-hoc-signed apps are blocked on other Macs, that Developer ID +
  notarization require the paid Apple Developer Program ($99/yr, a free
  Apple ID won't do), and the one-time cert / `notarytool
  store-credentials` setup ending in `tinyjs build && tinyjs notarize`.

## 0.10.2 — 2026-07-13

- Fix: `app.notify()` / `tiny.notify()` can no longer crash the backend when
  fired without `await`. The dev-mode osascript fallback could reject (e.g.
  `ENOENT` when launched with a minimal `PATH`), and an unhandled rejection
  kills the txiki process. notify now uses the absolute `/usr/bin/osascript`
  path and never rejects — it resolves `false` if delivery failed.

## 0.10.1 — 2026-07-13

- Fix: Vite-template scaffolds now include a `backend/tsconfig.json`
  (`jsconfig.json` for JS templates), so editors resolve `tjs` /
  `TinyApiHandler` / `TinyApp` in the backend — the Vite tsconfig only covers
  `src/`, and TS inferred projects ignore sibling ambient `.d.ts` files.

## 0.10.0 — 2026-07-13

- **Vite / React / Vue / TypeScript** — `tinyjs new myapp --template react-ts`
  (vue-ts, svelte-ts, solid-ts, vanilla-ts, …) scaffolds create-vite wired to
  tinyjs. `tinyjs dev` runs the Vite dev server with HMR inside the native
  window (`frontend.devUrl`); `tinyjs build` runs `frontend.build` and ships
  `frontend.dist`. The zero-dependency default scaffold is unchanged.
- **TypeScript backends** — a `.ts` backend entry (configurable via
  `"backend"`) is bundled with esbuild, which also makes npm packages usable
  in the backend.
- **`tiny` is injected everywhere** — no `tiny.js` script tag; every page in
  every window (file:// or dev server) gets the global automatically.
- **TypeScript definitions** — `types/tiny.d.ts` ships in scaffolds: the full
  `tiny.*` surface, `TinyApp`, `TinyApiHandler`, window state, menus, chrome.
- Vite builds run under `file://` (WebKit file-access flags — module scripts
  with `crossorigin` now load).

## 0.9.0 — 2026-07-13

- **Menu-bar agents** — `"activation": "accessory"` in tinyjs.json launches
  the app with no Dock icon and the window hidden, with no flash of either
  (packaged apps get `LSUIElement`; dev mode matches). Show the window later
  with `tiny.win.show()`; `setDockVisible(true)` restores a regular app.
- **SF Symbol tray icons** — `tiny.tray.set({ icon: 'sf:cup.and.saucer.fill' })`
  renders a crisp, auto-templating menu-bar icon straight from an SF Symbol
  name; no shipped assets needed. PNG paths still work.
- **Tray primary action** — `tiny.tray.set({ primaryAction: true, menu })`
  splits clicks: left click fires `tiny.tray.onClick` (backend
  `onTray(null, app)`), right/ctrl-click opens the menu — the classic
  Caffeine-style toggle.

## 0.8.0 — 2026-07-13

- **Multiple windows** — `tiny.win.open(id, { page, title, size })` turns any
  frontend html file into a window. Every window runs the full `tiny.*`
  bridge; `win.*` calls target the caller's own window; `tiny.win.id`,
  `tiny.win.close()`, `tiny.win.windows()`. Backend: `app.openWindow`,
  `app.window(id).…` (eval/push/close/title/size/position/chrome/getState),
  `app.push` broadcasts, `onWindowClosed` export + `window-closed` event,
  and api handlers receive `meta.window` (who called).
- **Unified page RPC** — one injected bridge for all windows (call ids carry
  the window id), replacing the webview library's binding; dialogs reply
  through the same path.

## 0.7.0 — 2026-07-13

- **Frameless windows** — `tiny.win.setChrome({ frame, trafficLights,
  transparent, vibrancy })` (also `"chrome"` in tinyjs.json; packaged apps
  apply it before first paint). Frameless keeps native resize edges, focus,
  shadows, and fullscreen. Vibrancy materials: sidebar, hud, menu, popover,
  window, content, and friends.
- **Drag regions** — mark any element `data-tiny-drag` to make it a titlebar:
  dragging moves the window natively, double-click zooms; buttons/inputs
  inside are excluded automatically (`data-tiny-nodrag` to opt out).
- `tiny.win.zoom()`, `tiny.win.startDrag()`; `getState()` now reports the
  chrome state.
- Fix: window chrome and `setResizable(false)` now survive `setSize()` (the
  webview library rewrites the styleMask on every resize; overrides are
  re-applied).

## 0.6.0 — 2026-07-13

- **Native Notification Center** — packaged apps built with a signing
  identity (even "Apple Development") get real banners: the app's own icon,
  a proper permission prompt on first `tiny.notify()`, banners while
  frontmost, and clicks routed back (`tiny.app.onNotificationClick` /
  backend `onNotificationClick`, cold starts included). `notify` gains
  `{ id, subtitle, sound }`. Ad-hoc and dev builds fall back to the
  osascript banner automatically; a user's explicit "Don't Allow" is
  respected (notifications drop silently).

## 0.5.0 — 2026-07-13

- **Stateful menus** — items support `checked`, `enabled: false`, and nested
  `submenu` everywhere (menu bar, tray, context menus).
  `tiny.menu.update(id, { label?, checked?, enabled? })` patches a live item
  without redeclaring; `tiny.menu.get(id)` reads one back.
- **Window read-backs** — `tiny.win.getState()` returns position, size,
  fullscreen/minimized/visible/focused/always-on-top/resizable, and screen
  info; set → get round-trips exactly. New `tiny.win.restore()` and absolute
  `tiny.win.setFullscreen(bool)`.
- Wire protocol grows its first launcher→backend replies (`GET`/`GOT`).
- **`tiny.app.info()`** — `{ version, tinyjs, runtime }`: the app's version,
  the tinyjs version it was built with, and the txiki.js runtime.

## 0.4.0 — 2026-07-13

- **Deep links** — claim a scheme with `"urlScheme": "myapp"` in tinyjs.json;
  `open myapp://…` delivers `tiny.app.onOpenUrl` / backend `onOpenUrl`, on
  cold start too (launch events are buffered until the app is ready).
- **File associations** — `"fileExtensions": ["md"]` puts you in "Open With";
  files arrive as real paths via `tiny.app.onOpenFiles` / `onOpenFiles`,
  including Dock-icon drops.
- **Single instance** — a second `open` (URL, file, Dock) activates the
  running app and delivers the event to it instead of launching a copy.
- **New .app architecture** — the launcher is now the bundle executable (it
  listens on the socket and spawns the backend), which is what makes the
  above work; the C shim is gone. Dev mode and the bare `dist/<name>` binary
  keep the backend-first arrangement. Bundles still sign and notarize clean.

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
