---
title: "auto-update"
---

# auto-update

<p class="tagline">ship new versions</p>

A built app can update itself. You host a small `manifest.json` and an archive of each release on any static web host. The app fetches the manifest, compares its version with its own, and when you call `install()` it downloads the archive, checks its sha256, swaps the new build in and relaunches.

tinyjs shows no update UI of its own. Your code decides when to check, what to show and when to install.

## walkthrough {#walkthrough}

**1. Point the app at a manifest.** Set [`update.url`](./config#update) and a `version` in tinyjs.json:

```json
{
  "name": "myapp",
  "version": "1.1.0",
  "update": { "url": "https://example.com/myapp/manifest.json", "auto": "launch" }
}
```

The url must be `https://`; `http://localhost` and `http://127.0.0.1` work for testing.

**2. Publish a release.** Bump `version`, then:

```sh
tinyjs publish --notes "Fixes the export bug"
```

[`tinyjs publish`](./cli#publish) builds the app and writes `dist/publish/`: the archive (`myapp-1.1.0.zip` on macOS) and `manifest.json`. The download url in the manifest is the archive's name in the same folder as `update.url`.

**3. Upload both files** to the folder `update.url` points at. The manifest can have any filename, as long as it matches `update.url`.

**4. The app checks.** With `"auto": "launch"` the app checks 5 seconds after it starts (`"daily"`: then every 24 hours too) and tells you when there's something newer:

```js
tiny.api.on('update-available', async ({ current, latest, notes }) => {
  if (await tiny.dialog.confirm(`Update to ${latest}?`, { detail: notes ?? '' }))
    await tiny.app.update.install();
});
```

Or check yourself, from a menu item for example:

```js
const { available, latest, notes } = await tiny.app.update.check();
if (available) await tiny.app.update.install();
```

**5. Install.** `install()` downloads and verifies the new build, puts it in place of the running one, opens it, and quits the old one about 250 ms later.

## the API {#api}

| page | backend | |
|---|---|---|
| `tiny.app.update.check()` | `app.update.check()` | fetch the manifest and compare versions |
| `tiny.app.update.install()` | `app.update.install()` | download, verify, swap and relaunch |
| `tiny.api.on('update-available', fn)` | [`onUpdateAvailable(info, app)`](./backend#hooks) | an automatic check found a newer version |

`check()` resolves `{ available, current, latest, notes }`:

| field | |
|---|---|
| `available` | `true` when the manifest's version for this OS and CPU is newer than the app's |
| `current` | the running app's `version` |
| `latest` | the manifest's version, or `null` |
| `notes` | the release notes from `publish --notes`, or `null` |

The backend's `app.update.check()` also returns the parsed `manifest`.

`install()` resolves `true` just before the app quits, so code after it rarely gets to run. If anything goes wrong it rejects with a reason and the current version keeps running (see [errors](#errors)). There are no progress events; the app keeps running normally while the archive downloads.

`check()` works in `tinyjs dev`, comparing against the `version` in tinyjs.json. `install()` and the automatic checks only work in a built app.

Under an [`api`](./config#api) gate the calls are `update.check` and `update.install`. The `"wrapper"` preset doesn't include them.

## automatic checks {#auto}

| `update.auto` | |
|---|---|
| left out | no automatic checks; call `check()` yourself |
| `"launch"` | one check 5 seconds after the app starts |
| `"daily"` | the same, then every 24 hours while the app runs |

A check that finds a newer version sends `{ current, latest, notes }` to the `update-available` page event and the `onUpdateAvailable` backend hook. Failed checks are silent, since being offline is normal. A check that finds nothing new sends nothing.

## which versions count as newer {#versions}

Versions are compared by [SemVer precedence](https://semver.org/#spec-item-11). A pre-release sorts below the corresponding release: `1.2.0-beta.2` → `1.2.0-beta.10` → `1.2.0` are each updates. Numeric pre-release identifiers compare as numbers; build metadata (`+…`) does not affect precedence. A leading `v` is also accepted for compatibility. Invalid versions (for example `1.2`, `1.2.0-beta..1` or `1.2.0-01`) never count as newer. `available` is `false` for the same or an older version.


## the manifest {#manifest}

The smallest manifest, which a plain macOS `tinyjs publish` writes:

```json
{
  "version": "1.2.0",
  "url": "https://example.com/myapp/myapp-1.2.0.zip",
  "sha256": "9f2c…",
  "notes": "Fixes the export bug"
}
```

Builds for other systems and CPUs go in their own blocks, so one manifest serves every install:

```json
{
  "version": "1.2.0",
  "notes": "Fixes the export bug",
  "url": "https://example.com/myapp/myapp-1.2.0-macos-arm64.zip",
  "sha256": "…",
  "mac": {
    "arm64":  { "url": "https://example.com/myapp/myapp-1.2.0-macos-arm64.zip", "sha256": "…" },
    "x86_64": { "url": "https://example.com/myapp/myapp-1.2.0-macos-x86_64.zip", "sha256": "…" }
  },
  "win": {
    "url": "https://example.com/myapp/myapp-1.2.0-win.zip", "sha256": "…",
    "x86_64": { "url": "https://example.com/myapp/myapp-1.2.0-win.zip", "sha256": "…" },
    "arm64":  { "url": "https://example.com/myapp/myapp-1.2.0-win-arm64.zip", "sha256": "…" }
  },
  "linux": {
    "x86_64": { "url": "https://example.com/myapp/myapp-1.2.0-linux-x86_64.tar.gz", "sha256": "…" },
    "arm64":  { "url": "https://example.com/myapp/myapp-1.2.0-linux-arm64.tar.gz", "sha256": "…" }
  }
}
```

Each app picks its entry like this, and when there's no entry for it, it reports no update rather than taking a build for another system:

| app | uses |
|---|---|
| macOS, no `mac` block | the top-level `url` and `sha256`, on every Mac |
| macOS, with a `mac` block | `mac.arm64` or `mac.x86_64`, by the Mac's hardware. An Intel build running under Rosetta moves to `mac.arm64` |
| Windows, x64 build | `win.arm64` on an ARM64 machine if there is one, else `win.x86_64`, else `win.url` |
| Windows, arm64 build | `win.arm64` only |
| Linux | `linux.x86_64` or `linux.arm64` only |

The top-level `url` is what Mac apps from before the `mac` block read, and `win.url` is what Windows apps from before `win.arm64` read; `publish` fills them with the arm64 Mac build and the x64 Windows build. So when you publish per-CPU Mac builds, always include `--arch arm64`.

A plain `tinyjs publish` on a Mac builds only for that Mac's CPU and writes no `mac` block, so every Mac gets that one build. If you ship to both Apple Silicon and Intel Macs, publish with `--arch arm64` and `--arch x86_64`, or build one app for both with `--universal`.

### merging platforms by hand {#merging}

`publish` adds to the manifest already in `dist/publish/` for a second Mac `--arch` or a second Windows CPU of the same version ([details](./cli#publish)). Builds made on different machines (Mac, Windows, Linux, and the two Linux CPUs) each write their own manifest; merge them into one before uploading: the top-level `url`/`sha256` and `mac` from the Mac, `win` from Windows, and each Linux arch into one `linux` block.

When the platforms aren't on the same release, give the lagging entry its own `version`. Otherwise its apps compare against the top-level `version`, see an update, and install the build they already have, over and over:

```json
"win": { "version": "1.1.0", "url": "…/myapp-1.1.0-win.zip", "sha256": "…" }
```

An entry's `version` overrides the top-level one on every OS. `notes` can be overridden per entry on macOS and Windows, on the `win` block itself, and on the `linux` block (not per Linux arch). `publish` only ever writes the top-level `version` and `notes`.

With [signed updates](./signed-updates), sign the merged file afterwards: see [several platforms](./signed-updates#platforms).

## hosting {#hosting}

- Any static host works: GitHub Releases, S3, a web server.
- `update.url` and every download url must be `https://` (`http://localhost` and `http://127.0.0.1` for testing). Redirects are followed, and every hop must pass the same rule — a redirect through plain `http://` is refused before it's requested.
- The manifest is fetched with a `cache-control: no-cache` request header. A CDN may still serve an old copy, so purge it after uploading if yours caches.
- Upload the archive before the manifest, so no app sees a version whose download isn't there yet.
- Keep archives at the url the manifest names; an app downloading it gets an `HTTP 404` error otherwise.

## what install does {#install}

1. Fetches the manifest again and picks this OS and CPU's entry (signed apps also re-verify it, see [signed updates](./signed-updates#format)).
2. Downloads the archive and checks its sha256 against the manifest.
3. Unpacks it into a temporary folder: `ditto` on macOS, the `tar.exe` that ships with Windows 10 and later, `tar` on Linux. The archive holds one top-level entry: the `.app` on macOS, the app folder on Windows and Linux.
4. Checks it and swaps it in, which differs per OS (below).
5. Opens the new version and quits the running one.

| | checks | swap |
|---|---|---|
| **macOS** | `codesign --verify --strict --deep` must pass. If the running app is signed with a real identity (a Team ID), the update must carry the same Team ID | puts the new `.app` beside the running one first (`<name>.app.update-new`, copied if it's on another disk), then renames the running app to `<name>.app.update-backup` and the new one into its place, and deletes the backup. If anything fails before the new app is in place, the current app is left as it was |
| **Windows** | sha256 only | file by file over the running install: each existing file is renamed to `<file>.update-old` and the new one dropped in. If any step fails, every change is undone. Files the new version no longer has are removed inside `resources/` and `executables/`, and stay everywhere else in the folder. Leftover `.update-old` files (the running exes) are deleted by the next update |
| **Linux** | sha256 only | the same as macOS for the whole app folder — staged as `<folder>.update-new` (copied, since the temp folder is usually another filesystem), then swapped |

On macOS and Linux the user needs write access to the folder the app sits in (for example `/Applications`), since the app itself is renamed. On Windows they need write access to the install folder.

### macOS: signing and notarization {#macos}

The code-signature check catches a damaged or tampered bundle. The Team ID check stops a build signed by someone else from replacing yours, but only when your app has a Team ID: an ad-hoc signed app (no [`signIdentity`](./config#signIdentity)) accepts any validly signed update, so its only protection is the https manifest and its sha256, or [signed updates](./signed-updates).

`tinyjs publish` rebuilds the app and doesn't [notarize](./cli#notarize) it, so the app in the update zip is never notarized, even if you notarized `dist/` before publishing. The installed app downloads the zip itself and nothing marks it as downloaded from the internet, so Gatekeeper doesn't block it from opening. Don't offer that zip as a first download on your website, though: a copy downloaded in a browser is quarantined, and Gatekeeper refuses an app that isn't notarized. Ship first installs from `tinyjs build` + `tinyjs notarize`.

An app opened straight from a downloaded zip or disk image runs from a read-only copy (App Translocation). `install()` refuses there and asks the user to move the app to /Applications and open it again.

## errors {#errors}

`check()` and `install()` reject with a readable message. The ones you're most likely to see:

| message | |
|---|---|
| `no update url configured …` | `update.url` is missing |
| `update url must be https:// …` | also `download url must be https://`, and the `(after redirect)` versions |
| `update check failed: HTTP 404` | the manifest isn't at `update.url` |
| `no newer update available for this platform` | `install()` with nothing newer, or no build in the manifest for this OS and CPU |
| `auto-update only works from the packaged .app build` | `install()` outside a built app, such as in `tinyjs dev` (on Windows and Linux: `auto-update only works in a built app, not in tinyjs dev`) |
| `the app is running from a quarantined location …` | macOS App Translocation, see [above](#macos) |
| `update manifest has no download url` / `… no sha256 — refusing to install` | a broken manifest entry |
| `download failed: HTTP …` | the archive isn't where the manifest says |
| `checksum mismatch — refusing to install` | the archive doesn't match its `sha256`, often an archive re-uploaded without its manifest |
| `could not extract the update archive` / `update archive does not contain an app …` | a damaged archive or one for another OS |
| `code signature verification failed on the update` | macOS: the new bundle's signature doesn't verify |
| `update is signed by a different team (X ≠ Y) …` | macOS: see [above](#macos) |
| `cannot move the current app (insufficient permissions?)` | no write access to the app's folder |
| `could not copy the new version next to the app (disk full?) …` | the staging copy failed; the current version is untouched |
| `update swap failed — the current version is intact (…)` | Windows: a file was locked. `… the rollback hit errors — please re-install the app` means it couldn't be undone cleanly |

Signed apps can also get signature and expiry errors, listed under [signed updates](./signed-updates#errors).

`install()` checks again before downloading anything, and only installs what `check()` would call an update for this OS and CPU — so calling it when nothing is newer, or when the release has no build for this machine, rejects rather than reinstalling or installing the wrong build.

## signed updates {#signing}

The sha256 in the manifest comes from the same host as the archive, so whoever controls that host can change both. With [signed updates](./signed-updates) the app pins your public key and only accepts a manifest signed with your private key, which never leaves your machine or CI secrets. It's opt-in, uses the same `update.url`, and installs from before you turn it on keep updating.
