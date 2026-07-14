// tinyjs CLI: scaffold, run, and package tinyjs projects.
//
//   tinyjs new <dir>     scaffold a new app
//   tinyjs dev           run the app in the current directory
//   tinyjs build         build dist/<name> + dist/<Name>.app
//   tinyjs update        self-update from the latest GitHub release
//
// Runs on txiki.js itself (via the `tinyjs` wrapper script).

const enc = new TextEncoder();
const dec = new TextDecoder();

const TOOL_DIR = new URL('.', import.meta.url).pathname;
// Invoked as: tjs run cli.js <cmd> [args...]
const [cmd, ...args] = tjs.args.slice(3);

function fail(msg) {
  console.log('tinyjs: ' + msg);
  tjs.exit(1);
}

async function exists(p) {
  try {
    await tjs.stat(p);
    return true;
  } catch {
    return false;
  }
}

async function copyTree(src, dest) {
  await tjs.makeDir(dest, { recursive: true }).catch(() => {});
  const iter = await tjs.readDir(src);
  for await (const e of iter) {
    const s = src + '/' + e.name;
    const d = dest + '/' + e.name;
    if (e.isDirectory) await copyTree(s, d);
    else await tjs.writeFile(d, await tjs.readFile(s));
  }
}

async function run(argv, opts = {}) {
  const p = tjs.spawn(argv, { stdin: 'inherit', stdout: 'inherit', stderr: 'inherit', ...opts });
  const st = await p.wait();
  if (st.exit_status !== 0 || st.term_signal) {
    fail(`command failed (${argv[0]}): ` + JSON.stringify(st));
  }
}

// Run a command and capture its stdout.
async function runCapture(argv) {
  const p = tjs.spawn(argv, { stdout: 'pipe', stderr: 'ignore' });
  const reader = p.stdout.getReader();
  let out = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    out += dec.decode(value, { stream: true });
  }
  const st = await p.wait();
  if (st.exit_status !== 0 || st.term_signal) fail(`command failed (${argv[0]})`);
  return out;
}

// Like run(), but returns false on failure instead of aborting.
async function tryRun(argv, opts = {}) {
  const p = tjs.spawn(argv, { stdin: 'inherit', stdout: 'ignore', stderr: 'ignore', ...opts });
  const st = await p.wait();
  return st.exit_status === 0 && !st.term_signal;
}

// --- self-update -------------------------------------------------------------

const REPO = 'tarwin/tinyjsapp';
const UPDATE_CHECK_FILE = TOOL_DIR + '.update-check';
const UPDATE_CHECK_INTERVAL = 24 * 60 * 60 * 1000;

async function toolVersion() {
  try {
    return dec.decode(await tjs.readFile(TOOL_DIR + 'VERSION')).trim();
  } catch {
    return 'dev';
  }
}

function parseVer(v) {
  const m = /^v?(\d+)\.(\d+)\.(\d+)/.exec(String(v));
  return m ? [+m[1], +m[2], +m[3]] : null;
}

// True when `latest` is a release newer than `current`.
function isNewer(current, latest) {
  const a = parseVer(current), b = parseVer(latest);
  if (!a || !b) return false;
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] < b[i];
  return false;
}

function withTimeout(promise, ms) {
  return new Promise((resolve) => {
    const t = setTimeout(() => resolve(null), ms);
    promise.then(
      (v) => { clearTimeout(t); resolve(v); },
      () => { clearTimeout(t); resolve(null); },
    );
  });
}

async function fetchLatestVersion() {
  try {
    const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
      headers: { 'user-agent': 'tinyjs-cli', accept: 'application/vnd.github+json' },
    });
    if (!res.ok) return null;
    const tag = (await res.json()).tag_name;
    return typeof tag === 'string' && parseVer(tag) ? tag : null;
  } catch {
    return null;
  }
}

// Once a day (and never from a source checkout), see if a newer release
// exists and mention it. Network errors are silent; never blocks or fails
// the command it piggybacks on.
async function maybeNotifyUpdate() {
  try {
    const current = await toolVersion();
    if (current === 'dev') return;
    let cache = null;
    try {
      cache = JSON.parse(dec.decode(await tjs.readFile(UPDATE_CHECK_FILE)));
    } catch {}
    const notify = (latest) =>
      console.log(`tinyjs: ${latest} is available (you have ${current}) — run \`tinyjs update\``);
    if (cache?.latest && isNewer(current, cache.latest)) notify(cache.latest);
    if (cache && Date.now() - (cache.checkedAt || 0) < UPDATE_CHECK_INTERVAL) return;
    const latest = await withTimeout(fetchLatestVersion(), 4000);
    if (!latest) return;
    await tjs.writeFile(UPDATE_CHECK_FILE,
      enc.encode(JSON.stringify({ checkedAt: Date.now(), latest })));
    if (isNewer(current, latest) && latest !== cache?.latest) notify(latest);
  } catch {}
}

async function cmdUpdate() {
  const current = await toolVersion();
  if (current === 'dev') {
    fail('running from a source checkout — update with `git pull` (+ ./setup.sh) instead');
  }
  const latest = await withTimeout(fetchLatestVersion(), 10000);
  if (!latest) fail('could not reach GitHub to check the latest release');
  if (!isNewer(current, latest)) {
    console.log(`already up to date (${current})`);
    tjs.exit(0);
  }
  if (args[0] === '--check') {
    console.log(`${latest} is available (you have ${current}) — run \`tinyjs update\` to install`);
    tjs.exit(0);
  }
  console.log(`==> updating ${current} → ${latest}`);
  // The installer re-resolves "latest", verifies checksums, and swaps
  // ~/.tinyjs (or $TINYJS_HOME) in place.
  await run(['sh', '-c', 'curl -fsSL https://tinyjs.app/install | sh']);
  tjs.exit(0);
}

async function loadConfig() {
  if (!(await exists('tinyjs.json'))) {
    fail('no tinyjs.json here — run this from a tinyjs project (or `tinyjs new <dir>` to create one)');
  }
  const cfg = JSON.parse(dec.decode(await tjs.readFile('tinyjs.json')));
  if (!cfg.name) fail('tinyjs.json needs a "name"');
  if (cfg.activation && !['regular', 'accessory'].includes(cfg.activation)) {
    fail('tinyjs.json "activation" must be "regular" or "accessory"');
  }
  return { title: cfg.name, size: '960x640', id: 'com.example.' + cfg.name, ...cfg };
}

// Generate .build/app/: bridge + copied backend sources + an entry module,
// in the layout `tjs app compile` expects (app dir with an app.json manifest
// — it bundles the whole module graph into one executable). The frontend
// ships as real files (the launcher loads file:// documents, so relative
// css/js/images just work): dev points at src/frontend directly; build
// copies it into .build/app/frontend.
async function generateBuild(cfg, dev = false) {
  await run(['rm', '-rf', '.build']);
  const B = '.build/app';
  await tjs.makeDir(B, { recursive: true });

  await tjs.writeFile(B + '/app.json',
    enc.encode(JSON.stringify({ version: 0, build: {}, main: 'entry.js' })));
  await tjs.writeFile(B + '/bridge.js', await tjs.readFile(TOOL_DIR + 'runtime/bridge.js'));
  await tjs.writeFile(B + '/update.js', await tjs.readFile(TOOL_DIR + 'runtime/update.js'));

  // Backend sources (everything under src/ except the frontend).
  await tjs.makeDir(B + '/src');
  const iter = await tjs.readDir('src');
  for await (const e of iter) {
    if (e.name === 'frontend') continue;
    const s = 'src/' + e.name;
    const d = B + '/src/' + e.name;
    if (e.isDirectory) await copyTree(s, d);
    else await tjs.writeFile(d, await tjs.readFile(s));
  }

  if (!(await exists('src/frontend/index.html'))) {
    fail('src/frontend/index.html not found');
  }
  if (!dev) await copyTree('src/frontend', B + '/frontend');

  // Frontend location at runtime: dev uses the project sources in place;
  // packaged apps resolve relative to entry.js (.app Resources/app/) with a
  // fallback next to the executable for the bare compiled binary, where
  // import.meta.url throws.
  const frontendResolver = dev
    ? `const FRONTEND = ${JSON.stringify(tjs.cwd + '/src/frontend')};`
    : `let FRONTEND;
try { FRONTEND = decodeURIComponent(new URL('./frontend', import.meta.url).pathname); }
catch { FRONTEND = tjs.exePath.replace(/\\/[^/]*$/, '') + '/frontend'; }`;

  let entry = `import { createApp } from './bridge.js';
import * as appMod from './src/main.js';

${frontendResolver}

const app = await createApp({
  htmlPath: FRONTEND + '/index.html',
  title: ${JSON.stringify(cfg.title)},
  size: ${JSON.stringify(cfg.size)},
  version: ${JSON.stringify(cfg.version || '0.0.0')},
  tinyjsVersion: ${JSON.stringify(await toolVersion())},
  id: ${JSON.stringify(cfg.id)},
  api: appMod.api ?? {},
  onMenu: appMod.onMenu,
  onTray: appMod.onTray,
  onHotkey: appMod.onHotkey,
  onContextMenu: appMod.onContextMenu,
  onSystem: appMod.onSystem,
  onOpenUrl: appMod.onOpenUrl,
  onOpenFiles: appMod.onOpenFiles,
  onNotificationClick: appMod.onNotificationClick,
  onWindowClosed: appMod.onWindowClosed,
  chrome: ${JSON.stringify(cfg.chrome ?? null)},
  update: ${JSON.stringify(cfg.update ?? null)},
  activation: ${JSON.stringify(cfg.activation ?? null)},
});
if (appMod.init) appMod.init(app);
`;

  if (dev) {
    // Hot-reload: any frontend change re-renders the page from disk in place.
    entry += `
let reloadTimer = null;
tjs.watch(FRONTEND, () => {
  clearTimeout(reloadTimer);
  reloadTimer = setTimeout(async () => {
    try {
      await app.reload();
      console.log('tinyjs: frontend reloaded');
    } catch (e) {
      console.log('tinyjs: frontend reload failed:', String(e));
    }
  }, 150);
});
`;
  }

  entry += `
await app.done;
tjs.exit(0);
`;
  await tjs.writeFile(B + '/entry.js', enc.encode(entry));
  return B;
}

async function cmdNew() {
  const dir = args[0];
  if (!dir) fail('usage: tinyjs new <dir>');
  if (await exists(dir)) fail(`'${dir}' already exists`);
  const name = dir.replace(/\/+$/, '').split('/').pop();

  await copyTree(TOOL_DIR + 'template', dir);
  const cfgPath = dir + '/tinyjs.json';
  const cfg = dec.decode(await tjs.readFile(cfgPath)).replaceAll('__NAME__', name);
  await tjs.writeFile(cfgPath, enc.encode(cfg));

  // Give coding agents working in the project a tinyjs reference skill.
  await tjs.makeDir(dir + '/.claude/skills/tinyjs', { recursive: true });
  await tjs.writeFile(dir + '/.claude/skills/tinyjs/SKILL.md',
    await tjs.readFile(TOOL_DIR + 'skill/SKILL.md'));

  console.log(`created ${dir}/
  cd ${dir}
  tinyjs dev      # run it
  tinyjs build    # package it`);
}

async function cmdDev() {
  const cfg = await loadConfig();
  maybeNotifyUpdate(); // fire-and-forget; prints if a newer release exists

  // Frontend changes hot-reload inside the app process (see the dev entry);
  // backend changes need a fresh process, so we watch and restart.
  let child = null;
  let restarting = false;
  let restartTimer = null;
  tjs.watch('src', (file) => {
    if (!file || String(file).startsWith('frontend')) return;
    clearTimeout(restartTimer);
    restartTimer = setTimeout(() => {
      console.log(`tinyjs: ${file} changed, restarting backend`);
      restarting = true;
      child?.kill();
    }, 150);
  });

  while (true) {
    const B = await generateBuild(cfg, true);
    restarting = false;
    child = tjs.spawn([tjs.exePath, 'run', B + '/entry.js'], {
      stdin: 'inherit',
      stdout: 'inherit',
      stderr: 'inherit',
      env: { ...tjs.env, TINYJS_LAUNCHER: TOOL_DIR + 'native/launcher' },
    });
    const st = await child.wait();
    if (!restarting) tjs.exit(st.exit_status ?? 0);
  }
}

async function cmdBuild() {
  const cfg = await loadConfig();
  await generateBuild(cfg);
  const cwd = tjs.cwd;

  console.log('==> compiling backend');
  await run(['rm', '-rf', 'dist']);
  await tjs.makeDir('dist');
  // `tjs app compile` runs from the parent of the app/ dir and bundles the
  // whole module graph into a standalone executable.
  await run([tjs.exePath, 'app', 'compile', cwd + '/dist/' + cfg.name], { cwd: cwd + '/.build' });
  await run(['cp', TOOL_DIR + 'native/launcher', 'dist/launcher']);
  // The bare binary resolves its frontend next to the executable.
  await run(['cp', '-R', '.build/app/frontend', 'dist/frontend']);

  // The .app does NOT use the compiled binary (it can't be codesigned, see
  // below): it ships the stock tjs runtime + the app as plain data files in
  // Resources/, with the launcher as the bundle executable (it spawns tjs).
  console.log('==> assembling ' + cfg.title + '.app');
  const APP = 'dist/' + cfg.title + '.app';
  await tjs.makeDir(APP + '/Contents/MacOS', { recursive: true });
  await tjs.makeDir(APP + '/Contents/Resources', { recursive: true });
  // The launcher IS the bundle executable ("bundle mode"): it owns the window,
  // receives Apple Events (deep links, file opens, single-instance activation),
  // and spawns the backend (tjs) itself.
  await run(['cp', TOOL_DIR + 'native/launcher', APP + '/Contents/MacOS/' + cfg.name]);
  await run(['cp', tjs.exePath, APP + '/Contents/MacOS/tjs']);
  await run(['cp', '-R', '.build/app', APP + '/Contents/Resources/app']);
  // App icon: icon.png in the project root (1024×1024; the template ships a
  // default) becomes AppIcon.icns via sips + iconutil.
  let iconKey = '';
  const iconSrc = cfg.icon || 'icon.png';
  if (await exists(iconSrc)) {
    console.log('==> generating icon from ' + iconSrc);
    const iconset = '.build/AppIcon.iconset';
    await tjs.makeDir(iconset, { recursive: true });
    for (const pt of [16, 32, 128, 256, 512]) {
      await run(['sips', '-z', String(pt), String(pt), iconSrc, '--out', `${iconset}/icon_${pt}x${pt}.png`], { stdout: 'ignore', stderr: 'ignore' });
      await run(['sips', '-z', String(pt * 2), String(pt * 2), iconSrc, '--out', `${iconset}/icon_${pt}x${pt}@2x.png`], { stdout: 'ignore', stderr: 'ignore' });
    }
    await tjs.makeDir(APP + '/Contents/Resources', { recursive: true });
    await run(['iconutil', '-c', 'icns', '-o', APP + '/Contents/Resources/AppIcon.icns', iconset]);
    iconKey = '\n  <key>CFBundleIconFile</key>      <string>AppIcon</string>';
  }

  // Window size for the launcher's bundle mode, plus optional deep-link
  // scheme(s) ("urlScheme": "myapp" or [..]) and file associations
  // ("fileExtensions": ["md", ...]) from tinyjs.json.
  let extraKeys = `
  <key>TinyjsWindowSize</key>    <string>${cfg.size}</string>`;
  if (cfg.activation === 'accessory') {
    // Menu-bar agent: LSUIElement starts the process with no Dock icon at the
    // system level; TinyjsActivation makes the launcher keep the window hidden
    // until the app shows it.
    extraKeys += `
  <key>LSUIElement</key>         <true/>
  <key>TinyjsActivation</key>    <string>accessory</string>`;
  }
  if (cfg.chrome) {
    const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
    const ch = cfg.chrome;
    const vib = ch.vibrancy === undefined ? '' : (ch.vibrancy === null ? 'none' : ch.vibrancy);
    extraKeys += `
  <key>TinyjsChrome</key>        <string>${[bit(ch.frame), bit(ch.trafficLights), bit(ch.transparent), vib].join('&#9;')}</string>`;
  }
  const schemes = cfg.urlScheme ? [].concat(cfg.urlScheme) : [];
  if (schemes.length) {
    extraKeys += `
  <key>CFBundleURLTypes</key>
  <array><dict>
    <key>CFBundleURLName</key>    <string>${cfg.id}</string>
    <key>CFBundleURLSchemes</key>
    <array>${schemes.map((s) => `<string>${s}</string>`).join('')}</array>
  </dict></array>`;
  }
  const exts = cfg.fileExtensions ?? [];
  if (exts.length) {
    extraKeys += `
  <key>CFBundleDocumentTypes</key>
  <array><dict>
    <key>CFBundleTypeName</key>   <string>${cfg.title} Document</string>
    <key>CFBundleTypeRole</key>   <string>Editor</string>
    <key>LSHandlerRank</key>      <string>Default</string>
    <key>CFBundleTypeExtensions</key>
    <array>${exts.map((e) => `<string>${e}</string>`).join('')}</array>
  </dict></array>`;
  }

  const plist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>${cfg.title}</string>
  <key>CFBundleDisplayName</key>     <string>${cfg.title}</string>
  <key>CFBundleIdentifier</key>      <string>${cfg.id}</string>
  <key>CFBundleVersion</key>         <string>${cfg.version || '0.1.0'}</string>
  <key>CFBundleExecutable</key>      <string>${cfg.name}</string>
  <key>CFBundlePackageType</key>     <string>APPL</string>
  <key>NSHighResolutionCapable</key> <true/>${iconKey}${extraKeys}
</dict>
</plist>
`;
  await tjs.writeFile(APP + '/Contents/Info.plist', enc.encode(plist));

  // Codesign (ad-hoc by default; set signIdentity in tinyjs.json or
  // TINYJS_SIGN_IDENTITY for a real Developer ID). The .app signs fully:
  // launcher/tjs are clean Mach-Os and the app code is data sealed by the
  // bundle signature.
  //
  // The bare dist/<name> single binary is the one thing that can't be
  // re-signed — txiki appends the bundled app after the Mach-O, which
  // codesign rejects ("failed strict validation"). It runs locally on its
  // original linker signature; use the .app for distribution.
  const identity = cfg.signIdentity || tjs.env.TINYJS_SIGN_IDENTITY || '-';
  console.log('==> codesigning' + (identity === '-' ? ' (ad-hoc)' : ' as ' + identity));
  for (const bin of ['dist/launcher',
                     APP + '/Contents/MacOS/' + cfg.name,
                     APP + '/Contents/MacOS/tjs',
                     APP]) {
    await run(['codesign', '--force', '--sign', identity, bin], { stdout: 'ignore', stderr: 'ignore' });
  }
  if (!(await tryRun(['codesign', '--verify', '--strict', '--deep', APP]))) {
    console.log('warning: bundle signature did not verify: ' + APP);
  }

  // Optional installer disk image: the .app plus an /Applications shortcut.
  if (args.includes('--dmg')) {
    console.log('==> creating dmg');
    const STAGE = '.build/dmg';
    await run(['rm', '-rf', STAGE]);
    await tjs.makeDir(STAGE, { recursive: true });
    await run(['cp', '-R', APP, STAGE + '/']);
    await run(['ln', '-s', '/Applications', STAGE + '/Applications']);
    const dmg = 'dist/' + cfg.name + '-' + (cfg.version || '0.0.0') + '.dmg';
    await run(['hdiutil', 'create', '-volname', cfg.title, '-srcfolder', STAGE,
               '-ov', '-quiet', '-format', 'UDZO', dmg]);
    console.log('    ' + dmg);
  }

  console.log('==> done');
  await run(['ls', '-lh', 'dist/' + cfg.name, 'dist/launcher']);
  console.log(`run it:  ./dist/${cfg.name}   (or open "${APP}")`);
}

// Submit dist/<Title>.app to Apple notarization and staple the ticket.
// Needs a real Developer ID signature plus a notarytool keychain profile
// (create one: xcrun notarytool store-credentials <name> --apple-id … --team-id …).
async function cmdNotarize() {
  const cfg = await loadConfig();
  const APP = 'dist/' + cfg.title + '.app';
  if (!(await exists(APP))) fail(`${APP} not found — run \`tinyjs build\` first`);
  const identity = cfg.signIdentity || tjs.env.TINYJS_SIGN_IDENTITY;
  if (!identity || identity === '-') {
    fail('notarization needs a real Developer ID — set "signIdentity" in tinyjs.json and rebuild');
  }
  const profile = cfg.notarize?.profile || tjs.env.TINYJS_NOTARY_PROFILE;
  if (!profile) {
    fail('no notarytool profile — set tinyjs.json "notarize": { "profile": "…" } or TINYJS_NOTARY_PROFILE');
  }
  const zip = '.build/notarize.zip';
  console.log('==> zipping for submission');
  await run(['ditto', '-c', '-k', '--keepParent', APP, zip]);
  console.log('==> submitting to Apple (this waits for their verdict)');
  await run(['xcrun', 'notarytool', 'submit', zip, '--keychain-profile', profile, '--wait']);
  console.log('==> stapling ticket');
  await run(['xcrun', 'stapler', 'staple', APP]);
  console.log('==> done: ' + APP + ' is notarized');
}

// Build, zip the .app, and emit the auto-update manifest next to it.
// Upload dist/publish/* to the directory tinyjs.json "update".url points at.
async function cmdPublish() {
  const cfg = await loadConfig();
  const version = cfg.version;
  if (!version) fail('tinyjs.json needs a "version" to publish (e.g. "1.0.0")');
  await cmdBuild();

  const zipName = cfg.name + '-' + version + '.zip';
  const PUB = 'dist/publish';
  await run(['rm', '-rf', PUB]);
  await tjs.makeDir(PUB, { recursive: true });
  console.log('==> zipping ' + zipName);
  await run(['ditto', '-c', '-k', '--keepParent', 'dist/' + cfg.title + '.app', PUB + '/' + zipName]);
  const sha = (await runCapture(['shasum', '-a', '256', PUB + '/' + zipName])).trim().split(/\s+/)[0];

  // Zips live next to the manifest, so derive the download url from update.url.
  const base = cfg.update?.url ? cfg.update.url.replace(/\/[^/]*$/, '') : null;
  const manifest = { version, url: (base ?? 'https://YOUR-HOST/updates') + '/' + zipName, sha256: sha };
  await tjs.writeFile(PUB + '/manifest.json', enc.encode(JSON.stringify(manifest, null, 2) + '\n'));

  console.log('==> dist/publish/ ready:');
  console.log('    ' + zipName);
  console.log('    manifest.json (version ' + version + ', url ' + manifest.url + ')');
  if (!base) {
    console.log('note: no "update": { "url": … } in tinyjs.json — the manifest url is a');
    console.log('placeholder; set update.url so shipped apps know where to check.');
  }
  console.log('upload both files to the directory update.url points at.');
}

async function cmdVersion() {
  let v = 'dev';
  try {
    v = dec.decode(await tjs.readFile(TOOL_DIR + 'VERSION')).trim();
  } catch {}
  console.log('tinyjs ' + v + ' (txiki.js ' + tjs.version + ')');
}

switch (cmd) {
  case 'new': await cmdNew(); break;
  case 'dev': await cmdDev(); break;
  case 'build': await cmdBuild(); break;
  case 'publish': await cmdPublish(); break;
  case 'notarize': await cmdNotarize(); break;
  case 'version': case '--version': case '-v': await cmdVersion(); break;
  case 'update': await cmdUpdate(); break;
  default:
    console.log(`tinyjs — tiny desktop apps with txiki.js + webview

usage:
  tinyjs new <dir>    scaffold a new app
  tinyjs dev          run the app in the current directory
  tinyjs build        build dist/<name> and dist/<Name>.app (--dmg: also a disk image)
  tinyjs publish      build + zip the .app + auto-update manifest
  tinyjs notarize     submit dist/<Name>.app to Apple notarization + staple
  tinyjs update       update the tinyjs CLI itself (--check: only report)
  tinyjs version      print version`);
    tjs.exit(cmd ? 1 : 0);
}
