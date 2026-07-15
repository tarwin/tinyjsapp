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

const dec = new TextDecoder();

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

async function sha256(path) {
  const { ok, out } = await runCapture(['shasum', '-a', '256', path]);
  return ok ? out.trim().split(/\s+/)[0] : null;
}

// …/MyApp.app/Contents/MacOS/tjs -> …/MyApp.app, or null when not bundled.
export function bundlePath() {
  const exe = tjs.exePath;
  const i = exe.indexOf('.app/Contents/MacOS/');
  return i < 0 ? null : exe.slice(0, i + 4);
}

export async function checkForUpdate({ url, version }) {
  if (!url) throw new Error('no update url configured (tinyjs.json "update": { "url": … })');
  assertSafeUrl(url, 'update url');
  const res = await fetch(url, { headers: { 'cache-control': 'no-cache' } });
  if (!res.ok) throw new Error('update check failed: HTTP ' + res.status);
  // A redirect must not downgrade the transport.
  if (res.url) assertSafeUrl(res.url, 'update url (after redirect)');
  const manifest = await res.json();
  const latest = manifest?.version ?? null;
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
export async function installUpdate({ url, version, manifest }) {
  const bundle = bundlePath();
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
  try {
    const zipPath = tmp + '/update.zip';
    await tjs.writeFile(zipPath, data);

    const got = await sha256(zipPath);
    if (!got || got.toLowerCase() !== String(manifest.sha256).toLowerCase()) {
      throw new Error('checksum mismatch — refusing to install');
    }

    if (!(await runOk(['ditto', '-x', '-k', zipPath, tmp + '/x']))) {
      throw new Error('could not extract the update zip');
    }
    let newApp = null;
    const iter = await tjs.readDir(tmp + '/x');
    for await (const e of iter) {
      if (e.name.endsWith('.app')) { newApp = tmp + '/x/' + e.name; break; }
    }
    if (!newApp) throw new Error('update zip does not contain an .app bundle');

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

    // Swap with rollback. Renaming a running .app is fine on macOS: open
    // files keep working via their inodes until the process exits.
    const backup = bundle + '.update-backup';
    await runOk(['rm', '-rf', backup]);
    if (!(await runOk(['mv', bundle, backup]))) {
      throw new Error('cannot move the current app (insufficient permissions?)');
    }
    if (!(await runOk(['mv', newApp, bundle]))) {
      await runOk(['mv', backup, bundle]);
      throw new Error('failed to move the new app into place');
    }
    await runOk(['rm', '-rf', backup]);
    return bundle;
  } finally {
    runOk(['rm', '-rf', tmp]);
  }
}

export function relaunch(bundle) {
  tjs.spawn(['open', '-n', bundle], { stdout: 'ignore', stderr: 'ignore' });
}
