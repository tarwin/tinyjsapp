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
    // opts: { id?, subtitle?, sound? }. Packaged apps get real Notification
    // Center banners (app icon, permission prompt); clicks arrive via
    // tiny.app.onNotificationClick. Dev falls back to osascript.
    notify: (title, body, opts = {}) => call('notify', { title, body, ...opts }),

    win: {
      id: window.__TINY_WIN || 'main',   // which window this page lives in
      // Open (or focus) another window; page = html file in your frontend dir.
      open: (id, opts = {}) => call('win.open', { id, ...opts }),
      close: (id) => call('win.close', id ? { id } : {}),  // no id = this window
      windows: () => call('win.windows'),                  // ['main', ...]
      setTitle: (title) => call('win.setTitle', { title }),
      setSize: (width, height) => call('win.setSize', { width, height }),
      // hide(): hides the APP — focus returns to the previous app (palettes
      // can hide-then-paste with no frontmost tracking). show({ activate:
      // false }): surface the window without stealing focus (overlays/HUDs).
      hide: () => call('win.hide'),
      show: (opts) => call('win.show', opts ?? {}),
      center: () => call('win.center'),
      minimize: () => call('win.minimize'),
      fullscreen: () => call('win.fullscreen'),                    // toggles
      setAlwaysOnTop: (enabled) => call('win.setAlwaysOnTop', { enabled }),
      setResizable: (enabled) => call('win.setResizable', { enabled }),
      setPosition: (x, y) => call('win.setPosition', { x, y }),    // top-left origin
      restore: () => call('win.restore'),
      setFullscreen: (enabled) => call('win.setFullscreen', { enabled }),
      // { x, y, width, height, fullscreen, minimized, visible, focused,
      //   alwaysOnTop, resizable, screen: { width, height, scale } }
      getState: () => call('win.getState'),
      setHideOnClose: (enabled) => call('win.setHideOnClose', { enabled }),
      // { frame?, trafficLights?, transparent?, vibrancy? } — frameless windows
      // keep native resize/focus; mark your own titlebar with data-tiny-drag.
      setChrome: (opts) => call('win.setChrome', opts),
      // No args: drag the window (frameless chrome). With { files: [path…],
      // image? }: drag real files OUT of the app (into Finder, Slack, …) —
      // call from a mousedown handler while the button is held.
      startDrag: (opts) => opts?.files ? call('win.dragOut', opts) : call('win.startDrag'),
      dragOut: (opts) => call('win.dragOut', opts),
      zoom: () => call('win.zoom'),
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
      // menus: [{ title, items: [...] }]; items support { id, label, key?,
      // checked?, enabled?, submenu?: [...] } | { separator: true } — same
      // item shape works for tray and context menus.
      set: (menus) => call('menu.set', { menus }),
      on(fn) { window.tiny.api.on('menu', ({ id }) => fn(id)); },
      // Patch one item in place: update('mute', { checked: true, label: 'Muted' })
      update: (id, patch = {}) => call('menu.update', { id, ...patch }),
      get: (id) => call('menu.get', { id }),   // { exists, label, checked, enabled }
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

    // Native clipboard (NSPasteboard in the launcher — no polling spawns).
    clipboard: {
      // -> { kind: 'files'|'image'|'color'|'text'|'empty', changeCount,
      //      text, html, paths, image (png temp path), color }
      read: () => call('clip.read'),
      // { text?, html?, paths?, image? (png path/data-url/base64), color? }
      write: (data) => call('clip.write', data),
      changeCount: () => call('clip.changeCount'),
      watch: (intervalMs) => call('clip.watch', { intervalMs }),
      unwatch: () => call('clip.unwatch'),
      // fn({ changeCount, self }) after watch(); self = our own write().
      onChange(fn) { window.tiny.api.on('clipboard-change', fn); },
    },

    // System theme; also 'sleep'/'wake' events via tiny.api.on.
    theme: {
      get: () => call('theme.get'),                      // { dark } | null
      on(fn) { window.tiny.api.on('theme', ({ dark }) => fn(dark)); },
    },

    app: {
      // { version: <app>, tinyjs: <framework that built it>, runtime: <txiki> }
      info: () => call('app.info'),
      // false: menu-bar-only app (no Dock icon); true: normal app.
      setDockVisible: (visible) => call('app.setDockVisible', { visible }),
      // Deep links + file associations (packaged .app; see tinyjs.json
      // "urlScheme" and "fileExtensions"). Cold-start events are buffered.
      onOpenUrl(fn) { window.tiny.api.on('open-url', ({ url }) => fn(url)); },
      onOpenFiles(fn) { window.tiny.api.on('open-files', ({ paths }) => fn(paths)); },
      // fn(id): a notification banner was clicked (packaged apps).
      onNotificationClick(fn) { window.tiny.api.on('notification-click', ({ id }) => fn(id)); },
      // Post a native keystroke (e.g. 'cmd+v') -> { ok, trusted }; needs the
      // Accessibility permission (which names your app, not osascript).
      keystroke: (combo) => call('app.keystroke', { combo }),
      // keystroke('cmd+v'): paste into the frontmost app (hide first).
      paste: () => call('app.paste'),
      // 'accessibility' | 'screen' | 'notifications' | 'automation[:<id>]'
      // -> 'granted' | 'denied' | 'undetermined' | 'unsupported'
      permissions: {
        check: (name) => call('perm.check', { name }),
        request: (name) => call('perm.request', { name }),
      },
      // Global cursor position (same top-left coords as win.setPosition):
      // { x, y, window: { x, y, inside }, screen: { x, y, width, height,
      //   scale } } — window is relative to THIS window's content area
      // (clientX/clientY units, works even while the cursor is outside it)
      mousePosition: () => call('app.mouse'),
    },

    tray: {
      // spec: { title?, icon?, template?, tooltip?, primaryAction?,
      //         menu?: [{ id, label, key? } | { separator: true }] }
      // icon: png path or 'sf:<name>' (SF Symbol); primaryAction: true makes a
      // left click fire onClick and moves the menu to right-click.
      set: (spec) => call('tray.set', spec),
      remove: () => call('tray.remove'),
      on(fn) { window.tiny.api.on('tray', ({ id }) => fn(id)); },          // menu item clicks
      onClick(fn) { window.tiny.api.on('trayclick', () => fn()); },        // icon clicks
    },
  };

  window.__emit = (msg) => {
    (handlers[msg.event] || []).forEach((fn) => fn(msg.data));
  };

  // Drag regions for frameless windows: any element with data-tiny-drag acts
  // as a titlebar — drag moves the window, double-click zooms. Interactive
  // children (or anything inside data-tiny-nodrag) are left alone.
  window.addEventListener('mousedown', (e) => {
    if (e.button !== 0) return;
    if (!e.target.closest('[data-tiny-drag]')) return;
    if (e.target.closest('button, a, input, textarea, select, [contenteditable], [data-tiny-nodrag]')) return;
    if (e.detail === 2) call('win.zoom');
    else call('win.startDrag');
  });
})();
