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

export async function createApp({ html, htmlPath, title = 'tinyjs', size = '960x640', version = '0.0.0', tinyjsVersion = 'dev', id = null, launcherPath, api = {}, onMenu, onTray, onHotkey, onContextMenu, onSystem, onOpenUrl, onOpenFiles, onNotificationClick, onWindowClosed, chrome = null, update = null, activation = null }) {
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
  const frontendDir = htmlPath ? htmlPath.replace(/\/[^/]*$/, '') : null;

  // Read-backs: GET <qid> <what> → launcher answers GOT <qid> <json>.
  let qidSeq = 1;
  const pendingGets = new Map();
  function query(what) {
    return new Promise((resolve) => {
      const qid = String(qidSeq++);
      pendingGets.set(qid, resolve);
      send('GET ' + qid + ' ' + what);
    });
  }

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
    const aq = (s) => '"' + String(s ?? '').replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
    let script = 'display notification ' + aq(body ?? '') + ' with title ' + aq(title ?? 'tinyjs');
    if (subtitle) script += ' subtitle ' + aq(subtitle);
    const p = tjs.spawn(['osascript', '-e', script], { stdout: 'ignore', stderr: 'ignore' });
    const st = await p.wait();
    return st.exit_status === 0 && !st.term_signal;
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
    // Window visibility & app presence (tray-app plumbing).
    hide() { send('WINOP hide'); },
    show() { send('WINOP show'); },
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
      if (!p.startsWith('/')) {
        if (!frontendDir) throw new Error('win.open needs an absolute page path here');
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
        show: () => t('WINOP', 'show'),
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
    'win.show': async (_p, _a, m) => (forWin(m).show(), true),
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
    'theme.get': async () => lastTheme,
    'app.info': async () => app.info,
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

  return app;
}
