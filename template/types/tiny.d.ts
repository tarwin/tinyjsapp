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
  /** drop macOS's rounded window corners by making the window BORDERLESS:
   *  square, no titlebar, no traffic lights. Tradeoff — no native titlebar
   *  drag (use data-tiny-drag) and a deliberately un-native look; resize
   *  edges, shadow, and focus are kept. Set it in tinyjs.json "chrome" to
   *  apply before first paint (no rounded→square flash on launch). */
  squareCorners?: boolean;
  /** make the click that focuses an unfocused window ALSO reach the page.
   *  macOS normally swallows that first click into web content ("click once
   *  to focus, again to act"); true delivers it straight through — useful for
   *  palettes/toolbars and for DOM drag regions on unfocused windows.
   *  Off by default (matches the platform). */
  acceptsFirstMouse?: boolean;
}

/** 'floating' = always-on-top; 'overlay' floats above almost everything
 *  (incl. most fullscreen apps); 'desktop' pins behind normal windows. */
declare type TinyWindowLevel = 'normal' | 'floating' | 'overlay' | 'desktop';

/** On-device LLM (Apple FoundationModels). 'available' = ready; 'unavailable'
 *  = Apple Intelligence off / model not downloaded; 'unsupported' = older
 *  macOS, or a stock build (tiny.ai ships only in TINYJS_AI builds). */
declare type TinyAiAvailability = 'available' | 'unavailable' | 'unsupported';

declare interface TinyAi {
  availability(): Promise<TinyAiAvailability>;
  /** offline, no API key. opts.instructions = a system prompt. Throws with
   *  the reason (incl. 'not built in' on stock builds). */
  generate(prompt: string, opts?: { instructions?: string }): Promise<string>;
}

declare interface TinyBattery {
  percent: number;
  charging: boolean;
  plugged: boolean;
  /** minutes to full (charging) or empty; null while calculating */
  minutesRemaining: number | null;
}

declare interface TinyWifi {
  /** null without the Location permission on macOS 14+ */
  ssid: string | null;
  bssid: string | null;
  /** signal strength, dBm */
  rssi: number;
  noise: number;
  /** Mbps */
  txRate: number;
}

/** A window belonging to another app (accessibility), top-left coords. */
declare interface TinyOtherWindow {
  app: string;
  bundleId: string | null;
  pid: number;
  title: string;
  index: number;
  x: number;
  y: number;
  width: number;
  height: number;
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
  clickThrough: boolean;
  level: TinyWindowLevel;
  allSpaces: boolean;
  chrome: {
    frame: boolean;
    trafficLights: boolean;
    transparent: boolean;
    vibrancy: string | null;
    squareCorners: boolean;
    acceptsFirstMouse: boolean;
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

declare interface TinyNotifyAction {
  id: string;
  title: string;
  /** show a text field instead of a plain button (the submit sends `reply`) */
  reply?: boolean;
  /** placeholder for the reply field */
  placeholder?: string;
  /** button title for the reply field (defaults to `title`) */
  buttonTitle?: string;
  /** render the button in red */
  destructive?: boolean;
}

declare interface TinyNotifyOptions {
  /** correlates notification clicks */
  id?: string;
  subtitle?: string;
  sound?: boolean;
  /** action buttons / a reply field (packaged apps); taps arrive via
   *  onNotificationAction / the 'notification-action' event */
  actions?: TinyNotifyAction[];
}

/** A notification action button or reply-field submit. */
declare interface TinyNotificationAction {
  id: string;
  action: string;
  /** the typed text for a reply action, '' otherwise */
  reply: string;
}

/** Now Playing metadata (Control Center / lock screen). */
declare interface TinyNowPlaying {
  title?: string;
  artist?: string;
  album?: string;
  /** seconds */
  duration?: number;
  /** seconds */
  elapsed?: number;
  playing?: boolean;
}

/** A hardware media key / Control Center transport event. */
declare interface TinyMediaKey {
  command: 'play' | 'pause' | 'toggle' | 'next' | 'previous' | 'seek';
  /** seek target in seconds (only for 'seek') */
  time?: number;
}

declare interface TinyVoice {
  id: string;
  name: string;
  lang: string;
  quality: 'default' | 'enhanced' | 'premium';
}

/** A finished screen recording; path is the .mp4 you asked for. */
declare interface TinyRecording {
  path: string;
  /** seconds */
  duration: number;
}

declare interface TinyRecorder {
  /** resolves once capture is running; needs the 'screen' permission +
   *  macOS 14, rejects with the reason otherwise */
  start(opts: { path: string; screenId?: number }): Promise<true>;
  stop(): Promise<TinyRecording>;
}

declare interface TinySayOptions {
  /** a voice id from voices(), or a BCP-47 language like 'en-AU' */
  voice?: string;
  /** 0..1 (~0.5 default) */
  rate?: number;
}

declare interface TinyOpenWindowOptions {
  /** html file in your frontend dir (e.g. 'settings.html') or absolute path */
  page?: string;
  title?: string;
  /** 'WxH', e.g. '420x300' */
  size?: string;
  /** applied BEFORE first paint — no titlebar flash for frameless panels */
  chrome?: TinyChromeOptions;
  /** top-left screen position, applied before the window is shown */
  x?: number;
  y?: number;
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
  /** pixel dimensions of `image` */
  imageSize: { width: number; height: number } | null;
  /** '#rrggbb' or '#rrggbbaa' (kind 'color') */
  color: string | null;
  /** org.nspasteboard Concealed/Transient marker (password managers) —
   *  clipboard-history apps must skip these */
  concealed: boolean;
  /** app the content came from (frontmost when the change was noticed;
   *  exact while watch() runs, best-effort otherwise) */
  sourceApp: { name: string | null; bundleId: string | null } | null;
  /** page URL a Chromium-browser copy came from */
  sourceURL: string | null;
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

/** 'automation' checks System Events; 'automation:<bundle-id>' any target.
 *  Note: 'screen' never reports 'undetermined' — macOS only exposes a
 *  yes/no preflight for screen recording, so it reads 'denied' until the
 *  user grants it in System Settings. */
declare type TinyPermissionName =
  | 'accessibility' | 'screen' | 'notifications'
  | 'microphone' | 'camera'
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

declare interface TinyScreen {
  /** CGDirectDisplayID */
  id: number;
  /** e.g. 'Built-in Retina Display' (null before macOS 10.15) */
  name: string | null;
  /** display frame — same top-left coordinates as win.setPosition */
  x: number;
  y: number;
  width: number;
  height: number;
  /** frame minus the menu bar and Dock */
  visible: { x: number; y: number; width: number; height: number };
  scale: number;
  /** the menu-bar screen (the coordinate origin) */
  primary: boolean;
}

/** Standard per-app directories; data/cache/logs are per app id and NOT
 *  auto-created. Prefer these over hardcoding ~/Library paths. */
declare interface TinyPaths {
  home: string;
  data: string;
  cache: string;
  logs: string;
  temp: string;
  downloads: string;
  desktop: string;
  documents: string;
}

/** 'requires-approval': the user must allow the item in System Settings >
 *  General > Login Items. 'unsupported': not a packaged .app (dev mode has
 *  no bundle identity to register) or macOS < 13. */
declare type TinyLoginStatus =
  'enabled' | 'disabled' | 'requires-approval' | 'unsupported';

/** NSWorkspace verbs — resolve true, reject with the reason on failure. */
declare interface TinyShell {
  /** open a URL (any scheme) or file path in the default app */
  open(target: string): Promise<true>;
  /** show the file in Finder */
  reveal(path: string): Promise<true>;
  /** move to the Trash (recoverable — prefer over deleting user files) */
  trash(path: string): Promise<true>;
}

declare interface TinyLaunchAtLogin {
  get(): Promise<TinyLoginStatus>;
  /** returns the resulting status */
  set(enabled: boolean): Promise<TinyLoginStatus>;
}

/** The active application (frontmostApp / clipboard sourceApp). */
declare interface TinyFrontmostApp {
  name: string | null;
  bundleId: string | null;
  pid: number;
}

/** Keep the system awake — one IOPMAssertion, replaced per call and
 *  released automatically when the app exits (unlike spawned caffeinate). */
declare interface TinyPower {
  /** reason shows in `pmset -g assertions`; display: true also keeps the
   *  screen on */
  preventSleep(reason?: string, opts?: { display?: boolean }): Promise<boolean>;
  allowSleep(): Promise<boolean>;
}

/** A captured screenshot; path is a png in the temp dir the caller owns. */
declare interface TinyCapture {
  path: string;
  width: number;
  height: number;
}

declare interface TinyOcrBlock {
  text: string;
  confidence: number;
  /** normalized 0..1, top-left origin */
  box: { x: number; y: number; width: number; height: number };
}

/** On-device Vision OCR result; text joins the blocks with newlines. */
declare interface TinyOcrResult {
  text: string;
  blocks: TinyOcrBlock[];
}

/** A generated thumbnail; path is a temp png the caller owns. */
declare interface TinyThumbnail {
  path: string;
  width: number;
  height: number;
}

/** Keychain-backed secrets (generic passwords under the app id) — the
 *  keytar/safeStorage role. Use for tokens; never tiny.store. */
declare interface TinySecrets {
  get(key: string): Promise<string | null>;
  set(key: string, value: string): Promise<boolean>;
  delete(key: string): Promise<boolean>;
}

/** Fired by "update": { "auto": … } when a newer version is available. */
declare interface TinyUpdateInfo {
  current: string;
  latest: string;
  notes: string | null;
}

declare interface TinyShareOptions {
  text?: string;
  url?: string;
  /** real file paths */
  paths?: string[];
  /** anchor at page coordinates (the click's clientX/clientY) */
  x?: number;
  y?: number;
}

/** The `tiny` global available in every window's page. */
/** Options for tiny.fetch — a superset of the common RequestInit fields, plus
 *  `stream` to opt into a live streaming body. */
declare interface TinyFetchInit {
  method?: string;
  headers?: HeadersInit;
  body?: string | ArrayBuffer | ArrayBufferView | Blob | URLSearchParams;
  redirect?: RequestRedirect;
  /** Stream the response body instead of buffering it. The resolved Response's
   *  `body` pulls chunks from the backend on demand (backpressured) — required
   *  for endless sources like internet radio, where a buffered fetch would
   *  never resolve. */
  stream?: boolean;
}

declare interface Tiny {
  api: {
    /** Call a backend api method; resolves with its return value. */
    call(method: string, params?: unknown): Promise<any>;
    /** Subscribe to backend push events (app.push / broadcasts). */
    on(event: string, fn: (data: any) => void): void;
  };

  /** Like window.fetch, but the request runs in the backend (a native
   *  process) — no CORS, CSP, or mixed-content limits, so the page can reach
   *  any origin. Resolves to a real Response. Small responses arrive whole;
   *  pass { stream: true } for a live streaming body (res.body.getReader()). */
  fetch(url: string, init?: TinyFetchInit): Promise<Response>;

  /** A same-app URL (tiny-media://…) that streams a remote http(s) resource
   *  through the native layer with permissive CORS. Drop it into a media
   *  element to get a cross-origin stream (e.g. internet radio) into Web
   *  Audio — a MediaElementSource on a cross-origin <audio> outputs silence by
   *  spec, but this URL is CORS-approved so the EQ/analyser graph gets real
   *  samples. Set the element's crossOrigin to 'anonymous':
   *    audio.crossOrigin = 'anonymous';
   *    audio.src = tiny.proxyURL('https://example.com/stream.mp3'); */
  proxyURL(url: string): string;

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
    /** mouse events pass through to whatever is behind the window */
    setClickThrough(enabled: boolean): Promise<any>;
    setLevel(level: TinyWindowLevel): Promise<any>;
    /** follow the user across every Space + float over fullscreen apps */
    setAllSpaces(enabled: boolean): Promise<any>;
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
    /** render the page to a PDF file (vector) */
    printToPDF(path: string): Promise<{ path: string }>;
    /** files dragged onto the window — real filesystem paths */
    onDrop(fn: (paths: string[]) => void): void;

    /** native share sheet — anchor at the click's clientX/clientY */
    share(opts?: TinyShareOptions): Promise<any>;
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

  /**
   * Read the app's (or the system's) *rendered* audio output as PCM chunks —
   * for VU meters / visualizers, including audio that bypasses Web Audio
   * (native HLS, CORS-tainted streams). Read-only: it observes the mix, it
   * can't process it (EQ still needs the signal in the graph — see proxyURL).
   * Requires an `"audioTap"` key in tinyjs.json. macOS 14.4+.
   */
  audioTap: {
    /**
     * Start tapping. Resolves `true`, or throws an Error whose `.code` is one
     * of `'unsupported'` (pre-14.4), `'not-declared'` (manifest missing the
     * scope), `'denied'` (TCC refused — surfaces as silent chunks) or
     * `'failed'` (Core Audio error). Idempotent for identical options.
     *
     * Authorization is deferred to this call (declaring the manifest key does
     * nothing until you call it) — so you can lazy-arm the tap. The *first*
     * `start()` prompts for "System Audio Recording", even for `scope:'app'`
     * (WKWebView renders audio in a separate `com.apple.WebKit.GPU` helper, so
     * the tap is a cross-process capture); the grant persists per app.
     */
    start(opts?: {
      /** 'app' (default) = this app's own output; 'system' = everything. */
      scope?: 'app' | 'system';
      /** system scope only: drop this app's own output from the mix. */
      excludeSelf?: boolean;
      /** ms of audio per chunk, clamped ~[20, 500] (default 80). */
      interval?: number;
    }): Promise<true>;
    stop(): Promise<any>;
    /**
     * Register a chunk handler. `pcm` is base64 of interleaved little-endian
     * Int16 (decode: `s[i] = int16(bin[2i] | bin[2i+1]<<8); float = s[i]/32768`);
     * `t` is the monotonic ms of the chunk's first sample.
     */
    on(fn: (chunk: {
      pcm: string;
      sampleRate: number;
      channels: number;
      frames: number;
      t: number;
    }) => void): void;
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
    /** every display, same top-left coords as win.setPosition */
    screens(): Promise<TinyScreen[]>;
    /** standard per-app directories */
    paths(): Promise<TinyPaths>;
    shell: TinyShell;
    launchAtLogin: TinyLaunchAtLogin;
    dock: {
      /** '' clears the badge */
      setBadge(text: string): Promise<any>;
      /** bounce until activated; critical: until the user acts */
      bounce(opts?: { critical?: boolean }): Promise<any>;
    };
    power: TinyPower;
    /** the active app right now (who focus returns to after win.hide()) */
    frontmostApp(): Promise<TinyFrontmostApp | null>;
    /** text selected in the frontmost app (Accessibility); null if none */
    selectedText(): Promise<string | null>;
    /** other apps' on-screen windows (Accessibility); null if not granted */
    otherWindows(): Promise<TinyOtherWindow[] | null>;
    /** move/resize another app's frontmost window (pid from otherWindows) */
    moveWindow(pid: number, rect: { x: number; y: number; width: number; height: number }): Promise<true>;
    /** trackpad haptic feedback (no-op without a Force Touch trackpad) */
    haptic(pattern?: 'generic' | 'alignment' | 'level'): Promise<any>;
    /** replace the Dock icon from a png ('' resets to the bundle icon) */
    dockIcon(path: string): Promise<any>;
    battery(): Promise<TinyBattery | null>;
    wifi(): Promise<TinyWifi | null>;
    /** find files by name/content (Spotlight) — up to 100 paths */
    spotlight(query: string): Promise<string[]>;
    /** on-device LLM (FoundationModels; TINYJS_AI builds on macOS 26) */
    ai: TinyAi;
    beep(): Promise<boolean>;
    /** a system sound name ('Ping', 'Glass', …) or an audio file path;
     *  false if it didn't load */
    playSound(target: string): Promise<boolean>;
    /** seconds since the user's last input, session-wide */
    idleTime(): Promise<number>;
    /** Quick Look panel for the path(s); no args closes it */
    quickLook(paths?: string | string[] | null): Promise<any>;
    /** screenshot a display (id from screens(); default primary) — png in
     *  the temp dir, caller owns the file; needs the 'screen' permission
     *  and macOS 14+, rejects with the reason otherwise */
    captureScreen(screenId?: number): Promise<TinyCapture>;
    /** system eyedropper — NO screen-recording permission needed;
     *  '#rrggbb', or null if the user cancels */
    pickColor(): Promise<string | null>;
    /** on-device OCR (Vision, accurate mode) */
    ocr(path: string): Promise<TinyOcrResult>;
    /** thumbnail png for ANY file type Quick Look understands; size is the
     *  bounding box in points (rendered @2x) */
    thumbnail(path: string, size?: number): Promise<TinyThumbnail>;
    secrets: TinySecrets;
    /** Touch ID (or the account-password sheet); false covers cancel */
    authenticate(reason?: string): Promise<boolean>;
    /** run AppleScript in-process (no osascript spawn; 'automation' TCC);
     *  resolves the result as a string, null if it isn't text; rejects
     *  with the script error message */
    applescript(source: string): Promise<string | null>;
    onNotificationClick(fn: (id: string) => void): void;
    /** action button / reply field on a notification was used */
    onNotificationAction(fn: (info: TinyNotificationAction) => void): void;
    nowPlaying: {
      /** also arms the media keys */
      set(info: TinyNowPlaying): Promise<any>;
      clear(): Promise<any>;
    };
    /** a hardware media key / Control Center transport fired */
    onMediaKey(fn: (info: TinyMediaKey) => void): void;
    /** speak text; resolves when playback finishes (false if interrupted) */
    say(text: string, opts?: TinySayOptions): Promise<boolean>;
    stopSpeaking(): Promise<any>;
    voices(): Promise<TinyVoice[]>;
    /** record a display to an .mp4 (video only, one at a time) */
    recorder: TinyRecorder;
  };

  tray: {
    set(spec: TinyTraySpec): Promise<any>;
    remove(): Promise<any>;
    /** the tray icon's rect { x, y, width, height } (top-left) | null */
    position(): Promise<{ x: number; y: number; width: number; height: number } | null>;
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
  setClickThrough(enabled: boolean): void;
  setLevel(level: TinyWindowLevel): void;
  setAllSpaces(enabled: boolean): void;
  setChrome(opts: TinyChromeOptions): void;
  getState(): Promise<TinyWinState>;
  /** native share sheet anchored at page coordinates in this window */
  share(opts?: TinyShareOptions): boolean;
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
  setClickThrough(enabled: boolean): void;
  setLevel(level: TinyWindowLevel): void;
  setAllSpaces(enabled: boolean): void;
  setHideOnClose(enabled: boolean): void;
  setDockVisible(visible: boolean): void;
  print(): void;
  /** render the page to a PDF file (vector) */
  printToPDF(path: string): Promise<{ path: string }>;
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
    position(): Promise<{ x: number; y: number; width: number; height: number } | null>;
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
  /** every display, same top-left coords as setPosition */
  screens(): Promise<TinyScreen[]>;
  /** standard per-app directories */
  paths: TinyPaths;
  shell: TinyShell;
  launchAtLogin: TinyLaunchAtLogin;
  dock: {
    /** '' clears the badge */
    setBadge(text: string): boolean;
    /** bounce until activated; critical: until the user acts */
    bounce(opts?: { critical?: boolean }): boolean;
  };
  power: TinyPower;
  /** the active app right now (who focus returns to after hide()) */
  frontmostApp(): Promise<TinyFrontmostApp | null>;
  /** text selected in the frontmost app (Accessibility); null if none */
  selectedText(): Promise<string | null>;
  /** other apps' on-screen windows (Accessibility); null if not granted */
  otherWindows(): Promise<TinyOtherWindow[] | null>;
  /** move/resize another app's frontmost window (pid from otherWindows) */
  moveWindow(pid: number, rect: { x: number; y: number; width: number; height: number }): Promise<true>;
  /** trackpad haptic feedback (no-op without a Force Touch trackpad) */
  haptic(pattern?: 'generic' | 'alignment' | 'level'): boolean;
  /** replace the Dock icon from a png ('' resets to the bundle icon) */
  dockIcon(path: string): boolean;
  battery(): Promise<TinyBattery | null>;
  wifi(): Promise<TinyWifi | null>;
  /** find files by name/content (Spotlight) — up to 100 paths */
  spotlight(query: string): Promise<string[]>;
  /** on-device LLM (FoundationModels; TINYJS_AI builds on macOS 26) */
  ai: TinyAi;
  beep(): Promise<boolean>;
  /** a system sound name ('Ping', 'Glass', …) or an audio file path;
   *  false if it didn't load */
  playSound(target: string): Promise<boolean>;
  /** seconds since the user's last input, session-wide */
  idleTime(): Promise<number>;
  /** Quick Look panel for the path(s); no args closes it */
  quickLook(paths?: string | string[] | null): boolean;
  /** screenshot a display (id from screens(); default primary) — png in
   *  the temp dir, caller owns the file; needs the 'screen' permission
   *  and macOS 14+, rejects with the reason otherwise */
  captureScreen(screenId?: number): Promise<TinyCapture>;
  /** system eyedropper — NO screen-recording permission needed;
   *  '#rrggbb', or null if the user cancels */
  pickColor(): Promise<string | null>;
  /** on-device OCR (Vision, accurate mode) */
  ocr(path: string): Promise<TinyOcrResult>;
  /** thumbnail png for ANY file type Quick Look understands; size is the
   *  bounding box in points (rendered @2x) */
  thumbnail(path: string, size?: number): Promise<TinyThumbnail>;
  secrets: TinySecrets;
  /** Touch ID (or the account-password sheet); false covers cancel */
  authenticate(reason?: string): Promise<boolean>;
  /** run AppleScript in-process (no osascript spawn; 'automation' TCC);
   *  resolves the result as a string, null if it isn't text; rejects
   *  with the script error message */
  applescript(source: string): Promise<string | null>;
  nowPlaying: {
    /** also arms the media keys */
    set(info: TinyNowPlaying): boolean;
    clear(): boolean;
  };
  /** speak text; resolves when playback finishes (false if interrupted) */
  say(text: string, opts?: TinySayOptions): Promise<boolean>;
  stopSpeaking(): boolean;
  voices(): Promise<TinyVoice[]>;
  /** record a display to an .mp4 (video only, one at a time) */
  recorder: TinyRecorder;
  update: {
    /** notes = release notes from the manifest ("tinyjs publish --notes") */
    check(): Promise<{ available: boolean; current: string; latest: string | null; notes: string | null }>;
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
