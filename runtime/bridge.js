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

// Desktop notification via osascript: works from dev and packaged builds
// without notification-center entitlements (macOS shows it under "Script
// Editor" in Notification Center settings). A native UNUserNotificationCenter
// path (own icon, actions) is possible for signed bundles later.
async function notify({ title, body, subtitle } = {}) {
  const aq = (s) => '"' + String(s ?? '').replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
  let script = 'display notification ' + aq(body ?? '') + ' with title ' + aq(title ?? 'tinyjs');
  if (subtitle) script += ' subtitle ' + aq(subtitle);
  const p = tjs.spawn(['osascript', '-e', script], { stdout: 'ignore', stderr: 'ignore' });
  const st = await p.wait();
  return st.exit_status === 0 && !st.term_signal;
}

export async function createApp({ html, htmlPath, title = 'tinyjs', size = '960x640', version = '0.0.0', launcherPath, api = {}, onMenu, onTray, update = null }) {
  const exeDir = tjs.exePath.replace(/\/[^/]*$/, '/');

  async function exists(p) {
    try {
      await tjs.stat(p);
      return true;
    } catch {
      return false;
    }
  }

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
  let pagePath;
  let ownsPage = false; // true when the bridge materialized the page file
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

  const proc = tjs.spawn([launcher, pagePath, sockPath, title, size, version], { stderr: 'inherit' });

  async function cleanup() {
    await tjs.remove(sockPath).catch(() => {});
    await tjs.remove(workDir, { recursive: true }).catch(() => tjs.remove(workDir).catch(() => {}));
  }

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

  const { readable, writable } = await first.sock.opened;
  const writer = writable.getWriter();

  function send(line) {
    dbg('>>', line);
    writer.write(enc.encode(line + '\n')).catch((e) => console.log('tinyjs send error:', e));
  }

  function push(event, data) {
    send('EVAL window.__emit && window.__emit(' + JSON.stringify({ event, data }) + ')');
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
        for (const it of m.items ?? []) {
          if (it.separator) send('SEP');
          else send('ITEM ' + [one(it.id), one(it.label ?? it.id), one(it.key ?? '')].join('\t'));
        }
      }
      send('MENUEND');
    },
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
    // spec: { title?, icon?, template?, tooltip?,
    //         menu?: [{ id, label, key? } | { separator: true }] }
    // icon is a png path (absolute or project-relative); template: false keeps
    // its colors instead of adapting to the menu bar (default true).
    // Menu clicks arrive as a 'tray' page event and via the onTray option;
    // with no menu, icon clicks arrive as 'trayclick'.
    tray: {
      set(spec = {}) {
        let icon = spec.icon ?? '';
        if (icon && !icon.startsWith('/')) icon = tjs.cwd + '/' + icon;
        send('TRAYBEGIN ' + [one(spec.title), one(icon),
                             spec.template === false ? '0' : '1',
                             one(spec.tooltip)].join('\t'));
        for (const it of spec.menu ?? []) {
          if (it.separator) send('SEP');
          else send('ITEM ' + [one(it.id), one(it.label ?? it.id), one(it.key ?? '')].join('\t'));
        }
        send('TRAYEND');
      },
      remove() { send('TRAYREMOVE'); },
    },
    quit() { send('QUIT'); },
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
    'win.setTitle': async ({ title: t }) => (app.setTitle(t), true),
    'win.setSize': async ({ width, height }) => (app.setSize(width, height), true),
    'win.hide': async () => (app.hide(), true),
    'win.show': async () => (app.show(), true),
    'win.center': async () => (app.center(), true),
    'win.minimize': async () => (app.minimize(), true),
    'win.fullscreen': async () => (app.fullscreen(), true),
    'win.setAlwaysOnTop': async ({ enabled }) => (app.setAlwaysOnTop(enabled), true),
    'win.setResizable': async ({ enabled }) => (app.setResizable(enabled), true),
    'win.setPosition': async ({ x, y }) => (app.setPosition(x, y), true),
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
  };
  const methods = { ...api, ...builtins };

  async function handleCall(line) {
    const sp = line.indexOf(' ', 5);
    const id = line.slice(5, sp);
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
      result = await fn(params ?? {}, app);
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
        }
      }
    }
  })().catch((e) => console.log('tinyjs read loop error:', e));

  app.done = proc.wait().then(async (st) => {
    await cleanup();
    return st;
  });

  return app;
}
