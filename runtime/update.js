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
// With update.publicKey, an Ed25519 envelope authenticates the complete
// manifest before platform selection; TLS, sha256 and code checks still apply.

import { verifyManifest } from './update-signing.js';

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

function assertSafeUrl(u, what) {
  const s = String(u ?? '');
  if (/^https:\/\//i.test(s)) return;
  if (/^http:\/\/(127\.0\.0\.1|localhost)([:/]|$)/i.test(s)) return; // local testing
  throw new Error(what + ' must be https:// (got ' + (s || 'nothing') + ')');
}

function parseVer(v) {
  const m = /^v?(\d+)\.(\d+)\.(\d+)/.exec(String(v));
  return m ? [+m[1], +m[2], +m[3]] : null;
}

function isNewer(current, latest) {
  const a = parseVer(current), b = parseVer(latest);
  if (!a || !b) return false;
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] < b[i];
  return false;
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
  if (!url) throw new Error('no update url configured (tinyjs.json "update": { "url": … })');
  assertSafeUrl(url, 'update url');
  const res = await fetch(url, { headers: { 'cache-control': 'no-cache' } });
  if (!res.ok) throw new Error('update check failed: HTTP ' + res.status);
  // A redirect must not downgrade the transport.
  if (res.url) assertSafeUrl(res.url, 'update url (after redirect)');
  const response = await res.json();
  const manifest = publicKey !== undefined
    ? await verifyManifest(response, publicKey) : response;
  let latest = manifest?.version ?? null;
  // Per-platform downloads: `url`/`sha256` are the macOS zip (the original,
  // pre-Windows fields); a `win: { url, sha256, version? }` block carries
  // the Windows zip. On Windows, overlay it — its own version wins when the
  // two platforms ship different builds — and if a release has no Windows
  // build, report "no update" rather than ever pulling a mac zip.
  if (IS_WIN) {
    if (manifest?.win?.url && manifest.win.sha256) {
      manifest.url = manifest.win.url;
      manifest.sha256 = manifest.win.sha256;
      if (manifest.win.version) latest = manifest.win.version;
      if (manifest.win.notes) manifest.notes = manifest.win.notes;
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
  if (publicKey !== undefined) {
    const checked = await checkForUpdate({ url, version, publicKey });
    if (!checked.available) throw new Error('no newer signed update available for this platform');
    manifest = checked.manifest;
  }
  const bundle = await bundlePath();
  if (!bundle) {
    throw new Error('auto-update only works from the packaged .app build');
  }
  if (bundle.includes('/AppTranslocation/')) {
    throw new Error('the app is running from a quarantined location — move it to /Applications and relaunch');
  }

  if (!manifest) manifest = (await checkForUpdate({ url, version })).manifest;
  if (!manifest?.url) throw new Error('update manifest has no download url');
  if (!manifest.sha256) throw new Error('update manifest has no sha256 — refusing to install');
  assertSafeUrl(manifest.url, 'download url');

  const res = await fetch(manifest.url);
  if (!res.ok) throw new Error('download failed: HTTP ' + res.status);
  if (res.url) assertSafeUrl(res.url, 'download url (after redirect)');
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

    // Swap with rollback. Renaming a running .app/folder is fine on macOS
    // and Linux: open files keep working via their inodes until the process
    // exits. (The rename can cross filesystems when tmp is a different
    // mount; the rollback path covers that failure.)
    const backup = bundle + '.update-backup';
    await rmrf(backup);
    try {
      await tjs.rename(bundle, backup);
    } catch {
      throw new Error('cannot move the current app (insufficient permissions?)');
    }
    try {
      await tjs.rename(newApp, bundle);
    } catch {
      // Cross-filesystem rename (tmp is tmpfs on most Linux systems) can't
      // move a directory — copy it instead.
      const copied = await copyTree(newApp, bundle);
      if (!copied) {
        await tjs.rename(backup, bundle).catch(() => {});
        throw new Error('failed to move the new app into place');
      }
    }
    await rmrf(backup);
    return bundle;
  } finally {
    rmrf(tmp);
  }
}

// Recursive copy preserving the executable bit (cross-filesystem fallback
// for the bundle swap). Returns false on any failure.
async function copyTree(src, dst) {
  try {
    await tjs.makeDir(dst, { recursive: true }).catch(() => {});
    const iter = await tjs.readDir(src);
    for await (const e of iter) {
      const s = src + '/' + e.name;
      const d = dst + '/' + e.name;
      if (e.isDirectory) {
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
