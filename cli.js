// tinyjs CLI: scaffold, run, and package tinyjs projects.
//
//   tinyjs new <dir>     scaffold a new app
//   tinyjs dev           run the app in the current directory
//   tinyjs build         build dist/<name> + dist/<Name>.app
//   tinyjs update        self-update from the latest GitHub release
//   tinyjs uninstall     remove ~/.tinyjs and the PATH symlink
//
// Runs on txiki.js itself (via the `tinyjs` wrapper script).

import { pinnedKeys, signManifest, verifyManifest } from './runtime/update-signing.js';

const enc = new TextEncoder();
const dec = new TextDecoder();

// txiki has no tjs.platform; OS=Windows_NT is always set by Windows itself,
// and navigator.platform reads "Linux …" from uname on Linux.
const IS_WIN = tjs.env.OS === 'Windows_NT';
const IS_LINUX = !IS_WIN && /linux/i.test(globalThis.navigator?.platform ?? '');

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

// tjs.watch on a folder: cb(rel) per change, rel relative to dir with '/'
// separators, skipped when skip(rel). macOS (FSEvents) and Windows watch the
// whole tree from one call, but Linux's inotify only sees a folder's direct
// entries, so with `perDir` every subfolder gets its own watch, including
// folders made later. Self-contained: the dev entry inlines its source for
// the frontend reload watcher.
async function watchTree(dir, cb, skip = () => false, perDir = false) {
  const watchers = new Map();
  const add = async (rel) => {
    if (watchers.has(rel)) return;
    const path = rel ? dir + '/' + rel : dir;
    try {
      watchers.set(rel, tjs.watch(path, (file) => {
        if (!file) return;
        const r = (rel ? rel + '/' : '') + String(file).replace(/\\/g, '/');
        if (skip(r)) return;
        if (perDir) sync(r);
        cb(r);
      }));
    } catch { return; }
    if (!perDir) return;
    try {
      for await (const e of await tjs.readDir(path)) {
        const r = (rel ? rel + '/' : '') + e.name;
        if (e.isDirectory && !skip(r)) await add(r);
      }
    } catch {}
  };
  // A name that changed may be a folder made (watch it and what's inside) or
  // removed (drop its watches, so one made again later is watched afresh).
  const sync = async (r) => {
    let isDir = false;
    try { isDir = (await tjs.stat(dir + '/' + r)).isDirectory; } catch {}
    if (isDir) return add(r);
    for (const [k, w] of watchers) {
      if (k === r || k.startsWith(r + '/')) { try { w.close(); } catch {} watchers.delete(k); }
    }
  };
  await add('');
}

// Data the app's own code reads at runtime lives in resources/ and reaches
// it as app.paths().resources: the whole folder, or only the files and
// directories inside it that tinyjs.json "resources" lists (per OS through
// the platform blocks). Files the OS reads (icon, Credits, help book) live
// in assets/ instead. Reject symlinks at every parent and below selected
// directories, even when they point back into the project.
const RESOURCES_DIR = 'resources';
async function validateResources(value) {
  if (value !== undefined && !Array.isArray(value)) fail('tinyjs.json "resources" must be an array of paths inside resources/ (got ' + JSON.stringify(value) + ')');
  let dirSt = null;
  try { dirSt = await tjs.lstat(RESOURCES_DIR); } catch {}
  if (dirSt?.isSymbolicLink) fail('resources/: symlinks are not supported');
  if (dirSt && !dirSt.isDirectory) fail('resources/ must be a directory');
  if (value === undefined) {
    // No list: everything in the folder, if there is one.
    if (!dirSt) return [];
    value = [];
    for await (const e of await tjs.readDir(RESOURCES_DIR)) value.push(e.name);
    value.sort();
  } else if (value.length && !dirSt) {
    fail('tinyjs.json "resources" lists paths inside resources/, but the project has no resources/ folder');
  }
  const paths = [];
  for (const raw of value) {
    const bad = (why) => fail('tinyjs.json resource ' + JSON.stringify(raw) + ': ' + why);
    if (typeof raw !== 'string' || !raw || raw.includes('\0')) bad('expected a non-empty path inside resources/');
    const p = raw.replace(/\\/g, '/');
    if (p.startsWith('/') || p.includes(':')) bad('absolute, drive and UNC paths are not allowed');
    const parts = p.split('/').filter((s) => s && s !== '.');
    if (parts.includes('..')) bad('parent traversal is not allowed');
    if (IS_WIN && parts.some((s) => /[. ]$/.test(s))) bad('Windows path components cannot end in a dot or space');
    if (!parts.length) bad('that is resources/ itself — leave "resources" out to ship the whole folder');
    const rel = parts.join('/');
    if (!paths.includes(rel)) paths.push(rel);
  }
  // A selected directory already includes its children. Keep the first
  // occurrence of each root, independent of the order of overlapping entries.
  const roots = paths.filter((p) => !paths.some((q) => p !== q && p.startsWith(q + '/')));
  const root = paths.length ? await tjs.realPath(RESOURCES_DIR) : null;
  async function check(rel, selected, recurse) {
    const bad = (why) => fail('tinyjs.json resource ' + JSON.stringify(selected) + ' (resources/' + rel + '): ' + why);
    let st;
    try { st = await tjs.lstat(root + '/' + rel); }
    catch { bad('not found or unreadable'); }
    if (st.isSymbolicLink) bad('symlinks are not supported');
    if (!st.isDirectory && !st.isFile) bad('expected a regular file or directory');
    if (recurse && st.isDirectory) {
      try {
        const iter = await tjs.readDir(root + '/' + rel);
        for await (const e of iter) await check(rel + '/' + e.name, selected, true);
      } catch (e) { bad('could not read directory: ' + String(e)); }
    }
  }
  // Validate ALL entries before dropping overlaps: e.g. file.txt/child must
  // still fail even if file.txt was also explicitly selected.
  for (const p of paths) {
    const parts = p.split('/');
    for (let i = 1; i < parts.length; i++) await check(parts.slice(0, i).join('/'), p, false);
    await check(p, p, true);
  }
  return roots;
}

async function stageResources(paths) {
  // Recheck after any frontend build hook, before copying its output.
  for (const p of await validateResources(paths)) {
    const src = RESOURCES_DIR + '/' + p;
    const dest = '.build/resources/' + p;
    try {
      if ((await tjs.lstat(src)).isDirectory) await copyTree(src, dest);
      else {
        await tjs.makeDir(dest.replace(/\/[^/]*$/, ''), { recursive: true });
        await copyFile(src, dest);
      }
    } catch (e) { fail('tinyjs.json resource ' + JSON.stringify(p) + ': could not copy: ' + String(e)); }
  }
}

// Sidecar programs the app spawns ship from executables/, the same way
// resources/ does: with no list, everything in it, overridden by name from
// executables/<os>/ (macos, windows, linux) and then executables/<os>-<arch>/
// (arm64 / x86_64) for that build. "executables": [...] lists paths inside
// the folder instead (per OS through the platform blocks). A .app puts them
// in Contents/Helpers/, where codesign treats them as nested code and signs
// each one; Windows/Linux put them in executables/ beside the exe. Either
// way they land flat, so app.paths().executables + '/<name>' is the program.
// Only this OS's native binaries (and the libraries they load) ship:
// anything else in a .app's code location breaks its signature. A no-list
// build skips other files at the root with a note (that's where the other
// OSes' binaries live); in an OS folder or a list they fail the build.
const EXECUTABLES_DIR = 'executables';
const EXEC_OS = IS_WIN ? 'windows' : IS_LINUX ? 'linux' : 'macos';
const EXEC_KIND = IS_WIN ? 'Windows (PE: .exe/.dll)' : IS_LINUX ? 'Linux (ELF or #! script)' : 'macOS (Mach-O)';
const EXEC_OS_DIR = /^(macos|windows|linux)(-(arm64|x86_64))?$/;
const JUNK_FILES = ['.DS_Store', 'Thumbs.db', 'desktop.ini'];
let hostArchP = null;
// On Windows the app is built from this very tjs.exe, so its CPU is the
// build's: read the PE header rather than the environment (an x64 tjs.exe
// emulated on ARM64 is building an x64 app).
const hostArch = () => (hostArchP ??= IS_WIN
  ? peArch(tjs.exePath).catch(() => null)
      .then((a) => a ?? (/arm64/i.test(tjs.env.PROCESSOR_ARCHITECTURE ?? '') ? 'arm64' : 'x86_64'))
  : capture(['uname', '-m']).then((m) => ({ aarch64: 'arm64', arm64: 'arm64' })[m.trim()] ?? 'x86_64'));

// A Windows PE file's CPU, from the machine field of its COFF header.
async function peArch(path) {
  const fh = await tjs.open(path, 'r');
  const b = new Uint8Array(4096);
  try { await fh.read(b, 0); } finally { await fh.close(); }
  const pe = (b[0x3c] | (b[0x3d] << 8) | (b[0x3e] << 16) | (b[0x3f] << 24)) >>> 0;
  const m = pe + 6 <= b.length ? b[pe + 4] | (b[pe + 5] << 8) : 0;
  return m === 0xaa64 ? 'arm64' : m === 0x8664 ? 'x86_64' : null;
}

function isNativeBinary(b) {
  const u32 = (o) => ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
  if (IS_WIN) return b[0] === 0x4d && b[1] === 0x5a; // MZ
  if (IS_LINUX) return (b[0] === 0x7f && b[1] === 0x45 && b[2] === 0x4c && b[3] === 0x46) || (b[0] === 0x23 && b[1] === 0x21);
  if (b.length < 8) return false;
  const m = u32(0);
  // Thin Mach-O (either endianness), or a fat header — which Java class files
  // share; their "arch count" is the class version (45+), so cap it.
  if ([0xfeedface, 0xfeedfacf, 0xcefaedfe, 0xcffaedfe].includes(m)) return true;
  return (m === 0xcafebabe || m === 0xcafebabf) && u32(4) > 0 && u32(4) < 45;
}

async function readsAsNative(path) {
  const fh = await tjs.open(path, 'r');
  const head = new Uint8Array(8);
  let n = 0;
  try { n = (await fh.read(head, 0)) ?? 0; } finally { await fh.close(); }
  return isNativeBinary(head.subarray(0, n));
}
const notNative = () => `not a ${EXEC_KIND} binary` + (IS_WIN || IS_LINUX ? '' :
  ' — a script in a .app can\'t carry a code signature; ship it in resources/ and run it with its interpreter');

// Returns { files: [{ name, src }] } for this build (name = where it lands,
// src = its path in the project), or null when nothing ships. target: a
// macOS build's MAC_TARGET (null = this Mac).
async function validateExecutables(list, target = null) {
  if (list !== undefined && !Array.isArray(list)) fail('tinyjs.json "executables" must be an array of paths inside executables/ (got ' + JSON.stringify(list) + ')');
  let st = null;
  try { st = await tjs.lstat(EXECUTABLES_DIR); } catch {}
  if (st?.isSymbolicLink) fail('executables/: symlinks are not supported');
  if (st && !st.isDirectory) fail('executables/ must be a directory');
  if (!st) {
    if (list?.length) fail('tinyjs.json "executables" lists paths inside executables/, but the project has no executables/ folder');
    return null;
  }
  const picked = new Map(); // name -> src
  const add = async (src, name, strict, where) => {
    const fst = await tjs.lstat(src);
    if (fst.isSymbolicLink) fail(`${where}: symlinks are not supported`);
    if (!fst.isFile) {
      if (strict) fail(`${where}: expected a file — executables ship flat; put data the program reads in resources/`);
      console.log(`==> note: ${src}/ isn't shipped — executables ship flat (OS folders are named macos, windows, linux, or <os>-arm64 / <os>-x86_64)`);
      return;
    }
    if (!(await readsAsNative(src))) {
      if (strict) fail(`${where}: ${notNative()}`);
      console.log(`==> note: ${src} isn't a ${EXEC_KIND} binary — not shipped in this build`);
      return;
    }
    if (!IS_WIN && !(fst.mode & 0o111))
      console.log(`==> note: ${src} isn't executable — builds mark it, but run chmod +x on it for dev`);
    picked.set(name, src);
  };
  if (list === undefined) {
    // Layers, least specific first; a later file replaces an earlier one of
    // the same name. Everything in an OS folder is meant for that OS, so a
    // wrong file there fails; at the root it's just another OS's.
    const arch = target === 'universal' ? null : target ?? await hostArch();
    const layers = [EXECUTABLES_DIR, EXECUTABLES_DIR + '/' + EXEC_OS];
    if (arch) layers.push(EXECUTABLES_DIR + '/' + EXEC_OS + '-' + arch);
    for (const dir of layers) {
      const root = dir === EXECUTABLES_DIR;
      if (!root) {
        let dst = null;
        try { dst = await tjs.lstat(dir); } catch {}
        if (!dst) continue;
        if (dst.isSymbolicLink || !dst.isDirectory) fail(`executables: ${dir}: expected a folder (symlinks are not supported)`);
      }
      const names = [];
      for await (const e of await tjs.readDir(dir)) names.push(e.name);
      for (const name of names.sort()) {
        if (JUNK_FILES.includes(name) || (root && EXEC_OS_DIR.test(name))) continue;
        await add(dir + '/' + name, name, !root, 'executables: ' + dir + '/' + name);
      }
    }
  } else {
    // A list: exactly these, each a binary for this OS (a folder means the
    // files in it). Same path rules as "resources".
    const from = new Map(); // name -> list entry, to report clashes
    for (const raw of list) {
      const where = 'tinyjs.json executable ' + JSON.stringify(raw);
      if (typeof raw !== 'string' || !raw || raw.includes('\0')) fail(where + ': expected a non-empty path inside executables/');
      const p = raw.replace(/\\/g, '/');
      if (p.startsWith('/') || p.includes(':')) fail(where + ': absolute, drive and UNC paths are not allowed');
      const parts = p.split('/').filter((x) => x && x !== '.');
      if (parts.includes('..')) fail(where + ': parent traversal is not allowed');
      if (!parts.length) fail(where + ': that is executables/ itself — leave "executables" out to ship the whole folder');
      for (let i = 1; i <= parts.length; i++) {
        let pst = null;
        try { pst = await tjs.lstat(EXECUTABLES_DIR + '/' + parts.slice(0, i).join('/')); } catch {}
        if (!pst) fail(where + ': not found');
        if (pst.isSymbolicLink) fail(where + ': symlinks are not supported');
      }
      const src = EXECUTABLES_DIR + '/' + parts.join('/');
      const srcs = [];
      if ((await tjs.lstat(src)).isDirectory) {
        const names = [];
        for await (const e of await tjs.readDir(src)) if (!JUNK_FILES.includes(e.name)) names.push(e.name);
        for (const name of names.sort()) srcs.push(src + '/' + name);
      } else srcs.push(src);
      for (const s of srcs) {
        const name = s.replace(/^.*\//, '');
        if (picked.get(name) === s) continue;
        if (picked.has(name)) fail(`${where}: two files named ${name} would ship (${picked.get(name)} from ${from.get(name)}) — they land side by side, so names must differ`);
        await add(s, name, true, where + (s === src ? '' : ' (' + s + ')'));
        from.set(name, JSON.stringify(raw));
      }
    }
  }
  const files = [...picked].map(([name, src]) => ({ name, src })).sort((a, b) => (a.name < b.name ? -1 : 1));
  return files.length ? { files } : null;
}

// Copy the staged sidecars to dest, keeping them executable.
async function copyExecutables(exe, dest) {
  if (!exe) return;
  await tjs.makeDir(dest, { recursive: true });
  for (const { name } of exe.files) {
    await copyFile('.build/executables/' + name, dest + '/' + name);
    if (!IS_WIN) await tjs.chmod(dest + '/' + name, 0o755);
  }
}

// "executableEntitlements": { "<file name>": ["com.apple…"] } (macOS) — the
// entitlements each sidecar is signed with, replacing any it arrived with.
function executableEntitlements(cfg) {
  const v = cfg.executableEntitlements;
  if (v === undefined) return {};
  const files = (cfg.executablesStaged?.files ?? []).map((f) => f.name);
  const bad = (why) => fail('tinyjs.json "executableEntitlements": ' + why);
  if (!isPlainObject(v)) bad('expected { "<file name>": ["com.apple.security…", …] }');
  for (const [name, list] of Object.entries(v)) {
    if (!files.includes(name)) bad(`${JSON.stringify(name)} isn't one of the executables this build ships` +
      (files.length ? ' (' + files.join(', ') + ')' : ' (none)'));
    if (!Array.isArray(list) || list.some((k) => typeof k !== 'string' || !/^[A-Za-z0-9.-]+$/.test(k)))
      bad(`${JSON.stringify(name)}: expected an array of entitlement keys, e.g. ["com.apple.security.cs.allow-jit"]`);
  }
  return v;
}

// Gather this build's sidecars flat in .build/executables. Dev links them on
// macOS/Linux, so a sidecar rebuilt while dev runs is the one that spawns;
// builds copy and mark them executable.
async function stageExecutables(exe, dev) {
  if (!exe) return;
  await tjs.makeDir('.build/executables', { recursive: true });
  for (const { name, src } of exe.files) {
    const dest = '.build/executables/' + name;
    if (dev && !IS_WIN) await tjs.symlink(await tjs.realPath(src), dest);
    else {
      await copyFile(src, dest);
      if (!IS_WIN) await tjs.chmod(dest, 0o755);
    }
  }
}

// Files for the person downloading the app — a README, a licence, install
// notes — ship from extras/, OUTSIDE the app: beside the executable in the
// Windows/Linux folder (so in the publish archive, and refreshed by each
// update), and at the root of the macOS dmg next to the .app. The .app and
// the macOS update zip never carry them — nobody looks inside a bundle they
// dragged to /Applications. Picked like executables/: with no list,
// everything at the top, overridden by name from extras/<os>/;
// "extras": [...] lists paths inside the folder instead. Each lands by its
// own name (a folder whole), next to files the build writes, so a name the
// build uses fails — case-insensitively, as macOS and Windows compare them.
const EXTRAS_DIR = 'extras';
const EXTRAS_OS_DIR = /^(macos|windows|linux)$/;

function extrasTaken(cfg) {
  if (!IS_WIN && !IS_LINUX) return [cfg.title + '.app', 'Applications'];
  const common = ['resources', 'executables', 'frontend', 'shared', 'bin', 'publish'];
  return IS_WIN ? [cfg.name + '.exe', 'launcher.exe', ...common]
                : [cfg.name, 'launcher', 'icon.png', ...common];
}

// Returns [{ name, src }] for this OS, [] when nothing ships.
async function validateExtras(cfg) {
  const list = cfg.extras;
  if (list !== undefined && !Array.isArray(list)) fail('tinyjs.json "extras" must be an array of paths inside extras/ (got ' + JSON.stringify(list) + ')');
  let st = null;
  try { st = await tjs.lstat(EXTRAS_DIR); } catch {}
  if (st?.isSymbolicLink) fail('extras/: symlinks are not supported');
  if (st && !st.isDirectory) fail('extras/ must be a directory');
  if (!st) {
    if (list?.length) fail('tinyjs.json "extras" lists paths inside extras/, but the project has no extras/ folder');
    return [];
  }
  const taken = extrasTaken(cfg).map((n) => n.toLowerCase());
  const picked = new Map(); // lower-cased name -> { name, src, where, layer }
  const check = async (src, where) => {
    let fst;
    try { fst = await tjs.lstat(src); } catch { fail(`${where}: not found or unreadable`); }
    if (fst.isSymbolicLink) fail(`${where}: symlinks are not supported (${src})`);
    if (!fst.isDirectory && !fst.isFile) fail(`${where}: expected a regular file or folder (${src})`);
    if (fst.isDirectory) for await (const e of await tjs.readDir(src)) await check(src + '/' + e.name, where);
  };
  // A later layer replaces an earlier one's file of the same name; within a
  // layer (or a list) two of them would land on each other.
  const add = async (src, where, layer) => {
    const name = src.replace(/^.*\//, '');
    const key = name.toLowerCase();
    if (picked.get(key)?.src === src) return;
    await check(src, where);
    if (taken.includes(key)) fail(`${where}: the build already writes a ${name} there — rename it`);
    const had = picked.get(key);
    if (had && had.layer === layer) fail(`${where}: two extras named ${name} would ship (${had.src}, from ${had.where}) — they land side by side, so names must differ`);
    picked.set(key, { name, src, where, layer });
  };
  if (list === undefined) {
    const layers = [EXTRAS_DIR, EXTRAS_DIR + '/' + EXEC_OS];
    for (const [layer, dir] of layers.entries()) {
      if (layer) {
        let dst = null;
        try { dst = await tjs.lstat(dir); } catch {}
        if (!dst) continue;
        if (dst.isSymbolicLink || !dst.isDirectory) fail(`extras: ${dir}: expected a folder (symlinks are not supported)`);
      }
      const names = [];
      for await (const e of await tjs.readDir(dir)) names.push(e.name);
      for (const name of names.sort()) {
        if (JUNK_FILES.includes(name) || (!layer && EXTRAS_OS_DIR.test(name))) continue;
        await add(dir + '/' + name, 'extras: ' + dir + '/' + name, layer);
      }
    }
  } else {
    // Same path rules as "resources".
    for (const raw of list) {
      const where = 'tinyjs.json extra ' + JSON.stringify(raw);
      if (typeof raw !== 'string' || !raw || raw.includes('\0')) fail(where + ': expected a non-empty path inside extras/');
      const p = raw.replace(/\\/g, '/');
      if (p.startsWith('/') || p.includes(':')) fail(where + ': absolute, drive and UNC paths are not allowed');
      const parts = p.split('/').filter((x) => x && x !== '.');
      if (parts.includes('..')) fail(where + ': parent traversal is not allowed');
      if (IS_WIN && parts.some((s) => /[. ]$/.test(s))) fail(where + ': Windows path components cannot end in a dot or space');
      if (!parts.length) fail(where + ': that is extras/ itself — leave "extras" out to ship the whole folder');
      for (let i = 1; i < parts.length; i++) {
        let pst = null;
        try { pst = await tjs.lstat(EXTRAS_DIR + '/' + parts.slice(0, i).join('/')); } catch {}
        if (!pst) fail(where + ': not found');
        if (pst.isSymbolicLink) fail(where + ': symlinks are not supported');
      }
      await add(EXTRAS_DIR + '/' + parts.join('/'), where, 0);
    }
  }
  return [...picked.values()].map(({ name, src }) => ({ name, src }))
    .sort((a, b) => (a.name < b.name ? -1 : 1));
}

// Copy one extra (a folder whole, minus OS junk), keeping exec bits — an
// install.sh in a Linux tarball is meant to run.
async function copyExtra(src, dest) {
  const st = await tjs.lstat(src);
  if (st.isDirectory) {
    await tjs.makeDir(dest, { recursive: true });
    for await (const e of await tjs.readDir(src))
      if (!JUNK_FILES.includes(e.name)) await copyExtra(src + '/' + e.name, dest + '/' + e.name);
  } else {
    await copyFile(src, dest);
    if (!IS_WIN && (st.mode & 0o111)) await tjs.chmod(dest, 0o755);
  }
}

async function shipExtras(cfg, dest, label) {
  const files = await validateExtras(cfg);
  if (!files.length) return;
  for (const { name, src } of files) await copyExtra(src, dest + '/' + name);
  if (label) console.log(`==> extras: ${files.map((f) => f.name).join(', ')} → ${label}`);
}

// assets/ holds the files the OS and the build read, each by a known name:
// the icon, the About panel's Credits, the macOS help book. Nothing else in
// it ships, so anything unrecognised is most likely a page or data file put
// in the wrong folder — say so instead of dropping it silently.
const CREDITS_FILES = ['Credits.html', 'Credits.rtf', 'Credits.rtfd'];

// The About panel's credits: assets/Credits.{html,rtf,rtfd}, or a
// Credits.html at the project root (where it went before assets/).
async function findCredits() {
  for (const f of CREDITS_FILES) if (await exists('assets/' + f)) return 'assets/' + f;
  return (await exists('Credits.html')) ? 'Credits.html' : null;
}

// "helpBook" (in the macos block) names a folder inside assets/.
function helpBookRel(value) {
  if (value === undefined) return null;
  const rel = typeof value === 'string' ? value.replace(/\\/g, '/').replace(/\/+$/, '') : '';
  if (!rel || rel.startsWith('/') || rel.includes(':') || rel.split('/').includes('..'))
    fail('tinyjs.json "helpBook" must name a folder inside assets/ (got ' + JSON.stringify(value) + ')');
  // It lands in the .app's Resources/, which already holds the app itself
  // and its icon and credits.
  const folder = rel.replace(/^.*\//, '');
  if (/^(app|AppIcon\.icns|Credits\.(html|rtf|rtfd))$/i.test(folder))
    fail(`tinyjs.json "helpBook": a help book can't be named ${folder} — rename the folder`);
  return rel;
}

async function warnUnusedAssets(cfg) {
  const used = new Set(['.DS_Store', 'Thumbs.db', 'desktop.ini', ...CREDITS_FILES]);
  if (cfg.icon && /^assets\/[^/]+$/.test(cfg.icon)) used.add(cfg.icon.slice(7));
  if (cfg.helpBookAnyOS) used.add(cfg.helpBookAnyOS.split('/')[0]);
  let iter;
  try { iter = await tjs.readDir('assets'); } catch { return; }
  for await (const e of iter) {
    if (used.has(e.name)) continue;
    // A README or licence here was most likely meant for the download.
    if (/^(read[ _-]?me|licen[cs]e|copying|install|changelog)\b/i.test(e.name)) {
      console.log(`==> note: assets/${e.name} isn't used by the build — files for the person ` +
        'downloading the app (a README, a licence) go in extras/');
      continue;
    }
    console.log(`==> note: assets/${e.name} isn't used by the build — assets/ is for the ` +
      'icon, Credits and help book; files for your page go in the frontend, data for your code in resources/');
  }
}

// Copy the help book (#47) into the .app's Resources and (re)build its
// search index. Returns the Info.plist values, or null without one: the
// folder, and the name Help Viewer knows it by — the book's own
// CFBundleIdentifier, falling back to its folder name for a book with no
// Info.plist (older ones).
async function stageHelpBook(cfg, resDir) {
  const rel = helpBookRel(cfg.helpBook);
  if (!rel) return null;
  const src = 'assets/' + rel;
  let st = null;
  try { st = await tjs.lstat(src); } catch {}
  if (!st?.isDirectory) fail(`tinyjs.json "helpBook": ${src} is not a folder`);
  const folder = rel.replace(/^.*\//, '');
  const dest = resDir + '/' + folder;
  await run(['cp', '-R', src, dest]);
  const info = dest + '/Contents/Info.plist';
  const key = async (k) => (await capture(['plutil', '-extract', k, 'raw', '-o', '-', info])).trim();
  let name = await key('CFBundleIdentifier');
  if (!name) {
    name = folder.replace(/\.help$/i, '');
    console.log(`==> note: ${src} has no Contents/Info.plist CFBundleIdentifier — naming the help book "${name}"`);
  }
  // A stale index is the usual reason Help search finds nothing, so build a
  // fresh one into the copy for each language, where the book says it lives.
  const indexPath = await key('HPDBookIndexPath');
  if (indexPath) {
    let iter = null;
    try { iter = await tjs.readDir(dest + '/Contents/Resources'); } catch {}
    for await (const e of iter ?? []) {
      if (!e.isDirectory || !e.name.endsWith('.lproj')) continue;
      const dir = dest + '/Contents/Resources/' + e.name;
      if (!(await tryRun(['hiutil', '-I', 'corespotlight', '-Caf', dir + '/' + indexPath, dir])))
        console.log(`==> note: hiutil could not index ${e.name} of the help book — keeping the index it shipped with`);
    }
  }
  console.log(`==> help book ${folder} (${name})`);
  return { folder, name };
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

// The first file found under dir (depth-first), or null if it holds none —
// for naming what kept a directory from being removed.
async function firstFileIn(dir) {
  try {
    for await (const e of await tjs.readDir(dir)) {
      const p = dir + '/' + e.name;
      if (!e.isDirectory) return p;
      const f = await firstFileIn(p);
      if (f) return f;
    }
  } catch {}
  return null;
}

async function copyFile(src, dest) {
  await tjs.writeFile(dest, await tjs.readFile(src));
}

// Run a shell command line: sh -c on POSIX, cmd /c on Windows (also the only
// way to reach npm/npx there — they are .cmd shims CreateProcess won't exec).
const shellArgv = (cmdline) => IS_WIN ? ['cmd', '/c', cmdline] : ['sh', '-c', cmdline];
// npm/npx-style argv: pass through on POSIX, hop through cmd /c on Windows.
const nodeToolArgv = (argv) => IS_WIN ? ['cmd', '/c', ...argv] : argv;

const SKILL_DIRS = ['.claude/skills/tinyjs', '.agents/skills/tinyjs'];

// Give coding agents working in a scaffolded project the tinyjs reference
// skill — in .claude/skills/ (Claude Code) and .agents/skills/ (the
// tool-agnostic location other agents read).
async function writeAgentSkill(dir) {
  // The whole skill dir: SKILL.md plus references/ (loaded on demand by the
  // agent, so the core stays small while the deep material rides along).
  for (const rel of SKILL_DIRS) await copyTree(TOOL_DIR + 'skill', dir + '/' + rel);
}

// Files `tinyjs new` copies into a project from the install: the agent skill
// folders and the ambient types (types/ for the plain template, src/ and
// backend/ for the Vite ones). Only those the project still has: a deleted
// skill folder or types file stays deleted.
//
// Symbolic links are never followed. A cloned project can ship
// .claude/skills/tinyjs -> ~/Documents or types/tiny.d.ts -> ~/.zshrc; syncing
// through those would delete or overwrite the user's own files. An item with
// a link anywhere on its path is reported and left alone (`linked`), and a
// link INSIDE a skill folder is removed as a link, then replaced by a file.
async function linkOnPath(rel, dir = '.') {
  const parts = rel.split('/');
  for (let i = 1; i <= parts.length; i++) {
    let st;
    try { st = await tjs.lstat(dir + '/' + parts.slice(0, i).join('/')); } catch { return false; }
    if (st.isSymbolicLink) return true;
  }
  return false;
}
const lexists = (p) => tjs.lstat(p).then(() => true, () => false);

async function projectToolFiles(dir = '.') {
  const items = [];
  for (const rel of SKILL_DIRS)
    if (await lexists(dir + '/' + rel))
      items.push({ rel, src: TOOL_DIR + 'skill', isDir: true, linked: await linkOnPath(rel, dir) });
  for (const sub of ['types', 'src', 'backend'])
    for (const f of ['tiny.d.ts', 'tjs.d.ts']) {
      // A Vite project's src/ is the page, which has no tjs: older scaffolds
      // left a tjs.d.ts stub there, and txiki's real types would only offer
      // the page calls it can't make. Leave it alone rather than upgrade it.
      if (sub === 'src' && f === 'tjs.d.ts') continue;
      const rel = `${sub}/${f}`;
      if (await lexists(dir + '/' + rel))
        items.push({ rel, src: TOOL_DIR + 'template/types/' + f, isDir: false, linked: await linkOnPath(rel, dir) });
    }
  return items;
}

// Every entry under root by relative path: a regular file's bytes, IS_DIR for
// a folder (descended into), NOT_FILE for anything else (link, fifo,
// socket), which is never read or followed, or an Unreadable for a file that
// couldn't be read (on Windows, one held open with no sharing): that file
// fails on its own instead of taking the whole folder with it. Folders are
// listed so that an extra empty one counts as a difference too.
const IS_DIR = Symbol('folder');
const NOT_FILE = Symbol('not a regular file');
class Unreadable { constructor(e) { this.message = e?.message ?? String(e); } }
async function filesUnder(root, rel = '', out = new Map()) {
  for await (const e of await tjs.readDir(root + (rel ? '/' + rel : ''))) {
    const r = (rel ? rel + '/' : '') + e.name;
    if (e.isDirectory) {
      out.set(r, IS_DIR);
      await filesUnder(root, r, out);
    } else if (!e.isFile) {
      out.set(r, NOT_FILE);
    } else {
      out.set(r, await tjs.readFile(root + '/' + r).catch((err) => new Unreadable(err)));
    }
  }
  return out;
}

const sameBytes = (a, b) => {
  if (a === NOT_FILE || b === NOT_FILE || a instanceof Unreadable || b instanceof Unreadable) return false;
  if (a === IS_DIR || b === IS_DIR) return a === b;
  return a.length === b.length && a.every((v, i) => v === b[i]);
};

// True when the project's copy matches the install's, entry for entry. Not
// meaningful for a linked item (callers skip those).
async function toolFileCurrent(item, dir = '.') {
  try {
    if (!item.isDir) return sameBytes(await tjs.readFile(dir + '/' + item.rel), await tjs.readFile(item.src));
    const [mine, theirs] = await Promise.all([filesUnder(dir + '/' + item.rel), filesUnder(item.src)]);
    return mine.size === theirs.size &&
      [...theirs].every(([r, entry]) => mine.has(r) && sameBytes(mine.get(r), entry));
  } catch { return false; }
}

// Bring a project's skill and types up to this tinyjs. Explicit, never run by
// dev or build: someone may have edited these files.
async function cmdUpdateProject() {
  if (args.length) fail('usage: tinyjs update-project (no arguments)');
  if (!(await exists('tinyjs.json'))) fail('no tinyjs.json here — run this from a tinyjs project');
  const items = await projectToolFiles();
  // File by file, so every change is named: a skill folder is made to match
  // this tinyjs's exactly, and a leftover (a reference since renamed or
  // dropped, or a file someone added) is deleted out loud, never silently.
  // Each step is caught on its own: a file another program holds open
  // (EBUSY on Windows) is named and skipped, and the run carries on.
  const changes = [], skipped = [], failed = [];
  const step = async (label, path, fn) => {
    try {
      await fn();
      changes.push(label + path);
      return true;
    } catch (e) {
      failed.push(`${path}: ${e?.message ?? e}`);
      return false;
    }
  };
  for (const item of items) {
    if (item.linked) {
      skipped.push(item.rel + (item.isDir ? '/' : ''));
      continue;
    }
    if (!item.isDir) {
      if (!(await toolFileCurrent(item)))
        await step('updated ', item.rel, () => copyFile(item.src, item.rel));
      continue;
    }
    let mine, theirs;
    try {
      [mine, theirs] = await Promise.all([filesUnder(item.rel), filesUnder(item.src)]);
    } catch (e) {
      failed.push(`${item.rel}/: could not read it: ${e?.message ?? e}`);
      continue;
    }
    // The install's own copy must be whole; a broken install is not synced.
    const broken = [...theirs].find(([, entry]) => entry instanceof Unreadable);
    if (broken) {
      failed.push(`${item.src}/${broken[0]} (in tinyjs itself): ${broken[1].message}`);
      continue;
    }
    // Removals first: a link or other non-file is unlinked before anything
    // is written at (or under) its name, so no write can go through it.
    // Deepest paths first, so a folder's contents go before the folder.
    const remove = [...mine]
      .filter(([r, entry]) => !theirs.has(r) || !sameBytes(entry, theirs.get(r)) &&
        (entry === NOT_FILE || entry === IS_DIR || theirs.get(r) === IS_DIR))
      .map(([r]) => r)
      .sort((a, b) => b.split('/').length - a.split('/').length);
    for (const r of remove) {
      const path = item.rel + '/' + r;
      const entry = mine.get(r);
      if (entry === IS_DIR) {
        // Only an emptied folder: txiki's remove() deletes a folder WITH its
        // contents even without { recursive }, and a non-empty one here means
        // something inside failed to go and has been reported already.
        let empty = true;
        try { for await (const _ of await tjs.readDir(path)) { empty = false; break; } } catch {}
        if (!empty) continue;
        if (await step('removed ', path + '/', () => tjs.remove(path))) mine.delete(r);
      } else if (await step('removed ', path + (entry === NOT_FILE ? ' (not a regular file)' : ''),
                            () => tjs.remove(path))) {
        mine.delete(r);
      }
    }
    for (const [r, entry] of theirs) {
      if (entry === IS_DIR || (mine.has(r) && sameBytes(mine.get(r), entry))) continue;
      const dest = item.rel + '/' + r;
      await step(mine.has(r) ? 'updated ' : 'added   ', dest, async () => {
        // A link whose removal failed above must not be written through.
        if (await linkOnPath(dest)) throw new Error('a symbolic link that could not be removed is in the way');
        await tjs.makeDir(dest.replace(/\/[^/]*$/, ''), { recursive: true }).catch(() => {});
        await tjs.writeFile(dest, entry);
      });
    }
  }
  if (!items.some((item) => item.isDir)) {
    for (const rel of SKILL_DIRS) {
      // .claude or .claude/skills may itself be a link, possibly a dangling one.
      if (await linkOnPath(rel)) { skipped.push(rel + '/'); continue; }
      await step('added   ', rel + '/', () => copyTree(TOOL_DIR + 'skill', rel));
    }
  }
  const version = await toolVersion();
  for (const s of skipped)
    console.log(`skipped ${s}: it is, or is inside, a symbolic link — tinyjs won't write through one`);
  if (changes.length) {
    console.log(`from tinyjs ${version}:`);
    for (const c of changes) console.log('  ' + c);
  } else if (!failed.length) {
    console.log(`${skipped.length ? 'nothing else to update' : 'already up to date'} with tinyjs ${version}`);
  }
  if (changes.some((c) => !c.startsWith('added')))
    console.log('edits to updated or removed files are gone; git has the old versions.');
  if (failed.length) {
    console.log(`could not update ${failed.length} item(s):`);
    for (const f of failed) console.log('  ' + f);
    console.log('check their permissions, close whatever has them open (an editor, Explorer, a terminal');
    console.log('inside the folder), and run it again.');
    tjs.exit(1);
  }
}

async function run(argv, opts = {}) {
  const p = tjs.spawn(argv, { stdin: 'inherit', stdout: 'inherit', stderr: 'inherit', ...opts });
  const st = await p.wait();
  if (st.exit_status !== 0 || st.term_signal) {
    fail(`command failed (${argv[0]}): ` + JSON.stringify(st));
  }
}

// Like run(), but stdout passes through only until a line matches `stopAt`;
// that line and everything after it are dropped. For create-vite, whose
// closing "Done. Now run: npm install / npm run dev" contradicts the steps
// we print next (issue #43).
async function runUntil(argv, stopAt, opts = {}) {
  const p = tjs.spawn(argv, { stdin: 'inherit', stdout: 'pipe', stderr: 'inherit', ...opts });
  const reader = p.stdout.getReader();
  let pending = '', stopped = false;
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    if (stopped) continue; // keep draining so the child never blocks on a full pipe
    pending += dec.decode(value, { stream: true });
    const lines = pending.split('\n');
    pending = lines.pop();
    let out = '';
    for (const l of lines) {
      if (stopAt.test(l)) { stopped = true; break; }
      out += l + '\n';
    }
    if (out) await writeOut(out);
  }
  if (!stopped && pending && !stopAt.test(pending)) await writeOut(pending);
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

// The tag comes off github.com's /releases/latest redirect (→ /releases/tag/
// vX.Y.Z), not the REST API: unauthenticated API calls are capped at 60 an
// hour per IP, which shared CI runners hit constantly (#51).
// TINYJS_UPDATE_URL is a test hook (point it at a listener that drops
// connections); only a tag that parses as a version is ever used.
const LATEST_URL = tjs.env.TINYJS_UPDATE_URL || `https://github.com/${REPO}/releases/latest`;

function tagFromUrl(url) {
  const tag = String(url || '').replace(/\/+$/, '').split('/').pop();
  return parseVer(tag) ? tag : null;
}

// Rejects after ms instead of resolving null, so the reason can be reported.
function rejectAfter(promise, ms) {
  let t;
  return Promise.race([
    promise,
    new Promise((_, reject) => { t = setTimeout(() => reject(new Error(`timed out after ${ms / 1000}s`)), ms); }),
  ]).finally(() => clearTimeout(t));
}

// Redirects are followed by hand: txiki follows a Location to file:// itself
// and throws outside the promise, killing the CLI. Usually one hop — the tag
// is already in /releases/latest's Location.
async function latestViaFetch(ms) {
  let url = LATEST_URL;
  for (let hop = 0; hop < 5; hop++) {
    const res = await rejectAfter(fetch(url, {
      method: 'HEAD', redirect: 'manual', headers: { 'user-agent': 'tinyjs-cli' },
    }), ms);
    if (res.status >= 300 && res.status < 400) {
      const next = new URL(res.headers.get('location') || '', url);
      if (next.protocol !== 'https:' && next.protocol !== 'http:') {
        throw new Error(`refusing redirect to ${next.protocol}`);
      }
      url = next.href;
      if (tagFromUrl(url)) return tagFromUrl(url);
      continue;
    }
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const tag = tagFromUrl(url);
    if (!tag) throw new Error(`no version in ${url}`);
    return tag;
  }
  throw new Error('too many redirects');
}

// txiki's fetch drops connections on some networks where curl and browsers
// don't ("closed before established", #67), and the CLI runs on bare tjs —
// bridge.js's fetch repair shim never loads here. curl ships with macOS,
// Linux distros and Windows 10 1803+. --proto keeps every hop http(s); the
// URL sits after -- so it can't parse as a flag.
async function latestViaCurl() {
  const p = tjs.spawn(['curl', '-sS', '-I', '-L', '--max-redirs', '5',
    '--proto', '=http,https', '--proto-redir', '=http,https',
    '--connect-timeout', '10', '--max-time', '15', '-A', 'tinyjs-cli',
    '-o', IS_WIN ? 'NUL' : '/dev/null',
    '-w', '%{http_code} %{url_effective}', '--', LATEST_URL],
    { stdin: 'ignore', stdout: 'pipe', stderr: 'pipe' });
  const readAll = async (s) => {
    const r = s.getReader();
    let out = '';
    for (;;) {
      const { value, done } = await r.read();
      if (done) return out;
      out += dec.decode(value, { stream: true });
    }
  };
  const [out, err] = await Promise.all([readAll(p.stdout), readAll(p.stderr)]);
  const st = await p.wait();
  if (st.exit_status !== 0 || st.term_signal) {
    throw new Error(err.trim().replace(/^curl: /, '') || `exit ${st.exit_status}`);
  }
  const [code, url] = out.trim().split(' ');
  if (!/^2/.test(code)) throw new Error(`HTTP ${code}`);
  const tag = tagFromUrl(url);
  if (!tag) throw new Error(`no version in ${url}`);
  return tag;
}

// { tag } or { error } — the error names what each attempt hit, so a failed
// update says why instead of only "could not reach GitHub". `quick` (the
// background notice) makes one fetch attempt and nothing else.
async function fetchLatestVersion({ quick = false } = {}) {
  const errors = [];
  const attempts = quick
    ? [['fetch', () => latestViaFetch(4000)]]
    : [['fetch', () => latestViaFetch(8000)],
       ['fetch', () => latestViaFetch(8000)],
       ['curl', latestViaCurl]];
  for (const [i, [name, attempt]] of attempts.entries()) {
    if (i > 0) await new Promise((r) => setTimeout(r, 500));
    try {
      return { tag: await attempt() };
    } catch (e) {
      errors.push(`${name}: ${e?.message || e}`);
    }
  }
  return { error: errors.join('; ') };
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
    const latest = (await withTimeout(fetchLatestVersion({ quick: true }), 4000))?.tag;
    if (!latest) return;
    await tjs.writeFile(UPDATE_CHECK_FILE,
      enc.encode(JSON.stringify({ checkedAt: Date.now(), latest })));
    if (isNewer(current, latest) && latest !== cache?.latest) notify(latest);
  } catch {}
}

async function cmdUpdate() {
  const dryRun = args.includes('--dry-run');
  const current = await toolVersion();
  if (current === 'dev') {
    fail('running from a source checkout — update with `git pull` (+ ' +
         (IS_WIN ? 'setup.ps1' : './setup.sh') + ') instead');
  }
  const { tag: latest, error } = await fetchLatestVersion();
  if (!latest) fail(`could not reach GitHub to check the latest release (${error})`);
  if (!isNewer(current, latest)) {
    console.log(`already up to date (${current})`);
    tjs.exit(0);
  }
  if (args[0] === '--check') {
    console.log(`${latest} is available (you have ${current}) — run \`tinyjs update\` to install`);
    tjs.exit(0);
  }
  // Run the installer that shipped WITH this install, not a fresh copy from
  // tinyjs.app: the website would otherwise be trusted to run code on every
  // update, on top of GitHub serving the release (#30). It downloads the
  // pinned tag from GitHub, checks it against the release's checksums.txt,
  // and swaps the install dir — TINYJS_HOME keeps a custom location where it
  // is. It runs from a temp copy because the swap deletes (Unix) or moves
  // (Windows) the directory it would otherwise be running from. Only a
  // hand-assembled install lacks the copy; that falls back to the website.
  // Backslashes on Windows: install.ps1 matches TINYJS_HOME against the user
  // PATH entries verbatim, and a forward-slash spelling would add a second.
  let installDir = TOOL_DIR.replace(/[\\/]+$/, '');
  if (IS_WIN) installDir = installDir.replace(/\//g, '\\');
  const local = TOOL_DIR + (IS_WIN ? 'install.ps1' : 'install');
  const haveLocal = await exists(local);
  const env = { ...tjs.env, TINYJS_HOME: installDir, TINYJS_VERSION: latest };
  console.log(`==> updating ${current} → ${latest} in ${installDir}`);
  if (dryRun) {
    console.log(haveLocal
      ? `    would run the installer shipped with this install: ${local}`
      : `    no installer in this install — would fall back to ${IS_WIN ? 'https://tinyjs.app/install.ps1' : 'https://tinyjs.app/install'}`);
    console.log(`    with TINYJS_HOME=${installDir} TINYJS_VERSION=${latest}`);
    console.log(`    it downloads from https://github.com/${REPO}/releases/tag/${latest} and checks checksums.txt`);
    tjs.exit(0);
  }
  if (!haveLocal) {
    console.log('    (this install has no bundled installer — using the one on tinyjs.app)');
    if (IS_WIN) {
      await run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                 '-Command', 'irm https://tinyjs.app/install.ps1 | iex'], { env });
    } else {
      await run(['sh', '-c', 'curl -fsSL https://tinyjs.app/install | sh'], { env });
    }
    tjs.exit(0);
  }
  const tmp = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-update-XXXXXX');
  const script = tmp + (IS_WIN ? '/install.ps1' : '/install');
  await copyFile(local, script);
  try {
    if (IS_WIN) {
      await run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script], { env });
    } else {
      await run(['sh', script], { env });
    }
  } finally {
    await tjs.remove(tmp, { recursive: true }).catch(() => {});
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

// Per-OS config: root keys apply everywhere, and an optional "macos" /
// "windows" / "linux" block is merged on top for that platform only. Block
// names match tiny.system.os() so the config vocabulary matches the runtime's.
// Plain objects merge (so "macos": { chrome: { vibrancy } } keeps a root
// chrome.frame); scalars and arrays replace outright.
const OS_KEYS = ['macos', 'windows', 'linux'];
// Written as "$schema" into new projects' tinyjs.json for editor completion;
// served from docs/public/schema/ by the site. tinyjs itself ignores the key.
const CONFIG_SCHEMA = 'https://tinyjs.app/schema/tinyjs.schema.json';
const isPlainObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);

function mergeConfig(base, over) {
  const out = { ...base };
  for (const [k, v] of Object.entries(over ?? {})) {
    out[k] = isPlainObject(v) && isPlainObject(base[k]) ? mergeConfig(base[k], v) : v;
  }
  return out;
}

// Env vars OVERRIDE the file — but never silently: a value exported in a shell
// quietly changing how a project signs is exactly the failure worth shouting
// about, so an override that displaces a real config value announces itself.
// [cfg path, env var] pairs; the path is dotted.
const ENV_OVERRIDES = [
  ['signIdentity', 'TINYJS_SIGN_IDENTITY'],
  ['notarize.profile', 'TINYJS_NOTARY_PROFILE'],
  ['notarize.keychain', 'TINYJS_NOTARY_KEYCHAIN'],
];

function applyEnvOverrides(cfg) {
  for (const [path, envName] of ENV_OVERRIDES) {
    const val = tjs.env[envName];
    if (!val) continue;
    const parts = path.split('.');
    let node = cfg;
    for (const p of parts.slice(0, -1)) {
      if (!isPlainObject(node[p])) node[p] = {};
      node = node[p];
    }
    const leaf = parts[parts.length - 1];
    const had = node[leaf];
    node[leaf] = val;
    if (had !== undefined && had !== val) {
      console.log(`==> ${path} from ${envName} (overriding tinyjs.json)`);
    }
  }
  return cfg;
}

// An app can declare the oldest tinyjs it works with. Optional — but without
// it, running an app against a tinyjs that predates an API it uses fails as an
// unexplained TypeError in the page, with nothing naming the real cause.
// A source checkout reports 'dev' and is assumed newest, so it never trips.
async function checkMinVersion(cfg) {
  const want = cfg.minTinyjsVersion;
  if (!want) return;
  const have = await toolVersion();
  if (have === 'dev') return;
  if (!parseVer(want)) return; // unparseable: not worth blocking a build over
  if (isNewer(have, want)) {
    fail(`${cfg.name} needs tinyjs ${want} or newer — you have ${have}.\n` +
         '  run `tinyjs update`, or drop "minTinyjsVersion" from tinyjs.json ' +
         'if you know better.');
  }
}

async function loadConfig() {
  if (!(await exists('tinyjs.json'))) {
    fail('no tinyjs.json here — run this from a tinyjs project (or `tinyjs new <dir>` to create one)');
  }
  const raw = JSON.parse(dec.decode(await tjs.readFile('tinyjs.json')));
  // Resolve the platform ONCE here, so every existing cfg.* read downstream
  // sees the merged value and no call site has to know about OS blocks.
  const osKey = IS_WIN ? 'windows' : IS_LINUX ? 'linux' : 'macos';
  const cfg = applyEnvOverrides(mergeConfig(raw, raw[osKey]));
  for (const k of OS_KEYS) delete cfg[k]; // the blocks themselves aren't config
  if (!cfg.name) fail('tinyjs.json needs a "name"');
  if (cfg.activation && !['regular', 'accessory'].includes(cfg.activation)) {
    fail('tinyjs.json "activation" must be "regular" or "accessory"');
  }
  if (cfg.mainWindow !== undefined && typeof cfg.mainWindow !== 'boolean') fail('tinyjs.json "mainWindow" must be a boolean');
  if (cfg.mainWindow === false && cfg.url) fail('tinyjs.json "url" needs a main window to load into — drop "mainWindow": false');
  cfg.resources = await validateResources(cfg.resources);
  // The app icon: "icon" if set, else assets/icon.png, else icon.png at the
  // project root (where projects made before assets/ keep it). Every reader
  // goes through `cfg.icon || 'icon.png'`, so resolving it here covers them.
  if (!cfg.icon && await exists('assets/icon.png')) cfg.icon = 'assets/icon.png';
  // The help book is macOS's, but its folder sits in assets/ for every OS's
  // build to see — keep the name around so they don't flag it as unused.
  cfg.helpBookAnyOS = helpBookRel(cfg.helpBook ?? raw.macos?.helpBook);
  await checkMinVersion(cfg);
  return { title: cfg.name, size: '960x640', id: 'com.example.' + cfg.name, ...cfg };
}

// Backend entry: cfg.backend, or the first of src/backend/main.js|ts (the
// vanilla layout: backend and frontend side by side under src/), src/main.js|ts
// (the vanilla layout before 0.52, frontend nested in the backend's folder),
// backend/main.js|ts (the Vite templates). .ts entries are bundled with
// esbuild — which also makes npm packages usable in the backend.
async function resolveBackendEntry(cfg) {
  if (cfg.backend) {
    if (!(await exists(cfg.backend))) fail('backend entry not found: ' + cfg.backend);
    return cfg.backend;
  }
  for (const p of ['src/backend/main.js', 'src/backend/main.ts', 'src/main.js', 'src/main.ts',
                   'backend/main.js', 'backend/main.ts']) {
    if (await exists(p)) return p;
  }
  fail('no backend entry found (src/backend/main.js|ts, src/main.js|ts, backend/main.js|ts, or "backend" in tinyjs.json)');
}

// Code both sides import (#58): a shared/ folder beside the backend's folder
// (src/shared next to src/backend; shared/ at the root next to the Vite
// templates' backend/). It ships as app/shared, beside app/src (the backend)
// and app/frontend, so `../shared/x.js` resolves the same from both sides in
// dev and in a build. A backend at the project root has no sibling folder.
function sharedDirFor(backendEntry) {
  const entry = String(backendEntry).replace(/\\/g, '/').replace(/^(\.\/)+/, '');
  if (!entry.includes('/')) return null;
  const entryDir = entry.replace(/\/[^/]*$/, '');
  return entryDir.includes('/') ? entryDir.replace(/\/[^/]*$/, '') + '/shared' : 'shared';
}

// A frontend with its own build tooling needs its packages installed first.
// Without node_modules the script's tools come from wherever else they are
// (a global tsc, say) and fail with errors that never say "install" (#41:
// "Cannot find type definition file for 'vite/client'"). Node resolves
// packages in every parent dir (workspaces hoist them), and Yarn PnP keeps
// none at all, so either of those counts as installed.
async function ensurePackagesInstalled(cmd) {
  if (!(await exists('package.json'))) return;
  let d = tjs.cwd.replace(/\\/g, '/').replace(/\/+$/, '');
  for (;;) {
    if ((await exists(d + '/node_modules')) || (await exists(d + '/.pnp.cjs'))) return;
    const up = d.replace(/\/[^/]*$/, '');
    if (!up || up === d) break;
    d = up;
  }
  fail(`no node_modules for this project, so \`${cmd}\` can't find its tools — ` +
       'run `npm install` (or your package manager\'s install) first');
}

// Run esbuild on project files. The project's own copy wins (that is what
// `npx esbuild` from the project used to pick); otherwise npx fetches it —
// run from a temp dir, because npm 11 refuses npx inside a project whose
// package.json pins another package manager (devEngines.packageManager:
// pnpm — Vite+ 1.0 scaffolds write one) with EBADDEVENGINES. Paths go in
// absolute so the cwd doesn't matter to esbuild.
async function runEsbuild(entry, flags, outfile) {
  const cwd = tjs.cwd.replace(/\\/g, '/').replace(/\/+$/, '');
  const abs = (p) => /^([A-Za-z]:)?\//.test(p) ? p : cwd + '/' + p;
  const argv = [abs(entry), ...flags, '--outfile=' + abs(outfile)];
  for (let d = cwd; ;) {
    const bin = d + '/node_modules/.bin/esbuild' + (IS_WIN ? '.cmd' : '');
    if (await exists(bin)) return run(nodeToolArgv([bin, ...argv]));
    const up = d.replace(/\/[^/]*$/, '');
    if (!up || up === d) break;
    d = up;
  }
  const tmp = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-esbuild-XXXXXX');
  try {
    await run(nodeToolArgv(['npx', '--yes', 'esbuild', ...argv]), { cwd: tmp });
  } finally {
    await tjs.remove(tmp, { recursive: true }).catch(() => {});
  }
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
  await tjs.writeFile(B + '/update-signing.js', await tjs.readFile(TOOL_DIR + 'runtime/update-signing.js'));

  // Backend: TypeScript (or any entry, with npm packages) bundles via esbuild;
  // plain JS copies sources as before.
  const backendEntry = await resolveBackendEntry(cfg);
  const entryDir = backendEntry.includes('/') ? backendEntry.replace(/\/[^/]*$/, '') : '.';
  let entryName = backendEntry.split('/').pop();
  if (backendEntry.endsWith('.ts')) {
    console.log('==> bundling backend (esbuild)');
    await runEsbuild(backendEntry, ['--bundle', '--format=esm',
                     '--platform=neutral', '--main-fields=module,main',
                     '--external:tjs:*', '--log-level=warning'], B + '/src/main.js');
    entryName = 'main.js';
  } else {
    // Copy the backend dir: the folder the entry sits in is the backend's
    // root ("backend": "server/index.js" → server/). Minus a nested
    // frontend/ (src/ layouts) and anything that isn't backend source, which
    // matters once the entry sits at the project root ("backend":
    // "main.js"): .build is where this copy is GOING (copying it recursed
    // until ENAMETOOLONG), and node_modules / dist / .git / the frontend
    // folder would bloat the app. A plain-JS backend can't import from
    // node_modules anyway; bundle (a .ts entry) to use npm packages.
    await tjs.makeDir(B + '/src');
    const fe0 = cfg.frontend ?? {};
    const skip = new Set(['frontend', '.build', 'node_modules', 'dist', '.git']);
    if (entryDir === '.') {
      for (const d of [fe0.dir, fe0.dist]) if (d) skip.add(String(d).replace(/^\.\//, '').split('/')[0]);
      // These ship on their own paths; a copy here would put unsigned code
      // in Resources/app (and duplicate the data).
      for (const d of [RESOURCES_DIR, EXECUTABLES_DIR, 'assets']) skip.add(d);
    }
    const iter = await tjs.readDir(entryDir);
    for await (const e of iter) {
      if (skip.has(e.name)) continue;
      const s = entryDir + '/' + e.name;
      const d = B + '/src/' + e.name;
      if (e.isDirectory) await copyTree(s, d);
      else await tjs.writeFile(d, await tjs.readFile(s));
    }
  }

  // shared/: copied in dev too, since the dev backend runs from .build/app/src.
  // The pre-0.52 layout nests the frontend in the backend's folder, so its
  // page would reach ../../shared in dev but nothing in a build.
  const sharedDir = sharedDirFor(backendEntry);
  if (sharedDir && await exists(sharedDir)) {
    await copyTree(sharedDir, B + '/shared');
  } else if (entryDir === 'src' && await exists('src/shared')) {
    console.log('tinyjs: src/shared/ is only shared with a backend in src/backend/ — ' +
                'move src/main.js (and the files it imports) there');
  }

  // tinyjs.json "inject": document-start JS injected into every window before
  // the page boots (site wrappers shim capabilities/behaviour there). Bundled
  // like the backend (.ts via esbuild); ships as Resources/app/inject.js in a
  // packaged .app, rides the spawn env (TINYJS_INJECT) in dev.
  let injectSrc = null;
  if (cfg.inject) {
    if (!(await exists(cfg.inject))) fail('tinyjs.json "inject": ' + cfg.inject + ' not found');
    if (String(cfg.inject).endsWith('.ts')) {
      console.log('==> bundling inject (esbuild)');
      await runEsbuild(cfg.inject, ['--bundle', '--format=iife', '--log-level=warning'],
                       B + '/inject.js');
      injectSrc = dec.decode(await tjs.readFile(B + '/inject.js'));
    } else {
      injectSrc = dec.decode(await tjs.readFile(cfg.inject));
      await tjs.writeFile(B + '/inject.js', enc.encode(injectSrc));
    }
  }

  // Frontend: optional build hook (bundlers) or plain source dir; dev with a
  // devUrl skips all of this — the dev server owns the frontend. A "url"
  // wrapper (the main window IS a remote page) needs no frontend at all.
  const fe = cfg.frontend ?? {};
  const devUrl = dev ? fe.devUrl : null;
  let frontendSrc = fe.dir ?? 'src/frontend';
  if (!dev && fe.build) {
    await ensurePackagesInstalled(fe.build);
    console.log('==> frontend build: ' + fe.build);
    await run(shellArgv(fe.build));
    frontendSrc = fe.dist ?? 'dist';
  }
  const noMain = cfg.mainWindow === false;
  if (!noMain && !devUrl && !cfg.url && !(await exists(frontendSrc + '/index.html'))) {
    fail(frontendSrc + '/index.html not found');
  }
  if (!dev && (await exists(noMain ? frontendSrc : frontendSrc + '/index.html'))) {
    await copyTree(frontendSrc, B + '/frontend');
  }

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

  // Resources stay outside the TPK, whose file URLs point at a temporary
  // extraction directory. Use the existing .app/portable bundle detection,
  // falling back to the executable's directory for a bare macOS binary.
  if (!dev) await stageResources(cfg.resources);
  // Sidecars (executables/) resolve the same way; a .app keeps them in
  // Contents/Helpers, and dev in .build/executables (links to the sources).
  cfg.executablesStaged = await validateExecutables(cfg.executables, dev ? null : MAC_TARGET);
  await stageExecutables(cfg.executablesStaged, dev);
  const bundled = (name, sub, macSub) => `const ${name} = PACKAGED_BUNDLE
  ? PACKAGED_BUNDLE + ${JSON.stringify(IS_WIN || IS_LINUX ? sub : macSub)}
  : tjs.exePath.replace(/[\\\\/][^\\\\/]*$/, '') + ${JSON.stringify(sub)};`;
  const resourceResolver = [
    !dev && (cfg.resources.length || cfg.executablesStaged)
      ? `import { bundlePath } from './update.js';\nconst PACKAGED_BUNDLE = await bundlePath();` : '',
    !cfg.resources.length
      ? 'const RESOURCES = null;'
      : dev
      ? `const RESOURCES = ${JSON.stringify(await tjs.realPath(RESOURCES_DIR))};`
      : bundled('RESOURCES', '/resources', '/Contents/Resources/app/resources'),
    !cfg.executablesStaged
      ? 'const EXECUTABLES = null;'
      : dev
      ? `const EXECUTABLES = ${JSON.stringify(await tjs.realPath('.build/executables'))};`
      : bundled('EXECUTABLES', '/executables', '/Contents/Helpers'),
  ].filter(Boolean).join('\n');

  let entry = `import { createApp } from './bridge.js';
import * as appMod from './src/${entryName}';

${frontendResolver}
${resourceResolver}

const app = await createApp({
  htmlPath: ${devUrl ? 'FRONTEND' : "FRONTEND + '/index.html'"},
  title: ${JSON.stringify(cfg.title)},
  size: ${JSON.stringify(cfg.size)},
  version: ${JSON.stringify(cfg.version || '0.0.0')},
  tinyjsVersion: ${JSON.stringify(await toolVersion())},
  id: ${JSON.stringify(cfg.id)},
  resourcesPath: RESOURCES,
  executablesPath: EXECUTABLES,
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
  onWindowState: appMod.onWindowState,
  onClipboardChange: appMod.onClipboardChange,
  onUpdateAvailable: appMod.onUpdateAvailable,
  onAudioTap: appMod.onAudioTap,
  onLocale: appMod.onLocale,
  onNavigate: appMod.onNavigate,
  onDownload: appMod.onDownload,
  onWindowOpen: appMod.onWindowOpen,
  url: ${JSON.stringify(cfg.url ?? null)},
  downloads: ${JSON.stringify(cfg.downloads ?? null)},
  popups: ${JSON.stringify(cfg.popups ?? null)},
  apiAccess: ${JSON.stringify(cfg.api ?? null)},
  inject: ${JSON.stringify(injectSrc)},
  chrome: ${JSON.stringify(cfg.chrome ?? null)},
  update: ${JSON.stringify(cfg.update ?? null)},
  activation: ${JSON.stringify(cfg.activation ?? null)},
  mainWindow: ${JSON.stringify(cfg.mainWindow ?? true)},
  readAccess: ${JSON.stringify(cfg.readAccess ?? null)},
  userAgent: ${JSON.stringify(cfg.userAgent ?? null)},
  audioTap: ${JSON.stringify(cfg.audioTap ?? null)},
  windowPlacement: ${JSON.stringify(cfg.windowPlacement ?? null)},
  contextMenu: ${JSON.stringify(cfg.contextMenu ?? true)},
  browserAccelerators: ${JSON.stringify(cfg.browserAccelerators ?? false)},
  debug: ${JSON.stringify(cfg.debug ?? false)},
  about: ${JSON.stringify(cfg.about ?? null)},
  urlScheme: ${JSON.stringify(cfg.urlScheme ?? null)},
  fileExtensions: ${JSON.stringify(cfg.fileExtensions ?? null)},
  openFolders: ${JSON.stringify(cfg.openFolders ?? false)},
  permissions: ${JSON.stringify(cfg.permissions ?? null)},
  offscreenRescue: ${JSON.stringify(cfg.offscreenRescue ?? null)},
  rememberWindows: ${JSON.stringify(cfg.rememberWindows ?? false)},
});
if (appMod.init) appMod.init(app);
`;

  if (dev && !devUrl && !cfg.url && fe.reload !== false && await exists(frontendSrc)) {
    // Hot-reload: any frontend change re-renders the page from disk in place.
    // "url" apps skip it twice over: the main window is a remote page, and
    // the frontend dir being watched may not even exist. "frontend.reload":
    // false hands reloading to a tool that already does its own (a watch-
    // mode bundler writing into the frontend dir plus a livereload script),
    // which would otherwise reload the page twice per save.
    entry += `
let reloadTimer = null;
${watchTree}
await watchTree(FRONTEND, () => {
  clearTimeout(reloadTimer);
  reloadTimer = setTimeout(async () => {
    try {
      await app.reload();
      console.log('tinyjs: frontend reloaded');
    } catch (e) {
      console.log('tinyjs: frontend reload failed:', String(e));
    }
  }, 150);
}, undefined, ${IS_LINUX});
`;
  }

  // Pass the launcher's death on (#78): GDK _exit(1)s on a Wayland protocol
  // error, and a flat exit(0) made that crash look like a normal quit.
  entry += `
const launcherSt = await app.done;
if (launcherSt?.term_signal || launcherSt?.exit_status) {
  console.error('tinyjs: webview launcher ' + (launcherSt.term_signal
    ? 'killed by ' + launcherSt.term_signal
    : 'exited with status ' + launcherSt.exit_status));
  tjs.exit(launcherSt.exit_status || 1);
}
tjs.exit(0);
`;
  await tjs.writeFile(B + '/entry.js', enc.encode(entry));
  return B;
}

// Interactive prompts for `tinyjs new`. Reads can return several typed-ahead
// lines at once, so whatever follows the first newline waits for the next call.
// tjs.stdin is a ReadableStream whose isTerminal is a plain boolean.
const stdinIsTTY = () => {
  try {
    const t = tjs.stdin.isTerminal;
    return !!(typeof t === 'function' ? t.call(tjs.stdin) : t);
  } catch { return false; }
};
// Prompt text with no newline (console.log would add one). The writer lock
// is released each time so console output in between still works.
async function writeOut(text) {
  const w = tjs.stdout.getWriter();
  try { await w.write(enc.encode(text)); } finally { w.releaseLock(); }
}
let stdinRest = '';
let stdinReader = null;
async function readLine() {
  stdinReader ??= tjs.stdin.getReader();
  while (!stdinRest.includes('\n')) {
    const { value, done } = await stdinReader.read();
    if (done) break; // EOF: take what there is
    stdinRest += dec.decode(value, { stream: true });
  }
  const i = stdinRest.indexOf('\n');
  const line = i === -1 ? stdinRest : stdinRest.slice(0, i);
  stdinRest = i === -1 ? '' : stdinRest.slice(i + 1);
  return line.replace(/\r$/, '').trim();
}

// Numbered menu → the chosen index. Enter takes the default; the number or
// the item's first word both work; anything else asks again.
async function chooseTty(title, items, def = 0) {
  console.log('\n' + title);
  items.forEach((it, i) => console.log(`  [${i + 1}] ${it}`));
  for (;;) {
    await writeOut(`choose [1-${items.length}, enter = ${def + 1}]: `);
    const a = (await readLine()).toLowerCase();
    if (a === '') return def;
    const n = Number(a);
    if (Number.isInteger(n) && n >= 1 && n <= items.length) return n - 1;
    const k = items.findIndex((it) => it.split(/\s/)[0].toLowerCase() === a);
    if (k !== -1) return k;
  }
}

// What each package manager runs. vp (Vite+) is a toolchain over one of the
// others: it scaffolds (and installs) itself, then the project's scripts call
// vp, so dev/build/install go through the package manager underneath it.
const PACKAGE_MANAGERS = {
  // npm's --yes only answers npm's own prompts; create-vite 9's "Install and
  // start now?" would run `npm run dev` and block here forever (issue #5) —
  // hence --no-interactive --no-immediate for every flavour.
  npm: { create: (dir, t) => ['npm', 'create', 'vite@latest', dir, '--yes', '--',
                              '--template', t, '--no-interactive', '--no-immediate'] },
  pnpm: { create: (dir, t) => ['pnpm', 'create', 'vite', dir,
                               '--template', t, '--no-interactive', '--no-immediate'] },
  yarn: { create: (dir, t) => ['yarn', 'create', 'vite', dir,
                               '--template', t, '--no-interactive', '--no-immediate'] },
  bun: { create: (dir, t) => ['bun', 'create', 'vite', dir,
                              '--template', t, '--no-interactive', '--no-immediate'] },
};
const VP_CREATE = (dir, t, under) => ['vp', 'create', 'vite', '--no-interactive', '--no-hooks',
  '--package-manager', under, '--', dir, '--template', t, '--no-interactive', '--no-immediate'];

// `<pm> --version` → the version, or null when it is missing or broken. On
// PATH is not enough (a stale shim fails here), and a hung one is killed.
async function pmVersion(pm) {
  let p;
  try { p = tjs.spawn(nodeToolArgv([pm, '--version']), { stdout: 'pipe', stderr: 'ignore' }); }
  catch { return null; }
  const timer = setTimeout(() => { try { p.kill(); } catch {} }, 8000);
  try {
    const reader = p.stdout.getReader();
    let out = '';
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      out += dec.decode(value, { stream: true });
    }
    const st = await p.wait();
    if (st.exit_status !== 0 || st.term_signal) return null;
    return out.match(/\d+\.\d+\.\d+\S*/)?.[0] ?? null;
  } catch { return null; } finally { clearTimeout(timer); }
}

const VITE_FRAMEWORKS = ['vanilla', 'react', 'vue', 'svelte', 'solid', 'preact', 'lit', 'alpine'];

// Overlay tinyjs onto a fresh create-vite scaffold: config wired for the dev
// server + build hook, a backend dir, ambient types, and the agent skill.
// Vite's own templates stay current upstream — we never fork them. Alpine has
// no create-vite template: it is the vanilla one with Alpine's demo page.
// opts: { pm: npm|pnpm|yarn|bun|vp, under: vp's package manager, install }
async function scaffoldViteTemplate(dir, name, template, opts) {
  const { pm, under, install } = opts;
  const runner = pm === 'vp' ? under : pm;
  const ts = template.endsWith('-ts');
  const alpine = template.replace(/-ts$/, '') === 'alpine';
  const viteTemplate = alpine ? (ts ? 'vanilla-ts' : 'vanilla') : template;
  console.log(`==> ${pm === 'vp' ? 'vp' : pm} create vite (${viteTemplate})`);
  await runUntil(nodeToolArgv(pm === 'vp' ? VP_CREATE(dir, viteTemplate, under)
                                          : PACKAGE_MANAGERS[pm].create(dir, viteTemplate)),
                 /Done\. Now run/);
  if (!(await exists(dir + '/package.json'))) fail(`${pm} create vite did not produce ${dir}/package.json`);
  const backendEntry = 'backend/main.' + (ts ? 'ts' : 'js');

  await tjs.writeFile(dir + '/tinyjs.json', enc.encode(JSON.stringify({
    $schema: CONFIG_SCHEMA,
    name,
    title: name,
    size: '960x640',
    id: 'com.example.' + name,
    version: '0.1.0',
    backend: backendEntry,
    frontend: {
      build: runner + ' run build',
      dist: 'dist',
      dev: runner + ' run dev',
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
  // txiki's types (tjs) go to the backend only: the page has no tjs.
  for (const d of ['/src', '/backend'])
    await tjs.writeFile(dir + d + '/tiny.d.ts', await tjs.readFile(TOOL_DIR + 'template/types/tiny.d.ts'));
  await tjs.writeFile(dir + '/backend/tjs.d.ts', await tjs.readFile(TOOL_DIR + 'template/types/tjs.d.ts'));
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
  // IPv4 fetch (our readiness probe) can't reach. Vite+ projects keep their
  // vp front end (same flags; vp dev/build pass them to Vite).
  const viteBin = /^vp\b/.test(String(pkg.scripts.dev ?? '')) ? 'vp dev' : 'vite';
  pkg.scripts.dev = viteBin + ' --host 127.0.0.1 --port 5173 --strictPort';
  pkg.scripts.build = String(pkg.scripts.build ?? 'vite build')
    .replace(/\b(vite|vp) build\b/, '$1 build --base=./');
  if (alpine) {
    pkg.dependencies = { ...pkg.dependencies, alpinejs: '^3' };
    if (ts) pkg.devDependencies = { ...pkg.devDependencies, '@types/alpinejs': '^3' };
  }
  await tjs.writeFile(pkgPath, enc.encode(JSON.stringify(pkg, null, 2) + '\n'));
  if (alpine) await overlayAlpine(dir, name, ts);

  await tjs.makeDir(dir + '/assets', { recursive: true });
  await tjs.writeFile(dir + '/assets/icon.png', await tjs.readFile(TOOL_DIR + 'template/assets/icon.png'));
  await writeAgentSkill(dir);

  // vp create installs on its own; only Alpine's added deps need a re-run.
  const needInstall = install && (pm !== 'vp' || alpine);
  if (needInstall) {
    console.log(`==> ${runner} install`);
    await run(nodeToolArgv([runner, 'install']), { cwd: dir });
  }
  const installed = install || pm === 'vp';

  console.log(`created ${dir}/ (${template} + tinyjs, ${pm === 'vp' ? 'vp + ' + under : pm})
  cd ${dir}${installed ? '' : '\n  ' + runner + ' install'}
  tinyjs dev      # vite dev server + native window, HMR included
  tinyjs build    # vite build + package .app`);
}

// Alpine on top of the vanilla template: one component that calls the
// backend, styled by the template's own stylesheet.
async function overlayAlpine(dir, name, ts) {
  const ext = ts ? 'ts' : 'js';
  await tjs.remove(dir + '/src/counter.' + ext).catch(() => {});
  await tjs.writeFile(dir + '/index.html', enc.encode(
`<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <link rel="icon" type="image/svg+xml" href="/favicon.svg" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>${name}</title>
  </head>
  <body>
    <section id="center" x-data="hello">
      <h1>tinyjs + Alpine</h1>
      <p>Edit <code>index.html</code> or <code>src/main.${ext}</code> and save to test <code>HMR</code></p>
      <input x-model="name" placeholder="your name" />
      <button type="button" class="counter" @click="ask">Ask the backend</button>
      <p x-show="reply" x-text="reply"></p>
    </section>
    <script type="module" src="/src/main.${ext}"></script>
  </body>
</html>
`));
  await tjs.writeFile(dir + '/src/main.' + ext, enc.encode(
`import './style.css'
import Alpine from 'alpinejs'

// x-data="hello" in index.html picks this component up.
Alpine.data('hello', () => ({
  name: 'world',
  reply: '',
  async ask() {
    // Every function in the backend's api object is callable from the page.
    this.reply = await tiny.api.call('hello', { name: this.name })
  },
}))

Alpine.start()
`));
}

// `tinyjs new <dir> --template [name] [--pm npm|pnpm|yarn|bun|vp[:pm]]
// [--no-install]`: every flag answers its question; on a terminal the rest
// are asked, off one they take defaults (npm, install) or fail (no template).
async function newFromTemplate(dir, name) {
  const flagVal = (flag) => {
    const eq = args.find((a) => a.startsWith(flag + '='));
    if (eq !== undefined) return eq.slice(flag.length + 1);
    const i = args.indexOf(flag);
    if (i === -1) return null;
    const v = args[i + 1];
    return v === undefined || v.startsWith('--') ? '' : v;
  };
  const tty = stdinIsTTY();
  const choices = VITE_FRAMEWORKS.map((f) => f + '-ts').concat(VITE_FRAMEWORKS).join('|');

  let template = flagVal('--template') ?? '';
  if (!template) {
    if (!tty) fail('--template needs a value here (no terminal to ask on): ' + choices);
    const fw = VITE_FRAMEWORKS[await chooseTty('Framework:', VITE_FRAMEWORKS)];
    const lang = await chooseTty('Language:', ['TypeScript', 'JavaScript']);
    template = fw + (lang === 0 ? '-ts' : '');
  }

  // Probe everything at once: a handful of `--version`s in parallel.
  const names = [...Object.keys(PACKAGE_MANAGERS), 'vp'];
  const versions = Object.fromEntries(
    (await Promise.all(names.map(pmVersion))).map((v, i) => [names[i], v]));
  const found = names.filter((n) => versions[n]);
  const foundPms = found.filter((n) => n !== 'vp');
  if (!foundPms.length) fail('no working package manager found (npm, pnpm, yarn, bun) — install Node.js first');
  const label = (n) => `${n} ${versions[n]}` + (n !== 'vp' ? ''
    : Number(versions.vp.split('.')[0]) < 1 ? '  (Vite+ — older than 1.0, needs `vp upgrade`)'
    : '  (Vite+, over a package manager)');

  let pm = flagVal('--pm') ?? flagVal('--package-manager') ?? '';
  let under = '';
  if (pm.includes(':')) [pm, under] = pm.split(':');
  if (pm) {
    if (!names.includes(pm)) fail(`unknown package manager '${pm}' (npm, pnpm, yarn, bun, vp)`);
    if (!versions[pm]) fail(`'${pm}' was asked for but \`${pm} --version\` didn't run here`);
  } else if (tty && found.length > 1) {
    pm = found[await chooseTty('Package manager (working on this machine):', found.map(label))];
  } else {
    pm = foundPms[0];
  }
  if (pm === 'vp') {
    // A pre-1.0 vp writes 0.1-era overrides (vitest → vite-plus-test@latest,
    // which stayed at 0.1.24 when 1.0 moved to real vitest). That drags in a
    // second, 0.1.24 vite-plus-core whose native loader requires
    // `vite-plus/binding` — gone in 1.0 — so every build dies with a
    // misleading "Cannot find native binding".
    if (Number(versions.vp.split('.')[0]) < 1) {
      fail(`vp ${versions.vp} is older than 1.0 and scaffolds projects that can't build ` +
           'with current Vite+ packages — run `vp upgrade` first');
    }
    if (under) {
      if (!foundPms.includes(under)) fail(`vp:${under} — '${under}' isn't a working package manager here`);
    } else if (tty && foundPms.length > 1) {
      under = foundPms[await chooseTty('Package manager for vp to use:', foundPms.map(label),
                                       Math.max(0, foundPms.indexOf('pnpm')))];
    } else {
      under = foundPms.includes('pnpm') ? 'pnpm' : foundPms[0];
    }
  }

  let install = !args.includes('--no-install');
  if (install && tty && pm !== 'vp' && !args.includes('--install')) {
    await writeOut('\nInstall dependencies now? [Y/n]: ');
    install = !/^n/i.test(await readLine());
  }

  await scaffoldViteTemplate(dir, name, template, { pm, under, install });
}

async function cmdNew() {
  const dir = args[0];
  if (!dir || dir.startsWith('--')) {
    fail('usage: tinyjs new <dir> [--template [react-ts|vue-ts|svelte-ts|solid-ts|preact-ts|lit-ts|alpine-ts|vanilla-ts|…]] [--pm npm|pnpm|yarn|bun|vp[:pnpm]] [--no-install]');
  }
  if (await exists(dir)) fail(`'${dir}' already exists`);
  const name = dir.replace(/\/+$/, '').split('/').pop();

  if (args.some((a) => a === '--template' || a.startsWith('--template='))) {
    await newFromTemplate(dir, name);
    return;
  }

  await copyTree(TOOL_DIR + 'template', dir);
  const cfgPath = dir + '/tinyjs.json';
  let cfg = dec.decode(await tjs.readFile(cfgPath)).replaceAll('__NAME__', name);
  // Record the tinyjs this app was made with, so running it on an older one
  // says so instead of half-working. A source checkout has no version to
  // stamp, so scaffolds from a checkout simply omit the key.
  const stamp = await toolVersion();
  if (parseVer(stamp)) {
    const o = JSON.parse(cfg);
    o.minTinyjsVersion = String(stamp).replace(/^v/, '');
    cfg = JSON.stringify(o, null, 2) + '\n';
  }
  await tjs.writeFile(cfgPath, enc.encode(cfg));

  await writeAgentSkill(dir);

  console.log(`created ${dir}/
  cd ${dir}
  tinyjs dev      # run it
  tinyjs build    # package it`);
}

// tinyjs wrap <url> — scaffold a site wrapper: the "url" config makes the
// site the main window, the per-origin API gate keeps the third-party origin
// away from the machine, and there is no frontend at all. Everything written
// is ordinary tinyjs config the user can edit afterwards.
// The generated wrapper backend: trace handlers always; an init block plus
// a tray toggle when --menubar is on (window or dropdown panel); always-on-
// top when --top is on; --external lists domains whose navigations open in
// the system browser (the onNavigate policy hook answers 'external').
function wrapperMain({ title, menubar, top, external, panel }) {
  const init = [];
  if (menubar) {
    init.push(
      '  // Menu-bar app: no Dock icon (activation in tinyjs.json), the tray\n' +
      '  // icon toggles the window, closing hides instead of quitting.\n' +
      '  app.setHideOnClose(true);\n' +
      "  // Tray icon: src/tray.png (a copy of the app icon), with its colors\n" +
      '  // (template:false — pngs would otherwise be monochrome templates).\n' +
      '  // Resolved next to this module, not the cwd: a packaged app runs\n' +
      "  // from / and ships src/ but not the project's icon.png. Falls back\n" +
      '  // to the globe symbol when there is none.\n' +
      "  let trayIcon = 'sf:globe';\n" +
      '  try {\n' +
      "    let p = decodeURIComponent(new URL('./tray.png', import.meta.url).pathname);\n" +
      '    if (/^\\/[A-Za-z]:\\//.test(p)) p = p.slice(1); // windows /C:/…\n' +
      '    await tjs.stat(p);\n' +
      '    trayIcon = p;\n' +
      '  } catch { }\n' +
      '  app.tray.set({ icon: trayIcon, template: false });');
  }
  if (top || panel) init.push('  app.setAlwaysOnTop(true);');
  if (panel) init.push('  app.setResizable(false); // a dropdown panel is fixed-size');
  let tray = '';
  if (menubar && panel) {
    tray = '\n// Tray click toggles the dropdown panel anchored under the icon; an' +
      '\n// outside click defocuses it and it hides itself.\n' +
      'let panelShown = false;\n' +
      'const PANEL_W = 360, PANEL_H = 520;\n' +
      'export async function onTray(id, app) {\n' +
      '  if (panelShown) { panelShown = false; app.hide(); return; }\n' +
      '  const t = await app.tray.position();\n' +
      '  const x = t ? Math.max(8, Math.round(t.x + t.width / 2 - PANEL_W / 2)) : 40;\n' +
      '  const y = t ? Math.round(t.y + t.height + 6) : 40;\n' +
      '  app.setPosition(x, y);\n' +
      '  app.setSize(PANEL_W, PANEL_H);\n' +
      '  panelShown = true;\n' +
      '  app.show(); // takes focus, so an outside click dismisses (onWindowState)\n' +
      '}\n' +
      'export function onWindowState(info, app) {\n' +
      '  if (panelShown && info.focused === false) { panelShown = false; app.hide(); }\n' +
      '}\n';
  } else if (menubar) {
    tray = '\n// Bare tray icon click: surface the window.\n' +
      'export function onTray(id, app) {\n  app.show();\n}\n';
  }
  return `// Wrapper backend — the wrapped site IS the app. It gets no api functions
// (tinyjs.json "api" gates the bridge by origin); these handlers just trace
// navigation and downloads to the terminal so \`tinyjs dev\` shows what the
// site does. Policy hooks: returning nothing allows, 'deny' blocks,
// 'external' hands the url to the system browser.
const EXTERNAL = ${JSON.stringify(external ?? [])}; // hostnames opened in the system browser
${init.length ? 'export async function init(app) {\n' + init.join('\n\n') + '\n}\n' : ''}${tray}
export function onNavigate(info) {
  if (info.kind === 'policy' && EXTERNAL.length) {
    try {
      const host = new URL(info.url).hostname;
      if (EXTERNAL.some((d) => host === d || host.endsWith('.' + d))) {
        console.log('[nav] external', info.url);
        return 'external';
      }
    } catch { }
  }
  console.log('[nav]', info.kind, info.url);
}
export function onDownload(info) {
  console.log('[dl]', info.state, info.filename ?? info.url);
}
export function onWindowOpen(info) {
  console.log('[popup]', info.mode ?? info.kind, info.url);
}
`;
}

const decodeEntities = (s) => s
  .replaceAll(/&#(\d+);/g, (_, n) => String.fromCodePoint(+n))
  .replaceAll(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
  .replaceAll('&lt;', '<').replaceAll('&gt;', '>')
  .replaceAll('&quot;', '"').replaceAll('&apos;', "'")
  .replaceAll('&amp;', '&'); // last, so "&amp;lt;" becomes "&lt;" not "<"

// Best icon a page advertises: apple-touch-icon (scored by sizes) > icon >
// nothing (caller falls back to the template icon). mask-icon is a monochrome
// glyph, deliberately scored down.
function pickIcon(html, base) {
  const cands = [];
  for (const tag of html.match(/<link\b[^>]*>/gi) ?? []) {
    const rel = (tag.match(/rel=["']([^"']+)["']/i)?.[1] ?? '').toLowerCase();
    const href = tag.match(/href=["']([^"']+)["']/i)?.[1];
    if (!href) continue;
    let score = rel.includes('apple-touch-icon') ? 100 : rel.includes('icon') ? 50 : 0;
    if (!score) continue;
    if (rel.includes('mask-icon')) score -= 40;
    score += Math.min(+(tag.match(/sizes=["'](\d+)x\d+/i)?.[1] ?? 0), 2048);
    try { cands.push({ score, url: new URL(decodeEntities(href), base).href }); } catch { }
  }
  cands.sort((a, b) => b.score - a.score);
  return cands[0]?.url ?? null;
}

// Modern favicons hide a real PNG inside an ICO wrapper (sips refuses the
// container): parse the ICO directory, take the largest entry, keep it only
// if it's a PNG — BMP entries fall back to the default icon.
function icoPng(b) {
  const count = b[4] | (b[5] << 8);
  let best = null;
  for (let i = 0; i < count; i++) {
    const e = 6 + 16 * i;
    const size = b[e + 8] | (b[e + 9] << 8) | (b[e + 10] << 16) | (b[e + 11] << 24);
    const off = b[e + 12] | (b[e + 13] << 8) | (b[e + 14] << 16) | (b[e + 15] << 24);
    const w = b[e] || 256, h = b[e + 1] || 256;
    if (!best || w * h > best.w * best.h) best = { w, h, size, off };
  }
  if (!best) return null;
  const img = b.subarray(best.off, best.off + best.size);
  return img[0] === 0x89 && img[1] === 0x50 ? img : null;
}

// First 8 hex chars of a SHA-256 digest (generation stamps).
const hex8 = (digest) => [...new Uint8Array(digest).slice(0, 4)]
  .map((b) => b.toString(16).padStart(2, '0')).join('');

// fetch with a hard timeout and a body cap: a slow or hostile site can
// neither hang wrap nor feed it an unbounded body. Resolves null on timeout,
// oversize, or any failure — callers treat that as "wrapped blind".
async function fetchCapped(url, ua, { timeout, cap }) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeout);
  try {
    const res = await fetch(url, { headers: { 'user-agent': ua }, signal: ctl.signal });
    if (!res.ok) return null;
    const reader = res.body.getReader();
    const chunks = [];
    let total = 0;
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      chunks.push(value);
      total += value.length;
      if (total > cap) { try { await reader.cancel(); } catch { } return null; }
    }
    const out = new Uint8Array(total);
    let off = 0;
    for (const c of chunks) { out.set(c, off); off += c.length; }
    return out;
  } catch { return null; }
  finally { clearTimeout(timer); }
}

// Icon bytes only if they really are an image (magic bytes, not content-type
// — CDNs lie and error pages lie harder). 5 s timeout, 5 MB cap.
async function fetchIcon(url, ua) {
  if (!url) return null;
  const b = await fetchCapped(url, ua, { timeout: 5000, cap: 5 * 1024 * 1024 });
  if (!b) return null;
  const is = (magic) => magic.every((byte, i) => b[i] === byte);
  if (is([0x89, 0x50]) || is([0xff, 0xd8]) ||
      (is([0x52, 0x49, 0x46, 0x46]) && b[8] === 0x57 && b[9] === 0x45)) return b; // png jpg webp
  if (is([0x00, 0x00, 0x01, 0x00])) return icoPng(b); // ICO container
  return null;
}

// ---- the public suffix list (PSL) ----
// Decides where a wildcard may stop: the registrable domain (etld+1), so a
// wrap of bbc.co.uk may offer *.bbc.co.uk but never *.co.uk, and a hosting-
// suffix tenant (sam.github.io) stays exact. Fetched at wrap time and cached
// ~30 days; with no list and no cache, wrap keeps the exact origin only.

const PSL_URL = 'https://publicsuffix.org/list/public_suffix_list.dat';

function parsePsl(dat) {
  const rules = new Set(), wild = new Set(), except = new Set(), priv = new Set();
  let privateSection = false;
  for (const raw of dat.split('\n')) {
    const line = raw.trim().toLowerCase();
    if (!line) continue;
    // The section markers live INSIDE comments in the real list — check for
    // them before the generic comment strip below.
    if (line.includes('===begin private domains')) { privateSection = true; continue; }
    if (line.includes('===end private domains')) { privateSection = false; continue; }
    if (line.startsWith('//')) continue;
    if (line.startsWith('!')) { const e = line.slice(1); except.add(e); if (privateSection) priv.add('!' + e); }
    else if (line.startsWith('*.')) { const e = line.slice(2); wild.add(e); if (privateSection) priv.add('*.' + e); }
    else { rules.add(line); if (privateSection) priv.add(line); }
  }
  return { rules, wild, except, priv };
}

// The prevailing rule for host: exceptions first (!x points one label down),
// then the LONGEST exact match, then a wildcard (*.x matches one label above
// x), the longer beating the shorter; no match at all → the TLD itself (the
// implicit * rule). priv = the match came from the PRIVATE section (hosting
// suffixes like github.io, where a tenant doesn't own the zone above it).
function publicSuffixOf(host, psl) {
  const labels = host.split('.');
  for (let i = 0; i < labels.length; i++) {
    const cand = labels.slice(i).join('.');
    if (psl.except.has(cand))
      return { ps: cand.split('.').slice(1).join('.'), priv: psl.priv.has('!' + cand) };
  }
  let best = null;
  for (let i = 0; i < labels.length; i++) {
    const cand = labels.slice(i).join('.');
    if (psl.rules.has(cand)) { best = { ps: cand, priv: psl.priv.has(cand) }; break; }
  }
  for (let i = 0; i + 1 < labels.length; i++) {
    if (psl.wild.has(labels.slice(i + 1).join('.'))) {
      const w = labels.slice(i).join('.');
      if (!best || w.split('.').length > best.ps.split('.').length)
        best = { ps: w, priv: psl.priv.has('*.' + labels.slice(i + 1).join('.')) };
      break;
    }
  }
  return best ?? { ps: labels[labels.length - 1], priv: false };
}

// etld+1, plus tenant=true when a wildcard would trust a zone we cannot
// attribute to the caller: the host IS a public suffix, or sits directly on
// a PRIVATE one (sam.github.io — the tenant doesn't own the zone above it;
// bbc.co.uk under the ICANN co.uk does, so *.bbc.co.uk is offered).
function registrableDomain(host, psl) {
  const { ps, priv } = publicSuffixOf(host, psl);
  if (!ps || host === ps) return { reg: null, tenant: true };
  const reg = host.slice(0, host.length - ps.length - 1).split('.').pop() + '.' + ps;
  return { reg, tenant: reg === host && priv };
}

async function loadPsl() {
  const home = tjs.env.HOME || tjs.env.USERPROFILE || '';
  const base = IS_WIN ? (tjs.env.LOCALAPPDATA || home + '\\AppData\\Local') + '\\tinyjs'
    : IS_LINUX ? (tjs.env.XDG_CACHE_HOME || home + '/.cache') + '/tinyjs'
    : home + '/Library/Caches/tinyjs';
  const path = base + '/public-suffix-list.dat';
  let dat = null;
  try {
    const st = await tjs.stat(path);
    if (Date.now() - st.mtim.getTime() < 30 * 86400 * 1000) dat = dec.decode(await tjs.readFile(path));
  } catch { }
  if (dat === null) {
    const b = await fetchCapped(PSL_URL, 'tinyjs-wrap', { timeout: 15000, cap: 2 * 1024 * 1024 });
    if (b) {
      dat = dec.decode(b);
      try { await tjs.makeDir(base, { recursive: true }); await tjs.writeFile(path, b); } catch { }
    } else {
      try { dat = dec.decode(await tjs.readFile(path)); console.log('==> public suffix list unreachable — using the cached copy'); }
      catch { console.log('==> public suffix list unreachable — wrapping without one (exact origins only)'); return null; }
    }
  }
  return parsePsl(dat);
}

async function cmdWrap() {
  if (!args[0] || args[0].startsWith('--')) {
    fail('usage: tinyjs wrap <url> [dir] [--name <name>] [--ua <userAgent>] [--[no-]menubar] [--[no-]panel] [--[no-]top] [--external a.com,b.com] [--origins exact|subdomains|url,…] [--yes] [--force]');
  }
  const url = /^https?:\/\//.test(args[0]) ? args[0] : 'https://' + args[0];
  let base;
  try { base = new URL(url); } catch { fail(`not a url: ${args[0]}`); }
  if (base.protocol !== 'https:' && base.protocol !== 'http:') fail(`not a website url: ${args[0]}`);
  const host = base.hostname;

  // --flag value and --flag=value both work (the old .replace(/^=/, '') ran
  // against the FOLLOWING arg, so --name=Foo silently did nothing).
  const argVal = (flag) => {
    const eq = args.find((a) => a.startsWith(flag + '='));
    if (eq !== undefined) return eq.slice(flag.length + 1) || null;
    const i = args.indexOf(flag);
    return i === -1 ? null : (args[i + 1] ?? '') || null;
  };
  const name = (argVal('--name') || host.replace(/^www\./, ''))
    .replace(/[^a-zA-Z0-9.-]/g, '').replace(/\./g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');
  const dir = args[1] && !args[1].startsWith('--') ? args[1] : name;
  const force = args.includes('--force');
  if (!name) fail(`cannot derive an app name from ${host} — pass --name`);
  const regen = force && await exists(dir + '/tinyjs.json');
  if (await exists(dir)) {
    // --force regenerates an existing wrapped project in place: it overwrites
    // ONLY the files this generator owns (tinyjs.json, src/main.js,
    // assets/icon.png, src/tray.png) and leaves the user's own edits, added
    // files and .git alone.
    if (!regen) fail(`'${dir}' already exists (--force overwrites a tinyjs project)`);
    console.log(`==> overwriting ${dir} (generated files only)`);
  }
  // A re-wrap keeps every choice its flags don't restate: the previous
  // tinyjs.json (origins, user agent, title, hand-added keys) and the window
  // modes read back from the previous generated backend. --no-menubar /
  // --no-panel / --no-top and --external '' turn a carried mode off.
  let prevCfg = {}, prevMain = '';
  if (regen) {
    try { prevCfg = JSON.parse(dec.decode(await tjs.readFile(dir + '/tinyjs.json'))); } catch { }
    try { prevMain = dec.decode(await tjs.readFile(dir + '/src/main.js')); } catch { }
  }
  const flagOr = (flag, prev) =>
    args.includes('--' + flag) ? true : args.includes('--no-' + flag) ? false : prev;
  const ua = argVal('--ua') ?? (typeof prevCfg.userAgent === 'string' ? prevCfg.userAgent : null);
  // The probe fetches as a plain Safari; a browser-ish UA keeps CDNs and
  // bot-walls from serving garbage. Only a --ua the user passes reaches the
  // app itself (Windows WebView2 serving Safari-flavored pages would be a
  // downgrade, not a fix).
  const probeUA = ua ||
    'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Safari/605.1.15';

  // Title + icon are best-effort: an unreachable site still wraps, with a
  // default title and the template icon. Timeout + body cap so a slow or
  // hostile site can't hang wrap or feed it an unbounded page.
  console.log(`==> fetching ${base.href}`);
  let title = null, iconUrl = null;
  const page = await fetchCapped(base.href, probeUA, { timeout: 10000, cap: 2 * 1024 * 1024 });
  if (page !== null) {
    const html = dec.decode(page);
    // The title becomes the .app name (dist/<title>.app, CFBundleName), so:
    // keep the site name before a tagline separator ("GitHub · Change is
    // constant…" → "GitHub", "BBC - Home" → "BBC"), and drop characters a
    // filename can't hold — the page is third-party, a "/" or ":" in it
    // would break the build path.
    const raw = decodeEntities((html.match(/<title[^>]*>([\s\S]*?)<\/title>/i)?.[1] ?? '').trim())
      .replace(/[\u0000-\u001f\u007f/\\:*?"<>|]/g, ' ').replace(/\s+/g, ' ').trim()
      .split(/\s+[·|•–—-]\s+/)[0].trim().replace(/^\.+/, '');
    title = raw.length > 44 ? raw.slice(0, 44).replace(/\s+\S*$/, '') + '…' : raw || null;
    iconUrl = pickIcon(html, base);
  }

  // ---- the API gate: exact origin by default, subdomains opt-in ----
  // The gate matches the page's ACTUAL origin — protocol and port included
  // (a wrapped http://127.0.0.1:8123 stamps "http://127.0.0.1:8123", which a
  // hardcoded "https://" key never matches). Wildcards are opt-in and never
  // cross a public suffix: the PSL decides where the registrable domain ends
  // (bbc.co.uk may widen to *.bbc.co.uk; sam.github.io, a tenant on a
  // hosting suffix, and every IP or http origin stay exact). No list and no
  // cache → exact only, the fail-closed side.
  const labels = host.replace(/^www\./, '').split('.').filter((s) => /^[a-zA-Z0-9-]+$/.test(s));
  const isIP = /^(?:\d{1,3}\.){3}\d{1,3}$/.test(host) || host.includes(':');
  const isTTY = stdinIsTTY();
  const yes = args.includes('--yes');
  const originsFlag = argVal('--origins');
  const psl = isIP || base.protocol !== 'https:' ? null : await loadPsl();
  const widen = psl ? registrableDomain(host.replace(/^www\./, ''), psl)
                    : { reg: null, tenant: true };
  const wildcardOrigin = widen.reg && !widen.tenant ? `https://*.${widen.reg}` : null;
  const regOrigin = widen.reg ? `https://${widen.reg}` : null;

  let origins = { [base.origin]: 'wrapper' };
  let gateNote = base.protocol !== 'https:' ? 'http origin — exact only'
    : isIP ? 'IP host — exact only'
    : widen.reg === null ? `${host} sits on a public suffix — exact only`
    : widen.tenant ? `${host} sits directly on a hosting suffix — exact only`
    : null;
  const addWildcard = () => {
    origins[wildcardOrigin] = 'wrapper';
    if (regOrigin && !(regOrigin in origins)) origins[regOrigin] = 'wrapper';
    gateNote = null;
  };
  if (originsFlag !== null) {
    if (originsFlag === 'subdomains') {
      if (wildcardOrigin) addWildcard();
      else console.log(`==> note: no wildcard available (${gateNote})`);
    } else if (originsFlag !== 'exact') {
      let ignored = 0;
      for (const entry of originsFlag.split(',').map((s) => s.trim()).filter(Boolean)) {
        try {
          const u = new URL(entry);
          if (u.protocol !== 'https:' && u.protocol !== 'http:') throw 0;
          origins[u.origin] = 'wrapper';
        } catch { ignored++; console.log(`==> note: ignoring unparseable --origins entry: ${entry}`); }
      }
      gateNote = ignored ? 'explicit --origins list (some entries ignored)' : 'explicit --origins list';
    }
  } else if (prevCfg.api?.origins && typeof prevCfg.api.origins === 'object' &&
             !Array.isArray(prevCfg.api.origins)) {
    origins = { ...prevCfg.api.origins };
    gateNote = 'kept from the previous tinyjs.json (pass --origins to change it)';
  } else if (!yes && isTTY && wildcardOrigin) {
    console.log('\nAPI access for this app:');
    console.log(`  [1] ${base.origin} only (recommended)`);
    console.log(`  [2] + subdomains:  ${wildcardOrigin}`);
    await writeOut('choose [1/2, enter = 1]: ');
    if ((await readLine()) === '2') addWildcard();
  } else if (!yes && !isTTY && originsFlag === null && wildcardOrigin) {
    console.log(`==> note: non-interactive — exact origin only (pass --origins subdomains for ${wildcardOrigin})`);
  }

  const cfg = {
    $schema: CONFIG_SCHEMA,
    name,
    title: title ?? name,
    size: '1280x800',
    id: isIP
      ? 'ip-' + host.replace(/[^a-zA-Z0-9]+/g, '-').replace(/^-+|-+$/g, '') + '.wrap'
      : labels.slice().reverse().join('.').toLowerCase() + '.wrap',
    version: '0.1.0',
    url: base.href,
    // Window chrome lives in the TOP-LEVEL lists, not in the runtime's
    // API_ALWAYS: an origin that matches no key (a redirect target like
    // consent.google.com) falls through to them, so a wrapped page keeps
    // close/minimize/zoom/drag on any origin it lands on while every other
    // capability stays closed. Per-app and visible in the manifest — the
    // gate policy no longer changes for every tinyjs app (#20 review).
    api: {
      disable: ['*'],
      enable: ['win.close', 'win.minimize', 'win.zoom', 'win.startDrag'],
      origins,
    },
    popups: 'window',   // OAuth popups keep window.opener/postMessage
    downloads: 'ask',
  };
  if (ua) cfg.userAgent = ua;
  // Previous window modes, read back from the generated backend's own
  // markers (each mode emits a distinctive line in wrapperMain).
  const prevPanel = prevMain.includes('PANEL_W');
  const menubar = flagOr('menubar', prevMain.includes('app.setHideOnClose(true)'));
  if (args.includes('--panel') && !menubar)
    console.log("==> note: --panel needs --menubar (a panel lives in the tray) — ignoring --panel");
  const panel = flagOr('panel', prevPanel) && menubar; // a panel lives in the tray
  const top = flagOr('top', !prevPanel && prevMain.includes('app.setAlwaysOnTop(true)'));
  let prevExternal = [];
  try { prevExternal = JSON.parse(prevMain.match(/^const EXTERNAL = (\[.*\]);/m)?.[1] ?? '[]'); } catch { }
  const externalArg = argVal('--external');
  const external = externalArg === null && !args.includes('--external') ? prevExternal
    : (externalArg ?? '').split(',').map((s) => s.trim().toLowerCase()).filter((s) => {
        if (s.includes('/')) console.log(`==> note: --external takes hostnames — ignoring ${s}`);
        return s && !s.includes('/');
      });
  // Keys the user owns once the project exists keep their previous values
  // (a changed id would also orphan the app's store and settings).
  const keep = ['title', 'size', 'id', 'version', ...(argVal('--name') ? [] : ['name'])];
  Object.assign(cfg, { ...prevCfg, ...cfg });
  for (const k of keep) if (prevCfg[k] !== undefined) cfg[k] = prevCfg[k];
  if (menubar) cfg.activation = 'accessory';
  else if (cfg.activation === 'accessory') delete cfg.activation;
  if (panel) cfg.chrome = { ...(cfg.chrome ?? {}), frame: false, windowControls: false };
  else if (prevPanel && cfg.chrome) {
    delete cfg.chrome.frame;
    delete cfg.chrome.windowControls;
    if (!Object.keys(cfg.chrome).length) delete cfg.chrome;
  }
  // (Studio form state used to persist here as a "studio" key — tinyjs.json
  // only carries what the runtime or CLI reads; Studio keeps its own file.)
  const stamp = await toolVersion();
  if (parseVer(stamp)) cfg.minTinyjsVersion = String(stamp).replace(/^v/, '');

  // The generated backend is stamped with the hash of its own body, so a
  // later --force can warn when src/main.js was edited since generation.
  const mainSrc = wrapperMain({ title: cfg.title, menubar, top, external, panel });
  const mainHash = hex8(await crypto.subtle.digest('SHA-256', enc.encode(mainSrc)));
  if (regen && await exists(dir + '/src/main.js')) {
    // The stamp records the hash of the generated body; re-hash the file's
    // own body and compare against it — an edit since generation (or an
    // unstamped older file) is kept as src/main.js.bak, never just lost.
    const m = prevMain.match(/^\/\/ generated by tinyjs wrap \(([0-9a-f]{8})\)\n([\s\S]*)$/);
    const intact = m && hex8(await crypto.subtle.digest('SHA-256', enc.encode(m[2]))) === m[1];
    if (!intact) {
      await tjs.writeFile(dir + '/src/main.js.bak', enc.encode(prevMain));
      console.log('==> note: src/main.js changed since it was generated — your version is in src/main.js.bak');
    }
  }
  await tjs.makeDir(dir + '/src', { recursive: true });
  await tjs.writeFile(dir + '/tinyjs.json', enc.encode(JSON.stringify(cfg, null, 2) + '\n'));
  await tjs.writeFile(dir + '/src/main.js',
    enc.encode(`// generated by tinyjs wrap (${mainHash})\n` + mainSrc));
  let iconSrc = iconUrl;
  let icon = await fetchIcon(iconSrc, probeUA);
  if (!icon) {
    iconSrc = new URL('/favicon.ico', base).href; // the /favicon.ico convention
    icon = await fetchIcon(iconSrc, probeUA);
  }
  // A --force re-wrap that finds no icon keeps the one already on disk —
  // overwriting a good icon with the template would be a downgrade.
  // New wraps keep it in assets/; one re-wrapped from before assets/ keeps
  // its root icon.png (its tinyjs.json may name it).
  const iconRel = regen && await exists(dir + '/icon.png') && !(await exists(dir + '/assets/icon.png'))
    ? 'icon.png' : 'assets/icon.png';
  const iconPath = dir + '/' + iconRel;
  const hadIcon = regen && await exists(iconPath);
  if (icon || !hadIcon) {
    await tjs.makeDir(dir + '/assets', { recursive: true });
    await tjs.writeFile(iconPath, icon ?? await tjs.readFile(TOOL_DIR + 'template/assets/icon.png'));
  }
  // Apple icon grid: fetched favicons are full-bleed, so the dock/menu-bar
  // icon renders oversized next to system apps. Pad to ~82% content on a
  // transparent 1024 canvas (JXA + Cocoa, macOS only — other platforms
  // keep the raw icon; their icon pipelines inset differently).
  // Best-effort: run()/runCapture() exit the CLI on failure (fail() calls
  // tjs.exit, so a try/catch around them never catches), and a release that
  // didn't ship pad-icon.jxa turned a cosmetic step into "tinyjs: command
  // failed (osascript)" with nothing wrapped after it (#91).
  if (!IS_WIN && !IS_LINUX && icon) {
    const ok = async (argv) => {
      try {
        const st = await tjs.spawn(argv, { stdout: 'ignore', stderr: 'ignore' }).wait();
        return st.exit_status === 0 && !st.term_signal;
      } catch { return false; }
    };
    // the Cocoa pass renders at the source's scale (can be 2048) — pin to 1024
    if (await ok(['osascript', '-l', 'JavaScript', TOOL_DIR + 'native/pad-icon.jxa', iconPath])
        && await ok(['sips', '-z', '1024', '1024', iconPath]))
      console.log('==> icon padded to the Apple grid');
    else
      console.log('==> note: couldn\'t pad the icon to the Apple grid — keeping it as fetched');
  }
  // The menu-bar backend loads src/tray.png: src/ ships inside the build,
  // the app icon does not.
  if (menubar) await tjs.writeFile(dir + '/src/tray.png', await tjs.readFile(iconPath));

  const extra = Object.keys(origins).filter((k) => k !== base.origin);
  console.log(`created ${dir}/
  title:  ${cfg.title}
  icon:   ${icon ? iconSrc : 'default — nothing usable advertised or at /favicon.ico'}
  gate:   ${base.origin}${extra.length ? ' + ' + extra.join(' + ') : ' (exact only)'} → wrapper preset
          ${gateNote ? gateNote + ' · ' : ''}every other origin → window chrome only
          (win.close/minimize/zoom/startDrag)

  cd ${dir}
  tinyjs dev      # run it
  tinyjs build    # package it`);
}

const LAUNCHER_EXE = TOOL_DIR + 'native/' +
  (IS_WIN ? 'launcher-win.exe' : IS_LINUX ? 'launcher-linux' : 'launcher-macos');

// Launcher sources (and the injected client, compiled into it) newer than the
// built launcher; every source when there is no launcher yet. Read-only, so
// `doctor` can report what ensureLauncherFresh would rebuild for.
async function launcherStaleSources() {
  const srcs = IS_WIN
    // win32_edge.hh carries tinyjs patches (env options, origin queue,
    // per-app profile) — an edit there must rebuild too.
    ? ['native/launcher-win.cc', 'native/combo.h', 'runtime/tiny.js',
       'native/include/webview/detail/backends/win32_edge.hh']
    : IS_LINUX
      ? ['native/launcher-linux.cc', 'native/combo.h', 'runtime/tiny.js']
      : ['native/launcher-macos.cc', 'native/combo.h', 'runtime/tiny.js'];
  const mtime = async (p) => {
    try { return (await tjs.stat(p)).mtim.getTime(); } catch { return null; }
  };
  const built = await mtime(LAUNCHER_EXE);
  const stale = [];
  for (const src of srcs) {
    const m = await mtime(TOOL_DIR + src);
    if (m !== null && (built === null || m > built)) stale.push(src);
  }
  return built === null && !stale.length ? srcs : stale;
}

// Dev-checkout convenience (Windows + Linux): if the native launcher sources
// (or the injected client, which is compiled into it) are newer than the
// built launcher, rebuild via setup.ps1 / setup.sh before starting — so
// hacking on tinyjs itself never runs a stale binary. Installed copies
// (VERSION file present) never rebuild.
async function ensureLauncherFresh() {
  // macOS was excluded here, and it was the only platform where editing
  // runtime/tiny.js left `tinyjs dev` silently running the PREVIOUS client —
  // the page then gets an API that predates the app's own code, which
  // surfaces as an unexplained TypeError rather than anything pointing at a
  // stale binary. Same rule everywhere now.
  if ((await toolVersion()) !== 'dev') return;
  const exe = LAUNCHER_EXE;
  const stale = (await launcherStaleSources()).length > 0;
  // `build --arch` / `--universal` may need a slice a checkout's host-only
  // launcher lacks, so rebuild it fat (setup.sh honours TINYJS_UNIVERSAL=1).
  // Once fat, a rebuild for changed sources stays fat — otherwise the next
  // cross-arch build would rebuild both slices all over again.
  const have = IS_WIN || IS_LINUX ? [] : await macArchsOf(exe);
  const wantArch = MAC_NEED.some((a) => !have.includes(a));
  const fat = wantArch || have.length > 1;
  if (!stale && !wantArch) return;
  console.log('==> ' + (stale ? 'launcher sources changed' : 'this build needs an arm64 + x86_64 launcher') +
              ' — rebuilding (' + (IS_WIN ? 'setup.ps1' : 'setup.sh') + ')');
  if (IS_WIN) {
    await run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
               '-File', TOOL_DIR + 'setup.ps1', '-SkipPath'], { cwd: TOOL_DIR });
  } else {
    await run(['sh', TOOL_DIR + 'setup.sh'],
              { cwd: TOOL_DIR, env: fat ? { ...tjs.env, TINYJS_UNIVERSAL: '1' } : tjs.env });
  }
}

async function cmdDev() {
  const cfg = await loadConfig();
  maybeNotifyUpdate(); // fire-and-forget; prints if a newer release exists
  await ensureLauncherFresh();

  // Frontend dev server (vite etc.): spawn it, wait until it responds, and
  // point the window at it. HMR replaces tinyjs' own frontend watcher; the
  // tiny.* bridge is injected into any origin, so it works over http too.
  let devServer = null;
  const fe = cfg.frontend ?? {};
  if (fe.devUrl) {
    if (fe.dev) {
      await ensurePackagesInstalled(fe.dev);
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

  // Backend changes need a fresh process, so we watch the backend's OWN
  // folder. The vanilla layout keeps backend and frontend side by side
  // (src/backend, src/frontend); the Vite templates put the backend in
  // backend/ with the frontend under src/ — watching src there restarted the
  // whole app on every frontend edit (#53) and never saw backend edits. A
  // pre-0.52 vanilla app (src/main.js) still nests its frontend inside the
  // watched folder, so that subtree is skipped. A backend at the project root
  // watches everything, including .build/ — which every restart rewrites, so
  // without skipping it the app would restart forever.
  const norm = (p) => String(p).replace(/\\/g, '/').replace(/^(\.\/)+/, '').replace(/\/+$/, '');
  const backendEntry = norm(cfg.backend ?? await resolveBackendEntry(cfg));
  const backendDir = backendEntry.includes('/') ? backendEntry.replace(/\/[^/]*$/, '') : '.';
  const under = (d) => backendDir === '.' ? d : d.startsWith(backendDir + '/') ? d.slice(backendDir.length + 1) : null;
  const skipDirs = [under(norm(fe.dir ?? 'src/frontend'))];
  if (backendDir === '.') skipDirs.push('.build', 'node_modules', '.git', 'dist', fe.dist && norm(fe.dist));
  const skips = skipDirs.filter(Boolean);
  let child = null;
  let restarting = false;
  let restartTimer = null;
  const restartOn = (rel) => {
    clearTimeout(restartTimer);
    restartTimer = setTimeout(() => {
      console.log(`tinyjs: ${rel} changed, restarting backend`);
      restarting = true;
      child?.kill();
    }, 150);
  };
  await watchTree(backendDir, restartOn,
    (rel) => skips.some((d) => rel === d || rel.startsWith(d + '/')), IS_LINUX);
  // shared/ lives beside the backend's folder, and the backend runs from a
  // copy of it, so an edit there restarts too (which reloads the page).
  const sharedDir = sharedDirFor(backendEntry);
  if (sharedDir && await exists(sharedDir)) {
    await watchTree(sharedDir, (rel) => restartOn(sharedDir + '/' + rel), undefined, IS_LINUX);
  }

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
    const devEnv = { ...tjs.env, TINYJS_LAUNCHER: tjs.env.TINYJS_LAUNCHER || (TOOL_DIR + 'native/' + (IS_WIN ? 'launcher-win.exe' : IS_LINUX ? 'launcher-linux' : 'launcher-macos')) };
    // Show the project's own icon while developing, on every platform: the
    // titlebar/taskbar on Windows and Linux, the Dock tile on macOS. Without
    // it a dev run wears the terminal's icon, which makes it indistinguishable
    // from every other dev run you have open.
    const iconSrc = cfg.icon || 'icon.png';
    if (await exists(iconSrc)) devEnv.TINYJS_ICON = tjs.cwd + '/' + iconSrc;
    // About-panel credit line (see the TinyjsAttribution plist key).
    if (cfg.attribution) devEnv.TINYJS_ATTRIBUTION = String(cfg.attribution);
    // Linux: the app id names the WM class (window ↔ .desktop matching).
    if (IS_LINUX) devEnv.TINYJS_APP_ID = cfg.id;
    // Dev always has devtools (F12), whatever the manifest says — the
    // launcher reads this env directly; the bridge only overrides it when
    // the manifest raises debug to 'open'. Seeded as 'dev', NOT '1': every
    // launcher treats any non-empty value as devtools-on, but the bridge's
    // message trace fires only on an explicit user-set value ('1'/'open') —
    // seeding '1' had every dev run spewing the full bridge log. Keep the
    // sentinel short: launcher-win reads this into char[8].
    if (!devEnv.TINYJS_DEBUG) devEnv.TINYJS_DEBUG = 'dev';
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
  await shipExtras(cfg, STAGE, 'dmg');
  const dmg ='dist/' + cfg.name + '-' + (cfg.version || '0.0.0') + MAC_SUFFIX + '.dmg';
  await run(['hdiutil', 'create', '-volname', cfg.title, '-srcfolder', STAGE,
             '-ov', '-quiet', '-format', 'UDZO', dmg]);
  console.log('    ' + dmg);
  return dmg;
}

// `tinyjs build --cli [name]` writes a shim so the app is runnable from a
// terminal. It's a build artifact because only the build knows where the
// executable lands — and because argv now reaches onOpenFiles on every
// platform, the shim is genuinely just exec: no `open -a`, no --open mode, no
// per-platform branch.
//
// It goes in dist/bin/, NOT dist/ — the bare executable is already dist/<name>
// and a shim of the same name would overwrite it (which is exactly what the
// first version of this did). dist/bin/<name> also means the symlink onto PATH
// carries the right command name.
//
// It targets the bare dist/<name> backend, NOT the .app. Inside the bundle
// the main executable is the LAUNCHER, which parses argv as <html> <socket>
// and dies on a file path ("cannot connect to /tmp/x.md" — found the hard
// way). The backend is the binary that reads argv and hands paths to
// onOpenFiles.
//
// Caveat worth knowing on macOS: the bare binary has no single-instance pipe
// (that's Windows/Linux only — macOS normally gets it from LaunchServices),
// so running the shim while the app is already open starts a SECOND copy.
// Cold start is the case this covers well.
async function maybeWriteCliShim(cfg, appBundle) {
  const i = args.indexOf('--cli');
  if (i < 0) return;
  const next = args[i + 1];
  const name = next && !next.startsWith('--') ? next : cfg.name;
  const isWin = tjs.env.OS === 'Windows_NT';
  await tjs.makeDir('dist/bin', { recursive: true });
  const shimPath = 'dist/bin/' + name + (isWin ? '.cmd' : '');
  // ../ from dist/bin back to dist/
  const target = isWin ? `%~dp0..\\${cfg.name}.exe` : `$DIR/../${cfg.name}`;
  const body = isWin
    ? `@echo off\r\n"${target}" %*\r\n`
    // exec, so signals and the exit code belong to the app rather than to a
    // shell sitting in front of it. "$@" quoted keeps paths with spaces whole.
    : `#!/bin/sh\n`
      + `# ${cfg.title} — run from a terminal. Files named here reach the app\n`
      + `# through onOpenFiles, whether it was already running or not.\n`
      + `DIR="$(cd "$(dirname "$0")" && pwd)"\n`
      + `exec "${target}" "$@"\n`;
  await tjs.writeFile(shimPath, enc.encode(body));
  if (!isWin) await tryRun(['chmod', '+x', shimPath]);
  console.log(`==> cli shim: ${shimPath}`);
  // Windows has no /usr/local/bin and no ln -sf; printing the Unix line there
  // told users to run a command that cannot work.
  if (isWin)
    console.log(`    put it on PATH:  setx PATH "%PATH%;${tjs.cwd}\\dist\\bin"`);
  else
    console.log(`    link it:  ln -sf "$(pwd)/${shimPath}" /usr/local/bin/${name}`);
}

// XML-escape a config string for Info.plist interpolation. These values land
// in the bundle's plist verbatim, and an unescaped & or < makes the plist
// invalid XML: plutil rejects it, LaunchServices can't resolve the bundle
// ("executable is missing" from open/Finder), and notarization tooling that
// parses the plist strictly refuses it. C0 control chars (bar tab/LF/CR) are
// illegal in XML 1.0 even as &#x..; references, so they're dropped.
const escXml = (s) => String(s ?? '')
  .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F]/g, '')
  .replace(/&/g, '&amp;').replace(/</g, '&lt;')
  .replace(/>/g, '&gt;').replace(/"/g, '&quot;');

async function cmdBuild() {
  const cfg = await loadConfig();
  // Same staleness guard `dev` has, and it matters more here: a build SHIPS
  // the launcher (dist/launcher.exe) and, on Windows, shells out to it to
  // stamp the app icon — so a stale binary doesn't just run old code, it goes
  // out in the release and can silently predate the --embed-icon flag it is
  // being asked for. No-op unless this is a dev checkout.
  await ensureLauncherFresh();
  await warnUnusedAssets(cfg);
  // A bad extras/ fails before the slow part, not after it.
  const extras = await validateExtras(cfg);
  await generateBuild(cfg);
  const cwd = tjs.cwd;

  console.log('==> compiling backend');
  // Per-arch builds share dist/: keep this version's per-arch dmgs across the
  // wipe, so `build --arch arm64 --dmg` then `--arch x86_64 --dmg` (each
  // notarized in between) ends with both installers side by side — and the
  // `publish --arch` runs after them (which rebuild) don't eat the notarized
  // ones. A new --dmg for the same arch simply overwrites its own.
  const keptDmgs = [];
  if (MAC_SUFFIX) {
    for (const a of MAC_ARCHS) {
      const f = `${cfg.name}-${cfg.version || '0.0.0'}-macos-${a}.dmg`;
      if (await exists('dist/' + f)) {
        await tjs.rename('dist/' + f, '.build/' + f);
        keptDmgs.push(f);
      }
    }
  }
  await rmTree('dist');
  // rmTree swallows errors, and Windows won't delete a running exe (or a
  // file another program has open), so the app still running left dist/ in
  // place and makeDir died with a bare "EEXIST: file already exists" (#72).
  if (await exists('dist')) {
    for (const f of keptDmgs) await tjs.rename('.build/' + f, 'dist/' + f).catch(() => {});
    const stuck = await firstFileIn('dist');
    fail(`couldn't clear dist/ — ${stuck || 'dist'} is in use.\n` +
         `    Is ${cfg.title || cfg.name} still running? Quit it and run the build again.`);
  }
  await tjs.makeDir('dist');
  for (const f of keptDmgs) await tjs.rename('.build/' + f, 'dist/' + f);
  // `tjs app compile` runs from the parent of the app/ dir and bundles the
  // whole module graph into a standalone executable.
  let compiler = tjs.exePath;
  if (IS_LINUX) {
    // The icon rides inside the compiled binary's TPK bundle (next to the
    // frontend); the launcher reads it for the window icon and notifications.
    const linIcon = cfg.icon || 'icon.png';
    if (await exists(linIcon)) await copyFile(linIcon, '.build/app/icon.png');
  }
  if (IS_WIN) {
    const winIcon = cfg.icon || 'icon.png';
    if (await exists(winIcon)) {
      console.log('==> embedding icon');
      // The icon rides inside the exe, twice: at the TPK app root (the bridge
      // hands it to the launcher for the window/taskbar) and as a PE resource
      // (Explorer, shortcuts, DefaultIcon registry entries). `app compile`
      // templates the RUNNING exe and the appended bundle rules out resource-
      // editing the output — so stamp a clean copy of the runtime first and
      // compile with that.
      await copyFile(winIcon, '.build/app/icon.png');
      compiler = cwd + '/.build/tjs-icon.exe';
      await copyFile(tjs.exePath, compiler);
      // Don't let this fail quietly: the exe still builds and runs without a
      // PE icon, it just shows Explorer's generic one, so a swallowed failure
      // reads as "Windows lost my icon" days later. A launcher-win.exe older
      // than the --embed-icon flag exits non-zero here, as does an unreadable
      // png or a resource update blocked by AV / an open handle.
      // stderr inherited: embed_icon says nothing on success and names the
      // actual reason on failure, which beats guessing from a generic warning.
      if (!(await tryRun([TOOL_DIR + 'native/launcher-win.exe', '--embed-icon',
                          compiler, cwd + '/' + winIcon], { stderr: 'inherit' }))) {
        console.log('    WARNING: could not embed ' + winIcon + ' into ' + cfg.name +
                    '.exe — it will show the default Windows icon.');
        console.log('    (rebuild the launcher with setup.ps1 if it predates --embed-icon)');
      }
    }
  }
  await run([compiler, 'app', 'compile', cwd + '/dist/' + cfg.name], { cwd: cwd + '/.build' });
  if (compiler !== tjs.exePath) await tjs.remove(compiler).catch(() => {});
  if (cfg.resources.length) await copyTree('.build/resources', 'dist/resources');
  await copyExecutables(cfg.executablesStaged, 'dist/executables');

  if (IS_WIN) {
    // Windows build: a portable dist/ folder — just <name>.exe (compiled
    // backend; the frontend and icon ride inside its TPK bundle and extract
    // to tmp at launch) and launcher.exe next to it (the bridge finds it
    // there). No bundle/codesign step; zip the folder to distribute.
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
    // Stamp launcher.exe (a clean PE) too, so its process/window class gets
    // the app icon.
    const winIcon = cfg.icon || 'icon.png';
    if (await exists(winIcon)) {
      if (!(await tryRun([TOOL_DIR + 'native/launcher-win.exe', '--embed-icon',
                          tjs.cwd + '/dist/launcher.exe', tjs.cwd + '/' + winIcon],
                         { stderr: 'inherit' }))) {
        console.log('    WARNING: could not embed ' + winIcon + ' into launcher.exe.');
      }
    }
    await shipExtras(cfg, 'dist', 'dist/');
    await maybeWriteCliShim(cfg, null);
    console.log('==> done');
    console.log(`run it:  .\\dist\\${cfg.name}.exe`);
    return;
  }

  if (IS_LINUX) {
    // Linux build: a portable dist/ folder — <name> (compiled backend; the
    // frontend and icon ride inside its TPK bundle) with launcher + icon.png
    // next to it. The bridge registers a .desktop entry (app menu, deep
    // links, file associations) on the app's first run — no install step.
    await copyFile(TOOL_DIR + 'native/launcher-linux', 'dist/launcher');
    await run(['chmod', '+x', 'dist/launcher', 'dist/' + cfg.name]);
    const linIcon = cfg.icon || 'icon.png';
    if (await exists(linIcon)) await copyFile(linIcon, 'dist/icon.png');
    await shipExtras(cfg, 'dist', 'dist/');
    await maybeWriteCliShim(cfg, null);
    console.log('==> done');
    console.log(`run it:  ./dist/${cfg.name}`);
    return;
  }

  await run(['cp', TOOL_DIR + 'native/launcher-macos', 'dist/launcher']);
  // The bare binary resolves its frontend next to the executable ("url"
  // wrappers may not have one).
  if (await exists('.build/app/frontend'))
    await run(['cp', '-R', '.build/app/frontend', 'dist/frontend']);
  if (await exists('.build/app/shared'))
    await run(['cp', '-R', '.build/app/shared', 'dist/shared']);

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
  // --arch / --universal pick the slices both executables carry (see MAC_TARGET).
  await run(['cp', await macLauncher(), APP + '/Contents/MacOS/' + cfg.name]);
  await run(['cp', await macTjs(), APP + '/Contents/MacOS/tjs']);
  await run(['cp', '-R', '.build/app', APP + '/Contents/Resources/app']);
  if (cfg.resources.length) await copyTree('.build/resources', APP + '/Contents/Resources/app/resources');
  // Sidecars: Contents/Helpers is where a bundle keeps helper tools, so
  // codesign signs them as code (in Resources they'd be sealed as data and
  // fail notarization).
  await copyExecutables(cfg.executablesStaged, APP + '/Contents/Helpers');
  // macOS convention: Resources/Credits.{html,rtf,rtfd} feeds the standard
  // About panel — the launcher defers to it when present instead of the
  // tinyjs credit (#8).
  const credits = await findCredits();
  if (credits) {
    await run(['cp', '-R', credits, APP + '/Contents/Resources/' + credits.replace(/^.*\//, '')]);
  }
  // A help book (#47) goes at the Resources root, where Help Viewer looks
  // for it; Info.plist names it below and the launcher adds the Help menu.
  const helpBook = await stageHelpBook(cfg, APP + '/Contents/Resources');
  // App icon: assets/icon.png (or "icon", or a root icon.png; 1024×1024, the
  // template ships a default) becomes AppIcon.icns via sips + iconutil.
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
  <key>TinyjsWindowSize</key>    <string>${escXml(cfg.size)}</string>`;
  if (cfg.readAccess) {
    // Widen the page's file:// read root (see createApp readAccess). true =
    // the user's home dir; a string = that path.
    const ra = cfg.readAccess === true ? '~' : String(cfg.readAccess);
    extraKeys += `
  <key>TinyjsReadAccess</key>    <string>${escXml(ra)}</string>`;
  }
  if (cfg.userAgent) {
    // Custom User-Agent for the webview (see createApp userAgent). Lets a
    // devUrl-wrapped site see a real browser UA instead of WKWebView's default.
    const ua = escXml(cfg.userAgent);
    extraKeys += `
  <key>TinyjsUserAgent</key>     <string>${ua}</string>`;
  }
  if (cfg.attribution) {
    // About-panel credit line (#8): a bundled Credits.html wins over it, and
    // the tinyjs default stays when neither is set. Dev carries it in env
    // (cmdDev); LaunchServices starts the packaged launcher, so the plist
    // carries it there.
    extraKeys += `
  <key>TinyjsAttribution</key>   <string>${escXml(cfg.attribution)}</string>`;
  }
  if (cfg.url) {
    // "url": the main window starts at this remote page (site wrappers) —
    // the launcher navigates there instead of Resources/app/frontend.
    const u = escXml(cfg.url);
    extraKeys += `
  <key>TinyjsUrl</key>           <string>${u}</string>`;
  }
  if (cfg.downloads) {
    // "downloads": auto | ask | deny (see the launcher's download delegate).
    extraKeys += `
  <key>TinyjsDownloads</key>     <string>${escXml(cfg.downloads)}</string>`;
  }
  {
    // Origins that get mic/camera without WebKit's per-site prompt (#24):
    // LaunchServices starts the launcher, not the bridge, so the env the
    // bridge sets in dev can't reach it — the plist carries the same lines.
    const { mediaTrustLines } = await import(TOOL_DIR + 'runtime/bridge.js');
    extraKeys += `
  <key>TinyjsMediaOrigins</key>  <string>${escXml(mediaTrustLines(cfg.api ?? null, ['file://']))}</string>`;
  }
  if (cfg.popups) {
    // "popups": external | window | deny (window.open / target=_blank).
    extraKeys += `
  <key>TinyjsPopups</key>        <string>${escXml(cfg.popups)}</string>`;
  }
  if (cfg.debug) {
    // Devtools in a packaged .app (see createApp debug): LaunchServices
    // starts the launcher, not the bridge, so the env can't carry it — the
    // plist does, like TinyjsUserAgent above.
    extraKeys += `
  <key>TinyjsDebug</key>         <string>${cfg.debug === 'open' ? 'open' : '1'}</string>`;
  }
  if (cfg.mainWindow === false) {
    // No main window/WebView at all (see createApp mainWindow); the plist is
    // the only channel a LaunchServices-started launcher has.
    extraKeys += `
  <key>TinyjsNoMainWindow</key>      <true/>`;
  }
  if (cfg.rememberWindows && cfg.mainWindow !== false && cfg.activation !== 'accessory') {
    // Build the main window hidden so the bridge can restore where it was
    // before anyone sees it (the env knob's plist twin — see createApp).
    extraKeys += `
  <key>TinyjsDeferShow</key>     <true/>`;
  }
  if (cfg.activation === 'accessory') {
    // Menu-bar agent: LSUIElement starts the process with no Dock icon at the
    // system level; TinyjsActivation makes the launcher keep the window hidden
    // until the app shows it.
    extraKeys += `
  <key>LSUIElement</key>         <true/>
  <key>TinyjsActivation</key>    <string>accessory</string>`;
  }
  if (cfg.chrome?.panel) {
    // chrome.panel (#89): the launcher must create the main window as a
    // non-activating panel, so it reads this before the window exists.
    extraKeys += `
  <key>TinyjsPanel</key>         <true/>`;
  }
  if (cfg.chrome) {
    const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
    const ch = cfg.chrome;
    // null and false both mean none, as chromeWire treats them on the socket
    const vib = ch.vibrancy === undefined ? '' : (ch.vibrancy === null || ch.vibrancy === false ? 'none' : ch.vibrancy);
    const wcp = ch.windowControlsPos;
    // windowControls: same tokens the bridge's controlsWire sends over the
    // socket — '' keep, 'all', 'none', or a comma list. This field read
    // `trafficLights` (the pre-0.30 name) long after the rename, so every
    // PACKAGED app with `windowControls: false` still showed the buttons
    // while dev and the bare binary — whose chrome rides the socket — hid
    // them. amp shipped that way twice before anyone caught it.
    const names = { close: 'close', minimize: 'minimize', maximize: 'maximize', zoom: 'maximize' };
    const wc = ch.windowControls;
    const controls = wc === undefined ? '' : wc === true ? 'all' : wc === false ? 'none'
      : Array.isArray(wc)
        ? ([...new Set(wc.map((n) => names[String(n).toLowerCase()]).filter(Boolean))].join(',') || 'none')
        : '';
    extraKeys += `
  <key>TinyjsChrome</key>        <string>${[bit(ch.frame), controls, bit(ch.transparent), escXml(vib), bit(ch.squareCorners), bit(ch.acceptsFirstMouse), wcp ? `${wcp.x | 0},${wcp.y | 0}` : ''].join('&#9;')}</string>`;
  }
  const schemes = cfg.urlScheme ? [].concat(cfg.urlScheme) : [];
  if (schemes.length) {
    extraKeys += `
  <key>CFBundleURLTypes</key>
  <array><dict>
    <key>CFBundleURLName</key>    <string>${escXml(cfg.id)}</string>
    <key>CFBundleURLSchemes</key>
    <array>${schemes.map((s) => `<string>${escXml(s)}</string>`).join('')}</array>
  </dict></array>`;
  }
  // Mic/camera ("permissions": { "microphone": "why", "camera": "why" }):
  // TCC kills a bundled app that requests capture without the usage string,
  // and the hardened runtime additionally denies the device without its
  // entitlement — the strings land here, the entitlements at codesign below.
  const perms = cfg.permissions ?? {};
  if (perms.microphone) extraKeys += `
  <key>NSMicrophoneUsageDescription</key> <string>${escXml(perms.microphone)}</string>`;
  if (perms.camera) extraKeys += `
  <key>NSCameraUsageDescription</key>     <string>${escXml(perms.camera)}</string>`;
  // Speech-to-text ("permissions": { "speechRecognition": "why" }). The page's
  // webkitSpeechRecognition needs BOTH this and the microphone string: WebKit
  // asks SFSpeechRecognizer for authorization, and without the key the OS
  // refuses the service rather than the mic — the page sees
  // `error: service-not-allowed` with no prompt and nothing to click.
  // Measured 2026-07-27: adding this key alone turned that error into
  // `start` + `audiostart` on an otherwise identical build.
  if (perms.speechRecognition) extraKeys += `
  <key>NSSpeechRecognitionUsageDescription</key> <string>${escXml(perms.speechRecognition)}</string>`;
  // Location ("permissions": { "location": "why" }) — the Wi-Fi SSID/BSSID
  // need it on macOS 14+ (#90). CoreLocation silently ignores a request from
  // an app without the string, and an app that never asked can't be added in
  // System Settings either. macOS reads NSLocationUsageDescription; the
  // WhenInUse key is what requestWhenInUseAuthorization documents.
  if (perms.location) extraKeys += `
  <key>NSLocationUsageDescription</key> <string>${escXml(perms.location)}</string>
  <key>NSLocationWhenInUseUsageDescription</key> <string>${escXml(perms.location)}</string>`;
  // tiny.audioTap ("audioTap": "app" | "system"): Core Audio process taps read
  // rendered output. The usage string is required for the capture TCC; a
  // custom reason via "audioTapReason" overrides the default.
  if (cfg.audioTap) {
    const why = cfg.audioTapReason ||
      `${cfg.title} reads audio output for metering and visualization.`;
    extraKeys += `
  <key>NSAudioCaptureUsageDescription</key> <string>${escXml(why)}</string>`;
  }
  // Document types: file extensions, and optionally folders. "openFolders":
  // true is its own dict rather than another extension — a folder has no
  // extension to match, so LaunchServices wants the public.folder UTI.
  const exts = cfg.fileExtensions ?? [];
  if (exts.length || cfg.openFolders) {
    const dicts = [];
    if (exts.length) dicts.push(`<dict>
    <key>CFBundleTypeName</key>   <string>${escXml(cfg.title)} Document</string>
    <key>CFBundleTypeRole</key>   <string>Editor</string>
    <key>LSHandlerRank</key>      <string>Default</string>
    <key>CFBundleTypeExtensions</key>
    <array>${exts.map((e) => `<string>${escXml(e)}</string>`).join('')}</array>
  </dict>`);
    if (cfg.openFolders) dicts.push(`<dict>
    <key>CFBundleTypeName</key>   <string>Folder</string>
    <key>CFBundleTypeRole</key>   <string>Viewer</string>
    <key>LSHandlerRank</key>      <string>Alternate</string>
    <key>LSItemContentTypes</key>
    <array><string>public.folder</string></array>
  </dict>`);
    extraKeys += `
  <key>CFBundleDocumentTypes</key>
  <array>${dicts.join('')}</array>`;
  }
  if (helpBook) {
    // Help Viewer finds the book by folder (relative to Resources) and
    // identifies it by the book's own bundle id.
    extraKeys += `
  <key>CFBundleHelpBookFolder</key> <string>${escXml(helpBook.folder)}</string>
  <key>CFBundleHelpBookName</key>   <string>${escXml(helpBook.name)}</string>`;
  }

  const plist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>${escXml(cfg.title)}</string>
  <key>CFBundleDisplayName</key>     <string>${escXml(cfg.title)}</string>
  <key>CFBundleIdentifier</key>      <string>${escXml(cfg.id)}</string>
  <key>CFBundleVersion</key>         <string>${escXml(cfg.version || '0.1.0')}</string>
  <key>CFBundleShortVersionString</key> <string>${escXml(cfg.version || '0.1.0')}</string>
  <key>CFBundleExecutable</key>      <string>${escXml(cfg.name)}</string>
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
  // loadConfig() has already folded TINYJS_SIGN_IDENTITY in (and announced
  // it if it displaced a config value), so this is just the resolved value.
  const identity = cfg.signIdentity || '-';
  console.log('==> codesigning' + (identity === '-' ? ' (ad-hoc)' : ' as ' + identity));
  // A real identity gets the hardened runtime + a secure timestamp — both
  // required by notarization. QuickJS interprets (no JIT/executable memory)
  // and WKWebView content runs out-of-process, so the only entitlements ever
  // needed are the capture devices and location: the hardened runtime denies
  // mic/camera/location outright — even with TCC granted — unless the binary
  // carries them.
  const sigFlags = identity === '-' ? [] : ['--options', 'runtime', '--timestamp'];
  const devices = [...new Set(
    [(perms.microphone || cfg.audioTap || perms.speechRecognition) && 'com.apple.security.device.audio-input',
     perms.camera && 'com.apple.security.device.camera',
     perms.location && 'com.apple.security.personal-information.location'].filter(Boolean))];
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
  // Sidecars sign first (innermost), each as its own program: the hardened
  // runtime and timestamp like the rest (notarization wants them on every
  // executable), an identifier under the app's, and NOT the app's device
  // entitlements. Their own come from "executableEntitlements", else from
  // the signature they arrived with — a JIT runtime (Node/pkg, Bun, Deno)
  // dies under the hardened runtime without allow-jit.
  const helperEnts = executableEntitlements(cfg);
  for (const { name: f } of cfg.executablesStaged?.files ?? []) {
    const flags = [...(identity === '-' ? [] : ['--options', 'runtime', '--timestamp']),
                   '--identifier', cfg.id + '.' + f.replace(/[^A-Za-z0-9.-]/g, '-')];
    if (helperEnts[f]) {
      const ent = '.build/entitlements-' + f + '.plist';
      await tjs.writeFile(ent, enc.encode(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>${helperEnts[f].map((k) => `
  <key>${escXml(k)}</key> <true/>`).join('')}
</dict>
</plist>
`));
      flags.push('--entitlements', ent);
    } else {
      flags.push('--preserve-metadata=entitlements');
    }
    for (const bin of ['dist/executables/' + f, APP + '/Contents/Helpers/' + f]) {
      // Unlike the ad-hoc runs below, a sidecar is the user's own binary and
      // can fail to sign for its own reasons — always show why.
      if (!(await tryRun(['codesign', '--force', ...flags, '--sign', identity, bin], { stderr: 'inherit' })))
        fail(`could not codesign ${bin} — see the codesign error above`);
    }
  }
  // Inside-out: tjs before the bundle executable, since codesign refuses a
  // main executable whose nested code is unsigned — and a lipo'd or freshly
  // downloaded tjs arrives with no usable signature.
  for (const bin of ['dist/launcher',
                     APP + '/Contents/MacOS/tjs',
                     APP + '/Contents/MacOS/' + cfg.name,
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
  else if (extras.length) console.log(`==> note: extras (${extras.map((f) => f.name).join(', ')}) ship in the dmg ` +
    'only — the .app and the update zip never carry them; build or notarize with --dmg');
  await maybeWriteCliShim(cfg, APP);
  await reportMacArchs(cfg, APP);

  console.log('==> done');
  await run(['ls', '-lh', 'dist/' + cfg.name, 'dist/launcher']);
  console.log(`run it:  ./dist/${cfg.name}   (or open "${APP}")`);
}

// ---- macOS target architecture ---------------------------------------------
// An .app opens on a Mac only if BOTH bundle executables (the launcher and
// tjs) carry that Mac's CPU slice. The host tjs is single-arch, so by default
// a build targets this Mac only. Two opt-ins, from any Mac:
//   --arch arm64|x86_64  (or TINYJS_ARCH) — a separate app per CPU. The
//                        other arch's tjs comes from the SAME txiki release,
//                        cached in ~/Library/Caches/tinyjs. dmg/zip names
//                        gain -macos-<arch> so both fit in one release, and
//                        publish merges them into one manifest.
//   --universal          (or TINYJS_UNIVERSAL=1) — one app with both slices,
//                        lipo'd together (lipo needs the Command Line Tools).
// The bare dist/<name> binary stays host-only (a local convenience, not the
// thing that ships). macOS only: Windows and Linux build for the CPU of the
// tinyjs running them (an x64 tinyjs on Windows on ARM builds x64 apps).
const MAC_ARCHS = ['arm64', 'x86_64'];
const MAC_LABEL = { arm64: 'Apple Silicon', x86_64: 'Intel' };
const MAC_TARGET = (() => {
  if (!['build', 'publish', 'notarize'].includes(cmd)) return null;
  const ai = args.indexOf('--arch');
  if (IS_WIN || IS_LINUX) {
    if (ai >= 0 || args.includes('--universal'))
      console.log('tinyjs: --arch / --universal only apply to macOS builds — ignoring them here');
    return null;
  }
  const raw = String((ai >= 0 ? args[ai + 1] : tjs.env.TINYJS_ARCH) ?? '').toLowerCase();
  const universal = args.includes('--universal') || tjs.env.TINYJS_UNIVERSAL === '1';
  if (!raw) return universal ? 'universal' : null;
  const arch = { arm64: 'arm64', aarch64: 'arm64', x86_64: 'x86_64', x64: 'x86_64',
                 amd64: 'x86_64', intel: 'x86_64', universal: 'universal' }[raw];
  if (!arch) fail(`--arch ${raw}: expected arm64, x86_64 or universal`);
  if (universal && arch !== 'universal') fail('--arch and --universal are exclusive — pick one');
  return arch;
})();
const UNIVERSAL = MAC_TARGET === 'universal';
// Per-arch artifacts are named <name>-<ver>-macos-<arch>.{dmg,zip}; default
// and universal builds keep the historical <name>-<ver>.{dmg,zip}.
const MAC_SUFFIX = MAC_ARCHS.includes(MAC_TARGET) ? '-macos-' + MAC_TARGET : '';
// The slices a macOS build needs from the launcher and tjs ([] = host default).
const MAC_NEED = UNIVERSAL ? MAC_ARCHS : MAC_SUFFIX ? [MAC_TARGET] : [];

// Architectures of a Mach-O, via `file` (ships with macOS; lipo needs the
// CLT, and only the paths that actually merge or thin binaries require it).
async function macArchsOf(p) {
  if (!(await exists(p))) return [];
  return [...new Set((await capture(['file', '-b', p])).match(/\b(?:arm64|x86_64)\b/g) ?? [])];
}

// /usr/bin/lipo is a shim that pops the "install the Command Line Tools"
// dialog when they're missing, so ask xcode-select (silent) before using it.
let haveLipoP = null;
const haveLipo = () => (haveLipoP ??= tryRun(['xcode-select', '-p']));
async function needLipo(why) {
  if (!(await haveLipo())) fail(why + ' needs lipo — install the Command Line Tools (xcode-select --install)');
}

async function sha256Of(p) {
  const d = new Uint8Array(await crypto.subtle.digest('SHA-256', await tjs.readFile(p)));
  return [...d].map((x) => x.toString(16).padStart(2, '0')).join('');
}

// Pinned sha256 of a txiki release's tjs binary (runtime/txiki.sha256).
async function txikiPin(ver, arch) {
  const key = `${ver}/txiki-macos-${arch}`;
  const lines = dec.decode(await tjs.readFile(TOOL_DIR + 'runtime/txiki.sha256')).split('\n');
  const hit = lines.map((l) => l.trim().split(/\s+/)).find(([h, k]) => k === key && /^[0-9a-f]{64}$/.test(h));
  if (!hit) fail(`no pinned sha256 for ${key} in runtime/txiki.sha256 — add it before building with this txiki version`);
  return hit[0];
}

// txiki.js for one arch, downloaded from the same release URL setup.sh and
// release.yml use, cached per version so repeat builds stay offline. The
// cache is user-writable and its exe ends up bundled and codesigned into the
// app, so it's checked against the pinned hash on download AND every reuse
// (#26) — an arch check alone accepts any Mach-O an attacker drops there.
async function fetchTxiki(arch) {
  const ver = 'v' + tjs.version;
  const want = await txikiPin(ver, arch);
  const cache = tjs.env.HOME + '/Library/Caches/tinyjs/txiki-' + ver;
  const exe = `${cache}/tjs-${arch}`;
  if (!(await exists(exe))) {
    console.log(`==> downloading txiki.js ${ver} (${arch})`);
    await tjs.makeDir(cache, { recursive: true });
    const zip = `${cache}/txiki-macos-${arch}.zip`;
    await run(['curl', '-fSsL', '-o', zip,
               `https://github.com/saghul/txiki.js/releases/download/${ver}/txiki-macos-${arch}.zip`]);
    await run(['unzip', '-q', '-o', zip, '-d', cache]);
    const got = await sha256Of(`${cache}/txiki-macos-${arch}/tjs`);
    if (got !== want) {
      await run(['rm', '-rf', zip, `${cache}/txiki-macos-${arch}`]);
      fail(`downloaded txiki.js ${ver} (${arch}) has sha256 ${got}, expected ${want} — refusing to bundle it`);
    }
    // mv last: the cached exe only exists once it's complete.
    await run(['mv', `${cache}/txiki-macos-${arch}/tjs`, exe]);
    await run(['rm', '-rf', zip, `${cache}/txiki-macos-${arch}`]);
  }
  const got = await sha256Of(exe);
  if (got !== want) fail(`${exe} has sha256 ${got}, expected ${want} — it was modified after download; delete it and rebuild`);
  if (!(await macArchsOf(exe)).includes(arch)) fail(`${exe} is not an ${arch} binary — delete it and rebuild`);
  return exe;
}

// The tjs to bundle for MAC_TARGET.
async function macTjs() {
  if (!MAC_NEED.length) return tjs.exePath;
  const have = await macArchsOf(tjs.exePath);
  if (!UNIVERSAL) {
    const [arch] = MAC_NEED;
    if (have.length === 1 && have[0] === arch) return tjs.exePath;
    if (!have.includes(arch)) return fetchTxiki(arch);
    // A fat host tjs that has the slice: thin it when lipo is around,
    // otherwise the fat copy runs fine, it's just bigger.
    const out = '.build/tjs-' + arch;
    return (await haveLipo()) && (await tryRun(['lipo', tjs.exePath, '-thin', arch, '-output', out]))
      ? out : tjs.exePath;
  }
  if (MAC_ARCHS.every((a) => have.includes(a))) return tjs.exePath;
  await needLipo('--universal');
  const slices = [tjs.exePath];
  for (const a of MAC_ARCHS.filter((x) => !have.includes(x))) slices.push(await fetchTxiki(a));
  const out = '.build/tjs-universal';
  await run(['lipo', '-create', ...slices, '-output', out]);
  console.log('==> universal tjs: ' + (await macArchsOf(out)).join(' + '));
  return out;
}

// The launcher to bundle for MAC_TARGET. Release installs ship it universal;
// a source checkout's is rebuilt with both slices by ensureLauncherFresh()
// when a build asks for one it lacks.
async function macLauncher() {
  const src = TOOL_DIR + 'native/launcher-macos';
  if (!MAC_NEED.length) return src;
  const have = await macArchsOf(src);
  const missing = MAC_NEED.filter((a) => !have.includes(a));
  if (missing.length) {
    fail(`the launcher has no ${missing.join(' / ')} slice — rebuild it with ` +
         '`TINYJS_UNIVERSAL=1 sh setup.sh` in ' + TOOL_DIR);
  }
  if (UNIVERSAL || have.length === 1) return src;
  // Per-arch build from a universal launcher: keep only the slice this app
  // runs (halves it). Without lipo the fat launcher works just as well.
  const out = '.build/launcher-' + MAC_TARGET;
  return (await haveLipo()) && (await tryRun(['lipo', src, '-thin', MAC_TARGET, '-output', out]))
    ? out : src;
}

// Which Macs can open the .app: the launcher AND tjs both need the CPU's
// slice, and a bundle built on Apple Silicon without --arch / --universal is
// refused on Intel with "not supported on this type of Mac" (issue #2).
async function reportMacArchs(cfg, APP) {
  const exe = await macArchsOf(APP + '/Contents/MacOS/' + cfg.name);
  const rt = await macArchsOf(APP + '/Contents/MacOS/tjs');
  if (!exe.length || !rt.length) return; // couldn't tell — don't guess
  const runsOn = exe.filter((a) => rt.includes(a));
  console.log('==> runs on: ' + (runsOn.map((a) => `${MAC_LABEL[a]} (${a})`).join(', ') || 'nothing — no common architecture'));
  // A sidecar missing a slice the app has: without x86_64 the app opens on
  // an Intel Mac and then fails to spawn it (bad CPU type) — say so now.
  // Without arm64 it still runs on Apple Silicon, translated by Rosetta 2,
  // which a Mac may not have installed yet.
  for (const { name, src } of cfg.executablesStaged?.files ?? []) {
    const has = await macArchsOf(APP + '/Contents/Helpers/' + name);
    if (!has.length) continue;
    if (runsOn.includes('x86_64') && !has.includes('x86_64')) {
      console.log(`warning: ${src} has no x86_64 slice — spawning it fails on Intel Macs. ` +
        'Ship a universal binary, or one per CPU in executables/macos-<arch>/.');
    }
    if (runsOn.includes('arm64') && !has.includes('arm64')) {
      console.log(`note: ${src} has no arm64 slice — Apple Silicon Macs run it under Rosetta 2 (installed on first use)`);
    }
  }
  if (runsOn.length === 1 && runsOn[0] === 'x86_64') {
    // Intel-only still opens on Apple Silicon, translated.
    console.log('    Apple Silicon Macs run it under Rosetta 2');
    return;
  }
  if (!runsOn.includes('x86_64') && MAC_TARGET !== 'arm64') {
    const lacking = [!exe.includes('x86_64') && 'the launcher', !rt.includes('x86_64') && 'tjs'].filter(Boolean).join(' and ');
    console.log(`note: won't open on Intel Macs — ${lacking} ${lacking.includes(' and ') ? 'have' : 'has'} no x86_64 slice.`);
    console.log('      Build an Intel copy with --arch x86_64, or one app for both with --universal.');
  }
}

// Submit dist/<Title>.app to Apple notarization and staple the ticket.
// Needs a real Developer ID signature plus a notarytool keychain profile
// (create one: xcrun notarytool store-credentials <name> --apple-id … --team-id …).
async function cmdNotarize() {
  if (IS_WIN || IS_LINUX) fail('notarization is a macOS step — nothing to do here');
  const cfg = await loadConfig();
  const APP = 'dist/' + cfg.title + '.app';
  if (!(await exists(APP))) fail(`${APP} not found — run \`tinyjs build\` first`);
  const identity = cfg.signIdentity;
  if (!identity || identity === '-') {
    fail('notarization needs a real Developer ID — set "signIdentity" in tinyjs.json ' +
         'or export TINYJS_SIGN_IDENTITY, then rebuild');
  }
  const profile = cfg.notarize?.profile;
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
  const keychain = cfg.notarize?.keychain;
  await run(['xcrun', 'notarytool', 'submit', zip, '--keychain-profile', profile,
             ...(keychain ? ['--keychain', keychain] : []), '--wait']);
  console.log('==> stapling ticket');
  await run(['xcrun', 'stapler', 'staple', APP]);

  // Rebuild the installer dmg from the now-stapled .app. A dmg made at build
  // time contains the pre-staple bundle (no ticket), which offline Gatekeeper
  // rejects — so refresh it whenever --dmg is passed, or whenever one already
  // exists on disk (from `build --dmg`), since that copy is guaranteed stale.
  const dmg = 'dist/' + cfg.name + '-' + (cfg.version || '0.0.0') + MAC_SUFFIX + '.dmg';
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

// Key-order-independent JSON, to compare a hand-edited manifest with its
// signed payload.
function canonicalJson(v) {
  if (Array.isArray(v)) return '[' + v.map(canonicalJson).join(',') + ']';
  if (v && typeof v === 'object')
    return '{' + Object.keys(v).sort().map((k) => JSON.stringify(k) + ':' + canonicalJson(v[k])).join(',') + '}';
  return JSON.stringify(v);
}

async function cmdPublish() {
  const cfg = await loadConfig();
  // Opt in only with an explicit baked publicKey; private material is never
  // read from config or copied into generated app sources.
  // Anything but a non-empty string (or list of them) leaves updates
  // unsigned, as at runtime. The private key may match any listed key.
  const publicKey = pinnedKeys(cfg.update?.publicKey);
  const keyFile = tjs.env.TINYJS_UPDATE_PRIVATE_KEY_FILE;
  let signingKey = null;
  if (cfg.update?.publicKey != null && !publicKey)
    console.log('note: update.publicKey is not a non-empty string or list — publishing an unsigned manifest');
  if (publicKey) {
    if (!keyFile) fail('signed updates require TINYJS_UPDATE_PRIVATE_KEY_FILE (external PKCS8 DER file)');
    try {
      signingKey = await crypto.subtle.importKey('pkcs8', await tjs.readFile(keyFile), 'Ed25519', false, ['sign']);
      await signManifest({ version: cfg.version ?? '0.0.0' }, signingKey, publicKey);
    } catch (e) {
      fail('cannot sign updates: ' + (e?.message ?? e));
    }
  } else if (keyFile) {
    fail('set update.publicKey before using TINYJS_UPDATE_PRIVATE_KEY_FILE');
  }
  // --expires 30d | 2027-01-31: signed manifests only (an unsigned one could
  // just have the date edited). Apps refuse the manifest after that moment.
  const ei = args.indexOf('--expires');
  let expires = null;
  if (ei >= 0) {
    if (!signingKey) fail('--expires needs signed updates (update.publicKey + TINYJS_UPDATE_PRIVATE_KEY_FILE)');
    const v = args[ei + 1] ?? '';
    const days = /^(\d+)d$/.exec(v);
    const at = days ? Date.now() + Number(days[1]) * 86400000 : Date.parse(v);
    if (!Number.isFinite(at) || at <= Date.now()) fail('--expires takes a number of days (30d) or a future date (2027-01-31)');
    expires = new Date(at).toISOString();
  }
  // --signed-only: host just the signed block, no plain fields, so apps
  // without a key (older installs included) see no update at all.
  let signedOnly = args.includes('--signed-only');
  if (signedOnly && !signingKey) fail('--signed-only needs signed updates (update.publicKey + TINYJS_UPDATE_PRIVATE_KEY_FILE)');
  const si = args.indexOf('--sign-manifest');
  if (si >= 0) {
    if (!signingKey || !args[si + 1]) fail('--sign-manifest FILE requires update.publicKey and a private key');
    const path = args[si + 1];
    try {
      let manifest = JSON.parse(dec.decode(await tjs.readFile(path)));
      // An already-signed manifest is re-signed (e.g. a fresh --expires),
      // but only once it checks out against the pinned key, and keeps its
      // shape: a signed-only one stays signed-only.
      if (manifest?.signed !== undefined) {
        const { signed: _, ...fields } = manifest;
        const payload = await verifyManifest(manifest, publicKey);
        if (typeof manifest.version !== 'string') signedOnly = true;
        // Plain fields that differ from the payload are hand edits (a merge
        // of several platforms): re-signing the old payload would silently
        // throw them away, and signing them unchecked could sign whatever a
        // compromised host put there. Make the edit explicit instead.
        else if (canonicalJson(fields) !== canonicalJson(payload))
          fail('cannot sign manifest: ' + path + ' has edits outside its signed block — delete the "signed" block to sign the plain fields as they are');
        manifest = payload;
      }
      if (expires) manifest.expires = expires;
      const signed = await signManifest(manifest, signingKey, publicKey, { signedOnly });
      await tjs.writeFile(path, enc.encode(JSON.stringify(signed, null, 2) + '\n'));
    } catch (e) {
      fail('cannot sign manifest: ' + (e?.message ?? e));
    }
    console.log('==> signed ' + path);
    return;
  }
  const version = cfg.version;
  if (!version) fail('tinyjs.json needs a "version" to publish (e.g. "1.0.0")');
  const PUB = 'dist/publish';
  // Per-arch macOS publishes accumulate: `publish --arch arm64` then
  // `publish --arch x86_64` leaves both zips and ONE manifest carrying both.
  // Windows likewise: an x64 and an arm64 tinyjs publishing the same project
  // (one machine can run both — x64 is emulated on ARM64) share a manifest.
  // The build wipes dist/, so park the previous output outside it meanwhile
  // (a stash left by a failed build is picked up the same way).
  const KEEP = '.publish-prev';
  const ACCUMULATE = !!MAC_SUFFIX || IS_WIN;
  if (ACCUMULATE && (await exists(PUB))) {
    await rmTree(KEEP);
    await tjs.rename(PUB, KEEP);
  }
  await cmdBuild();
  let prev = null;
  if (ACCUMULATE && (await exists(KEEP))) {
    try {
      prev = JSON.parse(dec.decode(await tjs.readFile(KEEP + '/manifest.json')));
      // Signed: merge only the verified payload. Unsigned: drop a signed
      // block left from an earlier signed publish (it would be stale).
      if (publicKey) prev = await verifyManifest(prev, publicKey);
      else delete prev.signed;
    } catch { prev = null; }
    // Another version's leftovers don't belong in this release.
    if (prev?.version === version) await tjs.rename(KEEP, PUB);
    else prev = null;
    await rmTree(KEEP);
  }

  // Windows zips are suffixed -win, Linux tarballs -linux-<arch>; the
  // manifest carries them in "win" / "linux" blocks alongside the macOS
  // url/sha256, so one manifest serves every OS.
  const linuxArch = IS_LINUX ? (/aarch64|arm64/i.test(globalThis.navigator?.platform ?? '') ? 'arm64' : 'x86_64') : '';
  // Windows builds are this tjs.exe's CPU; x86_64 keeps the original name.
  const winArch = IS_WIN ? await hostArch() : null;
  const zipName = cfg.name + '-' + version +
    (IS_WIN ? (winArch === 'arm64' ? '-win-arm64.zip' : '-win.zip') : IS_LINUX ? '-linux-' + linuxArch + '.tar.gz' : MAC_SUFFIX + '.zip');
  if (!prev) await rmTree(PUB);
  console.log('==> packing ' + zipName);
  if (IS_LINUX) {
    // Stage dist/ under a named folder so the tarball's one top-level entry
    // is the app folder (what update.js swaps in).
    const stage = '.build/publish-stage';
    await rmTree(stage);
    await tjs.makeDir(stage + '/' + cfg.name, { recursive: true });
    await copyTree('dist', stage + '/' + cfg.name);
    await run(['chmod', '+x', stage + '/' + cfg.name + '/' + cfg.name, stage + '/' + cfg.name + '/launcher']);
    // copyTree drops modes; sidecars need theirs back too.
    if (await exists(stage + '/' + cfg.name + '/executables'))
      await run(['chmod', '-R', '+x', stage + '/' + cfg.name + '/executables']);
    // Extras too (an install.sh): copy them over again, modes and all.
    await shipExtras(cfg, stage + '/' + cfg.name, null);
    await tjs.makeDir(PUB, { recursive: true });
    await run(['tar', '-czf', PUB + '/' + zipName, '-C', stage, cfg.name]);
  } else if (IS_WIN) {
    // Stage dist/ under a named folder so the zip's one top-level entry is
    // the app folder (what update.js swaps in); bsdtar (Windows 10+) writes
    // zip when told -a with a .zip name. By full path: from Git Bash, PATH
    // finds GNU tar first, which ignores -a and writes a tar named .zip.
    const stage = '.build/publish-stage';
    await rmTree(stage);
    await tjs.makeDir(stage + '/' + cfg.name, { recursive: true });
    await copyTree('dist', stage + '/' + cfg.name);
    await tjs.makeDir(PUB, { recursive: true });
    const tar = (tjs.env.SystemRoot || tjs.env.SYSTEMROOT || 'C:\\Windows') + '\\System32\\tar.exe';
    await run([tar, '-a', '-cf', PUB + '/' + zipName, '-C', stage, cfg.name]);
  } else {
    await tjs.makeDir(PUB, { recursive: true });
    await run(['ditto', '-c', '-k', '--keepParent', 'dist/' + cfg.title + '.app', PUB + '/' + zipName]);
  }
  const sha = await sha256File(PUB + '/' + zipName);

  // Zips live next to the manifest, so derive the download url from update.url.
  const base = cfg.update?.url ? cfg.update.url.replace(/\/[^/]*$/, '') : null;
  const zipUrl = (base ?? 'https://YOUR-HOST/updates') + '/' + zipName;
  const manifest = IS_WIN
    // Per-arch Windows builds go in the "win" block keyed by arch. Its own
    // url/sha256 is what apps from before arm64 builds read, and every one
    // of those is x64 — so the x86_64 build fills it.
    ? { ...prev, version,
        win: { ...prev?.win, [winArch]: { url: zipUrl, sha256: sha },
               ...(winArch === 'x86_64' ? { url: zipUrl, sha256: sha } : {}) } }
    : IS_LINUX
    ? { version, linux: { [linuxArch]: { url: zipUrl, sha256: sha } } }
    : MAC_SUFFIX
    // Per-arch mac builds go in a "mac" block keyed by arch. The top-level
    // url/sha256 is what apps from before that block read, and every one of
    // those is Apple Silicon (or universal) — so the arm64 build fills it.
    ? { ...prev, version,
        mac: { ...prev?.mac, [MAC_TARGET]: { url: zipUrl, sha256: sha } },
        ...(MAC_TARGET === 'arm64' ? { url: zipUrl, sha256: sha } : {}) }
    : { version, url: zipUrl, sha256: sha };
  // Publishing several platforms? Merge this manifest's fields into your
  // hosted one — mac owns url/sha256 (+ the per-arch mac block), Windows the
  // win block, Linux the per-arch linux block; they coexist.
  // Release notes for the in-app update prompt: --notes "text" or
  // --notes-file CHANGES.md (fed to update.check() as `notes`).
  const ni = args.findIndex((a) => a === '--notes' || a === '--notes-file');
  if (ni >= 0 && args[ni + 1]) {
    manifest.notes = args[ni] === '--notes-file'
      ? dec.decode(await tjs.readFile(args[ni + 1])).trim()
      : args[ni + 1];
  }
  if (expires) manifest.expires = expires;
  const output = signingKey ? await signManifest(manifest, signingKey, publicKey, { signedOnly }) : manifest;
  await tjs.writeFile(PUB + '/manifest.json', enc.encode(JSON.stringify(output, null, 2) + '\n'));

  console.log('==> dist/publish/ ready:');
  const kept = [...Object.values(manifest.mac ?? {}), manifest.win?.arm64, manifest.win?.x86_64];
  for (const z of kept.filter(Boolean).map((m) => m.url.replace(/.*\//, '')).filter((z) => z !== zipName))
    console.log('    ' + z + '   (kept from an earlier publish of this version)');
  console.log('    ' + zipName);
  if (MAC_SUFFIX && !manifest.mac.arm64) {
    console.log('note: no arm64 build in this manifest yet — apps built before per-arch');
    console.log('      manifests only read the top-level url; publish --arch arm64 too.');
  }
  if (IS_WIN && !manifest.win.x86_64) {
    console.log('note: no x86_64 build in this manifest yet — x64 apps (every app from');
    console.log('      before arm64 builds) see no update; publish with an x64 tinyjs too.');
  }
  console.log('    manifest.json (version ' + version + ', url ' + zipUrl + ')');
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

// Every executable called `name` on PATH, in PATH order (on Windows, only
// with a PATHEXT extension: cmd and PowerShell never run an extensionless
// file, so the bash `tinyjs` beside `tinyjs.cmd` would list one install
// twice). stat only — running a found tool to ask its version is
// pmVersion's job, and doctor starts no processes.
async function findOnPath(name) {
  const exts = IS_WIN
    ? (tjs.env.PATHEXT || '.COM;.EXE;.BAT;.CMD').split(';').filter(Boolean).map((e) => e.toLowerCase())
    : [''];
  const found = [];
  for (const dir of (tjs.env.PATH || '').split(IS_WIN ? ';' : ':')) {
    if (!dir) continue;
    for (const ext of exts) {
      const p = dir.replace(/[\\/]+$/, '') + (IS_WIN ? '\\' : '/') + name + ext;
      try {
        const st = await tjs.stat(p);
        if (st.isFile && (IS_WIN || st.mode & 0o111) && !found.includes(p)) found.push(p);
      } catch {}
    }
  }
  return found;
}

// Does this `tinyjs` on PATH start THIS install? Yes when it is (a symlink to)
// the install's own wrapper, or a small wrapper script that names the install
// directory (Homebrew's bin/tinyjs, tinyjs.cmd).
async function startsThisInstall(p) {
  const norm = (s) => s.replace(/\\/g, '/').replace(/\/+$/, '').toLowerCase();
  const home = norm(await tjs.realPath(TOOL_DIR).catch(() => TOOL_DIR));
  try {
    const real = await tjs.realPath(p);
    if (norm(real).replace(/\/[^/]*$/, '') === home) return true;
    const st = await tjs.stat(real);
    if (st.size > 64 * 1024) return false;
    return norm(dec.decode(await tjs.readFile(real))).includes(home);
  } catch { return false; }
}

// Diagnose why tinyjs or an app won't run here. Read-only: it stats and reads
// files, and never starts a process, opens a window, rebuilds, or touches the
// network. FAIL lines set exit status 1; WARN and NOTE lines don't.
async function cmdDoctor() {
  if (args.length) fail('usage: tinyjs doctor (no arguments)');
  const version = await toolVersion();
  let errors = 0;
  // Colour the labels only in a terminal (never when piped or redirected),
  // honouring NO_COLOR and TERM=dumb. libuv translates the codes on Windows.
  const tint = tjs.stdout.isTerminal && !tjs.env.NO_COLOR && tjs.env.TERM !== 'dumb'
    ? (code, text) => `\x1b[${code}m${text}\x1b[0m` : (_code, text) => text;
  const ok = (msg) => console.log(tint('32', 'OK:  ') + ' ' + msg);
  const warn = (msg) => console.log(tint('33', 'WARN:') + ' ' + msg);
  const note = (msg) => console.log(tint('2', 'NOTE:') + ' ' + msg);
  const bad = (msg, fix) => {
    errors++;
    console.log(tint('1;31', 'FAIL:') + ' ' + msg);
    if (fix) console.log('      ' + fix);
  };

  // hostArch() asks uname; doctor starts no processes, so read what the OS
  // already says: the env on Windows, uname via navigator.platform on Linux,
  // the CPU model on macOS (navigator.platform says MacIntel on Apple Silicon).
  // Windows on ARM tells an emulated x64 process (bin\tjs.exe is x64)
  // PROCESSOR_ARCHITECTURE=AMD64; PROCESSOR_IDENTIFIER isn't rewritten
  // ("ARMv8 (64-bit) Family 8 …"), so it says what the machine really is.
  const model = tjs.system?.cpus?.[0]?.model;
  const cpu = model && model !== 'unknown' ? model : '';
  const e = tjs.env;
  const arch = IS_WIN
    ? (/arm64/i.test((e.PROCESSOR_ARCHITECTURE ?? '') + ' ' + (e.PROCESSOR_ARCHITEW6432 ?? '')) ||
       /^ARM/i.test(e.PROCESSOR_IDENTIFIER ?? '') ? 'arm64' : 'x86_64')
    : IS_LINUX ? (/aarch64|arm64/i.test(globalThis.navigator?.platform ?? '') ? 'arm64' : 'x86_64')
    : /^Apple /.test(cpu) ? 'arm64' : isIntelMac() ? 'x86_64' : '';
  console.log(`tinyjs ${version} (txiki.js ${tjs.version}) on ${EXEC_OS}` +
              (arch ? ' ' + arch : '') + (cpu ? ', ' + cpu : ''));
  console.log('Installed at ' + TOOL_DIR);

  // The install itself. bin/tjs is running this, so only an odd invocation
  // (another runtime) gets here without it; the launcher is the real check.
  const restore = version === 'dev'
    ? 'Run ' + (IS_WIN ? 'setup.ps1' : './setup.sh') + ' in ' + TOOL_DIR + '.'
    : 'Reinstall tinyjs (see the README\'s install section).';
  async function checkExe(label, path, fix) {
    try {
      const st = await tjs.stat(path);
      if (!st.isFile || !st.size) throw new Error('not a non-empty file');
      if (!IS_WIN && !(st.mode & 0o111)) throw new Error('not executable — chmod +x it');
      ok(label + ' ' + path);
      return true;
    } catch (e) {
      bad(`${label} ${path}: ${e?.message ?? e}`, fix);
      return false;
    }
  }
  await checkExe('runtime', TOOL_DIR + 'bin/' + (IS_WIN ? 'tjs.exe' : 'tjs'), restore);
  if (await checkExe('launcher', LAUNCHER_EXE, restore) && version === 'dev') {
    const stale = await launcherStaleSources();
    if (stale.length)
      warn(`launcher is older than ${stale.join(', ')} — tinyjs dev and build rebuild it first`);
  }
  if (tjs.env.TINYJS_LAUNCHER)
    await checkExe('TINYJS_LAUNCHER override', tjs.env.TINYJS_LAUNCHER,
                   'Fix or unset TINYJS_LAUNCHER (tinyjs dev uses it; builds always use the bundled launcher).');
  if (isIntelMac())
    warn('Intel Mac: apps built here are Intel-only unless you pass --arch arm64 or --universal');

  // Which tinyjs a bare `tinyjs` runs. Two installs on PATH is the classic
  // "I updated but nothing changed".
  const onPath = await findOnPath('tinyjs');
  if (!onPath.length) {
    note('no tinyjs on PATH — run it by full path, or see the README to add it');
  } else {
    const mine = [];
    for (const p of onPath) mine.push(await startsThisInstall(p));
    if (!mine[0]) warn(`\`tinyjs\` on PATH is ${onPath[0]}, a different install from this one`);
    else ok('`tinyjs` on PATH is this install');
    const others = onPath.filter((_, i) => !mine[i]);
    if (others.length && mine[0]) warn('other tinyjs installs later on PATH: ' + others.join(', '));
  }

  // The webview. macOS always has WebKit.
  if (IS_WIN) {
    const roots = [tjs.env['ProgramFiles(x86)'], tjs.env.ProgramFiles, tjs.env.LOCALAPPDATA]
      .filter(Boolean).map((d) => d + '\\Microsoft\\EdgeWebView\\Application');
    let wv = null;
    for (const root of roots) {
      try {
        for await (const e of await tjs.readDir(root))
          if (e.isDirectory && /^\d+\.\d+/.test(e.name)) { wv = e.name; break; }
      } catch {}
      if (wv) break;
    }
    if (wv) ok('WebView2 Runtime ' + wv);
    else warn('WebView2 Runtime not found in the usual folders — if windows fail to open, ' +
              'install the Microsoft Edge WebView2 Evergreen Runtime');
  } else if (IS_LINUX) {
    const lib = 'libwebkit2gtk-4.1.so.0';
    let found = false;
    try { found = dec.decode(await tjs.readFile('/etc/ld.so.cache')).includes(lib); } catch {}
    for (const d of ['/usr/lib/x86_64-linux-gnu', '/usr/lib/aarch64-linux-gnu', '/usr/lib64', '/usr/lib']) {
      if (found) break;
      found = await exists(d + '/' + lib);
    }
    if (found) ok('WebKitGTK 4.1');
    else bad('WebKitGTK 4.1 (' + lib + ') not found',
             'Install it — Debian/Ubuntu: sudo apt install libwebkit2gtk-4.1-0; ' +
             'Fedora: sudo dnf install webkit2gtk4.1; openSUSE: sudo zypper install libwebkit2gtk-4_1-0');
    if (!tjs.env.DISPLAY && !tjs.env.WAYLAND_DISPLAY)
      warn('neither DISPLAY nor WAYLAND_DISPLAY is set — windows need a desktop session');
  }

  // Node tooling: only the framework templates (tinyjs new --template) need it.
  const tools = {};
  for (const t of ['node', 'npm', 'pnpm', 'yarn', 'bun', 'vp']) tools[t] = (await findOnPath(t))[0];
  const have = Object.keys(tools).filter((t) => tools[t]);
  if (have.length) ok('on PATH: ' + have.join(', '));
  else note('no Node.js or package manager on PATH — fine for the plain template; ' +
            'the React/Vue/Svelte/... templates need one');

  // The project in this directory, when there is one.
  if (await exists('tinyjs.json')) {
    let raw;
    try { raw = JSON.parse(dec.decode(await tjs.readFile('tinyjs.json'))); }
    catch (e) { bad('tinyjs.json is not valid JSON: ' + (e?.message ?? e)); }
    if (raw && (typeof raw !== 'object' || Array.isArray(raw))) {
      bad('tinyjs.json must be a JSON object');
      raw = null;
    }
    if (raw) {
      const cfg = isPlainObject(raw[EXEC_OS]) ? mergeConfig(raw, raw[EXEC_OS]) : raw;
      let fine = true;
      if (!cfg.name) {
        fine = false;
        bad(`tinyjs.json has no "name" (for ${EXEC_OS})`, 'Add "name": "myapp".');
      }
      if (cfg.minTinyjsVersion && version !== 'dev' && isNewer(version, cfg.minTinyjsVersion)) {
        fine = false;
        bad(`this project needs tinyjs ${cfg.minTinyjsVersion} or newer — this is ${version}`,
            'Run `tinyjs update`.');
      }
      for (const key of ['build', 'dev']) {
        const command = cfg.frontend?.[key];
        if (typeof command !== 'string' || !command.trim()) continue;
        const pm = command.trim().split(/\s+/)[0];
        if (pm in tools && !tools[pm]) {
          fine = false;
          bad(`frontend.${key} runs \`${pm}\`, which isn't on PATH`, `Install ${pm}.`);
        }
      }
      if ((cfg.frontend?.build || cfg.frontend?.dev) && (await exists('package.json'))) {
        let d = tjs.cwd.replace(/\\/g, '/').replace(/\/+$/, ''), installed = false;
        while (d && !installed) {
          installed = (await exists(d + '/node_modules')) || (await exists(d + '/.pnp.cjs'));
          const up = d.replace(/\/[^/]*$/, '');
          if (up === d) break;
          d = up;
        }
        if (!installed) {
          fine = false;
          bad('package.json but no node_modules — the frontend build has no tools',
              'Run `npm install` (or your package manager\'s install).');
        }
      }
      if (fine) ok(`project "${cfg.name}" in this directory`);
      // Editor help only, so a note; a $schema pointing elsewhere was chosen.
      if (raw.$schema === undefined)
        note(`add "$schema": "${CONFIG_SCHEMA}" to tinyjs.json for completion and checks in your editor`);
      const stale = [];
      for (const item of await projectToolFiles())
        if (!item.linked && !(await toolFileCurrent(item))) stale.push(item.rel + (item.isDir ? '/' : ''));
      if (stale.length)
        note(`not from this tinyjs: ${stale.join(', ')} — \`tinyjs update-project\` refreshes them`);
    }
  }

  note('a pass here can\'t prove a window will open (binary architecture, ' +
       'shared libraries and OS security settings aren\'t checked)');
  console.log(errors ? tint('1;31', `doctor: ${errors} problem(s)`) : tint('32', 'doctor: no problems found'));
  tjs.exit(errors ? 1 : 0);
}

// Apple-Silicon-first: the bundled tjs runtime is arm64, so an app built or run
// on an Intel Mac won't launch on Apple Silicon (and vice-versa). Heads-up for
// devs — non-fatal, since local dev with the x86_64 build still works. Apple
// Silicon CPUs report as "Apple M…"; Intel Macs report "Intel(R) …".
function isIntelMac() {
  if (IS_WIN || IS_LINUX) return false;
  const model = tjs.system?.cpus?.[0]?.model || '';
  // Only a positively-identified non-Apple CPU counts; if detection returns
  // nothing, stay quiet rather than false-alarm an Apple Silicon user.
  return !!model && !/^Apple /.test(model);
}

function warnIfIntelMac() {
  if (!isIntelMac()) return;
  console.log('tinyjs: heads-up — tinyjs targets Apple Silicon (M1 and later).');
  console.log('        This looks like an Intel Mac; apps you build here are');
  console.log('        Intel-only (Apple Silicon runs them under Rosetta) unless');
  console.log('        you pass --arch arm64 or --universal, and some features');
  console.log('        are untested. Continuing anyway.\n');
}
if (['new', 'dev', 'build', 'publish', 'notarize'].includes(cmd)) warnIfIntelMac();

switch (cmd) {
  case 'new': await cmdNew(); break;
  case 'wrap': await cmdWrap(); break;
  case 'dev': await cmdDev(); break;
  case 'doctor': await cmdDoctor(); break;
  case 'build': await cmdBuild(); break;
  case 'publish': await cmdPublish(); break;
  case 'notarize': await cmdNotarize(); break;
  case 'version': case '--version': case '-v': await cmdVersion(); break;
  case 'update': await cmdUpdate(); break;
  case 'update-project': await cmdUpdateProject(); break;
  case 'uninstall': await cmdUninstall(); break;
  default:
    console.log(`tinyjs — tiny desktop apps with txiki.js + webview

usage:
  tinyjs new <dir>    scaffold a new app (zero dependencies)
                        --template [name]: create-vite + tinyjs overlay instead
                        (react|vue|svelte|solid|preact|lit|alpine|vanilla,
                        -ts for TypeScript; no name = ask), --pm npm|pnpm|
                        yarn|bun|vp[:pm] (default: ask, else npm),
                        --no-install
  tinyjs wrap <url>   wrap a website into a desktop app (site wrapper:
                      gated API, downloads, popup policy) — [dir],
                      --name <name>, --ua <userAgent>, --menubar,
                      --panel (needs --menubar), --top,
                      --external a.com,b.com, --force (re-wrap in
                      place; unflagged choices carry over, --no-menubar /
                      --no-panel / --no-top turn one off),
                      --origins exact|subdomains|url,… (default: exact),
                      --yes (skip the origins prompt)
  tinyjs dev          run the app in the current directory
  tinyjs build        build dist/<name> and dist/<Name>.app (--dmg: also a disk image;
                      --arch arm64|x86_64: macOS .app for that CPU, from any Mac;
                      --universal: one macOS .app for Apple Silicon + Intel)
  tinyjs publish      build + zip the .app + auto-update manifest (takes --arch /
                      --universal; run once per --arch, the manifest merges both)
                      (--notes "text" | --notes-file FILE → manifest notes)
                      (--sign-manifest FILE: sign a final merged manifest without building;
                      requires update.publicKey + TINYJS_UPDATE_PRIVATE_KEY_FILE)
                      (--expires 30d | 2027-01-31: signed manifests stop being
                      accepted after that; re-sign with --sign-manifest to extend)
                      (--signed-only: no plain fields, so apps without a key see
                      no update)
  tinyjs notarize     submit dist/<Name>.app to Apple notarization + staple
                      (--dmg: also rebuild dist/<name>-<version>.dmg from the
                      stapled .app; auto-rebuilt if a dmg already exists;
                      after a build --arch, pass the same --arch)
  tinyjs update       update the tinyjs CLI itself (--check: only report,
                      --dry-run: print what it would run)
  tinyjs update-project  refresh this project's agent skill and types from
                      this tinyjs (overwrites edits to them)
  tinyjs uninstall    remove ~/.tinyjs and the PATH symlink (--yes: no prompt)
  tinyjs doctor       find why tinyjs or this project won't run (read-only)
  tinyjs version      print version`);
    tjs.exit(cmd ? 1 : 0);
}
