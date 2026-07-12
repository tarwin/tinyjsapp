// Your app's backend. Runs on txiki.js with full system access.
//
// Every function in `api` is callable from the page:
//   const result = await api.call('name', { ...params });
// Each handler receives (params, app) — `app` has push/setTitle/setSize/quit.
//
// Built-ins you get for free: ping, log, quit, win.setTitle, win.setSize,
// win.openFile, win.openFiles, win.pickFolder, win.saveFile (native dialogs).

const dec = new TextDecoder();

export const api = {
  async sysinfo() {
    return {
      runtime: 'txiki.js ' + tjs.version,
      quickjs: tjs.engine.versions.quickjs,
      libuv: tjs.engine.versions.uv,
      host: tjs.hostName,
      user: tjs.system.userInfo.userName,
      cpu: tjs.system.cpus[0].model + ' × ' + tjs.system.cpus.length,
      pid: tjs.pid,
      cwd: tjs.cwd,
      home: tjs.homeDir,
    };
  },

  async listDir({ path }) {
    const entries = [];
    const iter = await tjs.readDir(path);
    for await (const e of iter) {
      entries.push({ name: e.name, isDir: !!e.isDirectory });
      if (entries.length >= 500) break;
    }
    entries.sort((a, b) => (b.isDir - a.isDir) || a.name.localeCompare(b.name));
    return { path, entries };
  },

  async readFileHead({ path }) {
    const data = await tjs.readFile(path);
    return dec.decode(data.subarray(0, 4096));
  },
};

// Called once the window is up. Use `app` to push events to the page.
export function init(app) {
  const startedAt = Date.now();
  setInterval(() => {
    app.push('tick', {
      time: new Date().toLocaleTimeString(),
      uptime: Math.floor((Date.now() - startedAt) / 1000),
    });
  }, 1000);
}
