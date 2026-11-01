// tinyjs app auto-update.
//
// Sparkle-style flow driven by a static manifest JSON you host anywhere
// (GitHub Releases, S3, a plain web server):
//
//   { "version": "1.2.0", "url": "https://…/MyApp-1.2.0.zip", "sha256": "…" }
//
// `tinyjs publish` produces the zip + manifest for each release. At runtime:
// check compares the manifest version against the running app's version;
// install downloads the zip, verifies the sha256 and the code signature,
// swaps the .app bundle in place (with rollback on failure), and relaunches.
//
// Install only works from the packaged .app (a bare dev process has no bundle
// to replace). Quarantined apps get translocated to a read-only path by
// Gatekeeper; we detect that and ask the user to move the app first.
//
// Trust model: the manifest must be served over https (http is allowed only
// for 127.0.0.1/localhost, for testing) and must carry a sha256 — that hash,
// fetched over TLS, is what authenticates the download. The new bundle's
// code signature must verify, and when the running app is signed with a real
// identity, the update's Team ID must match (ad-hoc builds have no identity
// to pin, so the https+sha256 manifest is their only anchor — use a real
// Developer ID for anything security-sensitive).
// With update.publicKey (a non-empty string or list), an Ed25519 envelope authenticates the complete
// manifest before platform selection; TLS, sha256 and code checks still apply.

import { pinnedKeys, verifyManifest } from './update-signing.js';

const dec = new TextDecoder();
const IS_WIN = tjs.env.OS === 'Windows_NT';
const IS_LINUX = !IS_WIN && /linux/i.test(globalThis.navigator?.platform ?? '');
// bsdtar by full path: Git for Windows puts GNU tar first on PATH, and it
// can't read zips.
const WIN_TAR = (tjs.env.SystemRoot || tjs.env.SYSTEMROOT || 'C:\\Windows') + '\\System32\\tar.exe';
const LINUX_ARCH = /aarch64|arm64/i.test(globalThis.navigator?.platform ?? '') ? 'arm64' : 'x86_64';

// macOS reports navigator.platform "MacIntel" on every Mac, so ask the
// hardware. hw.optional.arm64 is 1 on Apple Silicon even for a process under
// Rosetta — an Intel build running there updates to the native arm64 build.
// Intel Macs don't have the key at all (sysctl fails → x86_64).
let macArchP = null;
function macArch() {
  return (macArchP ??= runCapture(['sysctl', '-n', 'hw.optional.arm64'])
    .then((r) => (r.ok && r.out.trim() === '1' ? 'arm64' : 'x86_64'), () => 'x86_64'));
}

// Windows: this build's CPU from its own exe's PE header, and whether the
// machine is ARM64 — so an x64 build emulated there updates to the native
// arm64 build when the manifest has one (the twin of the Rosetta move
// above). No spawn: a GUI-subsystem app shelling out flashes a console. An
// emulated x64 process sees PROCESSOR_ARCHITECTURE=AMD64; the machine shows
// through PROCESSOR_ARCHITEW6432 / PROCESSOR_IDENTIFIER. Missing both just
// leaves an x64 build on x64 updates.
let winArchP = null;
function winArch() {
  return (winArchP ??= (async () => {
    let own = /arm64/i.test(tjs.env.PROCESSOR_ARCHITECTURE ?? '') ? 'arm64' : 'x86_64';
    try {
      const fh = await tjs.open(tjs.exePath, 'r');
      const b = new Uint8Array(4096);
      try { await fh.read(b, 0); } finally { await fh.close(); }
      const pe = (b[0x3c] | (b[0x3d] << 8) | (b[0x3e] << 16) | (b[0x3f] << 24)) >>> 0;
      const m = pe + 6 <= b.length ? b[pe + 4] | (b[pe + 5] << 8) : 0;
      if (m === 0xaa64) own = 'arm64'; else if (m === 0x8664) own = 'x86_64';
    } catch {}
    const armMachine = own === 'arm64' || /arm64/i.test(tjs.env.PROCESSOR_ARCHITEW6432 ?? '')
      || /^arm/i.test(tjs.env.PROCESSOR_IDENTIFIER ?? '');
    return { own, machine: armMachine ? 'arm64' : 'x86_64' };
  })());
}

function assertSafeUrl(u, what) {
  const s = String(u ?? '');
  if (/^https:\/\//i.test(s)) return;
  if (/^http:\/\/(127\.0\.0\.1|localhost)([:/]|$)/i.test(s)) return; // local testing
  throw new Error(what + ' must be https:// (got ' + (s || 'nothing') + ')');
}

// fetch, following redirects one hop at a time and checking EVERY hop with
// assertSafeUrl. Checking only where it ended (res.url) let https → http →
// https through: a network attacker on the plain-http hop could send the
// app to a manifest of their choosing — the sha256 protects the archive only
// if the manifest that names it wasn't swapped.
async function fetchEveryHopSafe(url, what, init = {}) {
  let u = String(url ?? '');
  for (let hop = 0; ; hop++) {
    assertSafeUrl(u, hop ? what + ' (after redirect)' : what);
    const res = await fetch(u, { ...init, redirect: 'manual' });
    const loc = res.status >= 300 && res.status < 400 ? res.headers.get('location') : null;
    if (!loc) return res;
    if (hop >= 20) throw new Error(what + ': too many redirects');
    u = new URL(loc, u).href;
  }
}

function parseVer(v) {
  // Accept the historical leading v, but require a complete SemVer after it.
  const m = /^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/.exec(String(v));
  if (!m) return null;
  const pre = m[4]?.split('.') ?? [];
  if (pre.some((id) => /^0\d+$/.test(id))) return null;
  return { core: m.slice(1, 4), pre };
}

// Compare decimal strings rather than Numbers: SemVer does not limit the
// length of a numeric identifier, and Number loses precision above 2^53.
function compareNumber(a, b) {
  if (a.length !== b.length) return a.length < b.length ? -1 : 1;
  return a === b ? 0 : a < b ? -1 : 1;
}

function comparePre(a, b) {
  if (!a.length || !b.length) return a.length === b.length ? 0 : a.length ? -1 : 1;
  for (let i = 0; i < Math.min(a.length, b.length); i++) {
    if (a[i] === b[i]) continue;
    const an = /^\d+$/.test(a[i]);
    const bn = /^\d+$/.test(b[i]);
    if (an && bn) return compareNumber(a[i], b[i]);
    if (an !== bn) return an ? -1 : 1;
    return a[i] < b[i] ? -1 : 1;
  }
  return a.length === b.length ? 0 : a.length < b.length ? -1 : 1;
}

function isNewer(current, latest) {
  const a = parseVer(current), b = parseVer(latest);
  if (!a || !b) return false;
  for (let i = 0; i < 3; i++) {
    if (a.core[i] !== b.core[i]) return compareNumber(a.core[i], b.core[i]) < 0;
  }
  return comparePre(a.pre, b.pre) < 0;
}

async function runOk(argv) {
  const p = tjs.spawn(argv, { stdout: 'ignore', stderr: 'ignore' });
  const st = await p.wait();
  return st.exit_status === 0 && !st.term_signal;
}

async function runCapture(argv, which = 'stdout') {
  const p = tjs.spawn(argv, which === 'stderr'
    ? { stdout: 'ignore', stderr: 'pipe' }
    : { stdout: 'pipe', stderr: 'ignore' });
  const reader = p[which].getReader();
  let out = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    out += dec.decode(value, { stream: true });
  }
  const st = await p.wait();
  return { ok: st.exit_status === 0 && !st.term_signal, out };
}

// The Apple Team ID the bundle is signed with, or null for ad-hoc/unsigned.
// (codesign -dvv prints to stderr.)
async function teamIdentifier(bundle) {
  const { ok, out } = await runCapture(['codesign', '-dvv', bundle], 'stderr');
  if (!ok) return null;
  const m = /^TeamIdentifier=(.+)$/m.exec(out);
  return m && m[1] !== 'not set' ? m[1].trim() : null;
}

// WebCrypto everywhere — no shasum spawn, identical on macOS and Windows.
async function sha256(path) {
  try {
    const data = await tjs.readFile(path);
    const hash = await crypto.subtle.digest('SHA-256', data);
    return Array.from(new Uint8Array(hash))
      .map((b) => b.toString(16).padStart(2, '0')).join('');
  } catch {
    return null;
  }
}

async function exists(p) {
  try { await tjs.stat(p); return true; } catch { return false; }
}

// macOS: …/MyApp.app/Contents/MacOS/tjs -> …/MyApp.app. Windows/Linux: a
// built app is a portable folder — the compiled backend with launcher(.exe)
// beside it (the frontend rides inside the binary) — so that folder is the
// "bundle". null when not packaged (dev / bare CLI).
let portableBundle;  // memoized (involves stat calls)
let portableProbe = null;
export async function bundlePath() {
  const exe = tjs.exePath;
  if (!IS_WIN && !IS_LINUX) {
    const i = exe.indexOf('.app/Contents/MacOS/');
    return i < 0 ? null : exe.slice(0, i + 4);
  }
  await (portableProbe ??= portableBundleInit());
  return portableBundle ?? null;
}

// Windows/Linux bundle detection needs async stats. Probed on first ask, NOT
// at import: a top-level await here makes this module async, which makes
// bridge.js async, which lets an app's backend module body run BEFORE the
// fetch repair shim is installed — its first fetch would get txiki's raw
// broken one (measured: a top-level `fetch('https://rss.art19.com/')` failed
// with "mbedtls connect -1 5 0" while the identical call one await later
// succeeded).
async function portableBundleInit() {
  if (!IS_WIN && !IS_LINUX) return;
  const dir = tjs.exePath.replace(/[\\/][^\\/]*$/, '');
  const base = tjs.exePath.slice(dir.length + 1).toLowerCase();
  const launcher = IS_WIN ? 'launcher.exe' : 'launcher';
  if (base !== (IS_WIN ? 'tjs.exe' : 'tjs') && (await exists(dir + '/' + launcher))) {
    portableBundle = dir;
  }
}

export async function checkForUpdate({ url, version, publicKey }) {
  // Copy the pinned key list before any await, so it can't change mid-check.
  const pin = pinnedKeys(publicKey);
  if (!url) throw new Error('no update url configured (tinyjs.json "update": { "url": … })');
  const res = await fetchEveryHopSafe(url, 'update url', { headers: { 'cache-control': 'no-cache' } });
  if (!res.ok) throw new Error('update check failed: HTTP ' + res.status);
  const response = await res.json();
  const manifest = pin ? await verifyManifest(response, pin) : response;
  // Optional signed expiry (`tinyjs publish --expires`): past it, a host
  // replaying this manifest gets refused. Unsigned apps ignore it — anyone
  // who can edit their manifest can edit the date.
  if (pin && manifest.expires != null) {
    const until = typeof manifest.expires === 'string' ? Date.parse(manifest.expires) : NaN;
    if (!Number.isFinite(until)) throw new Error('update manifest has an invalid expires date');
    if (Date.now() > until) throw new Error('update manifest expired ' + manifest.expires + ' — the publisher needs to re-sign it');
  }
  let latest = manifest?.version ?? null;
  // Per-platform downloads: `url`/`sha256` are the macOS zip (the original,
  // pre-Windows fields); a `win: { url, sha256, version? }` block carries
  // the Windows zip. On Windows, overlay it — its own version wins when the
  // two platforms ship different builds — and if a release has no Windows
  // build, report "no update" rather than ever pulling a mac zip.
  // Per-arch Windows builds: "win": { "arm64": { url, sha256 }, "x86_64": … },
  // with the x86_64 one also in win.url/sha256 for apps that predate the
  // split (all x64). An arm64 build takes win.arm64 or nothing — never the
  // x64 build, which would quietly move it onto emulation.
  if (IS_WIN) {
    const w = manifest?.win;
    const ok = (e) => (e?.url && e.sha256 ? e : null);
    const { own, machine } = await winArch();
    const entry = (machine === 'arm64' ? ok(w?.arm64) : null)
      ?? (own === 'x86_64' ? ok(w?.x86_64) ?? ok(w) : null);
    if (entry) {
      manifest.url = entry.url;
      manifest.sha256 = entry.sha256;
      if (entry.version ?? w.version) latest = entry.version ?? w.version;
      if (entry.notes ?? w.notes) manifest.notes = entry.notes ?? w.notes;
    } else {
      return { available: false, current: version, latest,
               notes: manifest?.notes ?? null, manifest };
    }
  }
  // Linux downloads are per-arch: "linux": { "arm64": { url, sha256 }, … }.
  // No block for this arch → report "no update" rather than a foreign build.
  if (IS_LINUX) {
    const lin = manifest?.linux?.[LINUX_ARCH];
    if (lin?.url && lin.sha256) {
      manifest.url = lin.url;
      manifest.sha256 = lin.sha256;
      if (lin.version) latest = lin.version;
      if (manifest.linux.notes) manifest.notes = manifest.linux.notes;
    } else {
      return { available: false, current: version, latest,
               notes: manifest?.notes ?? null, manifest };
    }
  }
  // macOS per-arch builds (`tinyjs publish --arch …`): "mac": { "arm64":
  // { url, sha256 }, "x86_64": … }. The top-level url/sha256 stays the arm64
  // (or universal) build for apps that predate the block. With a block
  // present, take this Mac's entry or report "no update" — never a build for
  // the other CPU, which would install and then refuse to open.
  if (!IS_WIN && !IS_LINUX && manifest?.mac) {
    const m = manifest.mac[await macArch()];
    if (m?.url && m.sha256) {
      manifest.url = m.url;
      manifest.sha256 = m.sha256;
      if (m.version) latest = m.version;
      if (m.notes) manifest.notes = m.notes;
    } else {
      return { available: false, current: version, latest,
               notes: manifest?.notes ?? null, manifest };
    }
  }
  return {
    available: isNewer(version, latest), current: version, latest,
    // Release notes for the update prompt ("notes" in the manifest —
    // `tinyjs publish --notes "…"` writes it).
    notes: manifest?.notes ?? null,
    manifest,
  };
}

// Downloads, verifies, swaps the bundle. Returns the bundle path on success;
// the caller is expected to relaunch() + quit. Throws with a human-readable
// reason on any failure (the running app is untouched or rolled back).
export async function installUpdate({ url, version, manifest, publicKey }) {
  // A check result is mutable/untrusted. Fetch and authenticate again for
  // signed installs, even when the caller supplies a manifest. Parameters
  // (especially the baked key) are captured by value before any await.
  const pin = pinnedKeys(publicKey);
  if (pin) {
    const checked = await checkForUpdate({ url, version, publicKey: pin });
    if (!checked.available) throw new Error('no newer signed update available for this platform');
    manifest = checked.manifest;
  } else if (!manifest) {
    // Unsigned installs get the same rule: install only what check() calls
    // an update for THIS OS and CPU. checkForUpdate answers available:false
    // with the manifest's top-level url still in place when there's no entry
    // for this platform or nothing newer — installing that put an
    // arm64-only build on an Intel Mac, or the mac zip over a Windows arm64
    // install (whose swap then pruned resources/ and executables/), and
    // reinstalled or downgraded when nothing was newer.
    const checked = await checkForUpdate({ url, version });
    if (!checked.available) throw new Error('no newer update available for this platform');
    manifest = checked.manifest;
  }
  const bundle = await bundlePath();
  if (!bundle) {
    throw new Error(IS_WIN || IS_LINUX
      ? 'auto-update only works in a built app, not in tinyjs dev'
      : 'auto-update only works from the packaged .app build');
  }
  if (bundle.includes('/AppTranslocation/')) {
    throw new Error('the app is running from a quarantined location — move it to /Applications and relaunch');
  }

  if (!manifest?.url) throw new Error('update manifest has no download url');
  if (!manifest.sha256) throw new Error('update manifest has no sha256 — refusing to install');
  const res = await fetchEveryHopSafe(manifest.url, 'download url');
  if (!res.ok) throw new Error('download failed: HTTP ' + res.status);
  const data = new Uint8Array(await res.arrayBuffer());

  const tmp = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-update-XXXXXX');
  const rmrf = (p) => IS_WIN
    ? tjs.remove(p, { recursive: true }).then(() => true, () => false)
    : runOk(['rm', '-rf', p]);
  try {
    const zipPath = tmp + '/update.zip';
    await tjs.writeFile(zipPath, data);

    const got = await sha256(zipPath);
    if (!got || got.toLowerCase() !== String(manifest.sha256).toLowerCase()) {
      throw new Error('checksum mismatch — refusing to install');
    }

    // Extract: ditto on macOS; bsdtar (ships with Windows 10+) reads zips —
    // via `launcher --run` so the console tool doesn't flash a terminal
    // (the updating app is a GUI-subsystem exe); plain tar on Linux (the
    // Linux asset is a .tar.gz).
    await tjs.makeDir(tmp + '/x', { recursive: true }).catch(() => {});
    const winTar = (b => b ? [b + '/launcher.exe', '--run'] : [])(IS_WIN ? await bundlePath() : null);
    const extractOk = IS_WIN
      ? await runOk([...winTar, WIN_TAR, '-xf', zipPath, '-C', tmp + '/x'])
      : IS_LINUX
      ? await runOk(['tar', '-xzf', zipPath, '-C', tmp + '/x'])
      : await runOk(['ditto', '-x', '-k', zipPath, tmp + '/x']);
    if (!extractOk) throw new Error('could not extract the update archive');

    // The archive holds one top-level entry: MyApp.app (macOS) or a folder
    // with the compiled backend + launcher (Windows/Linux).
    let newApp = null;
    const iter = await tjs.readDir(tmp + '/x');
    for await (const e of iter) {
      if ((IS_WIN || IS_LINUX) ? e.isDirectory : e.name.endsWith('.app')) {
        newApp = tmp + '/x/' + e.name;
        break;
      }
    }
    if (!newApp) throw new Error('update archive does not contain an app ' + ((IS_WIN || IS_LINUX) ? 'folder' : 'bundle'));

    if (!IS_WIN && !IS_LINUX) {
      // Integrity check: the bundle's own seal must verify (ad-hoc or real
      // identity alike). A tampered or truncated download fails here.
      if (!(await runOk(['codesign', '--verify', '--strict', '--deep', newApp]))) {
        throw new Error('code signature verification failed on the update');
      }
      // Identity pinning: when the running app is signed with a real identity,
      // the update must come from the same Apple Team.
      const currentTeam = await teamIdentifier(bundle);
      if (currentTeam) {
        const newTeam = await teamIdentifier(newApp);
        if (newTeam !== currentTeam) {
          throw new Error('update is signed by a different team (' +
                          (newTeam ?? 'ad-hoc') + ' ≠ ' + currentTeam + ') — refusing to install');
        }
      }
    }
    // (Windows has no codesign equivalent here; the https + sha256 manifest
    // is the trust anchor.)

    if (IS_WIN) {
      // Windows cannot rename a directory that contains a running exe, but a
      // running exe FILE can be renamed. So the update is an in-place
      // file-by-file shuffle: locked files (the exes) are renamed aside to
      // *.update-old (cleaned up on the next update, once unlocked) and the
      // new files dropped in.
      await winSwapDir(newApp, bundle);
      return bundle;
    }

    await swapBundle(newApp, bundle);
    return bundle;
  } finally {
    rmrf(tmp);
  }
}

// Recursive copy preserving the executable bit and symlinks (the
// cross-filesystem fallback for the bundle swap — an .app can hold links, and
// reading through one copied its target, or failed on a linked folder).
// Returns false on any failure.
async function copyTree(src, dst) {
  try {
    await tjs.makeDir(dst, { recursive: true }).catch(() => {});
    const iter = await tjs.readDir(src);
    for await (const e of iter) {
      const s = src + '/' + e.name;
      const d = dst + '/' + e.name;
      if (e.isSymbolicLink) {
        await tjs.symlink(await tjs.readLink(s), d);
      } else if (e.isDirectory) {
        if (!(await copyTree(s, d))) return false;
      } else {
        await tjs.writeFile(d, await tjs.readFile(s));
        const st = await tjs.stat(s);
        if (st.mode & 0o111) await tjs.chmod(d, st.mode & 0o7777).catch(() => {});
      }
    }
    return true;
  } catch {
    return false;
  }
}

// macOS / Linux: put newApp where bundle is, keeping the current app intact
// until the new one is completely in place. Renaming a running .app/folder
// is fine: open files keep working via their inodes until the process exits.
//
// The new app is first moved — or, across filesystems, COPIED — to a staging
// folder beside the current one, so the swap itself is two renames within
// one folder. It used to move the current app aside first and copy straight
// into its place: a copy that failed partway (tmp is a tmpfs on most Linux
// systems, so the copy was the normal path there) left a half-copied app,
// and the rollback rename onto that non-empty folder failed silently,
// stranding the real app at .update-backup.
// `rename` is injectable so test/update-swap can force the copy path.
export async function swapBundle(newApp, bundle, { rename = tjs.rename } = {}) {
  const rmrf = (p) => runOk(['rm', '-rf', p]);
  const staged = bundle + '.update-new';
  const backup = bundle + '.update-backup';
  await rmrf(staged);
  await rmrf(backup);
  try {
    await rename(newApp, staged);
  } catch {
    if (!(await copyTree(newApp, staged))) {
      await rmrf(staged);
      throw new Error('could not copy the new version next to the app (disk full?) — the current version is intact');
    }
  }
  try {
    await rename(bundle, backup);
  } catch {
    await rmrf(staged);
    throw new Error('cannot move the current app (insufficient permissions?)');
  }
  try {
    await rename(staged, bundle);
  } catch (e) {
    // bundle's path is free again (it moved to backup), so this can land
    await rename(backup, bundle).catch(() => {});
    await rmrf(staged);
    throw new Error('failed to move the new app into place — the current version is intact');
  }
  await rmrf(backup);
}

// Windows in-place update: recursively move src's files over dst. Existing
// files are renamed aside as .update-old (never deleted outright — the aside
// is what makes a mid-swap failure fully reversible) and the new files
// dropped in. Cross-volume renames fall back to a copy.
//
// A failure partway through (a lock the retries below can't clear) must not
// leave a half-old half-new install: the walk journals every file it touches
// and the wrapper rolls the journal back, so the current version stays
// intact and one clear error surfaces.

// Windows: a freshly-written or just-closed file can be transiently locked
// by Defender / the indexer, failing the rename with EPERM even with a
// single writer — retry briefly before giving up (same posture as the
// store's rename retry in bridge.js). Only lock-shaped errors retry: a
// cross-volume rename (EXDEV) must fall through to the copy at once, or
// every file of an app installed off the temp dir's drive pays the full
// backoff.
const LOCK_CODES = new Set(['EPERM', 'EBUSY', 'EACCES']);
async function retryLocked(fn) {
  for (let attempt = 0; ; attempt++) {
    try {
      return await fn();
    } catch (e) {
      if (attempt >= 5 || !LOCK_CODES.has(e?.code)) throw e;
      await new Promise((r) => setTimeout(r, 25 * (attempt + 1)));
    }
  }
}

// journal entry: { d, aside, placed } — aside is where the displaced old
// file waits (null when the new file is a pure addition); placed once the
// new file has landed at d.
async function winSwapWalk(src, dst, journal) {
  // A folder this version adds is journaled too ({ dir }), so a rollback
  // doesn't leave it behind empty.
  if (!(await exists(dst))) {
    await tjs.makeDir(dst, { recursive: true }).catch(() => {});
    journal.push({ dir: dst });
  }
  // Sweep leftovers from the previous update first (unlocked by now) — but
  // not the asides this swap's prune just made, which rollback needs.
  const ours = new Set(journal.map((j) => j.aside));
  const sweep = await tjs.readDir(dst);
  for await (const e of sweep) {
    if (e.name.endsWith('.update-old') && !ours.has(dst + '/' + e.name))
      await retryLocked(() => tjs.remove(dst + '/' + e.name)).catch(() => {});
  }
  const iter = await tjs.readDir(src);
  for await (const e of iter) {
    const s = src + '/' + e.name;
    const d = dst + '/' + e.name;
    if (e.isDirectory) {
      await winSwapWalk(s, d, journal);
      continue;
    }
    const entry = { d, aside: null, placed: false };
    if (await exists(d)) {
      entry.aside = d + '.update-old';
      try {
        await retryLocked(() => tjs.rename(d, entry.aside));
      } catch (e) {
        throw new Error('cannot replace ' + d + ' (file locked?): ' + (e?.message ?? e));
      }
    }
    journal.push(entry);
    try {
      await retryLocked(() => tjs.rename(s, d));
    } catch {
      try {
        await tjs.writeFile(d, await tjs.readFile(s)); // cross-volume fallback
      } catch (copyErr) {
        await tjs.remove(d).catch(() => {}); // drop a partial copy
        throw copyErr;
      }
    }
    entry.placed = true;
  }
}

// Undo the walk, newest first: drop what landed, put the asides back.
// Best-effort per entry; returns false when any undo failed (the caller
// widens its error — a half-rolled-back install needs a manual re-install).
async function winSwapRollback(journal) {
  let ok = true;
  for (const e of [...journal].reverse()) {
    if (e.dir) await tjs.remove(e.dir).catch(() => { ok = false; }); // emptied above: its files came later
    if (e.placed) await tjs.remove(e.d).catch(() => { ok = false; });
    if (e.aside) await tjs.rename(e.aside, e.d).catch(() => { ok = false; });
  }
  return ok;
}

// The walk only adds and replaces, so a file the new version dropped would
// linger (macOS and Linux swap the whole bundle and never see this). Inside
// the folders the build owns, anything the new version lacks is renamed
// aside like a replaced file — journaled, so a failed swap puts it back.
// The rest of the install folder is left alone: the Shelf keeps its marker
// there. Runs BEFORE the walk, which moves src's files away.
const WIN_OWNED_DIRS = ['executables', 'resources'];

async function isDir(p) {
  try { return (await tjs.stat(p)).isDirectory; } catch { return false; }
}

// srcDir is null when the new version has no such folder at all.
async function winPruneWalk(srcDir, dst, journal, staleDirs) {
  // Listed up front: the renames below add entries to this very folder.
  const entries = [];
  try {
    for await (const e of await tjs.readDir(dst)) entries.push({ name: e.name, isDirectory: e.isDirectory });
  } catch { return; }
  for (const e of entries) {
    const d = dst + '/' + e.name;
    if (e.name.endsWith('.update-old')) {
      // The walk's sweep never visits a folder the new version dropped.
      await retryLocked(() => tjs.remove(d)).catch(() => {});
      continue;
    }
    const s = srcDir === null ? null : srcDir + '/' + e.name;
    if (e.isDirectory) {
      const keep = s !== null && (await isDir(s));
      await winPruneWalk(keep ? s : null, d, journal, staleDirs);
      if (!keep) staleDirs.push(d);
      continue;
    }
    if (s !== null && (await exists(s)) && !(await isDir(s))) continue;
    const entry = { d, aside: d + '.update-old', placed: false };
    try {
      await retryLocked(() => tjs.rename(d, entry.aside));
    } catch (err) {
      throw new Error('cannot remove ' + d + ' (file locked?): ' + (err?.message ?? err));
    }
    journal.push(entry);
  }
}

async function winSwapDir(src, dst) {
  const journal = [];
  const staleDirs = [];
  try {
    for (const name of WIN_OWNED_DIRS) {
      if (await isDir(dst + '/' + name)) {
        const s = src + '/' + name;
        await winPruneWalk((await isDir(s)) ? s : null, dst + '/' + name, journal, staleDirs);
        if (!(await isDir(s))) staleDirs.push(dst + '/' + name);
      }
    }
    await winSwapWalk(src, dst, journal);
  } catch (e) {
    const cause = e?.message ?? String(e);
    const restored = await winSwapRollback(journal);
    throw new Error(restored
      ? 'update swap failed — the current version is intact (' + cause + ')'
      : 'update swap failed and the rollback hit errors — please re-install the app (' + cause + ')');
  }
  // Swap complete — drop the asides we made; still-locked files (running
  // exes) fail removal and stay for the next update's sweep, as before.
  for (const e of journal) {
    if (e.aside) await tjs.remove(e.aside).catch(() => {});
  }
  // Then the folders the new version dropped, deepest first (pushed
  // children-first); one still holding a locked aside stays until the next
  // update's prune sweeps it.
  for (const d of staleDirs) await tjs.remove(d).catch(() => {});
}

// Captured at import: on Linux, once the running binary has been replaced by
// an update, /proc/self/exe (and so tjs.exePath) reads "…/name (deleted)" —
// resolve the name BEFORE any swap can happen.
const EXE_NAME = tjs.exePath.replace(/^.*[\\/]/, '');

// Passed to the relaunched app so it waits out the instance that spawned it
// (still alive, still holding the single-instance pipe — it quits ~250 ms
// later) instead of handing off to it and exiting. bridge.js checks for it.
export const RELAUNCH_FLAG = '--tinyjs-relaunched';

export function relaunch(bundle) {
  if (IS_WIN || IS_LINUX) {
    // The new folder keeps the same exe name as the running app. On Windows
    // a direct tjs.spawn child is killed when this process exits (libuv's
    // job object), so start it via `launcher --spawn`, which detaches it.
    const exe = bundle + (IS_WIN ? '\\' : '/') + EXE_NAME;
    tjs.spawn(IS_WIN ? [bundle + '\\launcher.exe', '--spawn', exe, RELAUNCH_FLAG]
                     : [exe, RELAUNCH_FLAG],
              { stdin: 'ignore', stdout: 'ignore', stderr: 'ignore' });
    return;
  }
  tjs.spawn(['open', '-n', bundle], { stdout: 'ignore', stderr: 'ignore' });
}
