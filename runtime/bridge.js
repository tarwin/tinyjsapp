// tinyjs backend bridge.
//
// Spawns the native webview launcher and bridges it to your API over a Unix
// domain socket in a private (0700) temp dir. No network, no ports.
//
// Wire protocol (newline-delimited; payloads are JSON so never contain raw \n):
//   launcher -> backend:  CALL <id> <json-args-array>
//   backend -> launcher:  RET <id> <status> <json>    resolve/reject a call
//                         EVAL <js>                   run JS in the page
//                         TITLE <text>                set window title
//                         SIZE <w> <h>                resize window
//                         DLG <id> <op>               native dialog; launcher
//                                                     answers the call itself
//                         QUIT                        close the window

import { checkForUpdate, installUpdate, relaunch } from './update.js';

const enc = new TextEncoder();
const dec = new TextDecoder();
const DEBUG = !!tjs.env.TINYJS_DEBUG;

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
const DIALOG_OPS = {
  'win.openFile': { op: 'open', args: () => [] },
  'win.openFiles': { op: 'openmulti', args: () => [] },
  'win.pickFolder': { op: 'dir', args: () => [] },
  'win.saveFile': { op: 'save', args: () => [] },
  'win.alert': { op: 'alert', args: (p) => [one(p.message), one(p.detail), one(p.ok)] },
  'win.confirm': { op: 'confirm', args: (p) => [one(p.message), one(p.detail), one(p.ok), one(p.cancel)] },
  'win.prompt': { op: 'prompt', args: (p) => [one(p.message), one(p.default), one(p.ok), one(p.cancel)] },
};

// Tiny persistent JSON store in ~/Library/Application Support/<app id>/.
// Flat string keys, JSON values, atomic writes.
function makeStore(appId) {
  const dir = tjs.homeDir + '/Library/Application Support/' + (appId || 'tinyjs-app');
  const path = dir + '/store.json';
  let data = null;
  async function load() {
    if (data) return data;
    try { data = JSON.parse(dec.decode(await tjs.readFile(path))); }
    catch { data = {}; }
    return data;
  }
  async function save() {
    await tjs.makeDir(dir, { recursive: true }).catch(() => {});
    const tmp = path + '.tmp';
    await tjs.writeFile(tmp, enc.encode(JSON.stringify(data, null, 2) + '\n'));
    await tjs.rename(tmp, path);
  }
  return {
    async get(key) { return (await load())[key] ?? null; },
    async set(key, value) { await load(); data[key] = value; await save(); return true; },
    async delete(key) { await load(); delete data[key]; await save(); return true; },
    async all() { return { ...(await load()) }; },
  };
}

export async function createApp({ html, htmlPath, title = 'tinyjs', size = '960x640', version = '0.0.0', tinyjsVersion = 'dev', id = null, launcherPath, api = {}, onMenu, onTray, onHotkey, onContextMenu, onSystem, onOpenUrl, onOpenFiles, onNotificationClick, onWindowClosed, onClipboardChange, chrome = null, update = null, activation = null }) {
  const exeDir = tjs.exePath.replace(/\/[^/]*$/, '/');

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

  if (attachPath) {
    const conn = await tjs.connect('pipe', attachPath);
    ({ readable, writable } = await conn.opened);
  } else {
    // Launcher: explicit option > env override > next to the executable.
    let launcher = launcherPath || tjs.env.TINYJS_LAUNCHER;
    if (!launcher && (await exists(exeDir + 'launcher'))) launcher = exeDir + 'launcher';
    if (!launcher || !(await exists(launcher))) {
      throw new Error('tinyjs launcher binary not found (looked at: ' + (launcher || exeDir + 'launcher') + ')');
    }

    // Private rendezvous dir: socket + materialized frontend.
    const workDir = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-XXXXXX');
    const sockPath = workDir + '/app.sock';

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
    const spawnOpts = { stderr: 'inherit' };
    if (activation === 'accessory') spawnOpts.env = { ...tjs.env, TINYJS_ACTIVATION: 'accessory' };
    proc = tjs.spawn([launcher, pagePath, sockPath, title, size, version], spawnOpts);

    cleanup = async () => {
      await tjs.remove(sockPath).catch(() => {});
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
    ? (isUrl(htmlPath) ? htmlPath.replace(/\/+$/, '') : htmlPath.replace(/\/[^/]*$/, ''))
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
  async function notify({ title, body, subtitle, id: nid, sound } = {}) {
    if (attachPath) {
      send('NOTIFY ' + [one(nid ?? ''), one(title ?? 'tinyjs'), one(body ?? ''),
                        one(subtitle ?? ''), sound ? '1' : '0'].join('\t'));
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
    writer.write(enc.encode(line + '\n')).catch((e) => console.log('tinyjs send error:', e));
  }

  function push(event, data) {
    // Broadcast so secondary windows receive backend events too.
    send('EVAL@* window.__emit && window.__emit(' + JSON.stringify({ event, data }) + ')');
  }

  const app = {
    push,
    setTitle(t) { send('TITLE ' + String(t).replace(/\n/g, ' ')); },
    setSize(w, h) { send(`SIZE ${w | 0} ${h | 0}`); },
    // Not JS eval(): sends script to the app's own page via webview_eval,
    // the same channel push() uses. Never receives external input.
    eval(js) { send('EVAL ' + String(js).replace(/\n/g, ' ')); },
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
    // Top-left origin in screen points (CSS-style coordinates).
    setPosition(x, y) { send(`WINOP pos ${x | 0} ${y | 0}`); },
    // false: no Dock icon / no app menu (menu-bar-only app); true: normal app.
    setDockVisible(v) { send('WINOP dock ' + (v ? 1 : 0)); },
    // true: the close button hides the window instead of quitting.
    setHideOnClose(v) { send('WINOP hideonclose ' + (v ? 1 : 0)); },
    // spec: { title?, icon?, template?, tooltip?, primaryAction?,
    //         menu?: [{ id, label, key? } | { separator: true }] }
    // icon is a png path (absolute or project-relative) or 'sf:<name>' for an
    // SF Symbol (e.g. 'sf:cup.and.saucer.fill' — no shipped assets needed);
    // template: false keeps its colors instead of adapting to the menu bar
    // (default true). Menu clicks arrive as a 'tray' page event and via the
    // onTray option; with no menu, icon clicks arrive as 'trayclick'.
    // primaryAction: true splits the two — left click fires 'trayclick' (a
    // Caffeine-style toggle) and the menu opens on right/ctrl-click.
    tray: {
      set(spec = {}) {
        let icon = spec.icon ?? '';
        if (icon && !icon.startsWith('/') && !icon.startsWith('sf:')) icon = tjs.cwd + '/' + icon;
        send('TRAYBEGIN ' + [one(spec.title), one(icon),
                             spec.template === false ? '0' : '1',
                             one(spec.tooltip),
                             spec.primaryAction ? '1' : '0'].join('\t'));
        sendItems(spec.menu);
        send('TRAYEND');
      },
      remove() { send('TRAYREMOVE'); },
    },
    print() { send('PRINT'); },
    // Window chrome: { frame?, trafficLights?, transparent?, vibrancy? }.
    // frame:false hides the titlebar (content extends under it; keep your own
    // drag region via data-tiny-drag). vibrancy: material name or null.
    setChrome(opts = {}) {
      const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
      const vib = opts.vibrancy === undefined ? ''
                : opts.vibrancy === null || opts.vibrancy === false ? 'none'
                : String(opts.vibrancy);
      send('CHROME ' + [bit(opts.frame), bit(opts.trafficLights),
                        bit(opts.transparent), one(vib)].join('\t'));
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
      async get() { return (await ask('LOGIN', 'get'))?.status ?? 'unsupported'; },
      async set(enabled) {
        const r = await ask('LOGIN', 'set ' + (enabled ? 1 : 0));
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
    // System beep / a sound: system sound name ('Ping', 'Glass', …) or an
    // audio file path. Resolve false if the name/file didn't load.
    async beep() { return (await ask('SOUND'))?.ok === true; },
    async playSound(target) { return (await ask('SOUND', esc(target)))?.ok === true; },
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
    // Standard per-app directories (data/cache/logs are per app id, not
    // auto-created — tjs.makeDir(..., { recursive: true }) first write).
    // Prefer these over hardcoding ~/Library paths.
    paths: {
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
    openWindow(id, { page, title, size } = {}) {
      let p = String(page ?? 'index.html');
      if (!isUrl(p) && !p.startsWith('/')) {
        if (!frontendDir) throw new Error('win.open needs an absolute page path or URL here');
        p = frontendDir + '/' + p;
      }
      send('WINOPEN ' + [one(id), one(p), one(title ?? id), one(size ?? '600x400')].join('\t'));
    },
    // Handle for any window ('main' or a secondary id).
    window(id) {
      const t = (cmd, rest) => send(id === 'main'
        ? cmd + (rest != null ? ' ' + rest : '')
        : cmd + '@' + id + (rest != null ? ' ' + rest : ''));
      return {
        eval: (js) => t('EVAL', String(js).replace(/\n/g, ' ')),
        push: (event, data) =>
          t('EVAL', 'window.__emit && window.__emit(' + JSON.stringify({ event, data }) + ')'),
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
        setChrome(opts = {}) {
          const bit = (v) => (v === undefined ? '' : v ? '1' : '0');
          const vib = opts.vibrancy === undefined ? ''
                    : opts.vibrancy === null || opts.vibrancy === false ? 'none'
                    : String(opts.vibrancy);
          t('CHROME', [bit(opts.frame), bit(opts.trafficLights),
                       bit(opts.transparent), one(vib)].join('\t'));
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
    // check() -> { available, current, latest }; install() downloads, verifies,
    // swaps the .app, relaunches the new version, and quits this instance.
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
  const builtins = {
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
      const { available, current, latest } = await app.update.check();
      return { available, current, latest };
    },
    'update.install': async () => app.update.install(),
    'win.print': async () => (app.print(), true),
    'store.get': async ({ key }) => app.store.get(key),
    'store.set': async ({ key, value }) => app.store.set(key, value),
    'store.delete': async ({ key }) => app.store.delete(key),
    'store.all': async () => app.store.all(),
    'hotkey.register': async ({ id: hid, combo }) => (app.hotkey.register(hid, combo), true),
    'hotkey.unregister': async ({ id: hid }) => (app.hotkey.unregister(hid), true),
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
          push('window-closed', { id });
          if (onWindowClosed) onWindowClosed(id, app);
        } else if (line.startsWith('NOTIFYCLICK ')) {
          const id = line.slice(12);
          push('notification-click', { id });
          if (onNotificationClick) onNotificationClick(id, app);
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

  // A clipboard handler implies watching; apps needing a custom interval can
  // call app.clipboard.watch(ms) on top (idempotent).
  if (onClipboardChange) app.clipboard.watch();

  return app;
}
