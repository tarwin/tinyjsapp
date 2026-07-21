// tinyjs CLI: scaffold, run, and package tinyjs projects.
//
//   tinyjs new <dir>     scaffold a new app
//   tinyjs dev           run the app in the current directory
//   tinyjs build         build dist/<name> + dist/<Name>.app
//   tinyjs update        self-update from the latest GitHub release
//   tinyjs uninstall     remove ~/.tinyjs and the PATH symlink
//
// Runs on txiki.js itself (via the `tinyjs` wrapper script).

const enc = new TextEncoder();
const dec = new TextDecoder();

// txiki has no tjs.platform; OS=Windows_NT is always set by Windows itself.
const IS_WIN = tjs.env.OS === 'Windows_NT';

// URL.pathname renders C:\Users\me as /C:/Users/me (percent-encoded) — decode
// and drop the leading slash so the result is a usable Windows path.
function pathFromUrl(u) {
  let p = decodeURIComponent(u.pathname);
  if (/^\/[A-Za-z]:\//.test(p)) p = p.slice(1);
  return p;
}

const TOOL_DIR = pathFromUrl(new URL('.', import.meta.url));
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

// Portable rm -rf (replaces shelling out, which Windows has no equivalent for).
async function rmTree(p) {
  try {
    await tjs.remove(p, { recursive: true });
    return;
  } catch {}
  try {
    const st = await tjs.stat(p);
    if (st.isDirectory) {
      const iter = await tjs.readDir(p);
      for await (const e of iter) await rmTree(p + '/' + e.name);
    }
    await tjs.remove(p);
  } catch {}
}

async function copyFile(src, dest) {
  await tjs.writeFile(dest, await tjs.readFile(src));
}

// Run a shell command line: sh -c on POSIX, cmd /c on Windows (also the only
// way to reach npm/npx there — they are .cmd shims CreateProcess won't exec).
const shellArgv = (cmdline) => IS_WIN ? ['cmd', '/c', cmdline] : ['sh', '-c', cmdline];
// npm/npx-style argv: pass through on POSIX, hop through cmd /c on Windows.
const nodeToolArgv = (argv) => IS_WIN ? ['cmd', '/c', ...argv] : argv;

// Give coding agents working in a scaffolded project the tinyjs reference
// skill — in .claude/skills/ (Claude Code) and .agents/skills/ (the
// tool-agnostic location other agents read).
async function writeAgentSkill(dir) {
  const skill = await tjs.readFile(TOOL_DIR + 'skill/SKILL.md');
  for (const base of ['/.claude/skills/tinyjs', '/.agents/skills/tinyjs']) {
    await tjs.makeDir(dir + base, { recursive: true });
    await tjs.writeFile(dir + base + '/SKILL.md', skill);
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

// Capture stdout, tolerating a non-zero exit (returns whatever was printed).
async function capture(argv, opts = {}) {
  const p = tjs.spawn(argv, { stdout: 'pipe', stderr: 'ignore', ...opts });
  const reader = p.stdout.getReader();
  let out = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    out += dec.decode(value, { stream: true });
  }
  await p.wait();
  return out;
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
    fail('running from a source checkout — update with `git pull` (+ ' +
         (IS_WIN ? 'setup.ps1' : './setup.sh') + ') instead');
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
  // The installer re-resolves "latest", verifies checksums, and swaps the
  // install dir (~/.tinyjs / %LOCALAPPDATA%\tinyjs, or $TINYJS_HOME) in place.
  if (IS_WIN) {
    await run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
               '-Command', 'irm https://tinyjs.app/install.ps1 | iex']);
  } else {
    await run(['sh', '-c', 'curl -fsSL https://tinyjs.app/install | sh']);
  }
  tjs.exit(0);
}

// Read a symlink's target (absolute or relative), or null if p isn't a symlink.
async function readLink(p) {
  const t = (await capture(['readlink', p])).trim();
  return t || null;
}

// Ask a yes/no question on the controlling terminal. No tty → treat as "no",
// so a piped/non-interactive `tinyjs uninstall` never deletes without --yes.
async function confirmTty(prompt) {
  const out = (await capture(['sh', '-c',
    'printf "%s" "$1" >/dev/tty 2>/dev/null && IFS= read -r r </dev/tty && printf "%s" "$r"',
    'sh', prompt])).trim();
  return /^y(es)?$/i.test(out);
}

async function cmdUninstall() {
  if (IS_WIN) {
    fail('there is no Windows installer yet — delete the checkout and remove it from your user PATH (Settings > Environment Variables; setup.ps1 added it)');
  }
  const version = await toolVersion();
  if (version === 'dev') {
    fail('running from a source checkout — nothing to uninstall (just delete the repo)');
  }
  // The install dir is the directory this CLI runs from — the installer created
  // it as ${TINYJS_HOME:-~/.tinyjs}. Trust the running location over the env so
  // we remove exactly the install that's executing.
  const installDir = TOOL_DIR.replace(/\/+$/, '');
  // Safety rails: never rm -rf a root or a git checkout.
  if (!installDir || installDir === '/' || await exists(installDir + '/.git')) {
    fail(`refusing to remove ${installDir || '/'} — does not look like a tinyjs install`);
  }

  // Collect PATH symlinks that point back into this install dir (the same
  // candidate dirs the installer picks from).
  const home = tjs.env.HOME || '';
  const wrapper = installDir + '/tinyjs';
  const links = [];
  for (const d of ['/usr/local/bin', '/opt/homebrew/bin', home + '/.local/bin']) {
    const link = d + '/tinyjs';
    const target = await readLink(link);
    if (!target) continue;
    // Resolve a relative target against the link's own directory.
    const abs = target.startsWith('/') ? target : d + '/' + target;
    if (abs === wrapper) links.push(link);
  }

  console.log(`this will remove tinyjs ${version}:`);
  console.log('  ' + installDir + '   (runtime + CLI)');
  for (const l of links) console.log('  ' + l + '   (PATH symlink)');
  if (!links.length) {
    console.log('  (no PATH symlink found pointing here — remove any stray one by hand)');
  }

  const yes = args.some(a => a === '-y' || a === '--yes' || a === '-f' || a === '--force');
  if (!yes && !(await confirmTty('\nremove it? [y/N] '))) {
    console.log('cancelled');
    tjs.exit(0);
  }

  for (const l of links) await run(['rm', '-f', l]);
  await run(['rm', '-rf', installDir]);

  console.log(`\nuninstalled tinyjs ${version}.`);
  console.log('if the installer added a PATH line to your shell profile, delete the');
  console.log('block marked "# added by tinyjs installer" (harmless if left).');
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

// Backend entry: cfg.backend, or the first of src/main.js|ts, backend/main.js|ts.
// .ts entries (or any entry with cfg.backendBundle) are bundled with esbuild —
// which also makes npm packages usable in the backend.
async function resolveBackendEntry(cfg) {
  if (cfg.backend) {
    if (!(await exists(cfg.backend))) fail('backend entry not found: ' + cfg.backend);
    return cfg.backend;
  }
  for (const p of ['src/main.js', 'src/main.ts', 'backend/main.js', 'backend/main.ts']) {
    if (await exists(p)) return p;
  }
  fail('no backend entry found (src/main.js|ts, backend/main.js|ts, or "backend" in tinyjs.json)');
}

// Generate .build/app/: bridge + copied backend sources + an entry module,
// in the layout `tjs app compile` expects (app dir with an app.json manifest
// — it bundles the whole module graph into one executable). The frontend
// ships as real files (the launcher loads file:// documents, so relative
// css/js/images just work): dev points at src/frontend directly; build
// copies it into .build/app/frontend.
async function generateBuild(cfg, dev = false) {
  await rmTree('.build');
  const B = '.build/app';
  await tjs.makeDir(B, { recursive: true });

  await tjs.writeFile(B + '/app.json',
    enc.encode(JSON.stringify({ version: 0, build: {}, main: 'entry.js' })));
  await tjs.writeFile(B + '/bridge.js', await tjs.readFile(TOOL_DIR + 'runtime/bridge.js'));
  await tjs.writeFile(B + '/update.js', await tjs.readFile(TOOL_DIR + 'runtime/update.js'));

  // Backend: TypeScript (or any entry, with npm packages) bundles via esbuild;
  // plain JS copies sources as before.
  const backendEntry = await resolveBackendEntry(cfg);
  const entryDir = backendEntry.includes('/') ? backendEntry.replace(/\/[^/]*$/, '') : '.';
  let entryName = backendEntry.split('/').pop();
  if (backendEntry.endsWith('.ts')) {
    console.log('==> bundling backend (esbuild)');
    await run(nodeToolArgv(['npx', '--yes', 'esbuild', backendEntry, '--bundle', '--format=esm',
               '--platform=neutral', '--main-fields=module,main',
               '--external:tjs:*', '--log-level=warning',
               '--outfile=' + B + '/src/main.js']));
    entryName = 'main.js';
  } else {
    // Copy the backend dir (minus a nested frontend/, for src/ layouts).
    await tjs.makeDir(B + '/src');
    const iter = await tjs.readDir(entryDir);
    for await (const e of iter) {
      if (e.name === 'frontend') continue;
      const s = entryDir + '/' + e.name;
      const d = B + '/src/' + e.name;
      if (e.isDirectory) await copyTree(s, d);
      else await tjs.writeFile(d, await tjs.readFile(s));
    }
  }

  // Frontend: optional build hook (bundlers) or plain source dir; dev with a
  // devUrl skips all of this — the dev server owns the frontend.
  const fe = cfg.frontend ?? {};
  const devUrl = dev ? fe.devUrl : null;
  let frontendSrc = fe.dir ?? 'src/frontend';
  if (!dev && fe.build) {
    console.log('==> frontend build: ' + fe.build);
    await run(shellArgv(fe.build));
    frontendSrc = fe.dist ?? 'dist';
  }
  if (!devUrl && !(await exists(frontendSrc + '/index.html'))) {
    fail(frontendSrc + '/index.html not found');
  }
  if (!dev) await copyTree(frontendSrc, B + '/frontend');

  // Frontend location at runtime: dev uses the project sources in place;
  // packaged apps resolve relative to entry.js (.app Resources/app/) with a
  // fallback next to the executable for the bare compiled binary, where
  // import.meta.url throws.
  const frontendResolver = devUrl
    ? `const FRONTEND = ${JSON.stringify(devUrl)};`
    : dev
    ? `const FRONTEND = ${JSON.stringify(tjs.cwd + '/' + frontendSrc)};`
    : `let FRONTEND;
try {
  FRONTEND = decodeURIComponent(new URL('./frontend', import.meta.url).pathname);
  if (/^\\/[A-Za-z]:\\//.test(FRONTEND)) FRONTEND = FRONTEND.slice(1); // windows /C:/…
} catch { FRONTEND = tjs.exePath.replace(/[\\\\/][^\\\\/]*$/, '') + '/frontend'; }`;

  let entry = `import { createApp } from './bridge.js';
import * as appMod from './src/${entryName}';

${frontendResolver}

const app = await createApp({
  htmlPath: ${devUrl ? 'FRONTEND' : "FRONTEND + '/index.html'"},
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
  onNotificationAction: appMod.onNotificationAction,
  onMediaKey: appMod.onMediaKey,
  onWindowClosed: appMod.onWindowClosed,
  onClipboardChange: appMod.onClipboardChange,
  onUpdateAvailable: appMod.onUpdateAvailable,
  onAudioTap: appMod.onAudioTap,
  chrome: ${JSON.stringify(cfg.chrome ?? null)},
  update: ${JSON.stringify(cfg.update ?? null)},
  activation: ${JSON.stringify(cfg.activation ?? null)},
  readAccess: ${JSON.stringify(cfg.readAccess ?? null)},
  userAgent: ${JSON.stringify(cfg.userAgent ?? null)},
  audioTap: ${JSON.stringify(cfg.audioTap ?? null)},
  contextMenu: ${JSON.stringify(cfg.contextMenu ?? true)},
  urlScheme: ${JSON.stringify(cfg.urlScheme ?? null)},
  fileExtensions: ${JSON.stringify(cfg.fileExtensions ?? null)},
});
if (appMod.init) appMod.init(app);
`;

  if (dev && !devUrl) {
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

// Overlay tinyjs onto a fresh create-vite scaffold: config wired for the dev
// server + build hook, a backend dir, ambient types, and the agent skill.
// Vite's own templates stay current upstream — we never fork them.
async function scaffoldViteTemplate(dir, name, template) {
  console.log('==> npm create vite (' + template + ')');
  await run(nodeToolArgv(['npm', 'create', 'vite@latest', dir, '--yes', '--', '--template', template]));
  const ts = template.endsWith('-ts');
  const backendEntry = 'backend/main.' + (ts ? 'ts' : 'js');

  await tjs.writeFile(dir + '/tinyjs.json', enc.encode(JSON.stringify({
    name,
    title: name,
    size: '960x640',
    id: 'com.example.' + name,
    version: '0.1.0',
    backend: backendEntry,
    frontend: {
      build: 'npm run build',
      dist: 'dist',
      dev: 'npm run dev',
      devUrl: 'http://127.0.0.1:5173',
    },
  }, null, 2) + '\n'));

  await tjs.makeDir(dir + '/backend', { recursive: true });
  await tjs.writeFile(dir + '/' + backendEntry, enc.encode(
`// tinyjs backend — full system access via txiki.js (the 'tjs' global).
// Every api method is callable from the page: await tiny.api.call('hello', {...})
export const api${ts ? ': Record<string, TinyApiHandler>' : ''} = {
  hello: async ({ name }${ts ? ': { name: string }' : ''}) => 'hi ' + name + ' — from the backend',
};

export function init(app${ts ? ': TinyApp' : ''}) {
  // window is up; push events with app.push('event', data)
}
`));

  // Ambient types where each side's editor picks them up automatically.
  for (const d of ['/src', '/backend']) {
    await tjs.writeFile(dir + d + '/tiny.d.ts', await tjs.readFile(TOOL_DIR + 'template/types/tiny.d.ts'));
    await tjs.writeFile(dir + d + '/tjs.d.ts', await tjs.readFile(TOOL_DIR + 'template/types/tjs.d.ts'));
  }
  // backend/ needs its own project file: the Vite tsconfig only covers src/,
  // and TS "inferred projects" don't load sibling ambient .d.ts files.
  const beConfig = {
    compilerOptions: {
      target: 'es2022',
      module: 'es2022',
      moduleResolution: 'bundler',
      strict: ts,
      checkJs: false,
      noEmit: true,
      types: [],
    },
    include: ['./**/*'],
  };
  await tjs.writeFile(dir + '/backend/' + (ts ? 'tsconfig.json' : 'jsconfig.json'),
    enc.encode(JSON.stringify(beConfig, null, 2) + '\n'));

  // package.json: pin the dev port and make built asset paths relative
  // (file:// documents need base './').
  const pkgPath = dir + '/package.json';
  const pkg = JSON.parse(dec.decode(await tjs.readFile(pkgPath)));
  pkg.scripts = pkg.scripts ?? {};
  // --host 127.0.0.1: modern node binds ::1 for 'localhost', which txiki's
  // IPv4 fetch (our readiness probe) can't reach.
  pkg.scripts.dev = 'vite --host 127.0.0.1 --port 5173 --strictPort';
  pkg.scripts.build = String(pkg.scripts.build ?? 'vite build')
    .replace('vite build', 'vite build --base=./');
  await tjs.writeFile(pkgPath, enc.encode(JSON.stringify(pkg, null, 2) + '\n'));

  await tjs.writeFile(dir + '/icon.png', await tjs.readFile(TOOL_DIR + 'template/icon.png'));
  await writeAgentSkill(dir);

  console.log(`created ${dir}/ (${template} + tinyjs)
  cd ${dir}
  npm install
  tinyjs dev      # vite dev server + native window, HMR included
  tinyjs build    # vite build + package .app`);
}

async function cmdNew() {
  const dir = args[0];
  if (!dir) fail('usage: tinyjs new <dir> [--template vanilla|vanilla-ts|react|react-ts|vue|vue-ts|svelte|svelte-ts|solid|solid-ts]');
  if (await exists(dir)) fail(`'${dir}' already exists`);
  const name = dir.replace(/\/+$/, '').split('/').pop();

  const ti = args.indexOf('--template');
  if (ti !== -1) {
    const template = (args[ti + 1] ?? '').replace(/^=/, '');
    if (!template) fail('--template needs a value (e.g. react-ts)');
    await scaffoldViteTemplate(dir, name, template);
    return;
  }

  await copyTree(TOOL_DIR + 'template', dir);
  const cfgPath = dir + '/tinyjs.json';
  const cfg = dec.decode(await tjs.readFile(cfgPath)).replaceAll('__NAME__', name);
  await tjs.writeFile(cfgPath, enc.encode(cfg));

  await writeAgentSkill(dir);

  console.log(`created ${dir}/
  cd ${dir}
  tinyjs dev      # run it
  tinyjs build    # package it`);
}

// Dev-checkout convenience (Windows): if the native launcher sources (or the
// injected client, which is compiled into it) are newer than the built
// launcher-win.exe, rebuild via setup.ps1 before starting — so hacking on
// tinyjs itself never runs a stale binary. Installed copies (VERSION file
// present) never rebuild.
async function ensureWinLauncherFresh() {
  if (!IS_WIN || (await toolVersion()) !== 'dev') return;
  const exe = TOOL_DIR + 'native/launcher-win.exe';
  const mtime = async (p) => {
    try { return (await tjs.stat(p)).mtim.getTime(); } catch { return null; }
  };
  const built = await mtime(exe);
  let stale = built === null;
  for (const src of ['native/launcher-win.cc', 'runtime/tiny.js']) {
    const m = await mtime(TOOL_DIR + src);
    if (m !== null && (built === null || m > built)) stale = true;
  }
  if (!stale) return;
  console.log('==> launcher sources changed — rebuilding (setup.ps1)');
  await run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
             '-File', TOOL_DIR + 'setup.ps1', '-SkipPath'], { cwd: TOOL_DIR });
}

async function cmdDev() {
  const cfg = await loadConfig();
  maybeNotifyUpdate(); // fire-and-forget; prints if a newer release exists
  await ensureWinLauncherFresh();

  // Frontend dev server (vite etc.): spawn it, wait until it responds, and
  // point the window at it. HMR replaces tinyjs' own frontend watcher; the
  // tiny.* bridge is injected into any origin, so it works over http too.
  let devServer = null;
  const fe = cfg.frontend ?? {};
  if (fe.devUrl) {
    if (fe.dev) {
      console.log('==> starting frontend dev server: ' + fe.dev);
      devServer = tjs.spawn(shellArgv(fe.dev), { stdout: 'inherit', stderr: 'inherit' });
    }
    // Probe both spellings: 'localhost' may be ::1-only (modern node) which
    // txiki's fetch can't reach even when the server is up.
    const probes = [fe.devUrl];
    if (fe.devUrl.includes('//localhost')) probes.push(fe.devUrl.replace('//localhost', '//127.0.0.1'));
    const deadline = Date.now() + 60000;
    let up = false;
    while (!up && Date.now() < deadline) {
      for (const u of probes) {
        try { await fetch(u, { method: 'HEAD' }); up = true; break; } catch {}
      }
      if (!up) await new Promise((r) => setTimeout(r, 500));
    }
    if (!up) {
      devServer?.kill();
      fail('frontend dev server did not respond at ' + fe.devUrl);
    }
  }

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
    // Absolute entry path with uniform native separators: txiki's Windows
    // relative-import resolution breaks on a relative main module and on
    // mixed / and \ in its path.
    const entryPath = IS_WIN
      ? (tjs.cwd + '\\' + B.replace(/\//g, '\\') + '\\entry.js')
      : tjs.cwd + '/' + B + '/entry.js';
    // An explicit TINYJS_LAUNCHER in the environment wins (matches the
    // bridge's own precedence) — useful for testing a different build.
    const devEnv = { ...tjs.env, TINYJS_LAUNCHER: tjs.env.TINYJS_LAUNCHER || (TOOL_DIR + 'native/' + (IS_WIN ? 'launcher-win.exe' : 'launcher')) };
    // Windows: the launcher shows the project icon in the titlebar/taskbar.
    const iconSrc = cfg.icon || 'icon.png';
    if (IS_WIN && (await exists(iconSrc))) devEnv.TINYJS_ICON = tjs.cwd + '/' + iconSrc;
    child = tjs.spawn([tjs.exePath, 'run', entryPath], {
      stdin: 'inherit',
      stdout: 'inherit',
      stderr: 'inherit',
      env: devEnv,
    });
    const st = await child.wait();
    if (!restarting) {
      devServer?.kill();
      tjs.exit(st.exit_status ?? 0);
    }
  }
}

// Installer disk image: the .app plus an /Applications shortcut. Kept as a
// helper so `notarize` can rebuild it from the STAPLED .app — a dmg made at
// build time holds the pre-staple bundle, so offline Gatekeeper can't find the
// ticket inside it.
async function makeDmg(cfg, APP) {
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
  return dmg;
}

async function cmdBuild() {
  const cfg = await loadConfig();
  await generateBuild(cfg);
  const cwd = tjs.cwd;

  console.log('==> compiling backend');
  await rmTree('dist');
  await tjs.makeDir('dist');
  // `tjs app compile` runs from the parent of the app/ dir and bundles the
  // whole module graph into a standalone executable.
  await run([tjs.exePath, 'app', 'compile', cwd + '/dist/' + cfg.name], { cwd: cwd + '/.build' });

  if (IS_WIN) {
    // Windows build: a portable dist/ folder — <name>.exe (compiled backend),
    // launcher.exe next to it (the bridge finds it there), and frontend/.
    // No bundle/codesign step; zip the folder to distribute.
    if (!(await exists('dist/' + cfg.name + '.exe')) && (await exists('dist/' + cfg.name))) {
      await tjs.rename('dist/' + cfg.name, 'dist/' + cfg.name + '.exe');
    }
    // The tjs runtime is a console app; double-clicking a console exe flashes
    // a terminal behind the window (and an attached console makes txiki treat
    // stdin as interactive). Flip the PE subsystem to GUI in the header —
    // stdout still works when a parent provides handles (tinyjs dev pipes).
    {
      const exePath = 'dist/' + cfg.name + '.exe';
      const exe = await tjs.readFile(exePath);
      const dv = new DataView(exe.buffer, exe.byteOffset, exe.byteLength);
      const peOff = dv.getUint32(0x3c, true);
      if (dv.getUint32(peOff, true) === 0x00004550 /* "PE\0\0" */) {
        const subsystemOff = peOff + 24 + 68; // OptionalHeader + Subsystem
        if (dv.getUint16(subsystemOff, true) === 3 /* console */) {
          dv.setUint16(subsystemOff, 2 /* GUI */, true);
          await tjs.writeFile(exePath, exe);
        }
      }
    }
    await copyFile(TOOL_DIR + 'native/launcher-win.exe', 'dist/launcher.exe');
    await copyTree('.build/app/frontend', 'dist/frontend');
    // Icon: ship it for the runtime titlebar/taskbar (the bridge passes it to
    // the launcher), and stamp launcher.exe (a clean PE) so windows get it.
    // dist/<name>.exe must NOT be resource-edited — txiki appends the app
    // bundle after the PE image and UpdateResource destroys it (the Windows
    // twin of the macOS "compiled binaries can't be codesigned" limitation).
    const winIcon = cfg.icon || 'icon.png';
    if (await exists(winIcon)) {
      console.log('==> embedding icon');
      await copyFile(winIcon, 'dist/icon.png');
      await tryRun([TOOL_DIR + 'native/launcher-win.exe', '--embed-icon',
                    tjs.cwd + '/dist/launcher.exe', tjs.cwd + '/' + winIcon]);
    }
    console.log('==> done');
    console.log(`run it:  .\\dist\\${cfg.name}.exe`);
    return;
  }

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
  if (cfg.readAccess) {
    // Widen the page's file:// read root (see createApp readAccess). true =
    // the user's home dir; a string = that path.
    const ra = cfg.readAccess === true ? '~' : String(cfg.readAccess);
    extraKeys += `
  <key>TinyjsReadAccess</key>    <string>${ra}</string>`;
  }
  if (cfg.userAgent) {
    // Custom User-Agent for the webview (see createApp userAgent). Lets a
    // devUrl-wrapped site see a real browser UA instead of WKWebView's default.
    const ua = String(cfg.userAgent).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    extraKeys += `
  <key>TinyjsUserAgent</key>     <string>${ua}</string>`;
  }
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
  <key>TinyjsChrome</key>        <string>${[bit(ch.frame), bit(ch.trafficLights), bit(ch.transparent), vib, bit(ch.squareCorners), bit(ch.acceptsFirstMouse)].join('&#9;')}</string>`;
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
  // Mic/camera ("permissions": { "microphone": "why", "camera": "why" }):
  // TCC kills a bundled app that requests capture without the usage string,
  // and the hardened runtime additionally denies the device without its
  // entitlement — the strings land here, the entitlements at codesign below.
  const perms = cfg.permissions ?? {};
  if (perms.microphone) extraKeys += `
  <key>NSMicrophoneUsageDescription</key> <string>${perms.microphone}</string>`;
  if (perms.camera) extraKeys += `
  <key>NSCameraUsageDescription</key>     <string>${perms.camera}</string>`;
  // tiny.audioTap ("audioTap": "app" | "system"): Core Audio process taps read
  // rendered output. The usage string is required for the capture TCC; a
  // custom reason via "audioTapReason" overrides the default.
  if (cfg.audioTap) {
    const why = cfg.audioTapReason ||
      `${cfg.title} reads audio output for metering and visualization.`;
    extraKeys += `
  <key>NSAudioCaptureUsageDescription</key> <string>${why}</string>`;
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
  // A real identity gets the hardened runtime + a secure timestamp — both
  // required by notarization. QuickJS interprets (no JIT/executable memory)
  // and WKWebView content runs out-of-process, so the only entitlements ever
  // needed are the capture devices: the hardened runtime denies mic/camera
  // outright — even with TCC granted — unless the binary carries them.
  const sigFlags = identity === '-' ? [] : ['--options', 'runtime', '--timestamp'];
  const devices = [...new Set(
    [(perms.microphone || cfg.audioTap) && 'com.apple.security.device.audio-input',
     perms.camera && 'com.apple.security.device.camera'].filter(Boolean))];
  if (identity !== '-' && devices.length) {
    const ENT = '.build/entitlements.plist';
    await tjs.writeFile(ENT, enc.encode(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>${devices.map((d) => `
  <key>${d}</key> <true/>`).join('')}
</dict>
</plist>
`));
    sigFlags.push('--entitlements', ENT);
  }
  for (const bin of ['dist/launcher',
                     APP + '/Contents/MacOS/' + cfg.name,
                     APP + '/Contents/MacOS/tjs',
                     APP]) {
    // Keychain errors (locked keychain, missing key) matter with a real
    // identity, so only silence stderr for the ad-hoc path.
    await run(['codesign', '--force', ...sigFlags, '--sign', identity, bin],
              identity === '-' ? { stdout: 'ignore', stderr: 'ignore' } : { stdout: 'ignore' });
  }
  if (!(await tryRun(['codesign', '--verify', '--strict', '--deep', APP]))) {
    console.log('warning: bundle signature did not verify: ' + APP);
  }

  // Optional installer disk image: the .app plus an /Applications shortcut.
  // Note: this dmg holds the un-notarized .app — `notarize --dmg` rebuilds it
  // from the stapled bundle so it validates offline.
  if (args.includes('--dmg')) await makeDmg(cfg, APP);

  console.log('==> done');
  await run(['ls', '-lh', 'dist/' + cfg.name, 'dist/launcher']);
  console.log(`run it:  ./dist/${cfg.name}   (or open "${APP}")`);
}

// Submit dist/<Title>.app to Apple notarization and staple the ticket.
// Needs a real Developer ID signature plus a notarytool keychain profile
// (create one: xcrun notarytool store-credentials <name> --apple-id … --team-id …).
async function cmdNotarize() {
  if (IS_WIN) fail('notarization is a macOS step — nothing to do on Windows');
  const cfg = await loadConfig();
  const APP = 'dist/' + cfg.title + '.app';
  if (!(await exists(APP))) fail(`${APP} not found — run \`tinyjs build\` first`);
  const identity = cfg.signIdentity || tjs.env.TINYJS_SIGN_IDENTITY;
  if (!identity || identity === '-') {
    fail('notarization needs a real Developer ID — set "signIdentity" in tinyjs.json ' +
         'or export TINYJS_SIGN_IDENTITY, then rebuild');
  }
  const profile = cfg.notarize?.profile || tjs.env.TINYJS_NOTARY_PROFILE;
  if (!profile) {
    fail('no notarytool profile — set tinyjs.json "notarize": { "profile": "…" } or TINYJS_NOTARY_PROFILE');
  }

  // Fail fast on the wrong signature instead of waiting minutes for Apple to
  // reject it. notarytool only accepts a "Developer ID Application" signature;
  // an ad-hoc, unsigned, or "Apple Development" build uploads fine and then
  // comes back "Invalid". Read what the .app is actually signed with (codesign
  // prints to stderr) rather than trusting the configured identity, since the
  // build on disk may predate a config change.
  const sig = await capture(['sh', '-c', 'codesign -dvvv "$1" 2>&1', 'sh', APP]);
  const authorities = (sig.match(/^Authority=.*/gm) || []).map(l => l.slice(10));
  if (!authorities.some(a => a.startsWith('Developer ID Application:'))) {
    const found = authorities[0] ? `signed as "${authorities[0]}"`
      : /\bSignature=adhoc\b/.test(sig) ? 'ad-hoc signed'
      : 'not signed with a Developer ID';
    fail(`${APP} won't notarize — it's ${found}. Rebuild with a "Developer ID ` +
         `Application" certificate: set "signIdentity" in tinyjs.json (or export ` +
         'TINYJS_SIGN_IDENTITY) and re-run `tinyjs build`.');
  }

  const zip = '.build/notarize.zip';
  console.log('==> zipping for submission');
  await run(['ditto', '-c', '-k', '--keepParent', APP, zip]);
  console.log('==> submitting to Apple (this waits for their verdict)');
  await run(['xcrun', 'notarytool', 'submit', zip, '--keychain-profile', profile, '--wait']);
  console.log('==> stapling ticket');
  await run(['xcrun', 'stapler', 'staple', APP]);

  // Rebuild the installer dmg from the now-stapled .app. A dmg made at build
  // time contains the pre-staple bundle (no ticket), which offline Gatekeeper
  // rejects — so refresh it whenever --dmg is passed, or whenever one already
  // exists on disk (from `build --dmg`), since that copy is guaranteed stale.
  const dmg = 'dist/' + cfg.name + '-' + (cfg.version || '0.0.0') + '.dmg';
  if (args.includes('--dmg') || (await exists(dmg))) await makeDmg(cfg, APP);
  console.log('==> done: ' + APP + ' is notarized');
}

// Build, zip the .app, and emit the auto-update manifest next to it.
// Upload dist/publish/* to the directory tinyjs.json "update".url points at.
// WebCrypto sha256 of a file (no shasum/CertUtil spawn; same on both OSes).
async function sha256File(path) {
  const hash = await crypto.subtle.digest('SHA-256', await tjs.readFile(path));
  return Array.from(new Uint8Array(hash)).map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function cmdPublish() {
  const cfg = await loadConfig();
  const version = cfg.version;
  if (!version) fail('tinyjs.json needs a "version" to publish (e.g. "1.0.0")');
  await cmdBuild();

  const zipName = cfg.name + '-' + version + '.zip';
  const PUB = 'dist/publish';
  await rmTree(PUB);
  console.log('==> zipping ' + zipName);
  if (IS_WIN) {
    // Stage dist/ under a named folder so the zip's one top-level entry is
    // the app folder (what update.js swaps in); bsdtar (Windows 10+) writes
    // zip when told -a with a .zip name.
    const stage = '.build/publish-stage';
    await rmTree(stage);
    await tjs.makeDir(stage + '/' + cfg.name, { recursive: true });
    await copyTree('dist', stage + '/' + cfg.name);
    await tjs.makeDir(PUB, { recursive: true });
    await run(['tar', '-a', '-cf', PUB + '/' + zipName, '-C', stage, cfg.name]);
  } else {
    await tjs.makeDir(PUB, { recursive: true });
    await run(['ditto', '-c', '-k', '--keepParent', 'dist/' + cfg.title + '.app', PUB + '/' + zipName]);
  }
  const sha = await sha256File(PUB + '/' + zipName);

  // Zips live next to the manifest, so derive the download url from update.url.
  const base = cfg.update?.url ? cfg.update.url.replace(/\/[^/]*$/, '') : null;
  const manifest = { version, url: (base ?? 'https://YOUR-HOST/updates') + '/' + zipName, sha256: sha };
  // Release notes for the in-app update prompt: --notes "text" or
  // --notes-file CHANGES.md (fed to update.check() as `notes`).
  const ni = args.findIndex((a) => a === '--notes' || a === '--notes-file');
  if (ni >= 0 && args[ni + 1]) {
    manifest.notes = args[ni] === '--notes-file'
      ? dec.decode(await tjs.readFile(args[ni + 1])).trim()
      : args[ni + 1];
  }
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

// Apple-Silicon-first: the bundled tjs runtime is arm64, so an app built or run
// on an Intel Mac won't launch on Apple Silicon (and vice-versa). Heads-up for
// devs — non-fatal, since local dev with the x86_64 build still works. Apple
// Silicon CPUs report as "Apple M…"; Intel Macs report "Intel(R) …".
function warnIfIntelMac() {
  if (IS_WIN) return;
  const model = tjs.system?.cpus?.[0]?.model || '';
  // Only warn on a positively-identified non-Apple CPU; if detection returns
  // nothing, stay quiet rather than false-alarm an Apple Silicon user.
  if (!model || /^Apple /.test(model)) return;
  console.log('tinyjs: heads-up — tinyjs targets Apple Silicon (M1 and later).');
  console.log('        This looks like an Intel Mac; apps you build here will');
  console.log('        not launch on Apple Silicon Macs, and some features are');
  console.log('        untested. Continuing anyway.\n');
}
if (['new', 'dev', 'build', 'publish', 'notarize'].includes(cmd)) warnIfIntelMac();

switch (cmd) {
  case 'new': await cmdNew(); break;
  case 'dev': await cmdDev(); break;
  case 'build': await cmdBuild(); break;
  case 'publish': await cmdPublish(); break;
  case 'notarize': await cmdNotarize(); break;
  case 'version': case '--version': case '-v': await cmdVersion(); break;
  case 'update': await cmdUpdate(); break;
  case 'uninstall': await cmdUninstall(); break;
  default:
    console.log(`tinyjs — tiny desktop apps with txiki.js + webview

usage:
  tinyjs new <dir>    scaffold a new app (zero dependencies)
                        --template react-ts|vue-ts|solid-ts|svelte-ts|vanilla-ts|…
                        scaffolds create-vite + tinyjs overlay instead
  tinyjs dev          run the app in the current directory
  tinyjs build        build dist/<name> and dist/<Name>.app (--dmg: also a disk image)
  tinyjs publish      build + zip the .app + auto-update manifest
                      (--notes "text" | --notes-file FILE → manifest notes)
  tinyjs notarize     submit dist/<Name>.app to Apple notarization + staple
                      (--dmg: also rebuild dist/<name>-<version>.dmg from the
                      stapled .app; auto-rebuilt if a dmg already exists)
  tinyjs update       update the tinyjs CLI itself (--check: only report)
  tinyjs uninstall    remove ~/.tinyjs and the PATH symlink (--yes: no prompt)
  tinyjs version      print version`);
    tjs.exit(cmd ? 1 : 0);
}
