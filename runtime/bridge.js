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

export async function createApp({ html, htmlPath, title = 'tinyjs', size = '960x640', version = '0.0.0', launcherPath, api = {}, onMenu }) {
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

  // The bridge always owns the page file so reload(html) can rewrite it.
  const srcPath = tjs.env.TINYJS_HTML || htmlPath;
  let pageHtml = html;
  if (srcPath) pageHtml = dec.decode(await tjs.readFile(srcPath));
  if (pageHtml == null) throw new Error('createApp needs `html` (string) or `htmlPath`');
  const pagePath = workDir + '/index.html';
  await tjs.writeFile(pagePath, enc.encode(pageHtml));

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
    async reload(newHtml) {
      if (newHtml != null) await tjs.writeFile(pagePath, enc.encode(newHtml));
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
    quit() { send('QUIT'); },
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
    'menu.set': async ({ menus }) => (app.setMenu(menus), true),
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
