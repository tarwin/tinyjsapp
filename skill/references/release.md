# Building, releasing, and auto-update — per OS

One project releases on three platforms; each has its own packaging truth.
Versions live in tinyjs.json `"version"`; bump before building.
`minTinyjsVersion` refuses to run an app on an older tinyjs with a real
message instead of a mystery TypeError (`tinyjs new` stamps it).

## Bundled data resources, executables and app assets

Three project folders ship beside the code, for different readers:

- `resources/` — data the app's own code reads. Ships whole by default;
  `(await app.paths()).resources` (backend) / `(await tiny.app.paths()).resources`
  (frontend) is the folder itself in dev and the bundled copy in builds
  (`Contents/Resources/app/resources/` in a .app, `resources/` beside the
  executable elsewhere), `null` without one. `"resources": [...]` lists paths
  inside it to ship only those. An OS block's list REPLACES the root one for
  that OS (like every config key); an OS without one uses the root value,
  else the whole folder — so `"macos": { "resources": ["x"] }` alone narrows
  only macOS. Mac-only file: root `"resources": []` + the macos list. Dev
  still reads the whole folder. Symlinks, `..`, absolute paths fail the build.
- `assets/` — files the OS reads, by name: `icon.png` (every OS),
  `Credits.html|rtf|rtfd` (macOS About panel), and a help book folder named by
  `"macos": { "helpBook": "<folder>" }`. The build copies the book into the
  .app's Resources, names it in Info.plist by its own `CFBundleIdentifier`,
  re-runs `hiutil` for its search index, and the launcher adds a Help menu
  ("<App> Help" ⌘? + search field). Built apps only — dev has no bundle.
  Help Viewer caches books by version: bump `version` to see an edit.
  Anything else in `assets/` gets a build note: it doesn't ship.

```js
export async function init(app) {
  const root = (await app.paths()).resources;   // string | null
  if (root) console.log(new TextDecoder().decode(await tjs.readFile(root + '/dictionary.json')));
}
```

- `executables/` — sidecar programs the backend spawns. Works like
  `resources/` but only ships THIS OS's native binaries (+ .dll/.dylib/.so;
  `#!` scripts on Linux). No list: the top-level files, overridden by name
  from `executables/<os>/` (macos|windows|linux), then `<os>-arm64/` /
  `<os>-x86_64/` for that CPU's build; another OS's file at the top is
  skipped with a note, a wrong file in an OS folder fails. `"executables":
  [...]` lists paths inside it instead (a folder = its files; per-OS blocks
  replace it). Ships FLAT: `(await app.paths()).executables + '/<name>'` — clashing
  names in a list fail. Dev: `.build/executables/` (links to the sources;
  copies on Windows); builds: `Contents/Helpers/` in a .app, `executables/`
  beside the exe elsewhere; `null` without any. macOS signs each with the app
  identity as `<id>.<file>`, hardened runtime under a Developer ID: a JIT
  sidecar (Bun, Deno, Node/pkg) then needs
  `"macos": { "executableEntitlements": { "<file>": ["com.apple.security.cs.allow-jit", …] } }`
  or it dies at its first JIT. Without that key a sidecar keeps the
  entitlements it arrived with. The build warns on a sidecar with no x86_64 slice in an
  app that runs on Intel (x86_64-only sidecars run on Apple Silicon via Rosetta).

```js
const exe = (await app.paths()).executables + '/my-cli' + (tjs.env.OS === 'Windows_NT' ? '.exe' : '');
const p = tjs.spawn([exe, '--version'], { stdout: 'pipe' });
```

## Download extras

`extras/` holds files for the person downloading: a README, a licence,
install notes. They ship BESIDE the app, never inside it: at the root of the
macOS dmg (so only with `--dmg`, or from `notarize --dmg`), and in the
Windows/Linux app folder beside the exe (`dist/` and the publish archive).
A Linux update replaces the folder; a Windows update overwrites by name, so
an extra you drop or rename stays behind there. The .app and the macOS
update zip never carry them.
No list: the top level plus `extras/<os>/`, which replaces a file by name;
folders ship whole, exec bits are kept. `"extras": [...]` lists paths inside
it, each landing by its own name; per-OS blocks replace the list. A name the
build writes there (`launcher.exe`, `<name>.exe`, `resources`, the .app,
`Applications`) fails the build, case-insensitively. For anything needed
AFTER install, use the app instead (Credits, a help book, a Help menu item).

## macOS

```sh
tinyjs build            # dist/<Title>.app, codesigned (+ bare dist/<name>)
tinyjs publish --notes "…"   # build + dist/publish/<name>-<ver>.zip + manifest.json
tinyjs notarize --dmg   # submit + staple + REBUILD the dmg from the stapled app
```

- Signing: `signIdentity` in tinyjs.json or `TINYJS_SIGN_IDENTITY` env
  (env wins, out loud). Without a Developer ID it ad-hoc signs — fine for
  local, not for distribution.
- Notarization: one-time `xcrun notarytool store-credentials <profile>
  --apple-id … --team-id …`, then `notarize.profile` in tinyjs.json or
  `TINYJS_NOTARY_PROFILE`. `notarize` fails fast on a non-Developer-ID
  signature instead of waiting minutes for Apple to reject.
  For a file-based keychain, use `store-credentials --keychain <path>` and
  set `notarize.keychain` or `TINYJS_NOTARY_KEYCHAIN` to the same path
  (absolute or relative to the project). Unlock it before notarizing.
  Without a path, `notarytool` uses the data protection keychain.
- **Ordering matters**: a dmg made at build time contains the pre-staple
  bundle, which OFFLINE Gatekeeper rejects — always take the dmg from
  `notarize --dmg` (it auto-rebuilds if one exists). The publish zip is the
  auto-update payload; apps download it themselves (no quarantine), so
  publish-then-notarize is the normal order.
- Roles: `.dmg` = the human download; `dist/publish/<name>-<ver>.zip` = the
  updater payload.

## Windows

```sh
tinyjs publish     # dist/publish/<name>-<ver>-win.zip (x64) or -win-arm64.zip + manifest fragment
```

Portable folder in a zip: `<name>.exe` + `launcher.exe` + `frontend/`. No
codesigning step — https + the manifest's sha256 is the update trust anchor
(or a signed manifest: `update.publicKey`). Built and packaged ON Windows
(use `tinyjs.cmd`). The build's CPU is the tinyjs install's: publish once
with an x64 tinyjs and once with an arm64 one, from the same project, and
both land in one manifest's `"win"` block (x64 also in `win.url`, which
older installs read). Windows on ARM can run both installs.

## Linux — the glibc floor rule (learned the hard way)

**Never package Linux tarballs on a newer distro than your support floor.**
The linker bakes the build machine's glibc symbol versions into the
binaries; a tarball built on Ubuntu 24.04 demands GLIBC_2.38 and refuses to
load on Ubuntu 22.04 / Debian 12 / Mint 21 with
``version `GLIBC_2.38' not found``. The build succeeds, the break only ever
shows on a user's machine. Build inside an `ubuntu:22.04` container (or a
22.04 CI runner) and verify what actually landed in the tarball:

```sh
objdump -T <binary> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # ≤ 2.35
```

`tinyjs publish` on Linux emits a per-arch tarball
(`<name>-<ver>-linux-<arch>.tar.gz`) — release both `x86_64` and `arm64`.
A built app self-registers its `.desktop` entry (menu listing, icon,
urlScheme, fileExtensions, single-instance) on first run; there is no
installer step.

## macOS — Apple Silicon and Intel

A default build carries only the build Mac's CPU (0.42+ prints `runs on:`
after each build). To ship for both, from any Mac:

```sh
tinyjs build --arch arm64 --dmg  && tinyjs notarize --arch arm64 --dmg
tinyjs build --arch x86_64 --dmg && tinyjs notarize --arch x86_64 --dmg
# -> dist/<name>-<ver>-macos-arm64.dmg + …-macos-x86_64.dmg (rebuilds,
#    publish's included, keep this version's per-arch dmgs)
tinyjs publish --arch arm64 && tinyjs publish --arch x86_64
# -> both zips + ONE manifest with a per-arch "mac" block
```

`notarize`/`publish` must repeat the build's `--arch`. The other CPU's
txiki.js is fetched once into `~/Library/Caches/tinyjs`. `--universal` is
the one-.app alternative (~6 MB bigger, needs the Command Line Tools for
`lipo`; keeps the plain `<name>-<ver>` names). The bare `dist/<name>` is
always host-CPU. Intel builds are Rosetta-tested; a check on real Intel
hardware is still owed (tarwin/tinyjsapp TODO-verify.md).

## Auto-update — one manifest, three platforms

`"update": { "url": "https://…/manifest.json", "auto": "launch" | "daily" }`
in tinyjs.json. `tinyjs publish` computes the platform fragment; you merge
fragments into ONE hosted manifest — mac owns the top level (plus the
per-arch `mac` block when you ship separate Apple Silicon / Intel builds),
Windows the `win` block, Linux the per-arch `linux` blocks:

```json
{ "version": "1.2.0", "url": "…/app-1.2.0-macos-arm64.zip", "sha256": "…", "notes": "…",
  "mac":   { "arm64":  { "url": "…-macos-arm64.zip",  "sha256": "…" },
             "x86_64": { "url": "…-macos-x86_64.zip", "sha256": "…" } },
  "win":   { "version": "1.2.0", "url": "…-win.zip", "sha256": "…" },
  "linux": { "x86_64": { "version": "1.2.0", "url": "…-linux-x86_64.tar.gz", "sha256": "…" },
             "arm64":  { "version": "1.2.0", "url": "…-linux-arm64.tar.gz", "sha256": "…" } } }
```

- **Merge, never overwrite**: each platform's updater reads only its own
  block, and platforms can sit at different versions. A Windows/Linux
  fragment carries `version` at the TOP level — copy it INTO its block when
  merging, or that platform compares against the mac version. Copying one
  platform's fresh manifest over the hosted file silently kills updates for
  the others.
- With a `mac` block, each Mac takes its own CPU's entry (an Intel build
  under Rosetta moves to arm64); the arm64 build also fills the top-level
  url/sha256 for apps from before the block. Block present but no entry for
  this CPU = "no update", never the wrong CPU's build.
- The manifest is the mutable pointer; payloads can live anywhere static
  (GitHub Releases works well — upload assets, then point urls at
  `releases/download/<tag>/<file>`).
- `--notes "text"` / `--notes-file FILE` ride into the update prompt
  (`update-available` event / `onUpdateAvailable` gets `{ current, latest,
  notes }`).
- Flow in-app: `update.check()` → `update.install()` verifies sha256, swaps
  files in place, relaunches (all three OSes).
- Always `curl -fsSLI` the manifest's urls after publishing — a dead url is
  an updater that fails silently on someone else's machine.

## CLI extras

- `tinyjs build --dmg` — dmg at build time (dev convenience; re-made by
  `notarize --dmg` for release).
- `tinyjs build --cli [name]` — `dist/bin/<name>` shim for terminal
  invocation; targets the BARE binary, not the .app.
- Per-OS config: `macos` / `windows` / `linux` blocks in tinyjs.json merge
  over the root for that platform (icons, signing, chrome that differs).
- `TINYJS_DEBUG=1 tinyjs dev` traces every bridge message.
