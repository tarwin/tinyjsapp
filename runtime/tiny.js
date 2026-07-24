// tinyjs client shim. Everything the runtime injects lives under `tiny`.
// window.__invoke is the native bound function (already promise-returning);
// window.__emit is how the backend pushes events into the page.
(() => {
  const call = (method, params) => window.__invoke(JSON.stringify({ method, params }));
  const handlers = {};

  // tiny.fetch: like window.fetch, but the request runs in the backend (a
  // native process) — no CORS, CSP, or mixed-content limits, so the page can
  // reach any origin. Resolves to a real Response. Small responses arrive
  // whole; pass { stream: true } for a live streaming body — the Response's
  // body pulls chunks from the backend on demand (backpressured), which is
  // what an endless source like internet radio needs (a buffered fetch of a
  // never-ending stream would never resolve).
  let fetchSeq = 0;
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
  const headersToObject = (h) => {
    if (!h) return undefined;
    if (typeof Headers !== 'undefined' && h instanceof Headers) {
      const o = {}; h.forEach((v, k) => { o[k] = v; }); return o;
    }
    if (Array.isArray(h)) { const o = {}; for (const [k, v] of h) o[k] = v; return o; }
    return h;
  };
  const normalizeBody = async (body) => {
    if (body == null) return {};
    if (typeof body === 'string') return { bodyText: body };
    if (body instanceof URLSearchParams) return { bodyText: body.toString() };
    if (body instanceof ArrayBuffer) return { bodyB64: u8ToB64(new Uint8Array(body)) };
    if (ArrayBuffer.isView(body)) return { bodyB64: u8ToB64(new Uint8Array(body.buffer, body.byteOffset, body.byteLength)) };
    if (typeof Blob !== 'undefined' && body instanceof Blob) return { bodyB64: u8ToB64(new Uint8Array(await body.arrayBuffer())) };
    return { bodyText: String(body) };
  };
  const tinyFetch = async (url, init = {}) => {
    const id = 'f' + (++fetchSeq);
    const streaming = !!init.stream;
    const { bodyText, bodyB64 } = await normalizeBody(init.body);
    const head = await call('fetch', {
      url: String(url), id, stream: streaming,
      method: init.method, headers: headersToObject(init.headers),
      redirect: init.redirect, bodyText, bodyB64,
    });
    const respInit = { status: head.status, statusText: head.statusText, headers: head.headers };
    let resp;
    if (streaming) {
      const stream = new ReadableStream({
        async pull(controller) {
          const r = await call('fetch.pull', { id });
          if (r.done) { controller.close(); return; }
          controller.enqueue(b64ToU8(r.bodyB64));
        },
        cancel() { return call('fetch.cancel', { id }); },
      });
      // 204/205/304 are null-body per spec — a Response body would throw.
      const nullBody = head.status === 204 || head.status === 205 || head.status === 304;
      resp = new Response(nullBody ? null : stream, respInit);
    } else {
      resp = new Response(head.bodyB64 ? b64ToU8(head.bodyB64) : null, respInit);
    }
    // Response.url / .redirected are read-only getters; shadow them so callers
    // that inspect the post-redirect URL see the real value.
    try {
      Object.defineProperty(resp, 'url', { value: head.url, configurable: true });
      Object.defineProperty(resp, 'redirected', { value: head.redirected, configurable: true });
    } catch {}
    return resp;
  };

  window.tiny = {
    api: {
      call,
      on(event, fn) { (handlers[event] ||= []).push(fn); },
    },

    // Backend-proxied fetch (no CORS/CSP). Same shape as window.fetch, returns
    // a Response. Add { stream: true } for a live body (res.body.getReader())
    // — required for endless sources like internet radio.
    fetch: (url, init) => tinyFetch(url, init),

    // Same-app URL that streams a remote http(s) resource through the native
    // layer with permissive CORS. Drop it into a media element to get a
    // cross-origin stream (internet radio) into Web Audio — a MediaElementSource
    // on a cross-origin <audio> outputs silence by spec, but this URL is
    // CORS-approved so the EQ/analyser graph gets real samples:
    //   audio.crossOrigin = 'anonymous';
    //   audio.src = tiny.proxyURL('https://example.com/stream.mp3');
    // The native layer does the HTTP (redirects, byte-range/seek), so playback
    // keeps CoreMedia's buffering/reconnect. http/https upstreams only.
    proxyURL: (url) => 'tiny-media://proxy/?u=' + encodeURIComponent(String(url)),

    // A correct file:// URL for a disk path on BOTH platforms — hand-rolled
    // versions break on Windows (file://C:/… makes the drive the URL host).
    // Use for <audio>/<img>/<video> src of backend-provided paths.
    fileURL: (p) => {
      p = String(p);
      // Backslashes are separators ONLY in Windows-shaped paths (drive
      // letter or \\server UNC) — Unix filenames may legally contain them.
      if (/^[A-Za-z]:[\\/]/.test(p) || p.startsWith('\\\\')) p = p.replace(/\\/g, '/');
      const unc = p.startsWith('//'); // \\server\share — network/Parallels mounts
      if (!unc && !p.startsWith('/')) p = '/' + p; // C:/… needs the third slash
      const enc = p.split('/').map(encodeURIComponent).join('/')
        .replace(/%3A/gi, ':'); // keep the drive colon
      // UNC: the server becomes the URL host (file://server/share/…)
      return (unc ? 'file:' : 'file://') + enc;
    },

    log: (msg) => call('log', { msg }),
    quit: () => call('quit'),

    // Which machine is this? os()/isMacOS()/isWindows()/isLinux() answer
    // synchronously (the webview's own UA is decisive: WKWebView says
    // Macintosh, WebView2 says Windows NT, WebKitGTK says Linux), so they're
    // safe to branch on during page setup. architecture() and capabilities()
    // ask the backend, which sees the real machine — a Mac webview reports
    // "MacIntel" even on Apple silicon, so arch cannot be read from the page.
    system: {
      os: () => (/Windows/i.test(navigator.userAgent) ? 'windows'
        : /Linux|X11/i.test(navigator.userAgent) ? 'linux' : 'macos'),
      isMacOS: () => window.tiny.system.os() === 'macos',
      isWindows: () => window.tiny.system.os() === 'windows',
      isLinux: () => window.tiny.system.os() === 'linux',
      // -> { os, arch: 'arm64'|'x86_64', session, desktop }. session/desktop
      // are the Linux display server ('x11' | 'wayland') and desktop name;
      // null elsewhere.
      info: () => call('system.info'),
      architecture: async () => (await call('system.info')).arch,
      // -> { os, <feature>: boolean, … }. What this machine can actually do,
      // so an app can degrade deliberately instead of calling something that
      // quietly does nothing (Wayland, for instance, ignores setPosition).
      capabilities: () => call('system.capabilities'),

      // What this machine is MISSING for a feature, and how to fix it. Linux
      // ships its media stack in pieces (AAC and H.264 live in optional
      // GStreamer plugin sets), so a feature can be absent on one box and
      // present on the next — ask instead of assuming, and tell the user
      // something actionable rather than letting audio go quiet.
      //
      //   const [aac] = await tiny.system.requirements(['media.aac']);
      //   if (!aac.ok) alert(`${aac.feature} needs:\n${aac.install.command}`);
      //
      // -> [{ id, ok, feature, detail, install: { manager, packages, command } }]
      // install is null when it's already there, or when nothing installable
      // would fix it (windowPosition on Wayland is the session, not a package).
      // Ids: media.aac, media.h264, media.mp3, speech, spotlight.index,
      // audioTap, tray, windowPosition. Everything reports ok on macOS/Windows.
      // Probes are cached for the life of the app, so a missing package stays
      // "missing" even after the user installs it — pass { refresh: true } to
      // re-probe, e.g. when they come back from a terminal and retry.
      requirements: (ids, opts = {}) =>
        call('system.requirements', { ids: ids ?? null, refresh: !!opts.refresh }),
      // Just the ones that aren't satisfied — the common case.
      missing: async (ids) => (await call('system.requirements', { ids: ids ?? null }))
        .filter((r) => !r.ok),

      // The presentable version: check, and if anything is missing put a native
      // dialog in front of the user that names the feature, explains why it's
      // absent, and offers to COPY the install command — nobody retypes
      // "gstreamer1.0-plugins-bad" correctly from a toast. Returns without
      // showing anything when everything is present, so it's safe to call
      // straight from a failure path:
      //
      //   audio.addEventListener('error', () => tiny.system.promptMissing(['media.aac']));
      //
      // -> { missing: [...], copied: boolean }. Packages for several missing
      // features merge into ONE command, so the user runs a single line.
      // opts: { title?, ok?, cancel? }.
      async promptMissing(ids, opts = {}) {
        const missing = (await call('system.requirements', { ids: ids ?? null }))
          .filter((r) => !r.ok);
        if (!missing.length) return { missing: [], copied: false };
        // One command for everything missing: same manager, packages merged,
        // deduped, order preserved.
        const mgr = missing.find((r) => r.install)?.install ?? null;
        const pkgs = [...new Set(missing.flatMap((r) => r.install?.packages ?? []))];
        const at = mgr ? mgr.command.indexOf(mgr.packages[0]) : -1;
        const cmd = !mgr || !pkgs.length ? null
          : at < 0 ? mgr.command                      // unexpected shape — use it as given
          : mgr.command.slice(0, at) + pkgs.join(' ');
        const detail = missing.map((r) => '• ' + r.feature + ' — ' + r.detail).join('\n\n')
          + (cmd ? '\n\n' + cmd : '');
        const title = opts.title
          ?? (missing.length === 1
            ? missing[0].feature + ' needs a system package'
            : 'Some features need system packages');
        // Nothing installable would fix it (Wayland's setPosition, say) — then
        // there is no command to copy, so just say what's up.
        if (!cmd) {
          await call('win.alert', { message: title, detail });
          return { missing, copied: false };
        }
        const yes = await call('win.confirm', {
          message: title, detail,
          ok: opts.ok ?? 'Copy install command',
          cancel: opts.cancel ?? 'Not now',
        });
        if (!yes) return { missing, copied: false };
        try { await call('clip.write', { text: cmd }); } catch (e) { return { missing, copied: false }; }
        return { missing, copied: true };
      },
    },
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
      // Mouse events pass through to what's behind (overlays/HUDs).
      setClickThrough: (enabled) => call('win.setClickThrough', { enabled }),
      // 'normal' | 'floating' | 'overlay' (above fullscreen) | 'desktop'.
      setLevel: (level) => call('win.setLevel', { level }),
      // Follow the user across every Space + float over fullscreen apps.
      setAllSpaces: (enabled) => call('win.setAllSpaces', { enabled }),
      // Top-left origin. Wayland forbids a client from placing its own
      // toplevels, so this (and center()) do nothing there — check
      // getWinState().canPosition and fall back to startDrag() for dragging.
      setPosition: (x, y) => call('win.setPosition', { x, y }),
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
      // Render the page to a PDF file (vector) -> { path }.
      printToPDF: (path) => call('win.printToPDF', { path }),
      // fn(paths): files dragged onto the window, as real filesystem paths.
      onDrop(fn) { window.tiny.api.on('drop', ({ paths }) => fn(paths)); },
      // Native share sheet ({ text?, url?, paths?, x?, y? }) — anchor it at
      // the click: tiny.win.share({ url, x: e.clientX, y: e.clientY }).
      share: (opts) => call('win.share', opts ?? {}),
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

    // Read the app's (or system's) rendered audio output as PCM — for VU
    // meters / visualizers, including audio that bypasses Web Audio (native
    // HLS, CORS-tainted streams). Read-only: it observes, it can't process.
    // Requires an "audioTap" key in tinyjs.json. macOS 14.4+.
    //   await tiny.audioTap.start({ scope: 'app', interval: 80 });
    //   tiny.audioTap.on(({ pcm, sampleRate, channels, frames, t }) => {
    //     const bin = atob(pcm), n = bin.length / 2, s = new Int16Array(n);
    //     for (let i = 0; i < n; i++) s[i] = (bin.charCodeAt(2*i) | (bin.charCodeAt(2*i+1) << 8)) << 16 >> 16;
    //     // …float = s[i] / 32768, interleaved by `channels`
    //   });
    audioTap: {
      // opts: { scope?: 'app'|'system', excludeSelf?: boolean, interval?: ms }.
      // Resolves true, or throws an Error with a .code: 'unsupported' |
      // 'not-declared' | 'denied' | 'failed'.
      async start(opts = {}) {
        const r = await call('audioTap.start', opts);
        if (!r || !r.ok) {
          const e = new Error(r?.message || ('audioTap: ' + (r?.code || 'failed')));
          e.code = r?.code || 'failed';
          throw e;
        }
        return true;
      },
      stop: () => call('audioTap.stop'),
      // fn({ pcm, sampleRate, channels, frames, t }); pcm is base64 of
      // interleaved little-endian Int16.
      on(fn) { window.tiny.api.on('audio-tap', fn); },
    },

    // Native clipboard (NSPasteboard in the launcher — no polling spawns).
    clipboard: {
      // -> { kind: 'files'|'image'|'color'|'text'|'empty', changeCount,
      //      text, html, paths, image (png temp path), imageSize, color,
      //      concealed (password-manager marker — history apps must skip),
      //      sourceApp ({ name, bundleId }), sourceURL (Chromium copies) }
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
      // fn({ id, action, reply }): a notification action button / reply field
      // was used (tiny.notify(t, b, { actions: [{ id, title, reply? }] })).
      onNotificationAction(fn) { window.tiny.api.on('notification-action', fn); },
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
      // Every display (same top-left coords as win.setPosition): [{ id,
      // name, x, y, width, height, visible: {x,y,width,height}, scale,
      // primary }] — visible excludes the menu bar and Dock.
      screens: () => call('app.screens'),
      // Standard per-app directories: { home, data, cache, logs, temp,
      // downloads, desktop, documents } (data/cache/logs are per app id).
      paths: () => call('app.paths'),
      // NSWorkspace verbs. open(): URL (any scheme) or file path in the
      // default app; reveal(): show in Finder; trash(): move to Trash
      // (recoverable). Resolve true; reject with the reason on failure.
      shell: {
        open: (target) => call('shell.open', { target }),
        reveal: (path) => call('shell.reveal', { path }),
        trash: (path) => call('shell.trash', { path }),
      },
      // Launch at login (packaged .app on macOS 13+, else 'unsupported').
      // get()/set(v) -> 'enabled' | 'disabled' | 'requires-approval' |
      // 'unsupported'; 'requires-approval' = user must allow it in System
      // Settings > General > Login Items.
      launchAtLogin: {
        get: () => call('login.get'),
        set: (enabled) => call('login.set', { enabled }),
      },
      // Dock tile: setBadge('3') / setBadge('') to clear; bounce() until
      // activated, bounce({ critical: true }) until the user acts.
      dock: {
        setBadge: (text) => call('dock.setBadge', { text }),
        bounce: (opts) => call('dock.bounce', opts ?? {}),
      },
      // Keep the system awake (replaces `caffeinate`; released on quit or
      // crash automatically). { display: true } also keeps the screen on.
      power: {
        preventSleep: (reason, opts) => call('power.prevent', { reason, ...opts }),
        allowSleep: () => call('power.allow'),
      },
      // The active app right now: { name, bundleId, pid } | null.
      frontmostApp: () => call('app.frontmost'),
      // Trackpad haptic feedback: 'generic'|'alignment'|'level'.
      haptic: (pattern) => call('app.haptic', { pattern }),
      // Replace the Dock icon from a png ('' resets). Render a canvas for
      // progress rings / unread badges.
      dockIcon: (path) => call('app.dockIcon', { path }),
      // { percent, charging, plugged, minutesRemaining } | null.
      battery: () => call('app.battery'),
      // { ssid, bssid, rssi, noise, txRate } | null (ssid needs Location).
      wifi: () => call('app.wifi'),
      // Find files by name/content (Spotlight) -> up to 100 paths.
      spotlight: (query) => call('app.spotlight', { query }),
      // On-device LLM (FoundationModels; offline, no key). Only in TINYJS_AI
      // builds on macOS 26 — check ai.availability() first.
      ai: {
        // 'available' | 'unavailable' | 'unsupported'
        availability: () => call('ai.availability'),
        // generate(prompt, { instructions }) -> completion text; throws.
        generate: (prompt, opts) => call('ai.generate', { prompt, ...(opts ?? {}) }),
      },
      // Text selected in the frontmost app (Accessibility) — null if none.
      selectedText: () => call('app.selectedText'),
      // Other apps' on-screen windows (Accessibility): [{ app, bundleId,
      // pid, title, index, x, y, width, height }] | null if not granted.
      otherWindows: () => call('app.otherWindows'),
      // Move/resize another app's frontmost window (pid from otherWindows()).
      moveWindow: (pid, rect) => call('app.moveWindow', { pid, ...(rect ?? {}) }),
      // System beep / a system sound name ('Ping', 'Glass', …) or an audio
      // file path -> false if the name/file didn't load.
      beep: () => call('sound.play', {}),
      playSound: (target) => call('sound.play', { target }),
      // Seconds since the user's last input (pause polling when idle).
      idleTime: () => call('app.idleTime'),
      // Quick Look panel for path(s); quickLook() closes it.
      quickLook: (paths) => call('app.quickLook', { paths }),
      // Screenshot a display (id from screens(); default primary) ->
      // { path (png temp file — copy to keep), width, height }. Needs the
      // 'screen' permission + macOS 14; rejects with the reason otherwise.
      captureScreen: (screenId) => call('app.captureScreen', { screenId }),
      // System eyedropper (no screen-recording permission!) -> '#rrggbb'
      // or null on cancel.
      pickColor: () => call('app.pickColor'),
      // On-device OCR -> { text, blocks: [{ text, confidence, box }] }
      // (box normalized 0..1, top-left origin).
      ocr: (path) => call('app.ocr', { path }),
      // Thumbnail png for ANY file type -> { path, width, height };
      // size = bounding box in points (rendered @2x).
      thumbnail: (path, size) => call('app.thumbnail', { path, size }),
      // Keychain secrets under the app id (keytar role) — use for tokens,
      // never tiny.store.
      secrets: {
        get: (key) => call('secrets.get', { key }),          // string | null
        set: (key, value) => call('secrets.set', { key, value }),
        delete: (key) => call('secrets.delete', { key }),
      },
      // Touch ID / account-password sheet -> true | false (false = cancel).
      authenticate: (reason) => call('app.authenticate', { reason }),
      // AppleScript in-process (no osascript) -> result string | null;
      // rejects with the script error. Uses the 'automation' permission.
      applescript: (source) => call('app.applescript', { source }),
      // Record a display to an .mp4. start({ screenId?, path }) resolves
      // once capturing; stop() -> { path, duration }. Needs the 'screen'
      // permission + macOS 14; rejects otherwise. Video only, one at a time.
      recorder: {
        start: (opts) => call('record.start', opts ?? {}),
        stop: () => call('record.stop'),
      },
      // Now Playing (Control Center / lock screen) + media keys. set() arms
      // the keys; presses arrive via onMediaKey.
      nowPlaying: {
        set: (info) => call('nowplaying.set', info ?? {}),   // { title, artist,
        clear: () => call('nowplaying.clear'),               //  album, duration,
      },                                                     //  elapsed, playing }
      // fn({ command, time }): a media key / Control Center transport fired
      // (command: play|pause|toggle|next|previous|seek; time = seek target).
      onMediaKey(fn) { window.tiny.api.on('media-key', fn); },
      // Speak text with a system voice -> resolves when playback finishes.
      // opts: { voice (id from voices() or a lang like 'en-AU'), rate 0..1 }.
      say: (text, opts) => call('app.say', { text, ...(opts ?? {}) }),
      stopSpeaking: () => call('app.stopSpeaking'),
      // [{ id, name, lang, quality }] — installed speech voices.
      voices: () => call('app.voices'),
    },

    tray: {
      // spec: { title?, icon?, template?, tooltip?, primaryAction?,
      //         menu?: [{ id, label, key? } | { separator: true }] }
      // icon: png path or 'sf:<name>' (SF Symbol); primaryAction: true makes a
      // left click fire onClick and moves the menu to right-click.
      set: (spec) => call('tray.set', spec),
      remove: () => call('tray.remove'),
      // The tray icon's on-screen rect { x, y, width, height } | null.
      position: () => call('tray.position'),
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
