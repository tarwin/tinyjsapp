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
  /**
   * ⌘+<key> shortcut (menu bar items). An uppercase letter carries its own
   * shift — 'S' is ⌘⇧S. Anything more is spelled with prefixes: 'alt+p' is
   * ⌥⌘P, 'alt+shift+f' is ⌥⇧⌘F. ⌘ (Ctrl on Windows/Linux) is always in.
   */
  key?: string;
  /** show a ✓ checkmark */
  checked?: boolean;
  /** false = grayed out */
  enabled?: boolean;
  submenu?: TinyMenuItem[];
  separator?: boolean;
}

declare interface TinyMenu {
  title?: string;
  items?: TinyMenuItem[];
  /**
   * A standard menu the launcher builds itself, taking this slot in the bar
   * instead of a menu of your own. 'edit' is macOS's Edit menu (Undo, Cut,
   * Copy, Paste, Select All): it is always installed — the webview needs its
   * key equivalents — and goes first unless you say where it belongs, which
   * is how you get File before Edit. Windows and Linux have no such menu and
   * skip the entry.
   */
  role?: 'edit';
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
  /** the close/minimize/maximize group (macOS's "traffic lights"):
   *  true | false | a subset such as ['close'] | [] for none */
  windowControls?: boolean | Array<'close' | 'minimize' | 'maximize'>;
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
  /** false: this window shows no menu bar. The app menu carries on in every
   *  other window, and this one's accelerators keep firing — only the bar is
   *  gone. Windows and Linux draw the bar inside each window, so it's a real
   *  per-window question there; macOS has one bar for the whole app and
   *  ignores the flag. Set it in tinyjs.json "chrome" (or win.open's chrome)
   *  to apply before first paint — no bar flashing in and out, and `size`
   *  still means the page's box either way. */
  menu?: boolean;
}

/** 'floating' = always-on-top; 'overlay' floats above almost everything
 *  (incl. most fullscreen apps); 'desktop' pins behind normal windows. */
declare type TinyWindowLevel = 'normal' | 'floating' | 'overlay' | 'desktop';

/** The portable sound names for app.playSound(). Every OS ships alert sounds
 *  but none agree on their names, so these four ask for a meaning and get the
 *  platform's nearest equivalent. Platform names and file paths also work —
 *  they're just not portable. */
declare type TinySoundName = 'info' | 'success' | 'alert' | 'error';

/** On-device LLM (Apple FoundationModels). 'available' = ready; 'unavailable'
 *  = Apple Intelligence off / model not downloaded; 'unsupported' = older
 *  macOS, or a launcher compiled without the FoundationModels shim. */
declare type TinyAiAvailability = 'available' | 'unavailable' | 'unsupported';

/** A tool the on-device model may call. `run` is YOUR function; whatever it
 *  returns goes back to the model (objects are JSON'd). Backend only — a real
 *  function can't cross the bridge from a page. */
declare interface TinyAiTool {
  name: string;
  description: string;
  /** { x: 'integer' } or { x: { type: 'integer', description: 'why' } }.
   *  Types: string | integer | number | boolean. */
  parameters?: Record<string, string | { type?: string; description?: string }>;
  run(args: Record<string, any>): any | Promise<any>;
}

/** What the model actually invoked — the record to trust, not the prose. */
declare interface TinyAiCall {
  name: string;
  args: Record<string, any>;
  result: any;
}

declare interface TinyAi {
  availability(): Promise<TinyAiAvailability>;
  /** offline, no API key. opts.instructions = a system prompt. Throws with
   *  the reason (incl. 'not built in' where the shim wasn't compiled). */
  generate(prompt: string, opts?: { instructions?: string }): Promise<string>;
}

/** The backend's AI surface: same as TinyAi, plus tool calling. */
declare interface TinyAiBackend extends TinyAi {
  generate(prompt: string, opts?: { instructions?: string }): Promise<string>;
  /** With tools, resolves { text, calls } — and `calls` is what happened.
   *  The model skips tools it was asked for and its prose says it didn't:
   *  measured 3-of-3 in one run out of four, with the text claiming all three
   *  every time. Never read completion off `text`. */
  generate(prompt: string, opts: { instructions?: string; tools: TinyAiTool[] }):
    Promise<{ text: string; calls: TinyAiCall[] }>;
}

/** The user's language preferences and time zone, read from the OS. */
declare interface TinyLocale {
  /** best match for what the app should render, e.g. 'en-AU' */
  language: string;
  /** preference order, FILTERED to what the app bundle declares it speaks */
  languages: string[];
  /** the raw system preference, whether or not this app speaks it — differs
   *  from `languages` for an English-only app on a French Mac */
  system: string[];
  /** ISO country code from the current locale, e.g. 'US'; null if unset */
  region: string | null;
  /** IANA name, e.g. 'America/Los_Angeles' */
  timeZone: string;
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

/** One filter in a tiny.audio chain. Biquad types take freq/q/gain (gain in
 *  dB); 'gain' is a LINEAR multiplier (a preamp). gainR — the right channel's
 *  gain when it differs — is native-only (the page chain ignores it; use
 *  balance()). */
declare interface TinyAudioFilter {
  type: 'peaking' | 'lowshelf' | 'highshelf' | 'lowpass' | 'highpass'
      | 'bandpass' | 'notch' | 'allpass' | 'gain';
  freq?: number;
  q?: number;
  gain?: number;
  gainR?: number;
}

/** tiny.audio.pageChain(ctx) — the same chain as Web Audio nodes in the page.
 *  Route your source through input → output; the four verbs then match
 *  tiny.audio, so one variable can hold either backend. */
declare interface TinyPageChain {
  input: GainNode;
  output: GainNode;
  filters(list: TinyAudioFilter[]): boolean;
  filter(index: number, patch: Partial<TinyAudioFilter>): boolean;
  balance(v: number): boolean;
  clear(): boolean;
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
  /** frame top-left in screen coordinates — the units setPosition takes */
  x: number;
  y: number;
  /** the PAGE's box, decorations excluded — the units win.open's `size`,
   *  setSize and setMinSize take, so set -> get round-trips */
  width: number;
  height: number;
  /** the footprint on screen, decorations included. Equals width/height for a
   *  frameless window. (window.outerWidth/outerHeight are 0 in a WKWebView, so
   *  a page keeping itself inside a screen rect needs this.) */
  outer: { width: number; height: number };
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
    /** what is actually shown; null where the OS won't say (Linux) */
    windowControls: Array<'close' | 'minimize' | 'maximize'> | null;
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
  /** 'WxH', e.g. '420x300' — the PAGE's box, decorations excluded */
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

declare interface TinyHideOptions {
  /** false: put away THIS WINDOW only — the app stays frontmost. Default
   *  true, which on macOS hides the app when the window is 'main' */
  app?: boolean;
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

  /** A correct file:// URL for a disk path on BOTH platforms — use for
   *  <audio>/<img>/<video> src of backend-provided paths. Hand-rolled
   *  'file://' + path breaks on Windows (the drive letter becomes the URL
   *  host). */
  fileURL(path: string): string;

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
    /** resize the PAGE's box (decorations excluded), top-left anchored —
     *  the same units getState().width/height reports back */
    setSize(width: number, height: number): Promise<any>;
    /** hides the APP (NSApp hide): focus returns to the previous app —
     *  palettes can hide-then-paste with no frontmost tracking.
     *  hide({ app: false }) puts away just this window instead */
    hide(opts?: TinyHideOptions): Promise<any>;
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
    /** true: the close button hides the window instead of quitting. A macOS
     *  idea — the app outlives its last window and the Dock icon brings it
     *  back. Windows and Linux have nowhere to put that, so there it holds
     *  only while something can bring the app back (a tray icon, accessory
     *  mode, or another window still up); closing the last window of an
     *  ordinary app quits it. */
    setHideOnClose(enabled: boolean): Promise<any>;
    print(): Promise<any>;
    /** render the page to a PDF file (vector) */
    printToPDF(path: string): Promise<{ path: string }>;
    /** files dragged onto the window — real filesystem paths */
    onDrop(fn: (paths: string[]) => void): void;

    /** native share sheet — anchor at the click's clientX/clientY */
    share(opts?: TinyShareOptions): Promise<any>;

    /** This window's OWN menu bar, overriding the app menu for it alone.
     *  Same spec and same click event as tiny.menu; windows that don't
     *  override keep showing the app menu, and so do windows opened later.
     *  macOS has one bar for the whole app, so it swaps this one in while the
     *  window is key and puts the app menu back when it isn't.
     *  To hide a bar rather than change it, use chrome.menu:false. */
    menu: {
      set(menus: TinyMenu[]): Promise<any>;
      /** back to showing the app menu */
      reset(): Promise<any>;
      /** patch an item in THIS window's bar only */
      update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): Promise<any>;
      get(id: string): Promise<TinyMenuItemState>;
    };
  };

  menu: {
    /** The APP menu: every window shows it, including windows opened later.
     *  (macOS has one bar for the whole app; Windows and Linux draw a copy of
     *  it inside each window.) For one window to differ, see tiny.win.menu;
     *  for one window to show no bar at all, chrome.menu:false. */
    set(menus: TinyMenu[]): Promise<any>;
    on(fn: (id: string) => void): void;
    /** patch a live item without redeclaring the menu — every window's copy of
     *  the id moves. tiny.win.menu.update patches just one window's. */
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
   * Native DSP on the app's OWN output — a graphic EQ, headphone correction —
   * applied below the browser, so it reaches audio the page never gets
   * samples for (native HLS, CORS-tainted streams) and survives reloads.
   * Linux (PipeWire) and macOS 14.2+ (muted process tap); on Windows
   * `capabilities().audioFilters` is false — silencing the direct path there
   * means persisted mixer state every WebView2 app shares — so use
   * `pageChain` instead, which speaks the same verbs.
   */
  audio: {
    /** Replace the whole chain. Idempotent; [] restores unprocessed output.
     *  At most 28 filters (15 with gainR) — the list is truncated past that. */
    filters(list: TinyAudioFilter[]): Promise<any>;
    /** Retune ONE filter in place — no rebuild, no gap; what a slider drag
     *  should call */
    filter(index: number, patch: Partial<TinyAudioFilter>): Promise<any>;
    /** Stereo balance -1 (left) .. 1 (right), applied to the chain's output —
     *  costs no filter slot; needs an active chain */
    balance(v: number): Promise<any>;
    clear(): Promise<any>;
    /** The same chain as Web Audio nodes IN THE PAGE — the fallback where the
     *  native chain doesn't exist (Windows). Same RBJ curves, same verbs;
     *  page-SCOPED: filters only what you route through it. Shelf `q` and
     *  per-filter `gainR` are ignored here. Don't use on Linux — Web Audio
     *  reaching ctx.destination crackles there, which is why the native
     *  chain exists. */
    pageChain(ctx: BaseAudioContext): TinyPageChain;
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

  /** Native dialogs, run by the launcher — NSOpenPanel / NSAlert on macOS,
   *  the common item dialog + MessageBox on Windows, GTK's chooser and
   *  message dialogs on Linux. Application-modal rather than attached to a
   *  window, which is why they aren't on `win`. */
  dialog: {
    openFile(): Promise<string | null>;
    openFiles(): Promise<string[] | null>;
    pickFolder(): Promise<string | null>;
    saveFile(): Promise<string | null>;
    alert(message: string, detail?: string): Promise<true>;
    confirm(message: string, opts?: { detail?: string; ok?: string; cancel?: string }): Promise<boolean>;
    prompt(message: string, opts?: { default?: string; ok?: string; cancel?: string }): Promise<string | null>;
  };

  /** The machine: what it is, what it can do, and its live state. Facts,
   *  not actions — anything the APP does lives on `app`. */
  system: {
    /** synchronous and safe during page setup (the webview's UA is decisive) */
    os(): 'macos' | 'windows' | 'linux';
    isMacOS(): boolean;
    isWindows(): boolean;
    isLinux(): boolean;
    info(): Promise<{ os: string; arch: string; session: string | null; desktop: string | null }>;
    architecture(): Promise<string>;
    /** what this machine can actually do — anything ABSENT is supported */
    capabilities(): Promise<Record<string, boolean | string>>;
    /** the user's languages + time zone, read from the OS. A page already has
     *  navigator.language(s), Intl and the 'languagechange' event; reach for
     *  this on the BACKEND (txiki has no Intl at all), or when you want the
     *  SYSTEM preference rather than what this app declares it speaks. */
    locale(): Promise<TinyLocale>;
    requirements(ids?: string[] | null, opts?: { refresh?: boolean }): Promise<any[]>;
    missing(ids?: string[] | null): Promise<any[]>;
    promptMissing(ids?: string[] | null, opts?: object): Promise<boolean>;
    /** live machine state */
        /** seconds since the user's last input, session-wide */
    };

  /** macOS-only: concepts the other OSes have no equivalent for at all.
   *  These reject on Windows and Linux — guard with tiny.system.isMacOS().
   *  Anything that COULD exist elsewhere stays on `app` and answers
   *  'unsupported' until it does. */
  macos: {
    /** run AppleScript in-process (no osascript spawn; 'automation' TCC);
     *  resolves the result as a string, null if it isn't text; rejects
     *  with the script error message */
    applescript(source: string): Promise<string | null>;
    /** Quick Look panel for the path(s); no args closes it */
    quickLook(paths?: string | string[] | null): Promise<any>;
     /** on-device OCR via Vision (accurate mode); box normalized 0..1 */
    ocr(path: string): Promise<TinyOcrResult>;
    /** record a display to an .mp4 (macOS 14+, needs the 'screen' permission;
     *  video only, one at a time) */
    recorder: TinyRecorder;
    /** on-device LLM (FoundationModels) — needs macOS 26 with Apple
     *  Intelligence on; check availability() first, always */
    ai: TinyAi;
    /** text selected in the frontmost app (Accessibility); null if none */
    selectedText(): Promise<string | null>;
    /** other apps' on-screen windows (Accessibility); null if not granted */
    otherWindows(): Promise<TinyOtherWindow[] | null>;
    /** move/resize another app's frontmost window (pid from otherWindows) */
    moveWindow(pid: number, rect: { x: number; y: number; width: number; height: number }): Promise<true>;
  };
  app: {
    info(): Promise<TinyAppInfo>;
    /** 'menubar': no Dock icon / taskbar button / app switcher entry */
    presence(mode: 'normal' | 'menubar'): Promise<any>;
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
    /** become the default opener for a file extension, or 'folder'.
     *  'ok' | 'unsupported' (macOS/Windows today) | 'failed'. Call it when
     *  the USER asks — silently claiming an extension is how apps get
     *  uninstalled. */
    setAsDefaultHandler(ext: string): Promise<'ok' | 'unsupported' | 'failed'>;
    shell: TinyShell;
    launchAtLogin: TinyLaunchAtLogin;
    /** '' clears the badge */
    badge(text: string): Promise<any>;
    /** bounce / flash / urgency-hint until activated; critical: until the
     * user acts */
    attention(opts?: { critical?: boolean }): Promise<any>;
    power: TinyPower;
    /** the active app right now (who focus returns to after win.hide()) */
    frontmostApp(): Promise<TinyFrontmostApp | null>;
    /** replace the app icon from a png ('' resets to the bundle icon) */
    icon(path: string): Promise<any>;
    /** progress bar on the app icon / taskbar button: 0..1, null clears */
    progress(value: number | null): Promise<any>;
    /** find files by name/content (Spotlight) — up to 100 paths */
    spotlight(query: string): Promise<string[]>;
    /** the system alert beep — the one portable sound */
    beep(): Promise<boolean>;
    /** 'info' | 'success' | 'alert' | 'error' (portable — mapped to each OS's
     *  nearest sound), a platform sound name ('Glass' macOS, 'SystemHand'
     *  Windows, 'complete' Linux — these do NOT port), or an audio file path.
     *  Resolves false if the name/file didn't load. */
    playSound(target: TinySoundName | (string & {})): Promise<boolean>;
    /** screenshot a display (id from screens(); default primary) — png in
     *  the temp dir, caller owns the file; needs the 'screen' permission
     *  and macOS 14+, rejects with the reason otherwise */
    captureScreen(screenId?: number): Promise<TinyCapture>;
    /** system eyedropper — NO screen-recording permission needed;
     *  '#rrggbb', or null if the user cancels */
    pickColor(): Promise<string | null>;
    /** thumbnail png for ANY path — a content preview where Quick Look has a
     *  renderer (images, video, PDF, source files), the document/app/folder
     *  ICON otherwise, so this never fails on file type alone (macOS and
     *  Windows; Linux is images-only and rejects the rest). size is the
     *  bounding box, aspect preserved — rendered @2x on macOS/Linux, exact
     *  pixels on Windows, so read width/height off the result. Rejects if the
     *  path doesn't exist. */
    thumbnail(path: string, size?: number): Promise<TinyThumbnail>;
    secrets: TinySecrets;
    /** Touch ID (or the account-password sheet); false covers cancel */
    authenticate(reason?: string): Promise<boolean>;
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
  /** resize the PAGE's box (decorations excluded), top-left anchored */
  setSize(width: number, height: number): void;
  setPosition(x: number, y: number): void;
  center(): void;
  /** hide({ app: false }) puts away this window alone; on 'main' a bare
   *  hide() hides the whole app (macOS) */
  hide(opts?: TinyHideOptions): void;
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
  /** This window's OWN menu bar, overriding the app menu for it alone.
   *  Clicks arrive the same way either bar sent them, so a handler doesn't
   *  have to care which. macOS shows it while the window is key. */
  setMenu(menus: TinyMenu[]): void;
  /** drop the override: back to showing the app menu */
  resetMenu(): void;
  /** patch an item in THIS window's bar, leaving other windows' copies alone */
  updateMenuItem(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
  getMenuItem(id: string): Promise<TinyMenuItemState>;
  /** print THIS window's page (its own print panel) */
  print(): void;
  /** render THIS window's page to a PDF file (vector) */
  printToPDF(path: string): Promise<{ path: string }>;
  /** native share sheet anchored at page coordinates in this window */
  share(opts?: TinyShareOptions): boolean;
}

/** The backend `app` handle (passed to init, api handlers, and events). */
declare interface TinyApp {
  /** push an event to every window (tiny.api.on) */
  push(event: string, data?: unknown): void;
  setTitle(title: string): void;
  setSize(width: number, height: number): void;
  /** The APP menu — every window shows it, including ones opened later.
   *  app.window(id).setMenu gives one window something else. */
  setMenu(menus: TinyMenu[]): void;
  /** patch a live item: every window's copy of the id moves */
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
  /** true: the close button hides the window instead of quitting. macOS keeps
   *  the app alive without windows (the Dock brings it back); Windows and
   *  Linux honour it only while a tray icon, accessory mode or another window
   *  offers a way back — otherwise the last close quits, as it does there. */
  setHideOnClose(enabled: boolean): void;
  presence(mode: 'normal' | 'menubar'): void;
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
  /** '' clears the badge */
  badge(text: string): boolean;
  /** bounce / flash / urgency-hint until activated; critical: until the user
   * acts */
  attention(opts?: { critical?: boolean }): boolean;
  power: TinyPower;
  /** the active app right now (who focus returns to after hide()) */
  frontmostApp(): Promise<TinyFrontmostApp | null>;
  /** replace the app icon from a png ('' resets to the bundle icon) */
  icon(path: string): boolean;
  /** macOS-only — same shape and names as tiny.macos.* in the page. These
   *  reject on Windows and Linux. */
  /** machine state — mirrors tiny.system.* in the page */
  system: {
    battery(): Promise<TinyBattery | null>;
    wifi(): Promise<TinyWifi | null>;
    idleTime(): Promise<number>;
  };
  macos: {
    applescript(source: string): Promise<string | null>;
    quickLook(paths?: string | string[] | null): boolean;
    /** on-device OCR via Vision (accurate mode); box normalized 0..1 */
    ocr(path: string): Promise<TinyOcrResult>;
    /** record a display to an .mp4 (macOS 14+, needs the 'screen' permission;
     *  video only, one at a time) */
    recorder: TinyRecorder;
    /** on-device LLM (FoundationModels) — needs macOS 26 with Apple
     *  Intelligence on; check availability() first, always */
    ai: TinyAiBackend;
    /** text selected in the frontmost app (Accessibility); null if none */
    selectedText(): Promise<string | null>;
    /** other apps' on-screen windows (Accessibility); null if not granted */
    otherWindows(): Promise<TinyOtherWindow[] | null>;
    /** move/resize another app's frontmost window (pid from otherWindows) */
    moveWindow(pid: number, rect: { x: number; y: number; width: number; height: number }): Promise<true>;
  };
  /** progress bar on the app icon / taskbar button: 0..1, null clears */
  progress(value: number | null): boolean;
  battery(): Promise<TinyBattery | null>;
  wifi(): Promise<TinyWifi | null>;
  /** find files by name/content (Spotlight) — up to 100 paths */
  spotlight(query: string): Promise<string[]>;
  /** the system alert beep — the one portable sound */
  beep(): Promise<boolean>;
  /** 'info' | 'success' | 'alert' | 'error' (portable — mapped to each OS's
   *  nearest sound), a platform sound name ('Glass' macOS, 'SystemHand'
   *  Windows, 'complete' Linux — these do NOT port), or an audio file path.
   *  Resolves false if the name/file didn't load. */
  playSound(target: TinySoundName | (string & {})): Promise<boolean>;
  /** seconds since the user's last input, session-wide */
  idleTime(): Promise<number>;
  /** screenshot a display (id from screens(); default primary) — png in
   *  the temp dir, caller owns the file; needs the 'screen' permission
   *  and macOS 14+, rejects with the reason otherwise */
  captureScreen(screenId?: number): Promise<TinyCapture>;
  /** system eyedropper — NO screen-recording permission needed;
   *  '#rrggbb', or null if the user cancels */
  pickColor(): Promise<string | null>;
  /** thumbnail png for ANY file type Quick Look understands; size is the
   *  bounding box — @2x on macOS/Linux, exact pixels on Windows */
  thumbnail(path: string, size?: number): Promise<TinyThumbnail>;
  secrets: TinySecrets;
  /** Touch ID (or the account-password sheet); false covers cancel */
  authenticate(reason?: string): Promise<boolean>;
  nowPlaying: {
    /** also arms the media keys */
    set(info: TinyNowPlaying): boolean;
    clear(): boolean;
  };
  /** speak text; resolves when playback finishes (false if interrupted) */
  say(text: string, opts?: TinySayOptions): Promise<boolean>;
  stopSpeaking(): boolean;
  voices(): Promise<TinyVoice[]>;
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
