// tinyjs client shim. Everything the runtime injects lives under `tiny`.
// window.__invoke is the native bound function (already promise-returning);
// window.__emit is how the backend pushes events into the page.
(() => {
  const call = (method, params) => window.__invoke(JSON.stringify({ method, params }));
  const handlers = {};

  window.tiny = {
    api: {
      call,
      on(event, fn) { (handlers[event] ||= []).push(fn); },
    },

    log: (msg) => call('log', { msg }),
    quit: () => call('quit'),

    win: {
      setTitle: (title) => call('win.setTitle', { title }),
      setSize: (width, height) => call('win.setSize', { width, height }),
      openFile: () => call('win.openFile'),                 // path | null
      openFiles: () => call('win.openFiles'),               // paths[] | null
      pickFolder: () => call('win.pickFolder'),             // path | null
      saveFile: () => call('win.saveFile'),                 // path | null
      alert: (message, detail) => call('win.alert', { message, detail }),
      confirm: (message, opts = {}) => call('win.confirm', { message, ...opts }),   // true | false
      prompt: (message, opts = {}) => call('win.prompt', { message, ...opts }),     // string | null
    },

    menu: {
      // menus: [{ title, items: [{ id, label, key? } | { separator: true }] }]
      set: (menus) => call('menu.set', { menus }),
      on(fn) { window.tiny.api.on('menu', ({ id }) => fn(id)); },
    },
  };

  window.__emit = (msg) => {
    (handlers[msg.event] || []).forEach((fn) => fn(msg.data));
  };
})();
