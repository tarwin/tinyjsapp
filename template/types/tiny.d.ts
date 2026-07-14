// Type definitions for tinyjs — https://tinyjs.app/api
//
// Frontend: the injected `tiny` global (window.tiny).
// Backend: annotate handlers with TinyApiHandler / TinyApp, e.g.
//   /** @type {Record<string, import('./types/tiny').TinyApiHandler>} */
// or in TypeScript backends:
//   export const api: Record<string, TinyApiHandler> = { ... }
//
// These are ambient declarations — no import needed for the `tiny` global.

declare interface TinyMenuItem {
  id?: string;
  label?: string;
  /** ⌘+<key> shortcut (menu bar items) */
  key?: string;
  /** show a ✓ checkmark */
  checked?: boolean;
  /** false = grayed out */
  enabled?: boolean;
  submenu?: TinyMenuItem[];
  separator?: boolean;
}

declare interface TinyMenu {
  title: string;
  items: TinyMenuItem[];
}

declare interface TinyMenuItemState {
  exists: boolean;
  label?: string;
  checked?: boolean;
  enabled?: boolean;
}

declare interface TinyChromeOptions {
  /** false: hide the titlebar; the page extends to the top edge */
  frame?: boolean;
  trafficLights?: boolean;
  transparent?: boolean;
  /** material name ('sidebar' | 'hud' | 'menu' | 'popover' | 'window' |
   *  'content' | 'header' | 'sheet' | 'tooltip' | 'fullscreen' |
   *  'underwindow' | 'underpage' | 'titlebar' | 'selection') or null */
  vibrancy?: string | null;
}

declare interface TinyWinState {
  x: number;
  y: number;
  width: number;
  height: number;
  fullscreen: boolean;
  minimized: boolean;
  visible: boolean;
  focused: boolean;
  alwaysOnTop: boolean;
  resizable: boolean;
  chrome: {
    frame: boolean;
    trafficLights: boolean;
    transparent: boolean;
    vibrancy: string | null;
  };
  screen: { width: number; height: number; scale: number };
}

declare interface TinyAppInfo {
  /** your app's version (tinyjs.json) */
  version: string;
  /** the tinyjs version the app was built with ('dev' from a checkout) */
  tinyjs: string;
  /** e.g. 'txiki.js 26.6.0' */
  runtime: string;
}

declare interface TinyTraySpec {
  title?: string;
  /** png path (absolute or project-relative); template image by default */
  icon?: string;
  /** false: keep icon colors instead of adapting to the menu bar */
  template?: boolean;
  tooltip?: string;
  menu?: TinyMenuItem[];
}

declare interface TinyNotifyOptions {
  /** correlates notification clicks */
  id?: string;
  subtitle?: string;
  sound?: boolean;
}

declare interface TinyOpenWindowOptions {
  /** html file in your frontend dir (e.g. 'settings.html') or absolute path */
  page?: string;
  title?: string;
  /** 'WxH', e.g. '420x300' */
  size?: string;
}

/** The `tiny` global available in every window's page. */
declare interface Tiny {
  api: {
    /** Call a backend api method; resolves with its return value. */
    call(method: string, params?: unknown): Promise<any>;
    /** Subscribe to backend push events (app.push / broadcasts). */
    on(event: string, fn: (data: any) => void): void;
  };

  log(msg: string): Promise<any>;
  quit(): Promise<any>;
  /** Desktop notification. Packaged + signed apps get native Notification
   *  Center banners; dev builds fall back to osascript. Never rejects —
   *  resolves false if delivery failed, so fire-and-forget is safe. */
  notify(title: string, body?: string, opts?: TinyNotifyOptions): Promise<boolean>;

  win: {
    /** which window this page lives in ('main' or a tiny.win.open id) */
    id: string;
    open(id: string, opts?: TinyOpenWindowOptions): Promise<any>;
    /** close a window; no id = this window ('main' quits the app) */
    close(id?: string): Promise<any>;
    windows(): Promise<string[]>;

    setTitle(title: string): Promise<any>;
    setSize(width: number, height: number): Promise<any>;
    hide(): Promise<any>;
    show(): Promise<any>;
    center(): Promise<any>;
    minimize(): Promise<any>;
    restore(): Promise<any>;
    /** toggle */
    fullscreen(): Promise<any>;
    setFullscreen(enabled: boolean): Promise<any>;
    setAlwaysOnTop(enabled: boolean): Promise<any>;
    setResizable(enabled: boolean): Promise<any>;
    /** top-left origin, screen points */
    setPosition(x: number, y: number): Promise<any>;
    getState(): Promise<TinyWinState>;
    setChrome(opts: TinyChromeOptions): Promise<any>;
    startDrag(): Promise<any>;
    zoom(): Promise<any>;
    setHideOnClose(enabled: boolean): Promise<any>;
    print(): Promise<any>;
    /** files dragged onto the window — real filesystem paths */
    onDrop(fn: (paths: string[]) => void): void;

    openFile(): Promise<string | null>;
    openFiles(): Promise<string[] | null>;
    pickFolder(): Promise<string | null>;
    saveFile(): Promise<string | null>;
    alert(message: string, detail?: string): Promise<true>;
    confirm(message: string, opts?: { detail?: string; ok?: string; cancel?: string }): Promise<boolean>;
    prompt(message: string, opts?: { default?: string; ok?: string; cancel?: string }): Promise<string | null>;
  };

  menu: {
    set(menus: TinyMenu[]): Promise<any>;
    on(fn: (id: string) => void): void;
    /** patch a live item without redeclaring the menu */
    update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): Promise<any>;
    get(id: string): Promise<TinyMenuItemState>;
    /** replace the right-click menu; null restores WebKit's default */
    setContext(items: TinyMenuItem[] | null): Promise<any>;
    onContext(fn: (id: string) => void): void;
  };

  store: {
    get(key: string): Promise<any | null>;
    set(key: string, value: unknown): Promise<any>;
    delete(key: string): Promise<any>;
    all(): Promise<Record<string, any>>;
  };

  hotkey: {
    /** combo like 'cmd+shift+k'; fires system-wide */
    register(id: string, combo: string): Promise<any>;
    unregister(id: string): Promise<any>;
    on(fn: (id: string) => void): void;
  };

  theme: {
    get(): Promise<{ dark: boolean } | null>;
    on(fn: (dark: boolean) => void): void;
  };

  app: {
    info(): Promise<TinyAppInfo>;
    /** false: menu-bar-only app (no Dock icon) */
    setDockVisible(visible: boolean): Promise<any>;
    onOpenUrl(fn: (url: string) => void): void;
    onOpenFiles(fn: (paths: string[]) => void): void;
    onNotificationClick(fn: (id: string) => void): void;
  };

  tray: {
    set(spec: TinyTraySpec): Promise<any>;
    remove(): Promise<any>;
    on(fn: (id: string) => void): void;
    onClick(fn: () => void): void;
  };
}

declare const tiny: Tiny;

interface Window {
  tiny: Tiny;
  /** set by the injected bridge before your scripts run */
  __TINY_WIN?: string;
}

/** Handle for one window (backend: app.window(id)). */
declare interface TinyWindowHandle {
  eval(js: string): void;
  push(event: string, data?: unknown): void;
  close(): void;
  setTitle(title: string): void;
  setSize(width: number, height: number): void;
  setPosition(x: number, y: number): void;
  center(): void;
  hide(): void;
  show(): void;
  minimize(): void;
  restore(): void;
  zoom(): void;
  fullscreen(): void;
  setFullscreen(enabled: boolean): void;
  setAlwaysOnTop(enabled: boolean): void;
  setResizable(enabled: boolean): void;
  setChrome(opts: TinyChromeOptions): void;
  getState(): Promise<TinyWinState>;
}

/** The backend `app` handle (passed to init, api handlers, and events). */
declare interface TinyApp {
  /** push an event to every window (tiny.api.on) */
  push(event: string, data?: unknown): void;
  setTitle(title: string): void;
  setSize(width: number, height: number): void;
  setMenu(menus: TinyMenu[]): void;
  updateMenuItem(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
  getMenuItem(id: string): Promise<TinyMenuItemState>;
  setContextMenu(items: TinyMenuItem[] | null): void;
  /** run JS in the main window's page (not JS eval of external input) */
  eval(js: string): void;
  reload(newHtml?: string): Promise<void>;
  quit(): void;
  /** Never rejects — resolves false if delivery failed, so fire-and-forget
   *  is safe. */
  notify(opts: { title?: string; body?: string } & TinyNotifyOptions): Promise<boolean>;
  hide(): void;
  show(): void;
  center(): void;
  minimize(): void;
  restore(): void;
  fullscreen(): void;
  setFullscreen(enabled: boolean): void;
  setPosition(x: number, y: number): void;
  setAlwaysOnTop(enabled: boolean): void;
  setResizable(enabled: boolean): void;
  setHideOnClose(enabled: boolean): void;
  setDockVisible(visible: boolean): void;
  print(): void;
  startDrag(): void;
  zoom(): void;
  setChrome(opts: TinyChromeOptions): void;
  getWinState(): Promise<TinyWinState>;

  openWindow(id: string, opts?: TinyOpenWindowOptions): void;
  window(id: string): TinyWindowHandle;
  windows(): Promise<string[]>;

  tray: {
    set(spec: TinyTraySpec): void;
    remove(): void;
  };
  store: {
    get(key: string): Promise<any | null>;
    set(key: string, value: unknown): Promise<boolean>;
    delete(key: string): Promise<boolean>;
    all(): Promise<Record<string, any>>;
  };
  hotkey: {
    register(id: string, combo: string): void;
    unregister(id: string): void;
  };
  update: {
    check(): Promise<{ available: boolean; current: string; latest: string | null }>;
    install(): Promise<boolean>;
  };
  info: TinyAppInfo;
  /** resolves when the window process ends */
  done: Promise<unknown> | null;
}

/** Metadata passed to api handlers. */
declare interface TinyApiMeta {
  /** id of the window the call came from ('main' or a tiny.win.open id) */
  window: string;
}

/** Signature for backend api methods:
 *  export const api = { myMethod: async (params, app, meta) => ... } */
declare type TinyApiHandler = (
  params: any,
  app: TinyApp,
  meta: TinyApiMeta,
) => unknown | Promise<unknown>;
