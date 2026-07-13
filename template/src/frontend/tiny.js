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
    notify: (title, body, opts = {}) => call('notify', { title, body, ...opts }),

    win: {
      setTitle: (title) => call('win.setTitle', { title }),
      setSize: (width, height) => call('win.setSize', { width, height }),
      hide: () => call('win.hide'),
      show: () => call('win.show'),
      center: () => call('win.center'),
      minimize: () => call('win.minimize'),
      fullscreen: () => call('win.fullscreen'),                    // toggles
      setAlwaysOnTop: (enabled) => call('win.setAlwaysOnTop', { enabled }),
      setResizable: (enabled) => call('win.setResizable', { enabled }),
      setPosition: (x, y) => call('win.setPosition', { x, y }),    // top-left origin
      setHideOnClose: (enabled) => call('win.setHideOnClose', { enabled }),
      print: () => call('win.print'),
      // fn(paths): files dragged onto the window, as real filesystem paths.
      onDrop(fn) { window.tiny.api.on('drop', ({ paths }) => fn(paths)); },
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
      // Right-click menu: [{ id, label } | { separator: true }]; null restores default.
      setContext: (items) => call('menu.setContext', { items }),
      onContext(fn) { window.tiny.api.on('contextmenu', ({ id }) => fn(id)); },
    },

    // Persistent settings (JSON, in ~/Library/Application Support/<app id>/).
    store: {
      get: (key) => call('store.get', { key }),          // value | null
      set: (key, value) => call('store.set', { key, value }),
      delete: (key) => call('store.delete', { key }),
      all: () => call('store.all'),
    },

    // System-wide hotkeys, e.g. register('boss', 'cmd+shift+k').
    hotkey: {
      register: (id, combo) => call('hotkey.register', { id, combo }),
      unregister: (id) => call('hotkey.unregister', { id }),
      on(fn) { window.tiny.api.on('hotkey', ({ id }) => fn(id)); },
    },

    // System theme; also 'sleep'/'wake' events via tiny.api.on.
    theme: {
      get: () => call('theme.get'),                      // { dark } | null
      on(fn) { window.tiny.api.on('theme', ({ dark }) => fn(dark)); },
    },

    app: {
      // false: menu-bar-only app (no Dock icon); true: normal app.
      setDockVisible: (visible) => call('app.setDockVisible', { visible }),
      // Deep links + file associations (packaged .app; see tinyjs.json
      // "urlScheme" and "fileExtensions"). Cold-start events are buffered.
      onOpenUrl(fn) { window.tiny.api.on('open-url', ({ url }) => fn(url)); },
      onOpenFiles(fn) { window.tiny.api.on('open-files', ({ paths }) => fn(paths)); },
    },

    tray: {
      // spec: { title?, icon?, template?, tooltip?,
      //         menu?: [{ id, label, key? } | { separator: true }] }
      set: (spec) => call('tray.set', spec),
      remove: () => call('tray.remove'),
      on(fn) { window.tiny.api.on('tray', ({ id }) => fn(id)); },          // menu item clicks
      onClick(fn) { window.tiny.api.on('trayclick', () => fn()); },        // bare icon clicks
    },
  };

  window.__emit = (msg) => {
    (handlers[msg.event] || []).forEach((fn) => fn(msg.data));
  };
})();
