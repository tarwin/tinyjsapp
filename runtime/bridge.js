// tinyjs backend bridge.
//
// Spawns the native webview launcher and bridges it to your API over a Unix
// domain socket in a private (0700) temp dir. No network, no ports.
//
// Wire protocol (newline-delimited; payloads are JSON, or wire-escaped via
// esc() so they never contain a raw \n that would break line framing):
//   launcher -> backend:  CALL <id> <json-args-array>
//   backend -> launcher:  RET <id> <status> <json>    resolve/reject a call
//                         EVAL <js>                   run JS in the page
//                                                     (js is esc()-escaped;
//                                                     launcher wire_unescapes)
//                         TITLE <text>                set window title
//                         SIZE <w> <h>                resize window
//                         DLG <id> <op>               native dialog; launcher
//                                                     answers the call itself
//                         QUIT                        close the window

import { bundlePath, checkForUpdate, installUpdate, relaunch } from './update.js';

const enc = new TextEncoder();
const dec = new TextDecoder();
const DEBUG = !!tjs.env.TINYJS_DEBUG;
// txiki has no tjs.platform; OS=Windows_NT is always set by Windows itself,
// and navigator.platform reads "Linux …" from uname on Linux.
const IS_WIN = tjs.env.OS === 'Windows_NT';
const IS_LINUX = !IS_WIN && /linux/i.test(globalThis.navigator?.platform ?? '');
// navigator.platform here is the runtime's own (uname on Linux), not a
// webview's — so unlike the page it reports the real machine.
const ARCH = /aarch64|arm64/i.test(globalThis.navigator?.platform ?? '') ? 'arm64'
  : /x86_64|amd64/i.test(globalThis.navigator?.platform ?? '') ? 'x86_64'
  : IS_WIN ? 'x86_64' : 'arm64';
const OS = IS_WIN ? 'windows' : IS_LINUX ? 'linux' : 'macos';
// Linux only: several capabilities exist on X11 but not on Wayland, which
// hides other windows and the global pointer by design. GDK_BACKEND wins
// because that is what the launcher will actually use (see windowPlacement).
const ON_X11 = IS_LINUX && (tjs.env.GDK_BACKEND === 'x11'
  || (!!tjs.env.DISPLAY && tjs.env.GDK_BACKEND !== 'wayland'
      && tjs.env.XDG_SESSION_TYPE !== 'wayland'));

// ── system requirements ─────────────────────────────────────────────────────
// Linux ships its media stack in pieces, and which pieces are present varies
// by distro and install. Rather than let a feature fail mutely (silent audio,
// a dead tray), an app can ask what's missing and tell the user exactly what
// to install. Every entry is a real probe, not a guess about the distro.

// Package names per family, keyed by the manager an app should suggest.
const PKG_MANAGERS = [
  { id: 'apt', test: /debian|ubuntu|mint|pop|elementary/i, cmd: 'sudo apt install' },
  { id: 'dnf', test: /fedora|rhel|centos|rocky|alma/i, cmd: 'sudo dnf install' },
  { id: 'pacman', test: /arch|manjaro|endeavour/i, cmd: 'sudo pacman -S' },
  { id: 'zypper', test: /suse/i, cmd: 'sudo zypper install' },
];

let osRelease = null;
async function distroId() {
  if (osRelease !== null) return osRelease;
  try {
    const txt = new TextDecoder().decode(await tjs.readFile('/etc/os-release'));
    const id = /^ID=(.*)$/m.exec(txt)?.[1]?.replace(/"/g, '') ?? '';
    const like = /^ID_LIKE=(.*)$/m.exec(txt)?.[1]?.replace(/"/g, '') ?? '';
    osRelease = `${id} ${like}`;
  } catch { osRelease = ''; }
  return osRelease;
}

// does `argv` exit 0? used to ask gst-inspect whether a decoder exists
async function probeOk(argv) {
  try {
    const p = tjs.spawn(argv, { stdin: 'ignore', stdout: 'ignore', stderr: 'ignore' });
    return (await p.wait()).exit_status === 0;
  } catch { return false; }
}

const GST_CODECS = ['gstreamer1.0-plugins-bad', 'gstreamer1.0-plugins-ugly', 'gstreamer1.0-libav'];
const REQUIREMENTS = {
  'media.aac': {
    feature: 'AAC / M4A playback',
    detail: 'WebKitGTK decodes through GStreamer, and AAC lives in the optional '
      + 'plugin sets. Without them <audio> refuses AAC outright — most podcasts '
      + 'and internet radio.',
    probe: () => probeOk(['gst-inspect-1.0', 'avdec_aac']).then((a) => a || probeOk(['gst-inspect-1.0', 'faad'])),
    packages: { apt: GST_CODECS, dnf: ['gstreamer1-plugins-bad-free', 'gstreamer1-libav'],
                pacman: ['gst-plugins-bad', 'gst-plugins-ugly', 'gst-libav'],
                zypper: ['gstreamer-plugins-bad', 'gstreamer-plugins-libav'] },
  },
  'media.h264': {
    feature: 'H.264 video playback',
    detail: 'Same GStreamer plugin sets as AAC; without them <video> stays black.',
    probe: () => probeOk(['gst-inspect-1.0', 'avdec_h264']).then((a) => a || probeOk(['gst-inspect-1.0', 'openh264dec'])),
    packages: { apt: GST_CODECS, dnf: ['gstreamer1-plugins-bad-free', 'gstreamer1-libav'],
                pacman: ['gst-plugins-bad', 'gst-libav'], zypper: ['gstreamer-plugins-libav'] },
  },
  'media.mp3': {
    feature: 'MP3 playback',
    detail: 'Usually present as part of the base GStreamer install.',
    probe: () => probeOk(['gst-inspect-1.0', 'mpg123audiodec']).then((a) => a || probeOk(['gst-inspect-1.0', 'avdec_mp3'])),
    packages: { apt: ['gstreamer1.0-plugins-good'], dnf: ['gstreamer1-plugins-good'],
                pacman: ['gst-plugins-good'], zypper: ['gstreamer-plugins-good'] },
  },
  'speech': {
    feature: 'tiny.app.say / voices',
    detail: 'Speech goes through speech-dispatcher; without it say() resolves false.',
    probe: () => probeOk(['sh', '-c', 'command -v spd-say']),
    packages: { apt: ['speech-dispatcher'], dnf: ['speech-dispatcher'],
                pacman: ['speech-dispatcher'], zypper: ['speech-dispatcher'] },
  },
  'spotlight.index': {
    feature: 'fast tiny.app.spotlight',
    detail: 'With plocate the search is indexed and instant; without it tinyjs '
      + 'falls back to a bounded find under $HOME, which is slower and name-only.',
    probe: () => probeOk(['sh', '-c', 'command -v plocate || command -v locate']),
    packages: { apt: ['plocate'], dnf: ['plocate'], pacman: ['plocate'], zypper: ['plocate'] },
  },
  'audioTap': {
    feature: 'tiny.audioTap',
    detail: 'Capturing the system mix needs pw-cat (PipeWire) or parec (PulseAudio).',
    probe: () => probeOk(['sh', '-c', 'command -v pw-cat || command -v parec']),
    packages: { apt: ['pipewire-bin'], dnf: ['pipewire-utils'], pacman: ['pipewire'],
                zypper: ['pipewire-tools'] },
  },
  'tray': {
    feature: 'tiny.tray',
    detail: 'The tray needs an AppIndicator/StatusNotifier host. GNOME needs the '
      + 'AppIndicator shell extension; most other desktops have one built in.',
    probe: async () => !!(await busNameOwned('org.kde.StatusNotifierWatcher')),
    packages: { apt: ['gnome-shell-extension-appindicator'], dnf: ['gnome-shell-extension-appindicator'],
                pacman: ['libappindicator-gtk3'], zypper: ['gnome-shell-extension-appindicator'] },
  },
  'windowPosition': {
    feature: 'placing your own windows (setPosition / center)',
    // nothing to install — it is the session, so say so plainly
    detail: 'Wayland forbids a client from placing its own toplevels. Set '
      + '"windowPlacement": true in tinyjs.json to run under X11/XWayland, or log '
      + 'out and pick an X11 session.',
    probe: async () => ON_X11,
    packages: null,
  },
};

// is a bus name currently owned? (tray host detection)
async function busNameOwned(name) {
  return probeOk(['sh', '-c',
    `gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus `
    + `-m org.freedesktop.DBus.NameHasOwner ${name} 2>/dev/null | grep -q true`]);
}

const reqCache = new Map();
async function systemRequirements(ids) {
  const wanted = (Array.isArray(ids) && ids.length ? ids : Object.keys(REQUIREMENTS))
    .filter((id) => REQUIREMENTS[id]);
  // Only Linux splits these out; elsewhere the platform ships them.
  if (!IS_LINUX) return wanted.map((id) => ({ id, ok: true, feature: REQUIREMENTS[id].feature }));
  const distro = await distroId();
  const mgr = PKG_MANAGERS.find((m) => m.test.test(distro)) ?? PKG_MANAGERS[0];
  const out = [];
  for (const id of wanted) {
    const r = REQUIREMENTS[id];
    if (!reqCache.has(id)) reqCache.set(id, await r.probe().catch(() => false));
    const ok = reqCache.get(id);
    const pkgs = r.packages?.[mgr.id] ?? null;
    out.push({
      id, ok, feature: r.feature,
      detail: ok ? null : r.detail,
      // ready to show, e.g. "sudo apt install gstreamer1.0-plugins-bad …"
      install: ok || !pkgs ? null : { manager: mgr.id, packages: pkgs, command: `${mgr.cmd} ${pkgs.join(' ')}` },
    });
  }
  return out;
}

function systemInfo() {
  return {
    os: OS,
    arch: ARCH,
    // Linux desktops differ enough that apps sometimes need the specifics;
    // null everywhere else.
    session: IS_LINUX ? (ON_X11 ? 'x11' : (tjs.env.XDG_SESSION_TYPE || null)) : null,
    desktop: IS_LINUX ? (tjs.env.XDG_CURRENT_DESKTOP || null) : null,
  };
}

// What this machine can actually do, so an app can degrade on purpose
// instead of calling something that quietly does nothing. true = works,
// false = the OS/session has no equivalent. Anything absent from a platform's
// column below is simply true on that platform.
function systemCapabilities() {
  const linux = {
    // Wayland forbids a client placing its own toplevels, reading the global
    // pointer, seeing other windows, or synthesising input. X11 allows all of
    // it — see the "windowPlacement" manifest key, which selects X11.
    windowPosition: ON_X11,
    mousePosition: ON_X11,
    captureScreen: ON_X11,
    keystroke: ON_X11,
    otherWindows: ON_X11,
    moveOtherWindows: ON_X11,
    selectedText: ON_X11,
    // no Linux equivalent at all
    recorder: false,
    ocr: false,
    quickLook: false,
    share: false,
    applescript: false,
    ai: false,
    wifi: false,
    dockBadge: false,
    authenticate: false,
    // present, with the caveats in the README
    globalHotkeys: true, tray: true, notifications: true,
    notificationActions: true, notificationReply: false,
    secrets: true, mediaKeys: true, nowPlaying: true, audioTap: true,
    speech: true, pickColor: true, spotlight: true, launchAtLogin: true,
    printToPDF: true, transparency: true, vibrancy: false,
  };
  const windows = {
    applescript: false, ai: false, quickLook: false, share: false,
    vibrancy: false, selectedText: false, ocr: false,
    windowPosition: true, mousePosition: true, captureScreen: true,
    keystroke: true, recorder: false,
  };
  const macos = { vibrancy: true, applescript: true, quickLook: true, share: true };
  const table = IS_LINUX ? linux : IS_WIN ? windows : macos;
  return { os: OS, ...table };
}
// Strip the file name off a path, tolerating both separators (Windows paths
// arrive with backslashes).
const dirOf = (p) => String(p).replace(/[\\/][^\\/]*$/, '');
const isAbs = (p) => p.startsWith('/') || /^[A-Za-z]:[\\/]/.test(p);
// Windows has no HOME env var; macOS-ish app code (tjs.env.HOME + '/…') dies
// at import without it. Point it at the profile so such apps degrade instead.
if (IS_WIN && !tjs.env.HOME) tjs.env.HOME = tjs.homeDir;

function dbg(dir, line) {
  if (DEBUG) console.log(dir, line.length > 160 ? line.slice(0, 160) + '…' : line);
}

// Dialogs run in the launcher, which answers the page's call directly.
// Each entry maps a method to its wire op and the params serialized as
// tab-separated args (order matters; see launcher.cc do_dialog).
const one = (s) => String(s ?? '').replace(/[\t\n\r]/g, ' ');
// Wire-escape for payloads that must survive tabs/newlines intact (clipboard
// text, drag-out paths); the launcher reverses it (wire_unescape).
const esc = (s) => String(s ?? '')
  .replace(/\\/g, '\\\\').replace(/\t/g, '\\t')
  .replace(/\r/g, '\\r').replace(/\n/g, '\\n');
// Reverse of esc() / the launcher's wire_escape, for inbound tab fields.
const unesc = (s) => String(s ?? '').replace(/\\(.)/g,
  (_, c) => c === 'n' ? '\n' : c === 't' ? '\t' : c === 'r' ? '\r' : c);
const DIALOG_OPS = {
  'win.openFile': { op: 'open', args: () => [] },
  'win.openFiles': { op: 'openmulti', args: () => [] },
  'win.pickFolder': { op: 'dir', args: () => [] },
  'win.saveFile': { op: 'save', args: () => [] },
  'win.alert': { op: 'alert', args: (p) => [one(p.message), one(p.detail), one(p.ok)] },
  'win.confirm': { op: 'confirm', args: (p) => [one(p.message), one(p.detail), one(p.ok), one(p.cancel)] },
  'win.prompt': { op: 'prompt', args: (p) => [one(p.message), one(p.default), one(p.ok), one(p.cancel)] },
};

// Per-app data root: ~/Library/Application Support/<id> (macOS),
// %APPDATA%\<id> (Windows), or $XDG_DATA_HOME/<id> (Linux).
function appDataDir(appId) {
  const id = appId || 'tinyjs-app';
  if (IS_WIN) return (tjs.env.APPDATA || tjs.homeDir + '/AppData/Roaming') + '/' + id;
  if (IS_LINUX) return (tjs.env.XDG_DATA_HOME || tjs.homeDir + '/.local/share') + '/' + id;
  return tjs.homeDir + '/Library/Application Support/' + id;
}

// Tiny persistent JSON store in the per-app data dir.
// Flat string keys, JSON values, atomic writes.
function makeStore(appId) {
  const dir = appDataDir(appId);
  const path = dir + '/store.json';
  let data = null;
  async function load() {
    if (data) return data;
    try { data = JSON.parse(dec.decode(await tjs.readFile(path))); }
    catch { data = {}; }
    return data;
  }
  // Persistence is best-effort: the in-memory value is always updated, and a
  // write failure (bad path, full disk, permissions) resolves false instead
  // of rejecting — an un-awaited store.set() must never crash the backend.
  let tmpSeq = 0;
  async function doSave() {
    try {
      await tjs.makeDir(dir, { recursive: true }).catch(() => {});
      // Unique tmp per write so concurrent (un-awaited) set()s don't race on
      // the same rename source.
      const tmp = path + '.' + (tmpSeq = (tmpSeq + 1) % 1e6) + '.tmp';
      await tjs.writeFile(tmp, enc.encode(JSON.stringify(data, null, 2) + '\n'));
      // Windows: a freshly-written target can be transiently locked by
      // Defender / the indexer, failing the rename with EPERM even with a
      // single writer — retry briefly before giving up.
      for (let attempt = 0; ; attempt++) {
        try {
          await tjs.rename(tmp, path);
          break;
        } catch (e) {
          if (attempt >= 5) throw e;
          await new Promise((r) => setTimeout(r, 25 * (attempt + 1)));
        }
      }
      return true;
    } catch (e) {
      console.log('tinyjs store write failed:', e?.message ?? String(e));
      return false;
    }
  }
  // One save in flight at a time: overlapping renames onto the same target
  // throw EPERM on Windows (seen when a burst of windows all set() at boot).
  // Each caller still gets the result of a save that includes its write.
  let saveChain = Promise.resolve(true);
  function save() {
    saveChain = saveChain.then(doSave, doSave);
    return saveChain;
  }
  return {
    async get(key) { return (await load())[key] ?? null; },
    async set(key, value) { await load(); data[key] = value; return save(); },
    async delete(key) { await load(); delete data[key]; return save(); },
    async all() { return { ...(await load()) }; },
  };
}

export async function createApp({ html, htmlPath, title = 'tinyjs', size = '960x640', version = '0.0.0', tinyjsVersion = 'dev', id = null, launcherPath, api = {}, onMenu, onTray, onHotkey, onContextMenu, onSystem, onOpenUrl, onOpenFiles, onNotificationClick, onNotificationAction, onMediaKey, onWindowClosed, onClipboardChange, onUpdateAvailable, onAudioTap, chrome = null, update = null, activation = null, readAccess = null, audioTap = null, windowPlacement = null, contextMenu = true, userAgent = null, urlScheme = null, fileExtensions = null }) {
  const exeDir = dirOf(tjs.exePath) + '/';

  async function exists(p) {
    try {
      await tjs.stat(p);
      return true;
    } catch {
      return false;
    }
  }

  // Two arrangements:
  //  - attach (packaged .app): the launcher IS the bundle executable — it owns
  //    the window, already loaded Resources/app/frontend, listens on a socket,
  //    and spawned us with TINYJS_SOCKET pointing at it. Being the
  //    LaunchServices-registered process gives it deep links, file-open events,
  //    and single-instancing.
  //  - spawn (dev + bare binary): we create the socket and spawn the launcher.
  const attachPath = tjs.env.TINYJS_SOCKET;
  let proc = null;
  let readable, writable;
  let pagePath = null;
  let ownsPage = false; // true when the bridge materialized the page file
  let cleanup = async () => {};
  // Windows: argv transform that routes a console command through
  // `launcher --run` (CREATE_NO_WINDOW) so no terminal window flashes when a
  // GUI-subsystem app shells out — tjs.spawn has no flag for this. Identity
  // everywhere else; assigned once the launcher path is known.
  let hiddenArgv = (args) => args;

  if (attachPath) {
    const conn = await tjs.connect('pipe', attachPath);
    ({ readable, writable } = await conn.opened);
  } else {
    // Launcher: explicit option > env override > next to the executable.
    const launcherName = IS_WIN ? 'launcher.exe' : 'launcher';
    let launcher = launcherPath || tjs.env.TINYJS_LAUNCHER;
    if (!launcher && (await exists(exeDir + launcherName))) launcher = exeDir + launcherName;
    if (!launcher || !(await exists(launcher))) {
      throw new Error('tinyjs launcher binary not found (looked at: ' + (launcher || exeDir + launcherName) + ')');
    }
    if (IS_WIN) hiddenArgv = (args) => [launcher, '--run', ...args];

    // Private rendezvous dir for the materialized frontend. The transport is a
    // Unix domain socket inside it — or, on Windows, a named pipe whose name
    // is derived from the (random) dir name; both are per-user namespaces.
    const workDir = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-XXXXXX');
    const sockPath = IS_WIN
      ? '\\\\.\\pipe\\' + workDir.slice(dirOf(workDir).length + 1)
      : workDir + '/app.sock';

    // Page source, in precedence order:
    //  - TINYJS_HTML env override (self-contained test pages): materialized
    //  - htmlPath: the real file is handed to the launcher, so sibling css/js/
    //    images load relatively (multi-file frontends); RELOAD re-reads disk
    //  - html string: materialized into the private workDir
    const overridePath = tjs.env.TINYJS_HTML;
    if (!overridePath && htmlPath) {
      pagePath = htmlPath;
    } else {
      let pageHtml = html;
      if (overridePath) pageHtml = dec.decode(await tjs.readFile(overridePath));
      if (pageHtml == null) throw new Error('createApp needs `html` (string) or `htmlPath`');
      pagePath = workDir + '/index.html';
      await tjs.writeFile(pagePath, enc.encode(pageHtml));
      ownsPage = true;
    }

    const server = await tjs.listen('pipe', sockPath);
    const serverInfo = await server.opened;

    // Accessory activation (menu-bar agents) rides in on the env; packaged
    // apps get it from the plist instead (LSUIElement + TinyjsActivation).
    // readAccess widens the page's file:// read root (same, via the env in
    // dev / the TinyjsReadAccess plist key in packaged apps).
    const spawnEnv = { ...tjs.env };
    if (activation === 'accessory') spawnEnv.TINYJS_ACTIVATION = 'accessory';
    // Windows: a transparent main window must drop its GDI redirection
    // bitmap AT CREATION (stale white shows through a late-cleared webview
    // otherwise) — but a window without one can't draw a Win32 menu bar
    // (GDI), so the launcher only does it when the manifest asks for
    // transparency. Declare it in tinyjs.json "chrome" — a setChrome from
    // page JS alone is too late for the main window on Windows.
    if (IS_WIN && chrome?.transparent) spawnEnv.TINYJS_TRANSPARENT = '1';
    if (readAccess) spawnEnv.TINYJS_READ_ACCESS = readAccess === true ? tjs.homeDir : String(readAccess);
    // "windowPlacement": true — the app places its own windows (setPosition/
    // center), e.g. to snap or dock them. Wayland forbids a client from
    // placing its own toplevels, so those calls do nothing there; X11 permits
    // them. Ask GTK for the X11 backend when an X server is reachable
    // (XWayland counts), which is the only way such an app works on a Wayland
    // desktop. No-op on macOS and Windows, which always allow placement.
    if (IS_LINUX && windowPlacement && tjs.env.DISPLAY && !tjs.env.GDK_BACKEND) {
      spawnEnv.GDK_BACKEND = 'x11';
    }
    // Custom User-Agent: WKWebView's default UA lacks the "Version/x Safari/x"
    // suffix, so UA-sniffing sites reject it. Packaged apps use the
    // TinyjsUserAgent plist key instead (this env only applies to the dev spawn).
    if (userAgent) spawnEnv.TINYJS_UA = String(userAgent);
    // Windows built apps: hand the launcher our exe so taskbar pins and the
    // Start-Menu shortcut relaunch the APP — the visible window belongs to
    // launcher.exe, which can't start on its own, so a default pin would be
    // dead on next launch. Dev spawns set nothing (nothing worth pinning).
    if (IS_WIN && bundlePath()) spawnEnv.TINYJS_APP_EXE = tjs.exePath;
    // Linux: the app id names the WM class (window ↔ .desktop matching) and
    // the notification identity. Dev sets it from the CLI; built apps here.
    if (IS_LINUX && id && !spawnEnv.TINYJS_APP_ID) spawnEnv.TINYJS_APP_ID = id;
    // Windows/Linux built apps: the icon rides inside the compiled binary
    // (app root of the TPK extraction, next to the frontend/ the page loads
    // from); a dist/icon.png next to the binary still wins for older builds.
    // Dev passes TINYJS_ICON from the CLI instead.
    if ((IS_WIN || IS_LINUX) && !spawnEnv.TINYJS_ICON) {
      const tpkIcon = pagePath ? dirOf(dirOf(pagePath)) + '/icon.png' : null;
      if (await exists(exeDir + 'icon.png')) spawnEnv.TINYJS_ICON = exeDir + 'icon.png';
      else if (tpkIcon && (await exists(tpkIcon))) spawnEnv.TINYJS_ICON = tpkIcon;
    }
    // Windows: Chromium gives every file:// URL an opaque origin, which
    // TAINTS local media in the WebAudio graph — createMediaElementSource
    // on a local track outputs pure zeros (macOS WKWebView doesn't taint
    // page-dir files). The WebView2 loader honors this env var; the flag
    // makes file:// same-origin so local media drives analysers/EQs like it
    // does on macOS. Tradeoff: file:// pages can then read other local
    // files — acceptable here because the page already holds an RPC channel
    // to a backend with full filesystem access.
    if (IS_WIN) {
      // --ignore-gpu-blocklist: WebGPU parity with the macOS launcher (which
      // force-enables the WebKit feature flag) — without it, virtualized or
      // older GPUs answer requestAdapter() with null.
      // autoplay: WKWebView never gates playback on a gesture, Chromium does
      // — and satellite windows (visualizers analysing a silent twin stream)
      // may never receive a click at all.
      const flags = ['--allow-file-access-from-files', '--ignore-gpu-blocklist',
                     '--enable-unsafe-webgpu',
                     '--autoplay-policy=no-user-gesture-required'];
      let extra = spawnEnv.WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS || '';
      for (const f of flags) if (!extra.includes(f)) extra = (extra ? extra + ' ' : '') + f;
      spawnEnv.WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = extra;
    }
    const spawnOpts = { stderr: 'inherit', env: spawnEnv };
    proc = tjs.spawn([launcher, pagePath, sockPath, title, size, version], spawnOpts);

    cleanup = async () => {
      if (!IS_WIN) await tjs.remove(sockPath).catch(() => {}); // pipes aren't files
      await tjs.remove(workDir, { recursive: true }).catch(() => tjs.remove(workDir).catch(() => {}));
    };

    // Wait for the launcher to connect, but bail out if it dies instead.
    const acceptReader = serverInfo.readable.getReader();
    const first = await Promise.race([
      acceptReader.read().then(({ value }) => ({ sock: value })),
      proc.wait().then((st) => ({ exited: st })),
    ]);
    if (first.exited) {
      await cleanup();
      throw new Error('launcher exited before connecting: ' + JSON.stringify(first.exited));
    }

    ({ readable, writable } = await first.sock.opened);
  }

  const writer = writable.getWriter();
  // Frontend base for win.open page resolution: a directory in file mode, or
  // the dev-server origin when htmlPath is a URL (devUrl mode).
  const isUrl = (s) => /^https?:\/\//i.test(String(s ?? ''));
  const frontendDir = htmlPath
    ? (isUrl(htmlPath) ? htmlPath.replace(/\/+$/, '') : dirOf(htmlPath))
    : null;

  // Read-backs: <OP> <qid> <rest> → launcher answers GOT <qid> <json>.
  let qidSeq = 1;
  const pendingGets = new Map();
  function ask(op, rest) {
    return new Promise((resolve) => {
      const qid = String(qidSeq++);
      pendingGets.set(qid, resolve);
      send(op + ' ' + qid + (rest != null ? ' ' + rest : ''));
    });
  }
  const query = (what) => ask('GET', what);
  const shellOp = async (op, target) => {
    const r = await ask('SHELL', op + '\t' + esc(target));
    if (!r?.ok) throw new Error(r?.error ?? op + ' failed');
    return true;
  };

  // Menu items, shared by menu bar / tray / context menu. Items support
  // { id, label, key?, checked?, enabled?, submenu?: [...] } | { separator }.
  function sendItems(items) {
    for (const it of items ?? []) {
      if (it.separator) { send('SEP'); continue; }
      if (it.submenu) {
        send('SUB ' + [one(it.id), one(it.label ?? it.id)].join('\t'));
        sendItems(it.submenu);
        send('SUBEND');
        continue;
      }
      const flags = (it.checked ? 'c' : '') + (it.enabled === false ? 'd' : '');
      send('ITEM ' + [one(it.id), one(it.label ?? it.id), one(it.key ?? ''), flags].join('\t'));
    }
  }

  // Desktop notification. Packaged apps (attach mode: the launcher is the
  // bundle executable) get real Notification Center banners — the app's own
  // icon, permission prompt on first use, and clicks back as the
  // 'notification-click' event (opts: { id, subtitle, sound }). Dev has no
  // bundle for UNUserNotificationCenter, so it falls back to osascript
  // (banner appears under "Script Editor").
  async function notify({ title, body, subtitle, id: nid, sound, actions } = {}) {
    // Windows (tray balloon / toast) and Linux (org.freedesktop.Notifications
    // over DBus) notify from the launcher in every mode — no bundle
    // requirement — so the osascript fallback below stays macOS-only.
    if (attachPath || IS_WIN || IS_LINUX) {
      // actions: [{ id, title, reply?, placeholder?, destructive? }] — buttons
      // (or a reply field) on the banner; taps come back as 'notification-action'.
      const acts = actions?.length ? esc(JSON.stringify(actions)) : '';
      send('NOTIFY ' + [one(nid ?? ''), one(title ?? 'tinyjs'), one(body ?? ''),
                        one(subtitle ?? ''), sound ? '1' : '0', acts].join('\t'));
      return true;
    }
    // notify() is naturally fire-and-forget; an unhandled rejection here
    // would kill the whole backend, so it never throws — it resolves false.
    try {
      const aq = (s) => '"' + String(s ?? '').replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
      let script = 'display notification ' + aq(body ?? '') + ' with title ' + aq(title ?? 'tinyjs');
      if (subtitle) script += ' subtitle ' + aq(subtitle);
      const p = tjs.spawn(['/usr/bin/osascript', '-e', script], { stdout: 'ignore', stderr: 'ignore' });
      const st = await p.wait();
      return st.exit_status === 0 && !st.term_signal;
    } catch (e) {
      console.log('tinyjs notify failed:', e?.message ?? String(e));
      return false;
    }
  }

  function send(line) {
    dbg('>>', line);
    // EPIPE is normal during shutdown (the launcher closes the socket the
    // moment the window goes away) — only unexpected errors are worth noise.
    writer.write(enc.encode(line + '\n')).catch((e) => {
      if (!/EPIPE|ECONNRESET/.test(String(e))) console.log('tinyjs send error:', e);
    });
  }

  function push(event, data) {
    // Broadcast so secondary windows receive backend events too.
    // esc() so any backslash in the JSON survives wire_unescape on the launcher.
    send('EVAL@* ' + esc('window.__emit && window.__emit(' + JSON.stringify({ event, data }) + ')'));
  }

  const app = {
    push,
    // tjs.spawn, minus the console window on Windows: console tools spawned
    // from a GUI-subsystem app each pop a terminal, so this routes through
    // `launcher --run` (CREATE_NO_WINDOW). Elsewhere it's plain tjs.spawn.
    spawnHidden(args, opts) { return tjs.spawn(hiddenArgv(args), opts); },
    setTitle(t) { send('TITLE ' + String(t).replace(/\n/g, ' ')); },
    setSize(w, h) { send(`SIZE ${w | 0} ${h | 0}`); },
    // Not JS eval(): sends script to the app's own page via webview_eval,
    // the same channel push() uses. Never receives external input.
    // esc() keeps newlines intact — flattening them would let a // line
    // comment swallow the rest of a multi-line snippet.
    eval(js) { send('EVAL ' + esc(js)); },
    // Re-render the page from disk. `newHtml` only applies to materialized
    // pages (html-string mode); direct htmlPath pages always reload the
    // real file, which is the point.
    async reload(newHtml) {
      if (newHtml != null && ownsPage) await tjs.writeFile(pagePath, enc.encode(newHtml));
      send('RELOAD');
    },
    // menus: [{ title, items: [{ id, label, key? } | { separator: true }] }]
    // Clicks arrive as a 'menu' page event and via the onMenu option.
    setMenu(menus) {
      send('MENUBEGIN');
      for (const m of menus ?? []) {
        send('MENU ' + one(m.title));
        sendItems(m.items);
      }
      send('MENUEND');
    },
    // Patch a live item without redeclaring the menu: { label?, checked?, enabled? }.
    updateMenuItem(id, patch = {}) {
      const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
      send('MENUUPD ' + [one(id), one(patch.label ?? ''),
                         bit(patch.checked), bit(patch.enabled)].join('\t'));
    },
    // { exists, label, checked, enabled } for a menu/tray/context item.
    getMenuItem(id) { return query('item:' + id); },
    // { x, y, width, height, fullscreen, minimized, visible, focused,
    //   alwaysOnTop, resizable, screen: { width, height, scale } }
    getWinState() { return query('win'); },
    restore() { send('WINOP restore'); },
    setFullscreen(v) { send('WINOP fullscreen ' + (v ? 1 : 0)); },
    notify,
    // Window visibility & app presence. hide() hides the APP (NSApp hide):
    // macOS returns focus to the previously active app on its own, so
    // hide-then-paste palettes need no frontmost-tracking. show() re-activates;
    // show({ activate: false }) surfaces the window without stealing focus
    // (overlay/HUD panels).
    hide() { send('WINOP hide'); },
    show(opts) { send('WINOP show' + (opts?.activate === false ? ' 0' : '')); },
    center() { send('WINOP center'); },
    minimize() { send('WINOP minimize'); },
    // Toggles native fullscreen.
    fullscreen() { send('WINOP fullscreen'); },
    setAlwaysOnTop(v) { send('WINOP ontop ' + (v ? 1 : 0)); },
    setResizable(v) { send('WINOP resizable ' + (v ? 1 : 0)); },
    // Mouse events pass through to whatever is behind (draw-on-screen
    // overlays, HUDs). Pair with setChrome({ transparent: true }).
    setClickThrough(v) { send('WINOP clickthrough ' + (v ? 1 : 0)); },
    // Stack the window in a band: 'normal' | 'floating' (= alwaysOnTop) |
    // 'overlay' (above almost everything, incl. most fullscreen apps) |
    // 'desktop' (behind normal windows — wallpaper/pets).
    setLevel(level) { send('WINOP level ' + one(level ?? 'normal')); },
    // Follow the user onto every Space and float over fullscreen apps.
    setAllSpaces(v) { send('WINOP allspaces ' + (v ? 1 : 0)); },
    // Top-left origin in screen points (CSS-style coordinates).
    setPosition(x, y) { send(`WINOP pos ${x | 0} ${y | 0}`); },
    // false: no Dock icon / no app menu (menu-bar-only app); true: normal app.
    setDockVisible(v) { send('WINOP dock ' + (v ? 1 : 0)); },
    // true: the close button hides the window instead of quitting.
    setHideOnClose(v) { send('WINOP hideonclose ' + (v ? 1 : 0)); },
    // spec: { title?, icon?, template?, tooltip?, primaryAction?,
    //         menu?: [{ id, label, key? } | { separator: true }] }
    // icon is a png path (absolute or project-relative), 'sf:<name>' for an
    // SF Symbol (e.g. 'sf:cup.and.saucer.fill' — no shipped assets needed;
    // macOS only), or 'emoji:<glyph>' for a glyph drawn as a monochrome
    // tray silhouette (Windows only) — branch per-OS for asset-free icons;
    // template: false keeps its colors instead of adapting to the menu bar
    // (default true). Menu clicks arrive as a 'tray' page event and via the
    // onTray option; with no menu, icon clicks arrive as 'trayclick'.
    // primaryAction: true splits the two — left click fires 'trayclick' (a
    // Caffeine-style toggle) and the menu opens on right/ctrl-click.
    tray: {
      set(spec = {}) {
        let icon = spec.icon ?? '';
        if (icon && !isAbs(icon) && !icon.startsWith('sf:') && !icon.startsWith('emoji:')) icon = tjs.cwd + '/' + icon;
        send('TRAYBEGIN ' + [one(spec.title), one(icon),
                             spec.template === false ? '0' : '1',
                             one(spec.tooltip),
                             spec.primaryAction ? '1' : '0'].join('\t'));
        sendItems(spec.menu);
        send('TRAYEND');
      },
      remove() { send('TRAYREMOVE'); },
      // The tray icon's on-screen rect { x, y, width, height } (top-left
      // coords) — anchor a dropdown window under it. null if no tray set.
      position: () => query('traypos'),
    },
    print() { send('PRINT'); },
    // Render the page to a PDF file (WKWebView vector PDF) -> { path }.
    async printToPDF(path) {
      const r = await ask('PDF', esc(path));
      if (!r?.ok) throw new Error(r?.error ?? 'pdf failed');
      return { path: r.path };
    },
    // Trackpad haptic feedback: 'generic' | 'alignment' | 'level'. No-op on
    // Macs without a Force Touch trackpad.
    haptic(pattern = 'generic') { send('HAPTIC ' + one(pattern)); return true; },
    // Replace the Dock icon from a png (render a canvas → progress rings,
    // unread counts). '' resets to the bundle icon.
    dockIcon(pngPath) { send('DOCKICON ' + esc(pngPath ?? '')); return true; },
    // Battery: { percent, charging, plugged, minutesRemaining } | null (on
    // desktops without a battery).
    battery: () => query('battery'),
    // Current Wi-Fi: { ssid, bssid, rssi, noise, txRate } | null. ssid/bssid
    // are null without the Location permission on macOS 14+.
    wifi: () => query('wifi'),
    // Find files by name or content (Spotlight/NSMetadataQuery, home scope)
    // -> up to 100 absolute paths.
    async spotlight(queryText) {
      return (await ask('SPOTLIGHT', esc(String(queryText ?? ''))))?.paths ?? [];
    },
    // On-device LLM (Apple's FoundationModels — offline, no API key). Only
    // in builds made with TINYJS_AI=1 on macOS 26; check availability first.
    ai: {
      // 'available' | 'unavailable' (Apple Intelligence off / not downloaded)
      // | 'unsupported' (older macOS or a non-AI build).
      async availability() { return (await ask('AI available'))?.status ?? 'unsupported'; },
      // generate(prompt, { instructions }) -> the completion text; throws
      // with the reason (incl. 'not built in' on stock builds).
      async generate(prompt, { instructions } = {}) {
        const r = await ask('AI generate', esc(String(prompt ?? '')) + '\t' + esc(instructions ?? ''));
        if (!r?.ok) throw new Error(r?.error ?? 'generation failed');
        return r.text;
      },
    },
    // Window chrome: { frame?, trafficLights?, transparent?, vibrancy?,
    // squareCorners?, acceptsFirstMouse? }. frame:false hides the titlebar
    // (content extends under it; keep your own drag region via data-tiny-drag).
    // vibrancy: material name or null. squareCorners:true drops macOS's rounded
    // corners by making the window BORDERLESS — square, no titlebar, no traffic
    // lights. The tradeoff: no native titlebar drag (use data-tiny-drag) and
    // it's a deliberately un-native look; resize edges, shadow, and keyboard
    // focus are kept. acceptsFirstMouse:true makes the click that focuses an
    // unfocused window ALSO reach the page (default macOS behavior swallows it
    // — "click once to focus, again to act"); handy for palettes/toolbars, and
    // for DOM drag regions on unfocused windows. Declare it in tinyjs.json
    // "chrome" so it applies before the first paint (no rounded→square flash).
    setChrome(opts = {}) {
      const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
      const vib = opts.vibrancy === undefined ? ''
                : opts.vibrancy === null || opts.vibrancy === false ? 'none'
                : String(opts.vibrancy);
      send('CHROME ' + [bit(opts.frame), bit(opts.trafficLights),
                        bit(opts.transparent), one(vib), bit(opts.squareCorners),
                        bit(opts.acceptsFirstMouse)].join('\t'));
    },
    startDrag() { send('DRAGWIN'); },
    zoom() { send('WINOP zoom'); },
    // Native NSPasteboard — no osascript/pbpaste spawns, no scratch files.
    clipboard: {
      // { kind: 'files'|'image'|'color'|'text'|'empty', changeCount, text,
      //   html, paths, image (png temp path, valid until the clipboard
      //   changes again — copy it to keep it), imageSize ({ width, height }
      //   px), color ('#rrggbb[aa]'), concealed (password-manager marker —
      //   history apps must skip), sourceApp ({ name, bundleId } — frontmost
      //   when the change was noticed; exact while watch() runs), sourceURL
      //   (page a Chromium-browser copy came from) }
      read: () => query('clipboard'),
      async changeCount() { return (await query('clipboard:count'))?.changeCount ?? 0; },
      // { text?, html?, paths?, image?, color? } — image: png path, data:
      // URL, or base64. Multiple paths flush reliably (long-lived process).
      write({ text, html, image, color, paths } = {}) {
        send('CLIPWRITE ' + [esc(text), esc(html), esc(image), esc(color),
                             ...(paths ?? []).map(esc)].join('\t'));
        return true;
      },
      // Poll changeCount in the launcher (in-process, ~free); changes arrive
      // as the 'clipboard-change' event / onClipboardChange option with
      // { changeCount, self } — self: our own write() caused it.
      watch(intervalMs = 500) { send('CLIPWATCH ' + (intervalMs | 0)); },
      unwatch() { send('CLIPWATCH 0'); },
    },
    // Post a real CGEvent keystroke (combo like 'cmd+v') from the launcher —
    // one Accessibility grant that names your app, no osascript spawn.
    // -> { ok, trusted }; trusted:false means Accessibility isn't granted
    // (see app.permissions).
    keystroke: (combo) => ask('KEYSTROKE', one(combo)),
    // Paste into the frontmost app (hide your window first).
    paste: () => ask('KEYSTROKE', 'cmd+v'),
    // TCC permissions: 'accessibility' | 'screen' | 'notifications' |
    // 'automation[:<bundle-id>]'. check -> 'granted'|'denied'|'undetermined'|
    // 'unsupported'; request also prompts (accessibility opens System
    // Settings pointing at your app).
    permissions: {
      async check(name) { return (await ask('PERMCHK', one(name)))?.status ?? 'unsupported'; },
      async request(name) { return (await ask('PERMREQ', one(name)))?.status ?? 'unsupported'; },
    },
    // Global cursor position, same top-left coordinates as setPosition /
    // getState: { x, y, window: { x, y, inside }, screen: { x, y, width,
    // height, scale } } — window is relative to the main window's content
    // area (clientX/clientY units, even while the cursor is outside);
    // screen is the display the cursor is on.
    mousePosition: () => query('mouse'),
    // Every display, same top-left coordinates as setPosition / getState:
    // [{ id, name, x, y, width, height, visible: { x, y, width, height },
    //   scale, primary }] — visible excludes the menu bar and Dock; primary
    // is the menu-bar screen (the coordinate origin).
    screens: () => query('screens'),
    // NSWorkspace verbs — no `open` spawns. open() takes a URL (any scheme)
    // or a file path; reveal() shows the file in Finder; trash() moves it to
    // the Trash (recoverable — prefer it over tjs.remove for user files).
    // Resolve true; throw with the reason ('no such file', 'no application
    // registered for URL', …) on failure.
    shell: {
      open: (target) => shellOp('open', target),
      reveal: (path) => shellOp('reveal', path),
      trash: (path) => shellOp('trash', path),
    },
    // Launch at login (SMAppService — bundle mode on macOS 13+, otherwise
    // 'unsupported'; dev mode has no bundle identity to register). get() ->
    // 'enabled' | 'disabled' | 'requires-approval' | 'unsupported'. set(v)
    // returns the resulting status: 'requires-approval' means macOS wants
    // the user to allow it in System Settings > General > Login Items.
    launchAtLogin: {
      // Windows: the launcher needs the app's exe path for the HKCU Run key
      // (a dev run's tjs.exe is refused there — built apps only).
      async get() {
        const rest = 'get' + (IS_WIN ? '\t' + esc(tjs.exePath) : '');
        return (await ask('LOGIN', rest))?.status ?? 'unsupported';
      },
      async set(enabled) {
        const rest = 'set ' + (enabled ? 1 : 0) + (IS_WIN ? '\t' + esc(tjs.exePath) : '');
        const r = await ask('LOGIN', rest);
        if (r?.ok === false && r?.error) throw new Error(r.error);
        return r?.status ?? 'unsupported';
      },
    },
    // Dock tile. setBadge('3') shows a badge, setBadge('') clears it.
    // bounce() bounces the icon until the app activates; { critical: true }
    // keeps bouncing until the user acts.
    dock: {
      setBadge(text) { send('BADGE ' + esc(text)); return true; },
      bounce(opts) { send('BOUNCE ' + (opts?.critical ? 1 : 0)); return true; },
    },
    // Keep the system awake (one IOPMAssertion, replaced on each call —
    // it dies with the process, so a crashed app never wedges sleep,
    // unlike a spawned `caffeinate`). { display: true } also keeps the
    // screen on. The reason shows in `pmset -g assertions`.
    power: {
      async preventSleep(reason, opts) {
        const r = await ask('POWER', 'on\t' + (opts?.display ? 1 : 0) + '\t' + esc(reason));
        return r?.ok === true;
      },
      async allowSleep() { return (await ask('POWER', 'off'))?.ok === true; },
    },
    // The active app right now: { name, bundleId, pid } | null — who focus
    // returns to after hide() (pair with paste()).
    frontmostApp: () => query('frontmost'),
    // The text currently selected in the frontmost app (PopClip-style
    // popovers) — needs the Accessibility permission. null if nothing is
    // selected or the app doesn't expose it.
    selectedText: () => query('selectedtext'),
    // Every on-screen window of OTHER apps (Rectangle/Magnet territory):
    // [{ app, bundleId, pid, title, index, x, y, width, height }] in
    // top-left screen coords. Needs Accessibility; null if not granted.
    otherWindows: () => query('otherwindows'),
    // Move + resize another app's frontmost window (pid from otherWindows()
    // or frontmostApp()), top-left screen coords. Needs Accessibility;
    // resolves true or throws.
    async moveWindow(pid, { x, y, width, height } = {}) {
      const r = await ask('WINCTRL', [pid | 0, x | 0, y | 0, width | 0, height | 0].join('\t'));
      if (!r?.ok) throw new Error(r?.error ?? 'move failed');
      return true;
    },
    // System beep / a sound: system sound name ('Ping', 'Glass', …) or an
    // audio file path. Resolve false if the name/file didn't load.
    async beep() { return (await ask('SOUND'))?.ok === true; },
    async playSound(target) { return (await ask('SOUND', esc(target)))?.ok === true; },
    // Now Playing (Control Center / lock screen) + hardware media keys.
    // set({ title, artist, album, duration, elapsed, playing }) also arms
    // the media keys — presses arrive as the 'media-key' event / onMediaKey
    // option with { command: 'play'|'pause'|'toggle'|'next'|'previous'|
    // 'seek', time? }. clear() tears it down.
    nowPlaying: {
      set(info = {}) { send('NOWPLAYING ' + esc(JSON.stringify(info))); return true; },
      clear() { send('NOWPLAYING clear'); return true; },
    },
    // Speak text with a system voice (AVSpeechSynthesizer). voice: a voice
    // id from voices() or a BCP-47 language ('en-AU'); rate 0..1 (~0.5
    // default). Resolves when playback FINISHES (false if interrupted).
    async say(text, { voice, rate } = {}) {
      return (await ask('SAY', esc(String(text ?? '')) + '\t' + esc(voice ?? '') +
                        '\t' + (rate ?? 0)))?.ok === true;
    },
    stopSpeaking() { send('SAYSTOP'); return true; },
    // Installed voices: [{ id, name, lang, quality: 'default'|'enhanced'|
    // 'premium' }]. Enhanced/premium need a one-time download in System
    // Settings > Accessibility > Spoken Content.
    async voices() { return (await ask('VOICES'))?.voices ?? []; },
    // Seconds since the user's last input, session-wide — pause polling /
    // dim UI when they walk away.
    idleTime: async () => (await query('idle'))?.seconds ?? 0,
    // Quick Look (the Finder-spacebar preview panel, no qlmanage spawn).
    // Path or array of paths (arrow keys page through); quickLook() closes.
    quickLook(paths) {
      const list = paths == null ? [] : [].concat(paths);
      send('QUICKLOOK' + (list.length ? ' ' + list.map(esc).join('\t') : ''));
      return true;
    },
    // Screenshot a display (id from screens(); default primary) ->
    // { path (png in the temp dir — the caller owns it), width, height }.
    // Needs the 'screen' permission and macOS 14+; throws with the reason
    // otherwise.
    async captureScreen(screenId) {
      const r = await ask('CAPTURE', String(screenId ?? 0));
      if (!r?.ok) throw new Error(r?.error ?? 'capture failed');
      return { path: r.path, width: r.width, height: r.height };
    },
    // The system eyedropper (NSColorSampler) — works across every app and
    // screen, and needs NO screen-recording permission. Resolves '#rrggbb',
    // or null if the user cancels (esc).
    async pickColor() {
      const r = await ask('PICKCOLOR');
      if (!r?.ok) throw new Error(r?.error ?? 'unsupported');
      return r.color;
    },
    // On-device OCR (Vision, accurate mode) -> { text, blocks: [{ text,
    // confidence, box }] }; box is normalized 0..1, top-left origin.
    // Pairs with captureScreen() for screenshot-to-text.
    async ocr(path) {
      const r = await ask('OCR', esc(path));
      if (!r?.ok) throw new Error(r?.error ?? 'ocr failed');
      return { text: r.text, blocks: r.blocks };
    },
    // A thumbnail png for ANY file type Quick Look understands (PSD, video,
    // 3D models, …) -> { path (temp png, yours), width, height }. size is
    // the bounding box in points (rendered @2x).
    async thumbnail(path, size = 256) {
      const r = await ask('THUMB', esc(path) + '\t' + (size | 0));
      if (!r?.ok) throw new Error(r?.error ?? 'no thumbnail');
      return { path: r.path, width: r.width, height: r.height };
    },
    // Keychain-backed secrets (generic passwords under the app id) — the
    // keytar/safeStorage role. Values survive reinstalls; never store
    // tokens in tiny.store when this exists.
    secrets: {
      async get(key) {
        const r = await ask('SECRET', 'get\t' + esc(id || 'tinyjs-app') + '\t' + esc(key));
        if (!r?.ok) throw new Error(r?.error ?? 'keychain error');
        return r.value ?? null;
      },
      async set(key, value) {
        const r = await ask('SECRET', 'set\t' + esc(id || 'tinyjs-app') + '\t' + esc(key) + '\t' + esc(String(value)));
        if (!r?.ok) throw new Error(r?.error ?? 'keychain error');
        return true;
      },
      async delete(key) {
        const r = await ask('SECRET', 'del\t' + esc(id || 'tinyjs-app') + '\t' + esc(key));
        if (!r?.ok) throw new Error(r?.error ?? 'keychain error');
        return true;
      },
    },
    // Touch ID (or the account-password sheet on Macs without it) — "the
    // user proved it's them". Resolves true/false; false covers cancel.
    async authenticate(reason) {
      return (await ask('AUTH', esc(reason ?? 'authenticate')))?.ok === true;
    },
    // Record a display to an .mp4 (SCStream → AVAssetWriter). start()
    // resolves once capture is running; stop() resolves { path, duration }
    // once the file is finalized. Needs the 'screen' permission + macOS 14;
    // rejects with the reason. Video only (no audio track yet). One
    // recording at a time.
    recorder: {
      async start({ screenId, path } = {}) {
        if (!path) throw new Error('recorder.start needs a { path }');
        const r = await ask('RECORD', 'start ' + (screenId ?? 0) + '\t' + esc(path));
        if (!r?.ok) throw new Error(r?.error ?? 'record failed');
        return true;
      },
      async stop() {
        const r = await ask('RECORD', 'stop');
        if (!r?.ok) throw new Error(r?.error ?? 'record failed');
        return { path: r.path, duration: r.duration };
      },
    },
    // Run AppleScript in-process (no osascript spawn) — Apple Events hit
    // the same 'automation' TCC permissions.check('automation:…') covers.
    // Resolves the script result as a string (null if it isn't text);
    // throws with the script error message.
    async applescript(source) {
      const r = await ask('OSA', esc(source));
      if (!r?.ok) throw new Error(r?.error ?? 'script error');
      return r.result ?? null;
    },
    // Standard per-app directories (data/cache/logs are per app id, not
    // auto-created — tjs.makeDir(..., { recursive: true }) first write).
    // Prefer these over hardcoding ~/Library paths.
    paths: IS_WIN
      ? {
          home: tjs.homeDir,
          data: appDataDir(id),
          cache: (tjs.env.LOCALAPPDATA || tjs.homeDir + '/AppData/Local') + '/' + (id || 'tinyjs-app') + '/Cache',
          logs: (tjs.env.LOCALAPPDATA || tjs.homeDir + '/AppData/Local') + '/' + (id || 'tinyjs-app') + '/Logs',
          temp: tjs.tmpDir,
          downloads: tjs.homeDir + '/Downloads',
          desktop: tjs.homeDir + '/Desktop',
          documents: tjs.homeDir + '/Documents',
        }
      : IS_LINUX
      ? {
          home: tjs.homeDir,
          data: appDataDir(id),
          cache: (tjs.env.XDG_CACHE_HOME || tjs.homeDir + '/.cache') + '/' + (id || 'tinyjs-app'),
          logs: (tjs.env.XDG_STATE_HOME || tjs.homeDir + '/.local/state') + '/' + (id || 'tinyjs-app'),
          temp: tjs.tmpDir,
          downloads: tjs.homeDir + '/Downloads',
          desktop: tjs.homeDir + '/Desktop',
          documents: tjs.homeDir + '/Documents',
        }
      : {
          home: tjs.homeDir,
          data: tjs.homeDir + '/Library/Application Support/' + (id || 'tinyjs-app'),
          cache: tjs.homeDir + '/Library/Caches/' + (id || 'tinyjs-app'),
          logs: tjs.homeDir + '/Library/Logs/' + (id || 'tinyjs-app'),
          temp: tjs.tmpDir,
          downloads: tjs.homeDir + '/Downloads',
          desktop: tjs.homeDir + '/Desktop',
          documents: tjs.homeDir + '/Documents',
        },
    // Raw launcher read-back (debug/test surface; the page twin is the
    // 'debug.get' builtin).
    debug: (what) => query(String(what)),
    // Persistent settings (see makeStore).
    store: makeStore(id),
    // System-wide hotkeys; combos like 'cmd+shift+k'. Presses arrive as a
    // 'hotkey' page event and via the onHotkey option.
    hotkey: {
      register(hid, combo) { send('HKREG ' + one(hid) + '\t' + one(combo)); },
      unregister(hid) { send('HKUNREG ' + one(hid)); },
    },
    // Replace the right-click menu: [{ id, label } | { separator: true }].
    // null/empty restores WebKit's default menu. Clicks: 'contextmenu' event.
    setContextMenu(items) {
      if (!items || !items.length) { send('CTXCLEAR'); return; }
      send('CTXBEGIN');
      sendItems(items);
      send('CTXEND');
    },
    quit() { send('QUIT'); },
    // --- multi-window ---------------------------------------------------
    // Open (or focus) a secondary window. `page` is an html file in your
    // frontend dir (e.g. 'settings.html') or an absolute path. Each window
    // runs the same tiny.* bridge; win.* calls from its page target itself.
    // chrome ({ frame?, trafficLights?, transparent?, vibrancy?,
    // squareCorners?, acceptsFirstMouse? }) and position ({ x, y }) are applied
    // BEFORE the window paints — no titlebar flash for frameless panels, no
    // jump from center.
    openWindow(id, { page, title, size, chrome, x, y } = {}) {
      let p = String(page ?? 'index.html');
      if (!isUrl(p) && !isAbs(p)) {
        if (!frontendDir) throw new Error('win.open needs an absolute page path or URL here');
        p = frontendDir + '/' + p;
      }
      const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
      const c = chrome ?? {};
      const vib = c.vibrancy === undefined ? ''
                : c.vibrancy === null || c.vibrancy === false ? 'none'
                : String(c.vibrancy);
      const hasPos = x != null && y != null;
      send('WINOPEN ' + [one(id), one(p), one(title ?? id), one(size ?? '600x400'),
                         bit(c.frame), bit(c.trafficLights), bit(c.transparent), one(vib),
                         bit(c.squareCorners), bit(c.acceptsFirstMouse),
                         hasPos ? (x | 0) : '', hasPos ? (y | 0) : ''].join('\t'));
    },
    // Handle for any window ('main' or a secondary id).
    window(id) {
      const t = (cmd, rest) => send(id === 'main'
        ? cmd + (rest != null ? ' ' + rest : '')
        : cmd + '@' + id + (rest != null ? ' ' + rest : ''));
      return {
        eval: (js) => t('EVAL', esc(js)),
        push: (event, data) =>
          t('EVAL', esc('window.__emit && window.__emit(' + JSON.stringify({ event, data }) + ')')),
        close: () => { if (id !== 'main') send('WINCLOSE ' + id); },
        setTitle: (v) => t('TITLE', String(v).replace(/\n/g, ' ')),
        setSize: (w2, h2) => t('SIZE', `${w2 | 0} ${h2 | 0}`),
        setPosition: (x, y) => t('WINOP', `pos ${x | 0} ${y | 0}`),
        center: () => t('WINOP', 'center'),
        hide: () => t('WINOP', 'hide'),
        show: (opts) => t('WINOP', 'show' + (opts?.activate === false ? ' 0' : '')),
        minimize: () => t('WINOP', 'minimize'),
        restore: () => t('WINOP', 'restore'),
        zoom: () => t('WINOP', 'zoom'),
        fullscreen: () => t('WINOP', 'fullscreen'),
        setFullscreen: (v) => t('WINOP', 'fullscreen ' + (v ? 1 : 0)),
        setAlwaysOnTop: (v) => t('WINOP', 'ontop ' + (v ? 1 : 0)),
        setResizable: (v) => t('WINOP', 'resizable ' + (v ? 1 : 0)),
        setClickThrough: (v) => t('WINOP', 'clickthrough ' + (v ? 1 : 0)),
        setLevel: (level) => t('WINOP', 'level ' + one(level ?? 'normal')),
        setAllSpaces: (v) => t('WINOP', 'allspaces ' + (v ? 1 : 0)),
        setChrome(opts = {}) {
          const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
          const vib = opts.vibrancy === undefined ? ''
                    : opts.vibrancy === null || opts.vibrancy === false ? 'none'
                    : String(opts.vibrancy);
          t('CHROME', [bit(opts.frame), bit(opts.trafficLights),
                       bit(opts.transparent), one(vib), bit(opts.squareCorners),
                       bit(opts.acceptsFirstMouse)].join('\t'));
        },
        getState: () => query(id === 'main' ? 'win' : 'win:' + id),
        // Native share sheet ({ text?, url?, paths? }) anchored at page
        // coordinates (pass the click's clientX/clientY; 0,0 otherwise).
        share({ text, url, paths, x, y } = {}) {
          t('SHARE', [(x | 0) + '', (y | 0) + '', esc(text), esc(url),
                      ...(paths ?? []).map(esc)].join('\t'));
          return true;
        },
      };
    },
    windows: () => query('windows'),
    // { version: <app>, tinyjs: <framework that built it>, runtime: <txiki> }
    info: { version, tinyjs: tinyjsVersion, runtime: 'txiki.js ' + tjs.version },
    // Auto-update (tinyjs.json "update": { "url": "https://…/manifest.json" }).
    // check() -> { available, current, latest, notes }; install() downloads,
    // verifies, swaps the .app, relaunches the new version, and quits this
    // instance. "auto": "launch" | "daily" checks in the background (packaged
    // apps only) and fires the 'update-available' page event /
    // onUpdateAvailable export with { current, latest, notes } — wire your
    // own prompt, then update.install().
    update: {
      check: () => checkForUpdate({ url: update?.url, version }),
      async install() {
        const bundle = await installUpdate({ url: update?.url, version });
        relaunch(bundle);
        setTimeout(() => app.quit(), 250);
        return true;
      },
    },
    done: null, // filled below
  };

  // Reserved methods every tinyjs exposes; user API is merged on top but
  // cannot shadow the win.* namespace.
  // tiny.fetch — run WHATWG fetch in the backend (a native process, so no
  // CORS/CSP/mixed-content and no browser Origin) and hand the result to the
  // page. Small responses come back whole (base64 in the RET); with
  // { stream: true } the body stays open and the page pulls it chunk-by-chunk
  // (fetch.pull), so an endless source (internet radio) streams with natural
  // backpressure and never buffers unbounded. Keyed by a page-supplied id and
  // cancelled by fetch.cancel or when the owner window closes.
  const fetchStreams = new Map(); // id -> { reader, win }
  // tiny.audioTap: one native tap per app; `audioTapOwner` is the window that
  // started it, so closing that window tears the tap down (like fetchStreams).
  let audioTapOwner = null;
  function stopAudioTap() {
    if (!audioTapOwner) return;
    send('AUDIOTAP STOP');
    audioTapOwner = null;
  }
  const u8ToB64 = (u8) => {
    let s = '';
    for (let i = 0; i < u8.length; i += 0x8000)
      s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return btoa(s);
  };
  const b64ToU8 = (str) => {
    const bin = atob(str);
    const u8 = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
    return u8;
  };
  function cancelFetchStream(id) {
    const s = fetchStreams.get(id);
    if (!s) return;
    fetchStreams.delete(id);
    try { s.reader.cancel(); } catch {}
  }
  async function doFetch(p, _a, m) {
    const init = {};
    if (p.method) init.method = p.method;
    if (p.headers) init.headers = p.headers;
    if (p.redirect) init.redirect = p.redirect;
    if (p.bodyText != null) init.body = p.bodyText;
    else if (p.bodyB64 != null) init.body = b64ToU8(p.bodyB64);
    const res = await globalThis.fetch(String(p.url), init);
    const headers = {};
    res.headers.forEach((v, k) => { headers[k] = v; });
    const head = {
      ok: res.ok, status: res.status, statusText: res.statusText,
      url: res.url ?? '', redirected: !!res.redirected, headers,
    };
    if (!p.stream) return { ...head, bodyB64: u8ToB64(new Uint8Array(await res.arrayBuffer())) };
    // The page pulls chunks on demand; keep the reader alive under its id.
    fetchStreams.set(p.id, { reader: res.body.getReader(), win: m?.window || 'main' });
    return { ...head, streaming: true };
  }
  async function pullFetchStream({ id }) {
    const s = fetchStreams.get(id);
    if (!s) return { done: true };
    let r;
    try {
      r = await s.reader.read();
    } catch (e) {
      fetchStreams.delete(id);
      throw e; // surfaces as an error on the page's ReadableStream
    }
    if (r.done) { fetchStreams.delete(id); return { done: true }; }
    return { done: false, bodyB64: u8ToB64(r.value) };
  }

  const builtins = {
    fetch: doFetch,
    'fetch.pull': pullFetchStream,
    'fetch.cancel': async ({ id }) => (cancelFetchStream(id), true),
    ping: async () => 'pong',
    log: async ({ msg }) => (console.log('[web]', msg), true),
    quit: async () => (app.quit(), true),
    // win.* calls target the window the page lives in.
    'win.setTitle': async ({ title: t }, _a, m) => (forWin(m).setTitle(t), true),
    'win.setSize': async ({ width, height }, _a, m) => (forWin(m).setSize(width, height), true),
    'win.hide': async (_p, _a, m) => (forWin(m).hide(), true),
    'win.show': async (p, _a, m) => (forWin(m).show(p), true),
    'win.center': async (_p, _a, m) => (forWin(m).center(), true),
    'win.minimize': async (_p, _a, m) => (forWin(m).minimize(), true),
    'win.fullscreen': async (_p, _a, m) => (forWin(m).fullscreen(), true),
    'win.setAlwaysOnTop': async ({ enabled }, _a, m) => (forWin(m).setAlwaysOnTop(enabled), true),
    'win.setResizable': async ({ enabled }, _a, m) => (forWin(m).setResizable(enabled), true),
    'win.setClickThrough': async ({ enabled }, _a, m) => (forWin(m).setClickThrough(enabled), true),
    'win.setLevel': async ({ level }, _a, m) => (forWin(m).setLevel(level), true),
    'win.setAllSpaces': async ({ enabled }, _a, m) => (forWin(m).setAllSpaces(enabled), true),
    'app.selectedText': async () => app.selectedText(),
    'app.otherWindows': async () => app.otherWindows(),
    'app.moveWindow': async ({ pid, ...rect }) => app.moveWindow(pid, rect),
    'tray.position': async () => app.tray.position(),
    'win.printToPDF': async ({ path }) => app.printToPDF(path),
    'app.haptic': async ({ pattern }) => app.haptic(pattern),
    'app.dockIcon': async ({ path }) => app.dockIcon(path),
    'app.battery': async () => app.battery(),
    'app.wifi': async () => app.wifi(),
    'app.spotlight': async ({ query: q }) => app.spotlight(q),
    'ai.availability': async () => app.ai.availability(),
    'ai.generate': async ({ prompt, instructions }) => app.ai.generate(prompt, { instructions }),
    'win.setPosition': async ({ x, y }, _a, m) => (forWin(m).setPosition(x, y), true),
    'win.open': async ({ id: wid, ...opts }) => (app.openWindow(wid, opts), true),
    'win.close': async ({ id: wid }, _a, m) => {
      const target = wid ?? m?.window ?? 'main';
      if (target === 'main') app.quit();
      else app.window(target).close();
      return true;
    },
    'win.windows': async () => app.windows(),
    'win.setHideOnClose': async ({ enabled }) => (app.setHideOnClose(enabled), true),
    'notify': async (params) => notify(params),
    'app.setDockVisible': async ({ visible }) => (app.setDockVisible(visible), true),
    'menu.set': async ({ menus }) => (app.setMenu(menus), true),
    'tray.set': async (spec) => (app.tray.set(spec), true),
    'tray.remove': async () => (app.tray.remove(), true),
    'update.check': async () => {
      const { available, current, latest, notes } = await app.update.check();
      return { available, current, latest, notes };
    },
    'update.install': async () => app.update.install(),
    'win.print': async () => (app.print(), true),
    'store.get': async ({ key }) => app.store.get(key),
    'store.set': async ({ key, value }) => app.store.set(key, value),
    'store.delete': async ({ key }) => app.store.delete(key),
    'store.all': async () => app.store.all(),
    'hotkey.register': async ({ id: hid, combo }) => (app.hotkey.register(hid, combo), true),
    'hotkey.unregister': async ({ id: hid }) => (app.hotkey.unregister(hid), true),
    // Read the app's (or system's) rendered audio output as PCM chunks
    // ('audio-tap' events). Gated by the "audioTap" manifest key; the requested
    // scope must be covered by the declared one.
    'audioTap.start': async ({ scope = 'app', excludeSelf = false, interval = 80 } = {}, _a, m) => {
      if (!audioTap)
        return { ok: false, code: 'not-declared',
                 message: 'add "audioTap": "app" | "system" to tinyjs.json' };
      if (scope === 'system' && audioTap !== 'system')
        return { ok: false, code: 'not-declared',
                 message: 'system scope requires "audioTap": "system" in tinyjs.json' };
      const iv = Math.max(20, Math.min(500, interval | 0));
      const r = await ask('AUDIOTAP', scope + '\t' + (excludeSelf ? 1 : 0) + '\t' + iv);
      if (r && r.ok) audioTapOwner = m?.window || 'main';
      return r ?? { ok: false, code: 'failed' };
    },
    'audioTap.stop': async () => (stopAudioTap(), true),
    'menu.setContext': async ({ items }) => (app.setContextMenu(items), true),
    'menu.update': async ({ id: mid, ...patch }) => (app.updateMenuItem(mid, patch), true),
    'menu.get': async ({ id: mid }) => app.getMenuItem(mid),
    'win.getState': async (_p, _a, m) => forWin(m).getState(),
    'debug.get': async ({ what }) => query(String(what)),
    'win.restore': async (_p, _a, m) => (forWin(m).restore(), true),
    'win.setFullscreen': async ({ enabled }, _a, m) => (forWin(m).setFullscreen(enabled), true),
    'win.setChrome': async (opts, _a, m) => (forWin(m).setChrome(opts), true),
    'win.startDrag': async () => (app.startDrag(), true),
    'win.zoom': async (_p, _a, m) => (forWin(m).zoom(), true),
    // Drag files OUT of the window (page must call this from a mousedown,
    // while the button is still held). files: real paths; image: optional
    // custom drag-image png (file icons otherwise).
    'win.dragOut': async ({ files, paths, image }, _a, m) => {
      const list = files ?? paths ?? [];
      const win = m?.window || 'main';
      send((win === 'main' ? 'DRAGOUT' : 'DRAGOUT@' + win) + ' ' +
           [esc(image), ...list.map(esc)].join('\t'));
      return true;
    },
    'clip.read': async () => app.clipboard.read(),
    'clip.write': async (data) => app.clipboard.write(data),
    'clip.changeCount': async () => app.clipboard.changeCount(),
    'clip.watch': async ({ intervalMs }) => (app.clipboard.watch(intervalMs ?? 500), true),
    'clip.unwatch': async () => (app.clipboard.unwatch(), true),
    'app.keystroke': async ({ combo }) => app.keystroke(combo),
    'app.paste': async () => app.paste(),
    'perm.check': async ({ name }) => app.permissions.check(name),
    'perm.request': async ({ name }) => app.permissions.request(name),
    // Pages get `window` relative to their own window.
    'app.mouse': async (_p, _a, m) => {
      const win = m?.window || 'main';
      return query(win === 'main' ? 'mouse' : 'mouse:' + win);
    },
    'theme.get': async () => lastTheme,
    'app.info': async () => app.info,
    'system.info': async () => systemInfo(),
    'system.capabilities': async () => systemCapabilities(),
    'system.requirements': async ({ ids } = {}) => systemRequirements(ids),
    'app.screens': async () => app.screens(),
    'app.paths': async () => app.paths,
    'shell.open': async ({ target }) => app.shell.open(target),
    'shell.reveal': async ({ path }) => app.shell.reveal(path),
    'shell.trash': async ({ path }) => app.shell.trash(path),
    'login.get': async () => app.launchAtLogin.get(),
    'login.set': async ({ enabled }) => app.launchAtLogin.set(enabled),
    'dock.setBadge': async ({ text }) => app.dock.setBadge(text ?? ''),
    'dock.bounce': async (p) => app.dock.bounce(p),
    'power.prevent': async ({ reason, display }) => app.power.preventSleep(reason, { display }),
    'power.allow': async () => app.power.allowSleep(),
    'app.frontmost': async () => app.frontmostApp(),
    'sound.play': async ({ target }) => (target ? app.playSound(target) : app.beep()),
    'win.share': async (p, _a, m) => forWin(m).share(p),
    'app.idleTime': async () => app.idleTime(),
    'app.quickLook': async ({ paths }) => app.quickLook(paths),
    'app.captureScreen': async ({ screenId }) => app.captureScreen(screenId),
    'app.pickColor': async () => app.pickColor(),
    'app.ocr': async ({ path }) => app.ocr(path),
    'app.thumbnail': async ({ path, size }) => app.thumbnail(path, size ?? 256),
    'secrets.get': async ({ key }) => app.secrets.get(key),
    'secrets.set': async ({ key, value }) => app.secrets.set(key, value),
    'secrets.delete': async ({ key }) => app.secrets.delete(key),
    'app.authenticate': async ({ reason }) => app.authenticate(reason),
    'app.applescript': async ({ source }) => app.applescript(source),
    'record.start': async (opts) => app.recorder.start(opts),
    'record.stop': async () => app.recorder.stop(),
    'nowplaying.set': async (info) => app.nowPlaying.set(info),
    'nowplaying.clear': async () => app.nowPlaying.clear(),
    'app.say': async ({ text, voice, rate }) => app.say(text, { voice, rate }),
    'app.stopSpeaking': async () => app.stopSpeaking(),
    'app.voices': async () => app.voices(),
  };
  const forWin = (m) => app.window(m?.window || 'main');
  const methods = { ...api, ...builtins };
  let lastTheme = null; // { dark } once the launcher reports it (at startup)
  let markEof;
  const eofDone = new Promise((r) => { markEof = () => r({ exit_status: 0 }); });

  async function handleCall(line) {
    const sp = line.indexOf(' ', 5);
    const id = line.slice(5, sp);
    // Call ids are "<windowId>:<seq>" — routing lives inside the id, so
    // RET lines need no changes and handlers learn who called.
    const callerWin = id.includes(':') ? id.slice(0, id.indexOf(':')) : 'main';
    let status = 0;
    let result;
    try {
      // Launcher forwards the bound call's argument array: ["<payload>"]
      const [payload] = JSON.parse(line.slice(sp + 1));
      const { method, params } = JSON.parse(payload);

      // Native dialogs: hand the call id to the launcher; it runs the panel
      // on the UI thread and resolves the page's promise itself.
      const dlg = DIALOG_OPS[method];
      if (dlg) {
        send(`DLG ${id} ${[dlg.op, ...dlg.args(params ?? {})].join('\t')}`);
        return;
      }

      const fn = methods[method];
      if (!fn) throw new Error('unknown method: ' + method);
      result = await fn(params ?? {}, app, { window: callerWin });
    } catch (e) {
      status = 1;
      result = String((e && e.message) || e);
    }
    send(`RET ${id} ${status} ${JSON.stringify(result === undefined ? null : result)}`);
  }

  (async () => {
    const reader = readable.getReader();
    let buf = '';
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i);
        buf = buf.slice(i + 1);
        dbg('<<', line);
        if (line.startsWith('CALL ')) handleCall(line);
        else if (line.startsWith('MENU ')) {
          const id = line.slice(5);
          push('menu', { id });
          if (onMenu) onMenu(id, app);
        } else if (line.startsWith('TRAY ')) {
          const id = line.slice(5);
          push('tray', { id });
          if (onTray) onTray(id, app);
        } else if (line === 'TRAYCLICK') {
          push('trayclick', {});
          if (onTray) onTray(null, app);
        } else if (line.startsWith('DROP ')) {
          // Files dragged onto the window; real filesystem paths.
          try { push('drop', { paths: JSON.parse(line.slice(5)) }); } catch {}
        } else if (line.startsWith('HOTKEY ')) {
          const id = line.slice(7);
          push('hotkey', { id });
          if (onHotkey) onHotkey(id, app);
        } else if (line.startsWith('AUDIOTAP ')) {
          // A tap PCM chunk: AUDIOTAP <pcmB64>\t<sampleRate>\t<channels>\t<frames>\t<t>
          const [pcm, sr, ch, frames, t] = line.slice(9).split('\t');
          const chunk = { pcm, sampleRate: +sr, channels: +ch, frames: +frames, t: +t };
          push('audio-tap', chunk);
          if (onAudioTap) onAudioTap(chunk, app);
        } else if (line.startsWith('CTX ')) {
          const id = line.slice(4);
          push('contextmenu', { id });
          if (onContextMenu) onContextMenu(id, app);
        } else if (line.startsWith('SYS ')) {
          const [kind, value] = line.slice(4).split(' ');
          if (kind === 'theme') {
            lastTheme = { dark: value === 'dark' };
            push('theme', lastTheme);
          } else {
            push(kind, {}); // 'sleep' | 'wake'
          }
          if (onSystem) onSystem(kind, value ?? null, app);
        } else if (line.startsWith('GOT ')) {
          const sp = line.indexOf(' ', 4);
          const resolve = pendingGets.get(line.slice(4, sp));
          if (resolve) {
            pendingGets.delete(line.slice(4, sp));
            let v = null;
            try { v = JSON.parse(line.slice(sp + 1)); } catch {}
            resolve(v);
          }
        } else if (line.startsWith('CLIPCHANGE ')) {
          const [count, selfFlag] = line.slice(11).split(' ');
          const info = { changeCount: parseInt(count, 10), self: selfFlag === '1' };
          push('clipboard-change', info);
          if (onClipboardChange) onClipboardChange(info, app);
        } else if (line.startsWith('WINCLOSED ')) {
          const id = line.slice(10);
          // Tear down any streaming tiny.fetch the closed window still owns.
          for (const [sid, s] of fetchStreams) if (s.win === id) cancelFetchStream(sid);
          // …and the audio tap, if this was the window that started it.
          if (audioTapOwner === id) stopAudioTap();
          push('window-closed', { id });
          if (onWindowClosed) onWindowClosed(id, app);
        } else if (line.startsWith('NOTIFYCLICK ')) {
          const id = line.slice(12);
          push('notification-click', { id });
          if (onNotificationClick) onNotificationClick(id, app);
        } else if (line.startsWith('NOTIFYACTION ')) {
          const [id, action, reply] = line.slice(13).split('\t');
          const info = { id, action, reply: unesc(reply ?? '') };
          push('notification-action', info);
          if (onNotificationAction) onNotificationAction(info, app);
        } else if (line.startsWith('MEDIAKEY ')) {
          const [command, secs] = line.slice(9).split('\t');
          const info = { command, time: secs != null ? +secs : undefined };
          push('media-key', info);
          if (onMediaKey) onMediaKey(info, app);
        } else if (line.startsWith('OPENURL ')) {
          // Deep link (custom URL scheme; packaged .app only).
          const url = line.slice(8);
          push('open-url', { url });
          if (onOpenUrl) onOpenUrl(url, app);
        } else if (line.startsWith('OPENFILES ')) {
          // "Open With" / Dock drop / file association (packaged .app only).
          try {
            const paths = JSON.parse(line.slice(10));
            push('open-files', { paths });
            if (onOpenFiles) onOpenFiles(paths, app);
          } catch {}
        }
      }
    }
    markEof();
  })().catch((e) => { console.log('tinyjs read loop error:', e); markEof(); });

  // Attach mode has no child process: done = the launcher closing the socket.
  app.done = proc
    ? proc.wait().then(async (st) => {
        await cleanup();
        return st;
      })
    : eofDone;

  // Attach mode: the launcher booted with plist defaults; apply the app's
  // configured title/size now.
  if (attachPath) {
    app.setTitle(title);
    const [w, h] = String(size).split('x').map((n) => parseInt(n, 10));
    if (w && h) app.setSize(w, h);
  }
  // Chrome from tinyjs.json. Packaged apps already applied it from the plist
  // (flash-free); re-sending is idempotent. Dev applies it here.
  if (chrome && !attachPath) app.setChrome(chrome);

  // contextMenu:false in the manifest suppresses WebKit's default right-click
  // menu (Reload/Back/Inspect Element…). A custom setContextMenu() still wins.
  if (contextMenu === false) send('CTXSUPPRESS 1');

  // A clipboard handler implies watching; apps needing a custom interval can
  // call app.clipboard.watch(ms) on top (idempotent).
  if (onClipboardChange) app.clipboard.watch();

  // --- Windows/Linux: single instance + deep links / file associations -----
  // Built apps only (macOS gets all of this from LaunchServices + the plist).
  // The app listens on \\.\pipe\tinyjs-app-<id> (Windows) or
  // $XDG_RUNTIME_DIR/tinyjs-app-<id>.sock (Linux); `launcher --open` (the
  // registered protocol/extension handler) forwards URLs and file paths
  // over it, starting the app first when needed. A second direct launch of
  // the exe detects the pipe, activates the running instance, and exits.
  if ((IS_WIN || IS_LINUX) && bundlePath()) {
    const instPipe = IS_WIN
      ? '\\\\.\\pipe\\tinyjs-app-' + (id || 'tinyjs-app')
      : (tjs.env.XDG_RUNTIME_DIR || tjs.tmpDir) + '/tinyjs-app-' + (id || 'tinyjs-app') + '.sock';
    let haveInstancePipe = false;
    try {
      const conn = await tjs.connect('pipe', instPipe);
      const { writable } = await conn.opened;
      const w = writable.getWriter();
      await w.write(enc.encode('{"activate":true}\n'));
      tjs.exit(0); // another instance owns the app — hand over
    } catch {}
    // A unix socket left by a crashed instance blocks listen() — nothing
    // answered above, so it's stale; clear it. (Windows pipes need no cleanup.)
    if (IS_LINUX) await tjs.remove(instPipe).catch(() => {});
    try {
      const srv = await tjs.listen('pipe', instPipe);
      const srvInfo = await srv.opened;
      haveInstancePipe = true;
      (async () => {
        const acceptReader = srvInfo.readable.getReader();
        for (;;) {
          const { value: sock, done } = await acceptReader.read();
          if (done) break;
          (async () => {
            const { readable: r } = await sock.opened;
            const rd = r.getReader();
            let buf = '';
            for (;;) {
              const { value, done: d } = await rd.read();
              if (d) break;
              buf += dec.decode(value, { stream: true });
            }
            for (const line of buf.split('\n')) {
              if (!line.trim()) continue;
              let msg = null;
              try { msg = JSON.parse(line); } catch { continue; }
              app.show();
              if (msg.url) {
                push('open-url', { url: msg.url });
                if (onOpenUrl) onOpenUrl(msg.url, app);
              } else if (msg.paths?.length) {
                push('open-files', { paths: msg.paths });
                if (onOpenFiles) onOpenFiles(msg.paths, app);
              }
            }
          })().catch(() => {});
        }
      })();
    } catch {}

    // --- Linux registration: a .desktop entry (app menu, window icon
    // matching via StartupWMClass) written on first run — idempotent file
    // writes under $XDG_DATA_HOME, no root. urlScheme becomes an
    // x-scheme-handler MimeType (+ xdg-mime default); fileExtensions get
    // glob'd mime types via a shared-mime-info package. Failures are silent:
    // a sandboxed/odd session still runs the app, just unregistered.
    if (IS_LINUX && haveInstancePipe) {
      try {
        const dataHome = tjs.env.XDG_DATA_HOME || tjs.homeDir + '/.local/share';
        const appIdStr = id || 'tinyjs-app';
        const openCmd = '"' + exeDir + 'launcher" --open ' + instPipe + ' "' + tjs.exePath + '" %u';
        let iconLine = '';
        if (await exists(exeDir + 'icon.png')) iconLine = 'Icon=' + exeDir + 'icon.png\n';
        else if (pagePath && (await exists(dirOf(dirOf(pagePath)) + '/icon.png'))) {
          iconLine = 'Icon=' + dirOf(dirOf(pagePath)) + '/icon.png\n';
        }
        const mimes = [];
        for (const scheme of urlScheme ? [].concat(urlScheme) : []) {
          mimes.push('x-scheme-handler/' + scheme);
        }
        if (fileExtensions?.length) {
          const safeId = appIdStr.toLowerCase().replace(/[^a-z0-9.-]/g, '-');
          let xml = '<?xml version="1.0" encoding="UTF-8"?>\n' +
            '<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">\n';
          for (const ext of fileExtensions) {
            const e = String(ext).replace(/^\./, '');
            const mt = 'application/x-' + safeId + '-' + e.toLowerCase();
            mimes.push(mt);
            xml += `  <mime-type type="${mt}"><comment>${title} document</comment><glob pattern="*.${e}"/></mime-type>\n`;
          }
          xml += '</mime-info>\n';
          await tjs.makeDir(dataHome + '/mime/packages', { recursive: true });
          await tjs.writeFile(dataHome + '/mime/packages/' + safeId + '.xml', enc.encode(xml));
          tjs.spawn(['update-mime-database', dataHome + '/mime'],
                    { stdout: 'ignore', stderr: 'ignore' }).wait().catch(() => {});
        }
        const desktop = '[Desktop Entry]\nType=Application\nName=' + title +
          '\nExec=' + openCmd + '\n' + iconLine +
          'Terminal=false\nStartupWMClass=' + appIdStr + '\n' +
          (mimes.length ? 'MimeType=' + mimes.join(';') + ';\n' : '');
        await tjs.makeDir(dataHome + '/applications', { recursive: true });
        await tjs.writeFile(dataHome + '/applications/' + appIdStr + '.desktop', enc.encode(desktop));
        tjs.spawn(['update-desktop-database', dataHome + '/applications'],
                  { stdout: 'ignore', stderr: 'ignore' }).wait().catch(() => {});
        for (const scheme of urlScheme ? [].concat(urlScheme) : []) {
          tjs.spawn(['xdg-mime', 'default', appIdStr + '.desktop', 'x-scheme-handler/' + scheme],
                    { stdout: 'ignore', stderr: 'ignore' }).wait().catch(() => {});
        }
      } catch {}
    }

    // Registration (idempotent HKCU writes; no admin). The handler command
    // is the launcher's --open mode pointing at this exe + instance pipe.
    if (IS_WIN && haveInstancePipe && (urlScheme || fileExtensions?.length)) {
      const launcherExe = exeDir + 'launcher.exe';
      const openCmd = '"' + launcherExe + '" --open ' + instPipe + ' "' + tjs.exePath + '" "%1"';
      const reg = (args) => tjs.spawn(hiddenArgv(['reg', 'add', ...args, '/f']),
        { stdout: 'ignore', stderr: 'ignore' }).wait().catch(() => {});
      const CLS = 'HKCU\\Software\\Classes\\';
      for (const scheme of urlScheme ? [].concat(urlScheme) : []) {
        await reg([CLS + scheme, '/ve', '/d', 'URL:' + title]);
        await reg([CLS + scheme, '/v', 'URL Protocol', '/d', '']);
        await reg([CLS + scheme + '\\DefaultIcon', '/ve', '/d', tjs.exePath + ',0']);
        await reg([CLS + scheme + '\\shell\\open\\command', '/ve', '/d', openCmd]);
      }
      if (fileExtensions?.length) {
        const progid = 'tinyjs.' + (id || 'tinyjs-app');
        await reg([CLS + progid, '/ve', '/d', title]);
        await reg([CLS + progid + '\\DefaultIcon', '/ve', '/d', tjs.exePath + ',0']);
        await reg([CLS + progid + '\\shell\\open\\command', '/ve', '/d', openCmd]);
        for (const ext of fileExtensions) {
          await reg([CLS + '.' + String(ext).replace(/^\./, '') + '\\OpenWithProgIds',
                     '/v', progid, '/d', '']);
        }
      }
    }
  }

  // Background update checks ("update": { "auto": "launch" | "daily" }).
  // Packaged apps only — dev processes have no bundle to update, and their
  // 0.0.0 version would flag every manifest as "available". Failures are
  // silent (offline is normal); the app decides the prompt UX.
  const auto = update?.auto;
  if ((auto === 'launch' || auto === 'daily') && update?.url && bundlePath()) {
    const autoCheck = async () => {
      try {
        const r = await app.update.check();
        if (!r.available) return;
        const info = { current: r.current, latest: r.latest, notes: r.notes ?? null };
        push('update-available', info);
        if (onUpdateAvailable) onUpdateAvailable(info, app);
      } catch {}
    };
    setTimeout(autoCheck, 5000); // let the window come up first
    if (auto === 'daily') setInterval(autoCheck, 24 * 60 * 60 * 1000);
  }

  return app;
}
