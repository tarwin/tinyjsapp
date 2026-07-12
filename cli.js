// tinyjs CLI: scaffold, run, and package tinyjs projects.
//
//   tinyjs new <dir>     scaffold a new app
//   tinyjs dev           run the app in the current directory
//   tinyjs build         build dist/<name> + dist/<Name>.app
//
// Runs on txiki.js itself (via the `tinyjs` wrapper script).

import { inlineHtml } from './runtime/inline.js';

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

// Like run(), but returns false on failure instead of aborting.
async function tryRun(argv, opts = {}) {
  const p = tjs.spawn(argv, { stdin: 'inherit', stdout: 'ignore', stderr: 'ignore', ...opts });
  const st = await p.wait();
  return st.exit_status === 0 && !st.term_signal;
}

async function loadConfig() {
  if (!(await exists('tinyjs.json'))) {
    fail('no tinyjs.json here — run this from a tinyjs project (or `tinyjs new <dir>` to create one)');
  }
  const cfg = JSON.parse(dec.decode(await tjs.readFile('tinyjs.json')));
  if (!cfg.name) fail('tinyjs.json needs a "name"');
  return { title: cfg.name, size: '960x640', id: 'com.example.' + cfg.name, ...cfg };
}

// Generate .build/app/: bridge + copied backend sources + inlined frontend +
// an entry module, in the layout `tjs app compile` expects (app dir with an
// app.json manifest — it bundles the whole module graph into one executable).
async function generateBuild(cfg, dev = false) {
  await run(['rm', '-rf', '.build']);
  const B = '.build/app';
  await tjs.makeDir(B, { recursive: true });

  await tjs.writeFile(B + '/app.json',
    enc.encode(JSON.stringify({ version: 0, build: {}, main: 'entry.js' })));
  await tjs.writeFile(B + '/bridge.js', await tjs.readFile(TOOL_DIR + 'runtime/bridge.js'));

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

  const { html, missing } = await inlineHtml('src/frontend/index.html');
  for (const m of missing) console.log(`warning: frontend references missing file: ${m}`);
  await tjs.writeFile(B + '/assets.js', enc.encode('export const INDEX_HTML = ' + JSON.stringify(html) + ';\n'));

  let entry = `import { createApp } from './bridge.js';
import * as appMod from './src/main.js';
import { INDEX_HTML } from './assets.js';

const app = await createApp({
  html: INDEX_HTML,
  title: ${JSON.stringify(cfg.title)},
  size: ${JSON.stringify(cfg.size)},
  version: ${JSON.stringify(cfg.version || '0.0.0')},
  api: appMod.api ?? {},
  onMenu: appMod.onMenu,
});
if (appMod.init) appMod.init(app);
`;

  if (dev) {
    // Hot-reload: watch the project frontend, re-inline, swap the page in place.
    await tjs.writeFile(B + '/inline.js', await tjs.readFile(TOOL_DIR + 'runtime/inline.js'));
    entry = `import { inlineHtml } from './inline.js';\n` + entry + `
const FRONTEND = ${JSON.stringify(tjs.cwd + '/src/frontend')};
let reloadTimer = null;
tjs.watch(FRONTEND, () => {
  clearTimeout(reloadTimer);
  reloadTimer = setTimeout(async () => {
    try {
      const { html, missing } = await inlineHtml(FRONTEND + '/index.html');
      for (const m of missing) console.log('warning: frontend references missing file: ' + m);
      await app.reload(html);
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

  // The .app does NOT use the compiled binary (it can't be codesigned, see
  // below): it ships the stock tjs runtime + the app as plain data files in
  // Resources/, started by a tiny signable shim.
  console.log('==> assembling ' + cfg.title + '.app');
  const APP = 'dist/' + cfg.title + '.app';
  await tjs.makeDir(APP + '/Contents/MacOS', { recursive: true });
  await tjs.makeDir(APP + '/Contents/Resources', { recursive: true });
  await run(['cp', TOOL_DIR + 'native/shim', APP + '/Contents/MacOS/' + cfg.name]);
  await run(['cp', tjs.exePath, APP + '/Contents/MacOS/tjs']);
  await run(['cp', 'dist/launcher', APP + '/Contents/MacOS/launcher']);
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
  <key>NSHighResolutionCapable</key> <true/>${iconKey}
</dict>
</plist>
`;
  await tjs.writeFile(APP + '/Contents/Info.plist', enc.encode(plist));

  // Codesign (ad-hoc by default; set signIdentity in tinyjs.json or
  // TINYJS_SIGN_IDENTITY for a real Developer ID). The .app signs fully:
  // shim/tjs/launcher are clean Mach-Os and the app code is data sealed by
  // the bundle signature.
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
                     APP + '/Contents/MacOS/launcher',
                     APP]) {
    await run(['codesign', '--force', '--sign', identity, bin], { stdout: 'ignore', stderr: 'ignore' });
  }
  if (!(await tryRun(['codesign', '--verify', '--strict', '--deep', APP]))) {
    console.log('warning: bundle signature did not verify: ' + APP);
  }

  console.log('==> done');
  await run(['ls', '-lh', 'dist/' + cfg.name, 'dist/launcher']);
  console.log(`run it:  ./dist/${cfg.name}   (or open "${APP}")`);
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
  case 'version': case '--version': case '-v': await cmdVersion(); break;
  default:
    console.log(`tinyjs — tiny desktop apps with txiki.js + webview

usage:
  tinyjs new <dir>    scaffold a new app
  tinyjs dev          run the app in the current directory
  tinyjs build        build dist/<name> and dist/<Name>.app
  tinyjs version      print version`);
    tjs.exit(cmd ? 1 : 0);
}
