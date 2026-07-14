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

declare interface TinyClipboardData {
  /** what the clipboard mainly holds */
  kind: 'files' | 'image' | 'color' | 'text' | 'empty';
  /** NSPasteboard change count — bumps on every clipboard change */
  changeCount: number;
  text: string | null;
  html: string | null;
  /** real filesystem paths (kind 'files') */
  paths: string[];
  /** png temp path (kind 'image'); valid until the clipboard changes again —
   *  copy the file to keep it */
  image: string | null;
  /** '#rrggbb' or '#rrggbbaa' (kind 'color') */
  color: string | null;
}

declare interface TinyClipboardWrite {
  text?: string;
  html?: string;
  /** multiple file URLs; all of them land (long-lived writer process) */
  paths?: string[];
  /** png path, data: URL, or raw base64 */
  image?: string;
  /** '#rrggbb' or '#rrggbbaa' */
  color?: string;
}

declare interface TinyKeystrokeResult {
  ok: boolean;
  /** false: the Accessibility permission isn't granted (see permissions) */
  trusted: boolean;
}

/** 'automation' checks System Events; 'automation:<bundle-id>' any target. */
declare type TinyPermissionName =
  | 'accessibility' | 'screen' | 'notifications'
  | 'automation' | `automation:${string}`;

declare type TinyPermissionStatus =
  'granted' | 'denied' | 'undetermined' | 'unsupported';

declare interface TinyDragOutOptions {
  /** real filesystem paths to drag out of the window */
  files: string[];
  /** optional custom drag-image png (file icons otherwise) */
  image?: string;
}

declare interface TinyShowOptions {
  /** false: surface the window WITHOUT stealing focus (overlay/HUD panels) */
  activate?: boolean;
}

declare interface TinyMousePosition {
  /** global cursor position — same top-left coords as win.setPosition */
  x: number;
  y: number;
  /** relative to the window's content area (clientX/clientY units, valid
   *  even while the cursor is outside it); pages get their own window,
   *  the backend gets main */
  window: { x: number; y: number; inside: boolean } | null;
  /** the display the cursor is on (frame in the same coords) */
  screen: { x: number; y: number; width: number; height: number; scale: number };
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
    /** hides the APP (NSApp hide): focus returns to the previous app —
     *  palettes can hide-then-paste with no frontmost tracking */
    hide(): Promise<any>;
    /** show({ activate: false }) surfaces the window without stealing focus */
    show(opts?: TinyShowOptions): Promise<any>;
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
    /** No args: drag the window (frameless chrome). With { files }: drag
     *  real files OUT of the app — call from a mousedown handler while the
     *  button is held. */
    startDrag(opts?: TinyDragOutOptions): Promise<any>;
    /** drag files out of the window (same as startDrag({ files })) */
    dragOut(opts: TinyDragOutOptions): Promise<any>;
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

  /** Native clipboard (NSPasteboard in the launcher — no polling spawns). */
  clipboard: {
    read(): Promise<TinyClipboardData>;
    write(data: TinyClipboardWrite): Promise<any>;
    changeCount(): Promise<number>;
    /** poll for changes every intervalMs (default 500) */
    watch(intervalMs?: number): Promise<any>;
    unwatch(): Promise<any>;
    /** after watch(); self = our own write() caused the change */
    onChange(fn: (info: { changeCount: number; self: boolean }) => void): void;
  };

  app: {
    info(): Promise<TinyAppInfo>;
    /** false: menu-bar-only app (no Dock icon) */
    setDockVisible(visible: boolean): Promise<any>;
    onOpenUrl(fn: (url: string) => void): void;
    onOpenFiles(fn: (paths: string[]) => void): void;
    onNotificationClick(fn: (id: string) => void): void;
    /** post a native keystroke, combo like 'cmd+v' (needs Accessibility) */
    keystroke(combo: string): Promise<TinyKeystrokeResult>;
    /** keystroke('cmd+v'): paste into the frontmost app (hide first) */
    paste(): Promise<TinyKeystrokeResult>;
    permissions: {
      check(name: TinyPermissionName): Promise<TinyPermissionStatus>;
      /** also prompts (accessibility opens System Settings at your app) */
      request(name: TinyPermissionName): Promise<TinyPermissionStatus>;
    };
    /** global cursor position + the screen it's on */
    mousePosition(): Promise<TinyMousePosition>;
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
  show(opts?: TinyShowOptions): void;
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
  /** hides the APP (NSApp hide): focus returns to the previous app */
  hide(): void;
  /** show({ activate: false }) surfaces the window without stealing focus */
  show(opts?: TinyShowOptions): void;
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
  /** Native clipboard (NSPasteboard in the launcher). */
  clipboard: {
    read(): Promise<TinyClipboardData>;
    write(data: TinyClipboardWrite): boolean;
    changeCount(): Promise<number>;
    /** poll for changes every intervalMs (default 500); changes arrive via
     *  the onClipboardChange option / 'clipboard-change' page event */
    watch(intervalMs?: number): void;
    unwatch(): void;
  };
  /** post a native keystroke, combo like 'cmd+v' (needs Accessibility) */
  keystroke(combo: string): Promise<TinyKeystrokeResult>;
  /** keystroke('cmd+v'): paste into the frontmost app (hide first) */
  paste(): Promise<TinyKeystrokeResult>;
  permissions: {
    check(name: TinyPermissionName): Promise<TinyPermissionStatus>;
    /** also prompts (accessibility opens System Settings at your app) */
    request(name: TinyPermissionName): Promise<TinyPermissionStatus>;
  };
  /** global cursor position + the screen it's on */
  mousePosition(): Promise<TinyMousePosition>;
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
