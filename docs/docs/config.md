---
title: "tinyjs.json"
---

# tinyjs.json

<p class="tagline">every key is optional except name</p>

`tinyjs.json` sits at the root of the project and describes the app: its name and id, how it's laid out and built, its windows, and how it plugs into the OS. `tinyjs dev`, `build`, `publish` and `notarize` read it. A new project starts with:

```json
{
  "$schema": "https://tinyjs.app/schema/tinyjs.schema.json",
  "name": "myapp",
  "title": "My App",
  "size": "960x640",
  "id": "com.example.myapp",
  "version": "0.1.0"
}
```

The `$schema` line gives editors completion and type checks for every key below — see [editor schema](./config-schema). Keys that need different values per OS go in a [`macos` / `windows` / `linux` block](#per-os-overrides), and two signing keys can come from [environment variables](#environment-variables) instead.

## the app {#the-app}

### `name` {#name}

`string` · **required**

The executable's name: `dist/<name>`, `<name>.exe`, and the name in dmg and zip files. Also the default for `title` and `id`.

### `title` {#title}

`string` · default: `name`

What people see: the window title, the menu bar's app name, the `.app` folder (`dist/<title>.app`), the dmg's volume name, and the app's name in Linux and Windows file associations.

### `id` {#id}

`string` · default: `com.example.<name>`

A reverse-domain identifier, unique to your app (`com.yourcompany.myapp`). The OS uses it to tell apps apart — it's the macOS bundle identifier — and tinyjs keys the app's own storage to it: the data folder, [store](./store), [secrets](./secrets), saved window positions, and single-instance detection. Changing it after release starts users over with empty settings, so pick it before you ship.

### `version` {#version}

`string`, like `"1.2.0"`

Shown in the About panel and returned by `tiny.app.info()`. [`tinyjs publish`](./cli#publish) requires it, and [auto-update](./auto-update) compares it (the first `major.minor.patch`) to decide whether a release is newer.

### `minTinyjsVersion` {#minTinyjsVersion}

`string`, like `"0.62.0"`

The oldest tinyjs this project works with. An older tinyjs refuses to `dev` or `build` it and says to run `tinyjs update`, instead of half-working with calls that do nothing. `tinyjs new` and `tinyjs wrap` fill it in with the version that made the project.

### `$schema` {#schema}

`string`

The [JSON Schema](./config-schema) URL for editors. tinyjs ignores it; [`tinyjs doctor`](./doctor) notes when it's missing.

## code and build {#code-and-build}

### `backend` {#backend}

`string` path · default: the first of `src/backend/main.js|ts`, `src/main.js|ts`, `backend/main.js|ts`

The [backend](./backend) entry file. Its folder is the backend's root: a `.js` backend ships that whole folder, and a `.ts` entry is bundled from it with esbuild (so npm packages work). Code both sides use goes in a [shared](./shared-code) folder beside it. `tinyjs dev` restarts the backend when anything in that folder changes.

### `frontend` {#frontend}

`object`

Where the page comes from. A plain project needs nothing here: the page is `src/frontend/`, served as-is.

```json
{ "frontend": { "dir": "web" } }
```

A bundler project (what `tinyjs new --template` writes) gives build and dev commands instead:

```json
{
  "frontend": {
    "build": "npm run build",
    "dist": "dist",
    "dev": "npm run dev",
    "devUrl": "http://127.0.0.1:5173"
  }
}
```

| field | |
|---|---|
| `dir` | the plain page folder, default `src/frontend`. It needs an `index.html` (unless the app has no main window or uses `url`). |
| `build` | a shell command [`tinyjs build`](./cli#build) runs first; its output ships instead of `dir`. The project's packages must be installed. |
| `dist` | where `build` writes, default `dist`. |
| `dev` | a shell command [`tinyjs dev`](./cli#dev) starts — the dev server. Used only with `devUrl`. |
| `devUrl` | the dev server's address. `tinyjs dev` waits for it to answer, then loads it, and the server's own hot reload takes over. Leave `dev` out to run the server yourself. Dev only; builds ship `dist`. |
| `reload` | `false`: `tinyjs dev` doesn't reload the page when files change, for a watcher of your own that does. |

### `inject` {#inject}

`string` path

A script run at document start in every window and every page, before the page's own scripts — for polyfills, or for adjusting a [wrapped site](./site-wrappers). A `.ts` file is bundled with esbuild.

### `icon` {#icon}

`string` path · default: `assets/icon.png`

The app icon, a 1024×1024 PNG. macOS turns it into `AppIcon.icns`; Windows stamps it into `<name>.exe`; Linux ships it as `icon.png` and uses it for the app's desktop entry. See [assets](./assets).

### `resources` {#resources}

`string[]` · default: everything in `resources/`

Which files and folders inside `resources/` to ship. Omit it to ship the whole folder; `[]` ships none. See [resources](./resources).

### `executables` {#executables}

`string[]` · default: the right files from `executables/` for the OS and CPU being built

Which programs inside `executables/` to ship. Each must be a native program for the target OS. `[]` ships none. See [executables](./executables).

### `executableEntitlements` {#executableEntitlements}

`object` · macOS

Entitlements for a sidecar when the app is signed with a Developer ID, by file name:

```json
{ "executableEntitlements": { "ffmpeg": ["com.apple.security.cs.disable-library-validation"] } }
```

Sidecars not listed keep the entitlements they were built with. See [executables](./executables).

### `extras` {#extras}

`string[]` · default: the top level of `extras/`, plus `extras/<os>/`

Files for the person downloading the app — a README, a licence — placed beside it: in the Windows and Linux folder and archive, and at the root of the macOS dmg (never inside the app). `[]` ships none. See [extras](./extras).

### `helpBook` {#helpBook}

`string` · macOS

A help book folder inside `assets/` (for example `"My App Help"`), so the Help menu opens it. Set it in the `macos` block. See [assets](./assets).

## windows {#windows}

### `size` {#size}

`string` · default: `"960x640"`

The main window's starting size, `"WIDTHxHEIGHT"` — the page area, not counting the title bar. See [tiny.win](./window).

### `mainWindow` {#mainWindow}

`boolean` · default: `true`

`false` starts the app with no main window and no page at all — a [tray](./tray) or background agent that opens [windows](./multiple-windows) when it needs them. Can't be combined with `url`.

### `chrome` {#chrome}

`object`

How the main window looks before the page draws anything: frameless, translucent, custom title-bar controls. Same fields as [`tiny.win.setChrome()`](./window), which changes them later.

```json
{ "chrome": { "frame": false, "vibrancy": "hud", "windowControls": ["close"] } }
```

| field | | macOS | Windows | Linux |
|---|---|:-:|:-:|:-:|
| `frame` | `false`: no title bar or border — make your own with [`data-tiny-drag`](./window) | ✓ | ✓ | ✓ |
| `windowControls` | `true` / `false`, or a list of `"close"` `"minimize"` `"maximize"` | ✓ | close always shows with the others | best effort |
| `transparent` | see-through window background | ✓ | main window, set here only | ✓ |
| `vibrancy` | blurred background material, such as `"hud"` `"sidebar"` `"popover"`; `false` for none | ✓ | Windows 11: Acrylic for `hud`, `popover`, `menu`; Mica otherwise | — |
| `squareCorners` | no rounded corners | ✓ | ✓ | ✓ |
| `acceptsFirstMouse` | a click on an inactive window also acts on what it hits | ✓ | — | — |
| `windowControlsPos` | `{ "x": 12, "y": 14 }`: where the traffic lights sit | ✓ | — | — |
| `menu` | `false`: no menu bar in this window | — (one menu bar per app) | ✓ | ✓ |
| `panel` | a non-activating panel: takes the keyboard without activating the app, so it can open over full-screen apps — see [window fx](./window-fx#panel) | ✓ | — | — |

### `rememberWindows` {#rememberWindows}

`boolean` · default: `false`

Put every window back where the user left it — position, size, maximized or full screen — remembered separately for each set of plugged-in screens. The main window stays hidden until it's been placed, so it doesn't jump. See [tiny.win](./window).

### `offscreenRescue` {#offscreenRescue}

`boolean` · default: `true`

When the screens have changed since the last run (a display unplugged), windows that would open where nobody can see them are moved onto a screen. `false` turns that off, for apps that park windows off-screen on purpose. [`tiny.win.ensureOnScreen()`](./window) works either way.

### `windowPlacement` {#windowPlacement}

`boolean` · Linux

`true` if the app places its own windows (`setPosition`, `center`). Wayland doesn't let apps do that, so on Linux this runs the app through X11 (XWayland) when it's available. Does nothing on macOS and Windows.

### `activation` {#activation}

`"regular"` (default) or `"accessory"`

`"accessory"` makes a menu-bar style app: no Dock icon on macOS, no taskbar button on Windows and Linux, and the window starts hidden until the app shows it — pair it with a [tray](./tray) icon.

## behaviour {#behaviour}

### `contextMenu` {#contextMenu}

`boolean` · default: `true`

`false` hides the webview's built-in right-click menu (Reload, Inspect…). A [custom context menu](./menus) still shows.

### `debug` {#debug}

`true`, `"open"` or `false` (default)

Developer tools in a built app: `true` lets F12 open them (⌥⌘I on macOS too), `"open"` opens them with every window. `tinyjs dev` always has F12.

### `browserAccelerators` {#browserAccelerators}

`boolean` · default: `false` · Windows

`true` keeps WebView2's own keyboard shortcuts (Ctrl+P print, Ctrl+F find, Ctrl+R reload…). They're off by default so they can't fire underneath your app's own shortcuts.

### `about` {#about}

`"menu"` · macOS

The app menu's About item sends `onMenu('about')` and the page's `menu` event instead of showing the standard About panel, so you can show your own. `about` becomes a reserved menu item id. See [menus](./menus).

### `attribution` {#attribution}

`string` · macOS · default: "Made with tinyjs"

The credit line in the standard About panel. An `assets/Credits.html` (or `.rtf`) replaces it with formatted credits — see [assets](./assets).

### `downloads` {#downloads}

`"auto"` (default), `"ask"` or `"deny"`

What happens when the page starts a download: `auto` saves to the Downloads folder, `ask` shows a save dialog, `deny` refuses. The backend hears each download's progress through `onDownload`, but can't change the decision — see [site wrappers](./site-wrappers).

### `popups` {#popups}

`"external"` (default), `"window"` or `"deny"`

What `window.open` and `target="_blank"` links do: `external` opens them in the default browser, `window` in a new app window (keeping `window.opener`, for sign-in popups), `deny` blocks them. The backend can decide per popup with `onWindowOpen` — see [site wrappers](./site-wrappers).

### `userAgent` {#userAgent}

`string`

Replaces the webview's user agent, for sites that sniff it.

### `readAccess` {#readAccess}

`true` or a folder path · macOS

Lets the page load local files from outside its own folder — your home folder with `true`, or one folder — through [`tiny.fileURL(path)`](./api). Windows and Linux already allow it.

## OS integration {#os-integration}

These are registered for built apps only — part of the `.app` on macOS, added when the app first runs on Windows and Linux — never by `tinyjs dev`. See [deep links & files](./deep-links).

### `urlScheme` {#urlScheme}

`string` or `string[]`

Links like `myapp://…` open the app and arrive in `onOpenUrl`. A list registers several schemes.

### `fileExtensions` {#fileExtensions}

`string[]`, like `["md", "txt"]`

File types the app can open: they're listed under "Open With", and opened files arrive in `onOpenFiles`. Windows adds the app to "Open with" without making it the default.

### `openFolders` {#openFolders}

`boolean` · macOS, Linux

Folders can be opened with the app too (dropped on its icon, or "Open With"); they arrive in `onOpenFiles`.

## permissions and audio {#permissions-and-audio}

### `permissions` {#permissions}

`object`

Camera, microphone, speech recognition and location, each with the reason shown to the user:

```json
{ "permissions": { "microphone": "To record voice notes", "camera": "For video calls" } }
```

- **macOS**: the reasons appear in the system permission prompts, and declaring a key is what lets the app ask at all. `speechRecognition` is macOS only and needs `microphone` too. `location` is macOS only: it lets the app ask with [`permissions.request('location')`](./permissions#names), which [`system.wifi()`](./system#wifi) needs to read the network name. Signed builds get the matching hardened-runtime entitlement.
- **Linux**: `microphone` and `camera` decide whether the page may use `getUserMedia` at all.
- **Windows**: not needed — WebView2 shows its own prompt.

See [permissions](./permissions).

### `audioTap` {#audioTap}

`"app"`, `"system"` or off (default)

Turns on [`tiny.audioTap`](./audiotap), which reads audio being played: `"app"` the app's own, `"system"` everything. Without it, `audioTap.start()` fails with `not-declared`.

### `audioTapReason` {#audioTapReason}

`string` · macOS · default: "&lt;title&gt; reads audio output for metering and visualization."

The reason shown in macOS's audio capture prompt.

## signing and notarizing (macOS) {#signing}

### `signIdentity` {#signIdentity}

`string` · macOS · default: ad-hoc

The codesigning certificate, by name: `"Developer ID Application: Your Name (TEAMID123)"`. Without it the app is signed ad-hoc — it runs on your Mac, but Gatekeeper blocks it on others. `TINYJS_SIGN_IDENTITY` overrides it. See [notarize](./cli#notarize) for setting up a certificate.

### `notarize` {#notarize}

`{ "profile": "…" }` · macOS

The `notarytool` keychain profile [`tinyjs notarize`](./cli#notarize) submits with. `TINYJS_NOTARY_PROFILE` overrides it.

For credentials stored in a file-based keychain, add `"keychain": "ci.keychain-db"` beside `profile`. The path is absolute or relative to the project directory; `TINYJS_NOTARY_KEYCHAIN` overrides it. Tinyjs passes it as `notarytool --keychain`, so use the same keychain when storing credentials and unlock it before submitting. Omit `keychain` (or set it to `null` or `""`) to use the data protection keychain.

## updates {#updates}

### `update` {#update}

`object`

```json
{ "update": { "url": "https://example.com/myapp/manifest.json", "auto": "daily" } }
```

| field | |
|---|---|
| `url` | the `manifest.json` [`tinyjs publish`](./cli#publish) wrote, wherever you host it. Must be `https://` (`http://localhost` works for testing). |
| `auto` | `"launch"`: check a few seconds after the app starts; `"daily"`: and then once a day. A newer version fires `onUpdateAvailable` and the page's `update-available` event. Leave it out to check only when you call `tiny.app.update.check()`. Built apps only. |
| `publicKey` | require updates to be signed: the base64 public key, or a list of keys (a signing key plus a backup). See [signed updates](./signed-updates). |

See [auto-update](./auto-update).

## site wrappers {#site-wrappers}

### `url` {#url}

`string`

Makes the main window a hosted web page instead of your own frontend — wrapping a web app as a desktop app. [`tinyjs wrap`](./cli#wrap) writes this, plus `api`, `popups` and `downloads`, for you. Can't be combined with `mainWindow: false`. See [site wrappers](./site-wrappers).

### `api` {#api}

`string`, `string[]` or `object`

Which `tiny` calls pages may make. **Set this whenever the app loads a site you don't control**: `tiny` is available to every page in the app, so without it a wrapped site's JavaScript could call anything your backend can do. Checked in the backend, by the calling frame's real origin.

| value | |
|---|---|
| omitted, `"all"` | no limit — fine for an app that only shows its own pages |
| `"none"` | no calls at all |
| `"wrapper"` | window, menu, tray, dialog, notification and store calls, plus a few read-only ones — no files, processes or network. Your own `api` methods are off; enable them by name |
| `["notify", "store.*"]` | only these |
| `{ "preset", "enable", "disable", "origins" }` | start from a preset, then turn calls on and off. `enable` wins over `disable` |

Names are the call's name (`notify`, `win.setTitle`), `prefix.*` for a group, or `*` for all. `origins` gives each site its own rules, first matching key wins (`*` in a key matches anything):

```json
{
  "api": {
    "disable": ["*"],
    "enable": ["win.*"],
    "origins": {
      "https://app.example.com": ["notify", "store.*", "media.microphone"],
      "file://*": "all"
    }
  }
}
```

A page whose origin matches no key gets the top-level `enable`/`disable` lists. `media.camera`, `media.microphone` and `media.proxy` in an origin's list let that site use the camera or microphone without a prompt (macOS) or at all (Linux), and use `tiny.proxyURL`. The same rules decide which events such a page receives — your own `app.push` events need `event.<name>` (see [events](./site-wrappers#events)). An unknown preset name blocks everything, and says so. See [site wrappers](./site-wrappers) for the full model.

## per-OS overrides {#per-os-overrides}

Root keys apply everywhere. A `macos`, `windows` or `linux` block is merged on top for that OS, so the keys that genuinely differ (an `.ico`, a macOS signing identity, a vibrancy that means nothing elsewhere) don't make you write three of everything. The block names are what `tiny.system.os()` returns.

```jsonc
{
  "name": "myapp",
  "chrome": { "frame": false },

  // merged on top on that OS — for dev, build and publish alike
  "macos":   { "signIdentity": "Developer ID Application: …",
               "chrome": { "vibrancy": "hud" } },
  "windows": { "icon": "assets/icon.ico" },
  "linux":   { "icon": "assets/icon-512.png" }
}
```

Objects merge, so the macOS block above ends up with `{ "frame": false, "vibrancy": "hud" }` rather than losing the root's `frame`. Everything else — strings, numbers, lists — replaces the root value.

## environment variables {#environment-variables}

Signing and notarization settings can come from the environment instead, so an identity, profile or keychain path can live in your shell or CI configuration rather than a committed file:

| variable | sets |
|---|---|
| `TINYJS_SIGN_IDENTITY` | [`signIdentity`](#signIdentity) |
| `TINYJS_NOTARY_PROFILE` | [`notarize.profile`](#notarize) |
| `TINYJS_NOTARY_KEYCHAIN` | [`notarize.keychain`](#notarize) |

They win over the file — root and OS block alike — and the build prints a line when one replaces a value the file set (`==> signIdentity from TINYJS_SIGN_IDENTITY (overriding tinyjs.json)`), so a variable left exported in a shell can't quietly change how a project signs. The order is **root → OS block → environment**. The other `TINYJS_*` variables are for running and debugging — see [cli](./cli#environment).
