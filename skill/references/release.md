# Building, releasing, and auto-update — per OS

One project releases on three platforms; each has its own packaging truth.
Versions live in tinyjs.json `"version"`; bump before building.
`minTinyjsVersion` refuses to run an app on an older tinyjs with a real
message instead of a mystery TypeError (`tinyjs new` stamps it).

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
- **Ordering matters**: a dmg made at build time contains the pre-staple
  bundle, which OFFLINE Gatekeeper rejects — always take the dmg from
  `notarize --dmg` (it auto-rebuilds if one exists). The publish zip is the
  auto-update payload; apps download it themselves (no quarantine), so
  publish-then-notarize is the normal order.
- Roles: `.dmg` = the human download; `dist/publish/<name>-<ver>.zip` = the
  updater payload.

## Windows

```sh
tinyjs publish     # dist/publish/<name>-<ver>-win.zip + manifest fragment
```

Portable folder in a zip: `<name>.exe` + `launcher.exe` + `frontend/`. No
codesigning step — https + the manifest's sha256 is the update trust anchor.
Built and packaged ON Windows (use `tinyjs.cmd`).

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
