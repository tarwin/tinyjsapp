---
title: "cli"
---

# cli

<p class="tagline">every command, every flag</p>

Run project commands from the app's folder (the one with `tinyjs.json`). Flags go after the command and take their value as the next argument (`--arch x86_64`); `new` and `wrap` also accept `--flag=value`. Unknown flags are ignored, except by `doctor` and `update-project`, which take none.

| command | what it does |
|---|---|
| [`tinyjs new <dir>`](#new) | scaffold an app, plain or with a Vite framework |
| [`tinyjs wrap <url>`](#wrap) | turn a website into a desktop app |
| [`tinyjs dev`](#dev) | run the app with hot reload |
| [`tinyjs build`](#build) | build the app for this OS |
| [`tinyjs publish`](#publish) | build, zip and write the auto-update manifest |
| [`tinyjs notarize`](#notarize) | notarize and staple the macOS build |
| [`tinyjs doctor`](#doctor) | find why tinyjs or the project won't run |
| [`tinyjs update`](#update) | update tinyjs itself |
| [`tinyjs update-project`](#update-project) | refresh the project's types and agent skill |
| [`tinyjs uninstall`](#uninstall) | remove tinyjs |
| [`tinyjs version`](#version) | print the version |

Settings that don't change per run live in [tinyjs.json](./config); the [environment variables](#environment) below override a few of them.

## tinyjs new {#new}

```sh
tinyjs new myapp                        # plain HTML/JS, zero dependencies
tinyjs new myapp --template react-ts    # Vite + a framework
tinyjs new myapp --template             # asks for framework and language
```

Creates `<dir>` (it must not exist yet) and names the app after its last path segment. The plain template is a [backend](./backend) in `src/backend/main.js`, a page in `src/frontend/`, an icon in `assets/` and a `tinyjs.json` — no packages, no build step. Then `cd myapp && tinyjs dev`.

`--template` scaffolds a Vite app instead (through `create-vite`): `tinyjs dev` runs Vite's dev server with HMR inside the native window, and `tinyjs build` ships Vite's build. The backend is TypeScript for `-ts` templates, bundled with esbuild, so npm packages work there too.

| flag | |
|---|---|
| `--template [name]` | `vanilla` `react` `vue` `svelte` `solid` `preact` `lit` `alpine`, each with `-ts` for TypeScript. `alpine` is Vite's vanilla template plus [Alpine.js](https://alpinejs.dev) and a component that calls the backend. Without a name it asks (and fails when there's no terminal to ask on). |
| `--pm <manager>` | `npm` `pnpm` `yarn` `bun`, or `vp` for [Vite+](https://viteplus.dev) 1.0+ over one of them (`vp:pnpm`, `vp:bun`…). Without it you pick from the managers that work on this machine, or get the first of npm → pnpm → yarn → bun when there's no terminal. The chosen manager runs the project's dev and build commands from then on. Also `--package-manager`. |
| `--no-install` | don't install dependencies. (`vp` installs as part of creating the project, so only Alpine's extra packages are left out.) |
| `--install` | install without asking. |

Every new project gets TypeScript definitions for `tiny` and the backend (`tiny.d.ts`, `tjs.d.ts`) and a tinyjs skill for coding agents (`.claude/skills/tinyjs/`, `.agents/skills/tinyjs/`). After updating tinyjs, [`update-project`](#update-project) brings them up to date. The `$schema` line in `tinyjs.json` gives editors completion — see [editor schema](./config-schema).

Needs Node.js and a package manager for `--template`; the plain template needs nothing.

## tinyjs wrap {#wrap}

```sh
tinyjs wrap https://app.example.com                 # → ./app-example-com
tinyjs wrap app.example.com mail --name mail --menubar --panel
tinyjs wrap https://app.example.com --force --origins subdomains
```

Makes a [site wrapper](./site-wrappers): an app whose main window is a hosted page rather than your own frontend. It fetches the page for its title and icon, and writes a `tinyjs.json` with the page's URL, popups opening as windows and downloads asking first, and an [API gate](./site-wrappers#api-gate): the site's own origin gets the `"wrapper"` preset (window, menu, tray, dialog, notification, store, clipboard-write and `shell.open` calls), and any other origin the window lands on only window controls. A small backend handles the window options below. The scheme defaults to `https://`; the folder (second argument) and app name default to the host.

| flag | |
|---|---|
| `--name <name>` | app name, and the folder when none is given. |
| `--ua <userAgent>` | user agent for the page (`userAgent` in tinyjs.json). |
| `--menubar` | a menu-bar app: tray icon, no Dock icon, closing hides the window. |
| `--panel` | with `--menubar`: a frameless dropdown under the tray icon that hides when it loses focus. |
| `--top` | keep the window above others. |
| `--external a.com,b.com` | open links to these hosts (and their subdomains) in the default browser. An empty value clears the list. |
| `--origins <which>` | origins the page's own JavaScript may call `tiny` from: `exact` (default — just the page's origin), `subdomains` (the whole site, `https://*.example.com`), or a comma-separated list of URLs. See [the api gate](./site-wrappers). |
| `--yes` | don't ask about origins (use `exact`, or `--origins`). |
| `--force` | wrap again into an existing tinyjs project. |
| `--no-menubar` `--no-panel` `--no-top` | with `--force`: turn that option off. |

Re-wrapping with `--force` keeps what you've changed: title, size, id, version, origins, user agent and any keys you added stay, flags you leave out keep their previous setting, and a hand-edited backend is saved as `src/main.js.bak` before it's regenerated. Wrapping into an existing folder that isn't a tinyjs project fails.

Uses the network: the page, its icon, and (for `--origins subdomains`) the public suffix list, cached for 30 days. If any fetch fails, the defaults are used. Then `tinyjs dev` to try it, and `tinyjs build` to ship it.

## tinyjs dev {#dev}

```sh
tinyjs dev
TINYJS_DEBUG=1 tinyjs dev    # also trace every message across the bridge
```

Runs the app from source. Edit the page (`src/frontend/`, or `frontend.dir`) and it reloads in place without restarting the app; edit the backend's folder or [shared code](./shared-code) and the backend restarts. Devtools are always on in dev (F12, or ⌥⌘I on macOS). Takes no flags.

- **Frameworks**: with `frontend.devUrl` set, `dev` starts `frontend.dev` (your Vite server), waits up to 60 seconds for it to answer, and loads it — the server's own HMR handles the page. Leave `frontend.dev` out to run the server yourself. A watcher of your own that reloads the page sets `"frontend": { "reload": false }`.
- **TypeScript** backends are bundled with esbuild on each start (the project's copy, else `npx esbuild`, which needs Node and the network the first time).
- `resources/` and `executables/` are used in place — see [resources](./resources) and [executables](./executables).
- Prints a one-line notice when a newer tinyjs is out (checked at most once a day).
- In a source checkout of tinyjs, the native launcher is rebuilt first when its sources changed.

Ends when the app quits, with the app's exit code. Working files go in `.build/`, rebuilt on every start.

## tinyjs build {#build}

```sh
tinyjs build
tinyjs build --dmg                # macOS: also a disk image
tinyjs build --arch x86_64        # macOS: for Intel Macs, from any Mac
tinyjs build --universal          # macOS: one app for both
tinyjs build --cli                # also a command-line shim
```

Builds the app for the OS you run it on into `dist/`, which is emptied first. A `frontend.build` command (Vite's build in the templates) runs first; its packages must be installed.

::: tabs
== macOS
- `dist/<title>.app` — the app to distribute: codesigned, with its icon (`AppIcon.icns`, made from [assets/icon.png](./assets)), [resources](./resources), [executables](./executables) (signed too), credits and help book.
- `dist/<name>` — the app as one bare executable for local use, loading its page from `dist/frontend/`.
- `--dmg` adds `dist/<name>-<version>.dmg`: the app, an Applications shortcut and your [extras](./extras).

Signing is ad-hoc unless `signIdentity` (or `TINYJS_SIGN_IDENTITY`) names a Developer ID; then the build adds the hardened runtime and a timestamp, ready for [notarize](#notarize). Ad-hoc apps open on your Mac but Gatekeeper blocks them elsewhere.

== Windows
- `dist/<name>.exe` — the app, with your icon.
- `dist/launcher.exe`, `dist/resources/`, `dist/executables/` — ship the whole folder. The page is packed inside `<name>.exe`.
- [Extras](./extras) are copied in beside them.

If the build can't clear `dist/` because the app is still running, it says which file is in use — quit the app and build again.

== Linux
- `dist/<name>` — the app.
- `dist/launcher`, `dist/icon.png`, `dist/resources/`, `dist/executables/` — ship the whole folder. The page is packed inside `<name>`.
- [Extras](./extras) are copied in beside them.
:::

| flag | |
|---|---|
| `--dmg` | macOS: also build the disk image. |
| `--arch arm64\|x86_64` | macOS: build the app for that CPU only, from either kind of Mac. Artifacts are named `<name>-<version>-macos-<arch>.dmg` / `.zip` so both can ship side by side, and building one arch keeps the other's dmg. The other CPU's runtime is downloaded once (checksum-pinned) and cached. Same as `TINYJS_ARCH`. |
| `--universal` | macOS: one app that runs on both, about 6 MB bigger. Needs the Xcode Command Line Tools (`xcode-select --install`). Same as `TINYJS_UNIVERSAL=1`. |
| `--cli [name]` | also write `dist/bin/<name>` (`<name>.cmd` on Windows), a command that runs the app; files passed to it arrive in [onOpenFiles](./deep-links). Defaults to the app's name. It prints how to put it on your PATH. |

Without `--arch` or `--universal`, a macOS build runs only on the CPU type of the Mac that built it, and says so (`runs on: Apple Silicon (arm64)`). Both flags are ignored, with a note, on Windows and Linux. The bare `dist/<name>` is always for the building Mac's CPU.

## tinyjs publish {#publish}

```sh
tinyjs publish --notes "Fixes the export bug"
tinyjs publish --arch arm64 && tinyjs publish --arch x86_64    # both Macs, one manifest
```

Runs a full [build](#build), then packs it for [auto-update](./auto-update) in `dist/publish/`: the archive plus `manifest.json`, which you upload to the address in `update.url`. Needs `version` in tinyjs.json.

- **macOS**: `<name>-<version>.zip` (or `-macos-<arch>.zip` with `--arch`).
- **Windows**: `<name>-<version>-win.zip` (`-win-arm64.zip` on ARM).
- **Linux**: `<name>-<version>-linux-<arch>.tar.gz`.

Publishing the same version again for another CPU (`--arch`, or a Windows arm64 build) adds to the existing manifest rather than replacing it, so one manifest serves every build.

| flag | |
|---|---|
| `--notes "text"` | release notes shown in the update prompt. |
| `--notes-file FILE` | the same, read from a file. |
| `--arch`, `--universal`, `--dmg`, `--cli` | passed to the build. |
| `--sign-manifest FILE` | sign (or re-sign) an existing manifest, for example one you merged by hand, without building. |
| `--expires 30d\|2027-01-31` | a signed manifest stops being accepted after this. |
| `--signed-only` | leave out the unsigned fields, so apps without a key see no update. |

The last three need [signed updates](./signed-updates): `update.publicKey` in tinyjs.json and `TINYJS_UPDATE_PRIVATE_KEY_FILE` pointing at the private key. With both set every publish is signed; with only one of them, publish stops and says which is missing.

On macOS, publish rebuilds the app and doesn't [notarize](#notarize) it, so the app in the update zip isn't notarized, even if you notarized `dist/` before publishing.

## tinyjs notarize {#notarize}

```sh
tinyjs build --dmg && tinyjs notarize --dmg
```

macOS only. Sends `dist/<title>.app` to Apple's notary service, waits for the result (usually a few minutes) and staples the ticket to the app, so it opens on other Macs without Gatekeeper warnings.

| flag | |
|---|---|
| `--dmg` | rebuild the dmg from the stapled app (a dmg made before notarizing doesn't carry the ticket). An existing dmg is rebuilt anyway. |
| `--arch arm64\|x86_64` | after a `build --arch`, pass the same arch so the right dmg is rebuilt. |

It needs a **Developer ID Application** certificate and the paid [Apple Developer Program](https://developer.apple.com/programs/). One-time setup:

1. Create the certificate: Xcode → Settings → Accounts → your Apple ID → Manage Certificates → **+** → *Developer ID Application*.
2. Put its exact name in tinyjs.json (`security find-identity -v -p codesigning` lists it):
   ```json
   { "signIdentity": "Developer ID Application: Your Name (TEAMID123)" }
   ```
3. Make an app-specific password at [account.apple.com](https://account.apple.com) and store it as a keychain profile:
   ```sh
   xcrun notarytool store-credentials tinyjs-notary \
     --apple-id you@example.com --team-id TEAMID123 --password <app-specific-password>
   ```
4. Name the profile in tinyjs.json: `"notarize": { "profile": "tinyjs-notary" }` (or `TINYJS_NOTARY_PROFILE`).

If the credentials are stored in a file-based keychain, set `notarize.keychain` to its path (or export `TINYJS_NOTARY_KEYCHAIN`). Tinyjs passes it to `notarytool --keychain`; relative paths start at the project directory. Use the same `--keychain` path with `store-credentials`, and unlock the keychain before submitting. Without a path, `notarytool` uses the data protection keychain.

Before submitting, notarize checks that the built app really carries a Developer ID signature and says what it found if not (ad-hoc, Apple Development…). Build again after changing `signIdentity`.

## tinyjs doctor {#doctor}

```sh
tinyjs doctor
```

Looks for the usual reasons tinyjs or an app won't run — the install, which `tinyjs` your PATH finds, the system webview, Node tooling, and (in a project folder) tinyjs.json and its build tools — and says how to fix each. It only reads files. Exits 1 when it prints a `FAIL`. Everything it checks: [doctor](./doctor).

## tinyjs update {#update}

```sh
tinyjs update
tinyjs update --check      # only say whether there's a newer version
tinyjs update --dry-run    # print what it would run
```

Updates tinyjs to the latest release by re-running the installer over the current install. A source checkout updates with `git pull` and `./setup.sh` (`setup.ps1` on Windows) instead. Then run [update-project](#update-project) in each project.

## tinyjs update-project {#update-project}

```sh
tinyjs update-project
```

Brings the project's type definitions (`tiny.d.ts`, `tjs.d.ts`) and coding-agent skill (`.claude/skills/tinyjs/`, `.agents/skills/tinyjs/`) up to the installed tinyjs. It overwrites edits to those files, adds the skill if the project has none, and leaves type files you deleted deleted. [Doctor](#doctor) notes when they're behind.

## tinyjs uninstall {#uninstall}

```sh
tinyjs uninstall          # asks first
tinyjs uninstall --yes
```

macOS and Linux: removes the tinyjs install and its `tinyjs` command from your PATH. Downloaded caches and the PATH line the installer added to your shell profile stay; it says where. On Windows, delete the install folder and remove it from your user PATH. See [install](./install).

## tinyjs version {#version}

```sh
tinyjs version     # also --version, -v
```

Prints the tinyjs version and the [txiki.js](https://txikijs.org) runtime it uses.

## environment variables {#environment}

| variable | |
|---|---|
| `TINYJS_DEBUG=1` | `dev`: trace every message across the bridge. |
| `TINYJS_ARCH`, `TINYJS_UNIVERSAL=1` | `build`, `publish`, `notarize`: same as `--arch` and `--universal`. |
| `TINYJS_SIGN_IDENTITY` | overrides `signIdentity`. |
| `TINYJS_NOTARY_PROFILE` | overrides `notarize.profile`. |
| `TINYJS_NOTARY_KEYCHAIN` | overrides `notarize.keychain` (file-based keychain path). |
| `TINYJS_UPDATE_PRIVATE_KEY_FILE` | `publish`: the key that signs the manifest — see [signed updates](./signed-updates). |
| `TINYJS_HTML=/abs/page.html` | `dev`: load a different page, such as a self-driving test page. |
| `TINYJS_LAUNCHER=/path` | run against a different launcher binary. |
| `TINYJS_HOME` | where the installer puts tinyjs. |
| `NO_COLOR` | `doctor`: plain output. |

The signing and notarization overrides print a line when they replace a value tinyjs.json really had. The [tinyjs.json](./config#per-os-overrides) per-OS blocks resolve first: root → OS block → environment.
