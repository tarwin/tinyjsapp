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
  /**
   * Optional icon beside the label: a png path (absolute or project-relative),
   * 'sf:<name>' for an SF Symbol (macOS only) or 'emoji:<glyph>' for a colour
   * emoji drawn at menu size (no asset needed). Shown in menu bars and
   * right-click menus on all three platforms, in tray menus on macOS and
   * Windows. Where it can't be drawn (sf: off macOS, an unreadable file,
   * Linux tray menus) the item shows its label alone.
   */
  icon?: string;
  submenu?: TinyMenuItem[];
  separator?: boolean;
  /** a stock entry in place of your own item: 'standard' is the whole stock
   *  set; standard: false on an { role: 'edit' } menu leaves macOS's out */
  role?: 'standard' | 'undo' | 'redo' | 'cut' | 'copy' | 'paste' | 'selectAll';
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
   *
   * 'app' (macOS) is the application menu itself: the `items` you declare
   * land inside it, between About and Quit, which is where the platform
   * keeps Settings… (⌘,) and the one slot setMenu could not otherwise
   * reach. They behave like any other item — ids, keys, ticks,
   * `menu.update` / `menu.get`, clicks through `menu.on`. Windows and Linux
   * have no application menu, so the role is unknown there and its items are
   * DROPPED rather than landing somewhere arbitrary — declare the same item
   * in a menu of your own for those platforms. Only the first 'app' block is
   * used.
   *
   * 'help' is the Help menu. On macOS it always goes last in the bar; when
   * the built app ships a help book (tinyjs.json `"macos": { "helpBook" }`)
   * it starts with "<App> Help" (⌘?), which opens the book, and your
   * `items` go below it (`standard: false` drops that item). macOS adds its
   * menu-search field at the top. A help book gets this menu even without
   * the role. Windows and Linux draw a plain "Help" menu of your items in
   * the slot where you declared it.
   */
  role?: 'edit' | 'app' | 'help';
  /**
   * macOS, with `role`: false leaves out the stock items the launcher would
   * add (the Edit menu's Undo…Select All group, "<App> Help").
   */
  standard?: boolean;
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
   *  'underwindow' | 'underpage' | 'titlebar' | 'selection'); null or
   *  false for none */
  vibrancy?: string | null | false;
  /** macOS: where the traffic lights sit, in points from the window's
   *  top-left; null/false puts them back */
  windowControlsPos?: { x: number; y: number } | null | false;
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
  /** macOS: a non-activating panel (Spotlight-style). show() gives it the
   *  keyboard WITHOUT activating the app, so with setAllSpaces(true) it
   *  opens over a full-screen app instead of switching Spaces; hide() just
   *  puts it away. Open-time only: tinyjs.json "chrome" (main) or win.open's
   *  chrome — setChrome can't turn it on or off. Panels aren't in the
   *  Window menu or ⌘` cycling. Ignored on Windows and Linux. */
  panel?: boolean;
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
  /** 0–100; null when the OS doesn't report it (possible on Windows/Linux) */
  percent: number | null;
  charging: boolean;
  plugged: boolean;
  /** minutes to full (charging) or empty; null while calculating, and
   *  always null on Linux */
  minutesRemaining: number | null;
}

declare interface TinyWifi {
  /** null without the Location permission on macOS 14+ — declare
   *  permissions.location in tinyjs.json, then permissions.request('location') */
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

/** A playing sampler voice (tiny.audio.sampler.play). Fire-and-forget with
 *  live updates: set() retunes without a restart, stop() fades out quickly
 *  with no click. */
declare interface TinySamplerVoice {
  id: number;
  set(patch: { vol?: number; pan?: number; rate?: number }): Promise<void>;
  stop(): Promise<void>;
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
    /** macOS: a moved traffic-light group's position; null when it's default
     *  (and on Windows/Linux) */
    windowControlsPos?: { x: number; y: number } | null;
  };
  screen: { width: number; height: number; scale: number };
  /** the screen holding most of the window — its id (match screens()) */
  screenId: number;
  /** frame top-left relative to that screen, in its points */
  local: { x: number; y: number };
  /** local x/y and the PAGE's box in that screen's pixels */
  pixels: TinyRect;
  /** Linux only: false on Wayland, where the compositor ignores setPosition */
  canPosition?: boolean;
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
  /** text beside the icon; Windows doesn't show it (it's the tooltip there
   *  when no tooltip is given) */
  title?: string;
  /** png path — absolute: a relative one is resolved against the working
   *  directory, which is the project in tinyjs dev but not in a built app
   *  (use (await paths()).resources + '/tray.png'). Template image by default.
   *  'sf:<name>' is an SF Symbol (macOS), 'emoji:<glyph>' a glyph drawn as a
   *  tray silhouette (Windows) — branch per OS for asset-free icons. */
  icon?: string;
  /** false: keep icon colors instead of adapting to the menu bar */
  template?: boolean;
  tooltip?: string;
  /** true: left click fires the click event and the menu opens on
   *  right/ctrl-click (a toggle-style tray icon) */
  primaryAction?: boolean;
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
  /** html file in your frontend dir (e.g. 'settings.html') or an http(s)
   *  URL. From a page, only files inside the frontend dir open; the backend
   *  may also pass an absolute path. */
  page?: string;
  title?: string;
  /** 'WxH', e.g. '420x300' — the PAGE's box, decorations excluded */
  size?: string;
  /** 'WxH' floor under user resizes */
  minSize?: string;
  /** applied BEFORE first paint — no titlebar flash for frameless panels */
  chrome?: TinyChromeOptions;
  /** top-left screen position, applied before the window is shown */
  x?: number;
  y?: number;
  /** x/y relative to this screen (id or a screens() entry) */
  screen?: number | TinyScreen;
  /** units for x/y with `screen`: 'pt' (default) or 'px' */
  units?: 'pt' | 'px';
  /** keep this window above another of YOUR windows (true = 'main', or a
   *  win.open id) without floating over other apps the way
   *  setLevel('floating') does. Native owner/transient/child relation: it
   *  minimizes/hides with its parent, closes when the parent closes, and on
   *  Windows/Linux gets no taskbar entry. Open-time only — it can't be set
   *  later. macOS difference: the window also MOVES with its parent. */
  parent?: true | string;
  /** false: create, place and load the window but keep it hidden until
   *  tiny.win.show() (from its page) or app.window(id).show(). Fill it in
   *  first, then reveal it. A remembered placement still applies; a
   *  remembered maximize/fullscreen lands on that first show. Nothing shows
   *  it for you. Default true. */
  show?: boolean;
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
  | 'microphone' | 'camera' | 'location'
  | 'automation' | `automation:${string}`;

declare type TinyPermissionStatus =
  'granted' | 'denied' | 'undetermined' | 'unsupported';

/** The pre-0.57.0 object form of app.paths / app.info — still readable,
 *  with a one-time warning; call them instead. */
declare interface TinyPathsDeprecatedFields {
  /** @deprecated read it from `(await app.paths()).resources` */
  readonly resources: string | null;
  /** @deprecated read it from `(await app.paths()).executables` */
  readonly executables: string | null;
  /** @deprecated read it from `(await app.paths()).home` */
  readonly home: string;
  /** @deprecated read it from `(await app.paths()).data` */
  readonly data: string;
  /** @deprecated read it from `(await app.paths()).cache` */
  readonly cache: string;
  /** @deprecated read it from `(await app.paths()).logs` */
  readonly logs: string;
  /** @deprecated read it from `(await app.paths()).temp` */
  readonly temp: string;
  /** @deprecated read it from `(await app.paths()).downloads` */
  readonly downloads: string;
  /** @deprecated read it from `(await app.paths()).desktop` */
  readonly desktop: string;
  /** @deprecated read it from `(await app.paths()).documents` */
  readonly documents: string;
}
declare interface TinyAppInfoDeprecatedFields {
  /** @deprecated read it from `(await app.info()).version` */
  readonly version: string;
  /** @deprecated read it from `(await app.info()).tinyjs` */
  readonly tinyjs: string;
  /** @deprecated read it from `(await app.info()).runtime` */
  readonly runtime: string;
}

declare type TinyResizeEdge = 'n' | 'ne' | 'e' | 'se' | 's' | 'sw' | 'w' | 'nw';

/** tiny.win.onState / the onWindowState export */
declare interface TinyWindowStateEvent {
  /** 'main' or a win.open id */
  win: string;
  fullscreen: boolean;
  maximized: boolean;
  /** Wayland never reports it */
  minimized: boolean;
  focused: boolean;
  /** id of the screen holding most of the window; the event also fires when
   *  only this changes (the window moved to another screen) */
  screenId: number;
}

/** system.requirements(): what a feature needs on this machine */
declare interface TinyRequirement {
  id: string;
  ok: boolean;
  feature: string;
  detail: string;
  /** null when already satisfied, or when no package would fix it */
  install: { manager: string; packages: string[]; command: string } | null;
}

declare interface TinySystemInfo {
  os: 'macos' | 'windows' | 'linux';
  arch: string;
  /** Linux display server ('x11' | 'wayland'); null elsewhere */
  session: string | null;
  /** Linux desktop name; null elsewhere */
  desktop: string | null;
}

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
  screen: { id: number; x: number; y: number; width: number; height: number; scale: number };
}

declare interface TinyRect { x: number; y: number; width: number; height: number }

declare interface TinyScreen {
  /** session handle (what captureScreen/recording take) — NOT stable across
   *  reboots or replugs; persist `uid` instead */
  id: number;
  /** the most stable identity the OS offers: display UUID (macOS), monitor
   *  device path (Windows), connector name (Linux). Identical monitors can
   *  still collide — matchScreen() scores more than this */
  uid: string;
  /** human-readable, e.g. 'Built-in Retina Display'; null if unknown */
  name: string | null;
  /** display frame — same top-left coordinates as win.setPosition */
  x: number;
  y: number;
  width: number;
  height: number;
  /** frame minus the menu bar and Dock */
  visible: { x: number; y: number; width: number; height: number };
  /** device pixels per point. Linux/GTK3: an INTEGER buffer scale — a 125%
   *  fractional setup reports 1 or 2 depending on the compositor */
  scale: number;
  /** the menu-bar screen (the coordinate origin) */
  primary: boolean;
  /** the built-in panel (laptop); false when the OS can't tell */
  internal: boolean;
  /** the same screen in its own pixels; visible is screen-local */
  pixels: { width: number; height: number; visible: TinyRect };
}

/** What remembered placement keeps about a screen (matchScreen input). */
declare interface TinyScreenSnapshot {
  uid: string | null;
  name: string | null;
  width: number;
  height: number;
  scale: number;
  primary: boolean;
  internal: boolean;
  /** position in left-to-right order — tells identical monitors apart */
  rank?: number;
}

declare interface TinyPositionOptions {
  /** x/y are relative to this screen's top-left (id or a screens() entry) */
  screen?: number | TinyScreen;
  /** 'pt' (default): the screen's logical points; 'px': its pixels —
   *  needs `screen` (there is no global pixel space on macOS or Wayland) */
  units?: 'pt' | 'px';
}

declare interface TinySizeOptions {
  /** 'px': pixels of the screen the window is on */
  units?: 'pt' | 'px';
}

declare interface TinyFullscreenOptions {
  /** go fullscreen on this screen (moving there first) */
  screen?: number | TinyScreen;
}

/** Standard per-app directories; data/cache/logs are per app id, and exist
 *  by the time paths() resolves. Prefer these over hardcoding ~/Library paths. */
declare interface TinyPaths {
  /** The project's resources/ folder in dev, its bundled copy in builds; null without one. */
  resources: string | null;
  /** This OS's sidecar programs from executables/, flat: .build/executables in
   *  dev, the bundled copy in builds (Contents/Helpers in a .app); null without any. */
  executables: string | null;
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

/** One cookie in the webview's jar (app.cookies). A leading '.' on domain
 *  means "and its subdomains"; without one the cookie is host-only. */
declare interface TinyCookie {
  name: string;
  value: string;
  domain: string;
  path: string;
  /** ms since the epoch; null = session cookie */
  expires: number | null;
  secure: boolean;
  httpOnly: boolean;
  /** the policy the engine enforces. A cookie a SITE set without SameSite
   *  reads as that engine's default: 'none' on macOS, 'lax' on Windows,
   *  'none' or 'lax' on Linux by WebKitGTK version. Hand it back to set()
   *  unchanged to keep the cookie working the same way. */
  sameSite: 'lax' | 'strict' | 'none';
}

declare interface TinyCookieInit {
  name: string;
  value: string;
  /** where it belongs: url alone = host-only for its host */
  url?: string;
  /** '.example.com' covers subdomains; needed when url is omitted */
  domain?: string;
  /** default '/' */
  path?: string;
  /** Date or ms since the epoch; omit for a session cookie */
  expires?: Date | number;
  secure?: boolean;
  httpOnly?: boolean;
  /** default 'lax' on every OS (engines disagree about an unset one) */
  sameSite?: 'lax' | 'strict' | 'none';
}

/** The webview's own cookie jar — shared by every window and a wrapped
 *  site, HttpOnly cookies included. Backend only. */
declare interface TinyCookies {
  /** url keeps what a request to it would carry (domain, path, secure).
   *  Without url, Linux needs WebKitGTK 2.42+. */
  get(filter?: { url?: string; name?: string; domain?: string; path?: string }): Promise<TinyCookie[]>;
  /** replaces any cookie with the same name/domain/path */
  set(cookie: TinyCookieInit): Promise<true>;
  /** one cookie by name + domain (+ path, default '/') — e.g. straight from
   *  get() — or everything get({ url, name }) matches; resolves the count */
  remove(target: { name: string; domain: string; path?: string } | { url: string; name?: string }): Promise<number>;
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
    on(event: string, fn: (data: any) => void): () => void;
    /** unhook a handler passed to on() by reference (the tiny.*.on sugar
     *  wraps yours, so keep the function those return instead) */
    off(event: string, fn: (data: any) => void): void;
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
   *    audio.src = tiny.proxyURL('https://example.com/stream.mp3');
   *  Only the app's own pages may use it; any other origin needs
   *  "media.proxy" in its tinyjs.json "api".origins key.
   *  Not on Windows: there's no tiny-media:// handler in WebView2, so the URL
   *  loads nothing — check tiny.system.capabilities().proxyURL !== false. */
  proxyURL(url: string): string;

  /** A correct file:// URL for a disk path on every platform — use for
   *  <audio>/<img>/<video> src of backend-provided paths. Hand-rolled
   *  'file://' + path breaks on Windows (the drive letter becomes the URL
   *  host). */
  fileURL(path: string): string;

  /** print to the backend's console (the tinyjs dev terminal); any JSON value */
  log(msg: unknown): Promise<true>;
  quit(): Promise<void>;
  /** Desktop notification. Signed macOS apps get Notification Center
   *  banners; tinyjs dev and ad-hoc builds fall back to osascript. Never
   *  rejects. Resolves true once handed to the OS — even if the user has
   *  turned notifications off — and false only when the macOS dev fallback
   *  couldn't show it. */
  notify(title: string, body?: string, opts?: TinyNotifyOptions): Promise<boolean>;

  win: {
    /** which window this page lives in ('main' or a tiny.win.open id) */
    readonly id: string;
    open(id: string, opts?: TinyOpenWindowOptions): Promise<void>;
    /** close a window; no id = this window ('main' quits the app) */
    close(id?: string): Promise<void>;
    windows(): Promise<string[]>;

    setTitle(title: string): Promise<void>;
    /** resize the PAGE's box (decorations excluded), top-left anchored —
     *  the same units getState().width/height reports back */
    setSize(width: number, height: number, opts?: TinySizeOptions): Promise<void>;
    /** hides the APP (NSApp hide): focus returns to the previous app —
     *  palettes can hide-then-paste with no frontmost tracking.
     *  hide({ app: false }) puts away just this window instead */
    hide(opts?: TinyHideOptions): Promise<void>;
    /** show({ activate: false }) surfaces the window without stealing focus */
    show(opts?: TinyShowOptions): Promise<void>;
    center(): Promise<void>;
    minimize(): Promise<void>;
    restore(): Promise<void>;
    /** toggle */
    fullscreen(): Promise<void>;
    setFullscreen(enabled: boolean, opts?: TinyFullscreenOptions): Promise<void>;
    setAlwaysOnTop(enabled: boolean): Promise<void>;
    setResizable(enabled: boolean): Promise<void>;
    /** floor for user resizes */
    setMinSize(width: number, height: number): Promise<void>;
    /** Native page zoom, 0.25–5 — the WEBVIEW's own (WKWebView pageZoom,
     *  WebView2 ZoomFactor, webkit_web_view_set_zoom_level), so it renders
     *  crisp and the page keeps laying out in ordinary CSS px with fewer of
     *  them. Distinct from zoom(), which is the green-button maximize. */
    setZoom(factor: number): Promise<void>;
    /** clamp this window onto the nearest screen if it's (nearly) off all */
    ensureOnScreen(): Promise<void>;
    /** mouse events pass through to whatever is behind the window */
    setClickThrough(enabled: boolean): Promise<void>;
    setLevel(level: TinyWindowLevel): Promise<void>;
    /** follow the user across every Space + float over fullscreen apps */
    setAllSpaces(enabled: boolean): Promise<void>;
    /** top-left origin, screen points (global). With { screen }: relative to
     *  that screen's top-left, in its points — or its pixels with
     *  units: 'px'. No-op on Wayland. */
    setPosition(x: number, y: number, opts?: TinyPositionOptions): Promise<void>;
    /** null if the window is already gone */
    getState(): Promise<TinyWinState | null>;
    /** fullscreen/maximized/minimized/focused transitions, from any cause.
     *  Broadcast to every window — check `win` for your own. */
    onState(fn: (state: TinyWindowStateEvent) => void): () => void;
    setChrome(opts: TinyChromeOptions): Promise<void>;
    /** No args: drag the window (frameless chrome). With { files }: drag
     *  real files OUT of the app — call from a mousedown handler while the
     *  button is held. */
    startDrag(opts?: TinyDragOutOptions): Promise<void>;
    /** drag files out of the window (same as startDrag({ files })) */
    dragOut(opts: TinyDragOutOptions): Promise<void>;
    /** begin a resize from an edge (frameless windows' own grips) */
    startResize(edge?: TinyResizeEdge): Promise<void>;
    zoom(): Promise<void>;
    /** true: the close button hides the window instead of quitting. A macOS
     *  idea — the app outlives its last window and the Dock icon brings it
     *  back. Windows and Linux have nowhere to put that, so there it holds
     *  only while something can bring the app back (a tray icon, accessory
     *  mode, or another window still up); closing the last window of an
     *  ordinary app quits it. */
    setHideOnClose(enabled: boolean): Promise<void>;
    print(): Promise<void>;
    /** render the page to a PDF file (vector) -> the path written. From the
     *  app's own pages any path works. From another origin (a wrapped site),
     *  only Downloads (a bare name lands there), the app's data dir or the
     *  temp dir are written directly; anything else opens a save panel
     *  pre-filled with the name, and cancelling rejects. */
    printToPDF(path: string): Promise<{ path: string }>;
    /** Find-in-page: selects and scrolls to the next match; call again to
     *  step. matches/activeMatch come from a JS text-walk on top of the
     *  native find — approximate on exotic pages on macOS/Windows (hidden
     *  text counts, shadow DOM doesn't); Linux's counts come from the
     *  engine. All three platforms; ask capabilities().findInPage. */
    find(term: string, opts?: { forward?: boolean; matchCase?: boolean }): Promise<{ found: boolean; matches?: number; activeMatch?: number }>;
    /** clear the find selection */
    stopFind(): Promise<void>;
    /** files dragged onto the window — real filesystem paths */
    onDrop(fn: (paths: string[]) => void): () => void;

    /** native share sheet — anchor at the click's clientX/clientY. macOS
     *  only: elsewhere it does nothing (capabilities().share is false). */
    share(opts?: TinyShareOptions): Promise<boolean>;

    /** This window's OWN menu bar, overriding the app menu for it alone.
     *  Same spec and same click event as tiny.menu; windows that don't
     *  override keep showing the app menu, and so do windows opened later.
     *  macOS has one bar for the whole app, so it swaps this one in while the
     *  window is key and puts the app menu back when it isn't.
     *  To hide a bar rather than change it, use chrome.menu:false. */
    menu: {
      set(menus: TinyMenu[]): Promise<void>;
      /** back to showing the app menu */
      reset(): Promise<void>;
      /** patch an item in THIS window's bar only */
      update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): Promise<void>;
      get(id: string): Promise<TinyMenuItemState>;
    };
  };

  menu: {
    /** The APP menu: every window shows it, including windows opened later.
     *  (macOS has one bar for the whole app; Windows and Linux draw a copy of
     *  it inside each window.) For one window to differ, see tiny.win.menu;
     *  for one window to show no bar at all, chrome.menu:false. */
    set(menus: TinyMenu[]): Promise<void>;
    /** With `"about": "menu"` in tinyjs.json, the macOS About menu item fires
     *  here with the reserved id 'about' instead of showing the standard
     *  panel. */
    on(fn: (id: string) => void): () => void;
    /** patch a live item without redeclaring the menu — every window's copy of
     *  the id moves. tiny.win.menu.update patches just one window's. */
    update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): Promise<void>;
    get(id: string): Promise<TinyMenuItemState>;
    /** replace the right-click menu; null restores WebKit's default */
    setContext(items: TinyMenuItem[] | null): Promise<void>;
    onContext(fn: (id: string) => void): () => void;
  };

  store: {
    get(key: string): Promise<any | null>;
    /** Resolves true once on disk. A failed write (full disk, permissions)
     *  resolves false and never rejects: like localStorage, the value is
     *  already in memory (get() returns it) and the next successful save
     *  writes it, but it is lost if the app quits first. To retry, set the
     *  same value again rather than redoing a read-modify-write. */
    set(key: string, value: unknown): Promise<boolean>;
    /** Same result and failure rules as set(). */
    delete(key: string): Promise<boolean>;
    all(): Promise<Record<string, any>>;
  };

  hotkey: {
    /** combo like 'cmd+shift+k'; fires system-wide */
    /** true once the system has the hotkey; false if the combo isn't valid or
     *  another app holds it (the reason is logged). On Linux/Wayland, true
     *  means it was handed to the desktop, which asks the user. */
    register(id: string, combo: string): Promise<boolean>;
    unregister(id: string): Promise<void>;
    on(fn: (id: string) => void): () => void;
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
     *  At most 28 filters on Linux (15 with gainR), 32 on macOS — the list is
     *  truncated past that. */
    filters(list: TinyAudioFilter[]): Promise<void>;
    /** Retune ONE filter in place — no rebuild, no gap; what a slider drag
     *  should call */
    filter(index: number, patch: Partial<TinyAudioFilter>): Promise<void>;
    /** Stereo balance -1 (left) .. 1 (right), applied to the chain's output —
     *  costs no filter slot; needs an active chain */
    balance(v: number): Promise<void>;
    clear(): Promise<void>;
    /** The same chain as Web Audio nodes IN THE PAGE — the fallback where the
     *  native chain doesn't exist (Windows). Same RBJ curves, same verbs;
     *  page-SCOPED: filters only what you route through it. Shelf `q` and
     *  per-filter `gainR` are ignored here. Don't use on Linux — Web Audio
     *  reaching ctx.destination crackles there, which is why the native
     *  chain exists. */
    pageChain(ctx: BaseAudioContext): TinyPageChain;
    /**
     * One sampled-SFX mixer per app: short decoded sounds (wav/mp3/flac
     * guaranteed) fired with per-voice volume, pan and pitch, mixed into one
     * output. Game/UI sound effects — not streaming, not music (that's
     * <audio>), not a sequencer. Works on Linux (native mix in the launcher —
     * Web Audio crackles there) and macOS/Windows (Web Audio in the main
     * window's page) with identical behavior; `capabilities().sampler`
     * reports 'native' | 'page' but apps shouldn't need to care. App-scoped:
     * every window and the backend (app.audio.sampler) drive the SAME mixer.
     */
    sampler: {
      /** Decode a sound into the bank. `source` is an absolute path (best —
       *  zero copies over any wire) or an ArrayBuffer/typed array/Blob,
       *  which is written to the app cache once and loaded from there.
       *  Rejects if the file can't be decoded. Reloading a name replaces it. */
      load(name: string, source: string | ArrayBuffer | ArrayBufferView | Blob): Promise<true>;
      /** Fire a voice. vol linear 0..1 (default 1), pan −1..1 equal-power
       *  (StereoPanner's law, same numbers = same sound on every OS), rate a
       *  playbackRate-style ratio (pitch and speed together, default 1). Up
       *  to 32 voices — past that the oldest is stolen; play() never rejects
       *  for "too many" (it does for an unknown name). */
      play(name: string, opts?: {
        vol?: number; pan?: number; rate?: number; loop?: boolean;
      }): Promise<TinySamplerVoice>;
      stopAll(): Promise<void>;
      /** One master gain over the whole mixer (linear, default 1). */
      master(v: number): Promise<void>;
      /** Drop a sound and free its decoded PCM. Voices still playing it are
       *  cut (not faded). */
      unload(name: string): Promise<void>;
    };
  };

  /**
   * Read the app's (or the system's) *rendered* audio output as PCM chunks —
   * for VU meters / visualizers, including audio that bypasses Web Audio
   * (native HLS, CORS-tainted streams). Read-only: it observes the mix, it
   * can't process it (EQ still needs the signal in the graph — see proxyURL).
   * Requires an `"audioTap"` key in tinyjs.json. macOS 14.2+.
   */
  audioTap: {
    /**
     * Start tapping. Resolves `true`, or throws an Error whose `.code` is one
     * of `'unsupported'` (before macOS 14.2), `'not-declared'` (manifest
     * missing the scope) or `'failed'` (Core Audio error). A refused
     * permission is not an error: the chunks arrive silent. Idempotent for
     * identical options (macOS; elsewhere start() restarts the tap).
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
    stop(): Promise<void>;
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
    }) => void): () => void;
  };

  theme: {
    get(): Promise<{ dark: boolean } | null>;
    on(fn: (dark: boolean) => void): () => void;
  };

  /** Native clipboard (NSPasteboard in the launcher — no polling spawns). */
  clipboard: {
    read(): Promise<TinyClipboardData>;
    write(data: TinyClipboardWrite): Promise<void>;
    changeCount(): Promise<number>;
    /** poll for changes every intervalMs (default 500) */
    watch(intervalMs?: number): Promise<void>;
    unwatch(): Promise<void>;
    /** after watch(); self = our own write() caused the change */
    onChange(fn: (info: { changeCount: number; self: boolean }) => void): () => void;
  };

  /** Native dialogs, run by the launcher — NSOpenPanel / NSAlert on macOS,
   *  the common item dialog + MessageBox on Windows, GTK's chooser and
   *  message dialogs on Linux. Application-modal rather than attached to a
   *  window, which is why they aren't on `win`. */
  dialog: {
    /** `types`: extensions without dots (`['md', 'txt']`) limiting what's
     *  choosable — allowedContentTypes / COMDLG_FILTERSPEC / GtkFileFilter.
     *  Windows and Linux also show an "All files" escape hatch. Omit for
     *  no filter. `saveFile` appends the first extension when the name is
     *  typed without one (and the type filter, not "All files", is the one
     *  selected) — on Windows and Linux; macOS's own panel decides there.
     *  `saveFile`'s `name` pre-fills the file name field: a name, not a path
     *  (any folder part is dropped). Without it macOS shows "Untitled" and
     *  Windows and Linux an empty field. */
    openFile(opts?: { types?: string[] }): Promise<string | null>;
    openFiles(opts?: { types?: string[] }): Promise<string[] | null>;
    pickFolder(): Promise<string | null>;
    saveFile(opts?: { types?: string[]; name?: string }): Promise<string | null>;
    /** `alert`/`confirm`/`prompt` are native panels with the message as the
     *  headline. A string second argument to `alert`/`confirm` is the
     *  detail; `prompt` rejects one (a TypeError — pass `{ default }`).
     *  `ok`/`cancel` relabel the buttons. The page's bare
     *  `alert()`/`confirm()`/`prompt()` are the browser primitives: one
     *  argument, origin (or app name) as the headline, no custom labels. */
    alert(message: string, opts?: string | { detail?: string; ok?: string }): Promise<true>;
    confirm(message: string, opts?: string | { detail?: string; ok?: string; cancel?: string }): Promise<boolean>;
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
    info(): Promise<TinySystemInfo>;
    architecture(): Promise<string>;
    /** What this machine can actually do — anything ABSENT is supported.
     *  Mostly booleans (`sampler` is 'page' | 'native'), plus `api`: what
     *  the tinyjs.json "api" gate denies THIS origin. A page usually wants
     *  tiny.capabilities.can() instead, which answers both at once. */
    capabilities(): Promise<Record<string, boolean | string> & {
      api?: { gated: boolean; denied: string[] };
    }>;
    /** the user's languages + time zone, read from the OS. A page already has
     *  navigator.language(s), Intl and the 'languagechange' event; reach for
     *  this on the BACKEND (txiki has no Intl at all), or when you want the
     *  SYSTEM preference rather than what this app declares it speaks. */
    /** null where the launcher has no locale reader yet (Windows, Linux) */
    locale(): Promise<TinyLocale | null>;
    requirements(ids?: string[] | null, opts?: { refresh?: boolean }): Promise<TinyRequirement[]>;
    missing(ids?: string[] | null): Promise<TinyRequirement[]>;
    promptMissing(ids?: string[] | null, opts?: { title?: string; ok?: string; cancel?: string }): Promise<{ missing: TinyRequirement[]; copied: boolean }>;
    /** live machine state */
    battery(): Promise<TinyBattery | null>;
    wifi(): Promise<TinyWifi | null>;
    /** seconds since the user's last input, session-wide */
    idleTime(): Promise<number>;
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
    quickLook(paths?: string | string[] | null): Promise<void>;
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
    /** self-update for built apps ("update" in tinyjs.json) */
    update: {
      check(): Promise<{ available: boolean; current: string; latest: string | null; notes: string | null }>;
      /** downloads, verifies, swaps the bundle and relaunches */
      install(): Promise<true>;
    };
    /** 'menubar': no Dock icon / taskbar button / app switcher entry */
    presence(mode: 'normal' | 'menubar'): Promise<void>;
    onOpenUrl(fn: (url: string) => void): () => void;
    onOpenFiles(fn: (paths: string[]) => void): () => void;
    onNotificationClick(fn: (id: string) => void): () => void;
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
    /** opt-in outside-the-window tracking. Already global on macOS/Windows/
     *  X11 (start() is a cheap ok); on Linux-Wayland it arms the ScreenCast
     *  portal's cursor stream — one consent dialog, remembered across runs,
     *  sharing indicator while armed. start() resolves { ok: true } or
     *  throws an Error with .code: 'unsupported' | 'denied' | 'failed'. */
    mouseTracking: {
      start(): Promise<{ ok: true }>;
      stop(): Promise<boolean>;
    };
    /** every display, same top-left coords as win.setPosition */
    screens(): Promise<TinyScreen[]>;
    /** the screen containing a global point; null if none */
    screenAt(x: number, y: number): Promise<TinyScreen | null>;
    /** the screen holding most of a global rect; null if it touches none */
    screenFor(rect: TinyRect): Promise<TinyScreen | null>;
    /** the screen of the focused window of ANY app (no permission needed);
     *  the cursor's screen when that app shows no window; null on Wayland */
    foregroundScreen(): Promise<TinyScreen | null>;
    /** a saved TinyScreen / TinyScreenSnapshot -> the connected screen it
     *  most likely is now (ids don't survive replugs/reboots), or null */
    matchScreen(saved: Partial<TinyScreen> | TinyScreenSnapshot): Promise<TinyScreen | null>;
    /** screens plugged/unplugged/rearranged/rescaled; returns unsubscribe */
    onScreensChanged(fn: (screens: TinyScreen[]) => void): () => void;
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
    badge(text: string): Promise<void>;
    /** ask for the user's attention: macOS bounces the Dock icon once
     *  (critical: until the app is activated), Windows flashes the taskbar
     *  button (critical: longer), Linux sets the urgency hint (X11 only;
     *  critical makes no difference) */
    attention(opts?: { critical?: boolean }): Promise<void>;
    power: TinyPower;
    /** the active app right now (who focus returns to after win.hide()) */
    frontmostApp(): Promise<TinyFrontmostApp | null>;
    /** replace the app icon from a png ('' resets to the bundle icon) */
    icon(path: string): Promise<void>;
    /** progress bar on the app icon / taskbar button: 0..1, null clears */
    progress(value: number | null): Promise<void>;
    /** find files — up to 100 paths. macOS: Spotlight, by name and content,
     *  under the home folder. Linux: by name (locate, or find as a fallback).
     *  Windows: rejects. */
    spotlight(query: string): Promise<string[]>;
    /** the system alert beep — the one portable sound */
    beep(): Promise<boolean>;
    /** 'info' | 'success' | 'alert' | 'error' (portable — mapped to each OS's
     *  nearest sound), a platform sound name ('Glass' macOS, 'SystemHand'
     *  Windows, 'complete' Linux — these do NOT port), or an audio file path.
     *  Resolves false if the name/file didn't load. */
    playSound(target?: TinySoundName | (string & {})): Promise<boolean>;
    /** screenshot a display (id from screens(); default: the first display
     *  the OS lists, usually the main one — Linux always captures the whole
     *  desktop and ignores the id) — png in
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
    /** action button / reply field on a notification was used */
    onNotificationAction(fn: (info: TinyNotificationAction) => void): () => void;
    nowPlaying: {
      /** also arms the media keys */
      set(info: TinyNowPlaying): Promise<void>;
      clear(): Promise<void>;
    };
    /** a hardware media key / Control Center transport fired */
    onMediaKey(fn: (info: TinyMediaKey) => void): () => void;
    /** speak text; resolves when playback finishes (false if interrupted) */
    say(text: string, opts?: TinySayOptions): Promise<boolean>;
    stopSpeaking(): Promise<void>;
    voices(): Promise<TinyVoice[]>;
  };

  tray: {
    set(spec: TinyTraySpec): Promise<void>;
    remove(): Promise<void>;
    /** the tray icon's rect { x, y, width, height } (top-left) | null.
     *  Always null on Linux, whose tray protocol doesn't say where it is. */
    position(): Promise<{ x: number; y: number; width: number; height: number } | null>;
    on(fn: (id: string) => void): () => void;
    onClick(fn: () => void): () => void;
  };

  /** What THIS page can actually do, keyed by the tiny.* path you'd call
   *  ('clipboard.write', 'app.badge'; a leading 'tiny.' is fine). One answer
   *  for the OS, the Linux session and tinyjs.json's "api" gate for this
   *  page's origin. Await `ready` once; everything else is synchronous.
   *  Informational: the backend gate still decides every call. */
  capabilities: {
    /** resolves once the answer has arrived, or after 3s without one — it
     *  never hangs; a late answer fires onChange. Until then can() is false. */
    readonly ready: Promise<void>;
    /** true when the call works here. 'ns.*' is true when ANY path under
     *  ns works. */
    can(path: string): boolean;
    /** why not, when it doesn't */
    get(path: string): TinyCapability;
    /** every path that works here, sorted */
    list(): string[];
    /** fires when the answer changes, including when the first one lands */
    onChange(fn: (change: { added: string[]; removed: string[] }) => void): () => void;
    /** ask the backend again; fires onChange if anything differs */
    refresh(): Promise<void>;
  };
}

declare type TinyCapability =
  | { ok: true }
  /** 'gate': tinyjs.json "api" denies it — `needs` are the names to enable */
  | { ok: false; why: 'gate'; needs: string[] }
  /** 'platform': this OS can't; 'session': this Linux session can't (Wayland).
   *  `feature` is the system.capabilities() key that says so. */
  | { ok: false; why: 'platform' | 'session'; feature: string }
  /** 'unknown': no such path; 'pending': no answer yet */
  | { ok: false; why: 'unknown' | 'pending' };

declare const tiny: Tiny;

interface Window {
  tiny: Tiny;
  /** set by the injected bridge before your scripts run */
  __TINY_WIN?: string;
}

/** Handle for one window (backend: app.window(id)). */
declare interface TinyWindowHandle {
  readonly id: string;
  eval(js: string): void;
  push(event: string, data?: unknown): void;
  /** on 'main', quits the app (as tiny.win.close() does from main's page) */
  close(): void;
  setTitle(title: string): void;
  /** resize the PAGE's box (decorations excluded), top-left anchored */
  setSize(width: number, height: number, opts?: TinySizeOptions): void;
  setPosition(x: number, y: number, opts?: TinyPositionOptions): void;
  center(): void;
  /** hide({ app: false }) puts away this window alone; on 'main' a bare
   *  hide() hides the whole app (macOS) */
  hide(opts?: TinyHideOptions): void;
  show(opts?: TinyShowOptions): void;
  minimize(): void;
  restore(): void;
  zoom(): void;
  fullscreen(): void;
  setFullscreen(enabled: boolean, opts?: TinyFullscreenOptions): void;
  setAlwaysOnTop(enabled: boolean): void;
  setResizable(enabled: boolean): void;
  setMinSize(width: number, height: number): void;
  ensureOnScreen(): void;
  /** drags the window; dragging files out is page-only */
  startDrag(): void;
  startResize(edge?: TinyResizeEdge): void;
  /** Native page zoom, 0.25–5 — the WEBVIEW's own (WKWebView pageZoom,
   *  WebView2 ZoomFactor, webkit_web_view_set_zoom_level), so it renders
   *  crisp and the page keeps laying out in ordinary CSS px with fewer of
   *  them. Distinct from zoom(), which is the green-button maximize. */
  setZoom(factor: number): void;
  setClickThrough(enabled: boolean): void;
  setLevel(level: TinyWindowLevel): void;
  setAllSpaces(enabled: boolean): void;
  setChrome(opts: TinyChromeOptions): void;
  getState(): Promise<TinyWinState | null>;
  /** This window's OWN menu bar, overriding the app menu for it alone.
   *  Clicks arrive the same way either bar sent them, so a handler doesn't
   *  have to care which. macOS shows it while the window is key. */
  menu: {
    set(menus: TinyMenu[]): void;
    /** drop the override: back to showing the app menu */
    reset(): void;
    /** patch an item in THIS window's bar, leaving other windows' copies alone */
    update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
    get(id: string): Promise<TinyMenuItemState>;
  };
  /** @deprecated use menu.set() */
  setMenu(menus: TinyMenu[]): void;
  /** @deprecated use menu.reset() */
  resetMenu(): void;
  /** @deprecated use menu.update() */
  updateMenuItem(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
  /** @deprecated use menu.get() */
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
  setSize(width: number, height: number, opts?: TinySizeOptions): void;
  /** Same names as tiny.menu.* in a page. */
  menu: {
    /** The APP menu — every window shows it, including ones opened later.
     *  app.window(id).menu.set gives one window something else. */
    set(menus: TinyMenu[]): void;
    /** patch a live item: every window's copy of the id moves */
    update(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
    get(id: string): Promise<TinyMenuItemState>;
    /** the right-click menu; null/[] restores the default */
    setContext(items: TinyMenuItem[] | null): void;
  };
  /** @deprecated use app.menu.set() */
  setMenu(menus: TinyMenu[]): void;
  /** @deprecated use app.menu.update() */
  updateMenuItem(id: string, patch?: { label?: string; checked?: boolean; enabled?: boolean }): void;
  /** @deprecated use app.menu.get() */
  getMenuItem(id: string): Promise<TinyMenuItemState>;
  /** @deprecated use app.menu.setContext() */
  setContextMenu(items: TinyMenuItem[] | null): void;
  /** run JS in the main window's page (not JS eval of external input) */
  eval(js: string): void;
  reload(newHtml?: string): Promise<void>;
  quit(): void;
  /** Same shape as tiny.notify, and the same result: true once handed to
   *  the OS, false only when the macOS dev fallback couldn't show it. */
  notify(title: string, body?: string, opts?: TinyNotifyOptions): Promise<boolean>;
  /** Same dialogs and cancellation results as tiny.dialog, also with
   *  mainWindow: false. Runs in the launcher, without a WebView. */
  dialog: Tiny['dialog'];
  /** hides the APP (NSApp hide): focus returns to the previous app;
   *  hide({ app: false }) puts away the main window alone */
  hide(opts?: TinyHideOptions): void;
  /** show({ activate: false }) surfaces the window without stealing focus */
  show(opts?: TinyShowOptions): void;
  center(): void;
  minimize(): void;
  restore(): void;
  fullscreen(): void;
  setFullscreen(enabled: boolean, opts?: TinyFullscreenOptions): void;
  setPosition(x: number, y: number, opts?: TinyPositionOptions): void;
  setAlwaysOnTop(enabled: boolean): void;
  setResizable(enabled: boolean): void;
  setClickThrough(enabled: boolean): void;
  setLevel(level: TinyWindowLevel): void;
  setAllSpaces(enabled: boolean): void;
  ensureOnScreen(): void;
  startResize(edge?: TinyResizeEdge): void;
  /** tjs.spawn without the console window a GUI app gets on Windows */
  spawnHidden(args: string[], opts?: any): any;
  /** make this app the default opener for an extension or 'folder' (Linux) */
  setAsDefaultHandler(ext: string): Promise<'ok' | 'unsupported' | 'failed'>;
  /** true: the close button hides the window instead of quitting. macOS keeps
   *  the app alive without windows (the Dock brings it back); Windows and
   *  Linux honour it only while a tray icon, accessory mode or another window
   *  offers a way back — otherwise the last close quits, as it does there. */
  setHideOnClose(enabled: boolean): void;
  presence(mode: 'normal' | 'menubar'): void;
  print(): void;
  /** render the page to a PDF file (vector) */
  printToPDF(path: string): Promise<{ path: string }>;
  /** drags the window (frameless chrome); dragging files out is page-only:
   *  tiny.win.startDrag({ files }) */
  startDrag(): void;
  zoom(): void;
  setChrome(opts: TinyChromeOptions): void;
  /** @deprecated use app.win.getState() */
  getWinState(): Promise<TinyWinState | null>;

  /** The main window under the names a page uses for its own (tiny.win.*),
   *  plus open(). getState() is null when there is no main window
   *  ("mainWindow": false). */
  win: TinyWindowHandle & {
    /** With "mainWindow": false there is no main window; open named ones
     *  on demand. Closing the last one keeps the backend and tray running. */
    open(id: string, opts?: TinyOpenWindowOptions): void;
    /** close that window; no id = main, which quits */
    close(id?: string): void;
    windows(): Promise<string[]>;
    setHideOnClose(enabled: boolean): void;
  };
  /** @deprecated use app.win.open() */
  openWindow(id: string, opts?: TinyOpenWindowOptions): void;
  window(id: string): TinyWindowHandle;
  windows(): Promise<string[]>;

  /** Native DSP on the app's own output — the same verbs as tiny.audio. */
  audio: {
    /** Replace the whole chain; [] restores unprocessed output. */
    filters(list: TinyAudioFilter[]): void;
    /** Retune ONE filter in place; the patch merges onto what filters() set */
    filter(index: number, patch: Partial<TinyAudioFilter>): void;
    /** -1 (left) .. 1 (right), applied to the chain's output */
    balance(v: number): void;
    /** no filters, balance back to centre */
    clear(): void;
    /** the same mixer the pages drive (tiny.audio.sampler) */
    sampler: {
      load(name: string, source: string | ArrayBuffer | ArrayBufferView): Promise<true>;
      play(name: string, opts?: { vol?: number; pan?: number; rate?: number; loop?: boolean }):
        Promise<{ id: number; set(patch: { vol?: number; pan?: number; rate?: number }): true; stop(): true }>;
      /** stop a voice by id (same as voice.stop()) */
      stop(id: number): true;
      stopAll(): true;
      master(v: number): true;
      unload(name: string): true;
    };
  };
  /** @deprecated use app.audio.filters() */
  setAudioFilters(list: TinyAudioFilter[]): void;
  /** @deprecated use app.audio.filter() */
  setAudioFilter(index: number, patch: Partial<TinyAudioFilter>): void;
  /** @deprecated use app.audio.balance() */
  setAudioBalance(v: number): void;

  tray: {
    set(spec: TinyTraySpec): void;
    remove(): void;
    position(): Promise<{ x: number; y: number; width: number; height: number } | null>;
  };
  store: {
    get(key: string): Promise<any | null>;
    /** Resolves true once on disk. A failed write (full disk, permissions)
     *  resolves false and never rejects: like localStorage, the value is
     *  already in memory (get() returns it) and the next successful save
     *  writes it, but it is lost if the app quits first. To retry, set the
     *  same value again rather than redoing a read-modify-write. */
    set(key: string, value: unknown): Promise<boolean>;
    /** Same result and failure rules as set(). */
    delete(key: string): Promise<boolean>;
    all(): Promise<Record<string, any>>;
  };
  hotkey: {
    /** see tiny.hotkey.register */
    register(id: string, combo: string): Promise<boolean>;
    unregister(id: string): void;
  };
  /** Native clipboard (NSPasteboard in the launcher). */
  clipboard: {
    read(): Promise<TinyClipboardData>;
    write(data: TinyClipboardWrite): boolean;
    changeCount(): Promise<number>;
    /** poll for changes every intervalMs (default 500); changes arrive via
     *  the onClipboardChange option / 'clipboard-change' page event */
    watch(intervalMs?: number): true;
    unwatch(): true;
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
  /** opt-in outside-the-window tracking. Already global on macOS/Windows/
   *  X11 (start() resolves untouched); on Linux-Wayland it arms the
   *  ScreenCast portal's cursor stream — one consent dialog, remembered
   *  across runs, sharing indicator while armed. start() resolves
   *  { ok: true } or throws an Error with .code: 'unsupported' | 'denied' |
   *  'failed' — the same as tiny.app.mouseTracking in a page. */
  mouseTracking: {
    start(): Promise<{ ok: true }>;
    stop(): boolean;
  };
  /** every display, same top-left coords as setPosition. Changes arrive as
   *  onSystem('screens') and the 'screens-changed' page event */
  screens(): Promise<TinyScreen[]>;
  screenAt(x: number, y: number): Promise<TinyScreen | null>;
  screenFor(rect: TinyRect): Promise<TinyScreen | null>;
  foregroundScreen(): Promise<TinyScreen | null>;
  matchScreen(saved: Partial<TinyScreen> | TinyScreenSnapshot): Promise<TinyScreen | null>;
  /** standard per-app directories — a function, like tiny.app.paths()
   *  (reading app.paths.data as an object still works but is deprecated) */
  paths: (() => Promise<TinyPaths>) & TinyPathsDeprecatedFields;
  shell: TinyShell;
  launchAtLogin: TinyLaunchAtLogin;
  /** '' clears the badge */
  badge(text: string): boolean;
  /** ask for the user's attention — see tiny.app.attention */
  attention(opts?: { critical?: boolean }): boolean;
  power: TinyPower;
  /** the active app right now (who focus returns to after hide()) */
  frontmostApp(): Promise<TinyFrontmostApp | null>;
  /** replace the app icon from a png ('' resets to the bundle icon) */
  icon(path: string): boolean;
  /** the machine — same names as tiny.system.* in the page */
  system: {
    os(): 'macos' | 'windows' | 'linux';
    isMacOS(): boolean;
    isWindows(): boolean;
    isLinux(): boolean;
    info(): Promise<TinySystemInfo>;
    architecture(): Promise<string>;
    /** anything ABSENT is supported (no `api` key: the backend isn't gated) */
    capabilities(): Promise<Record<string, boolean | string>>;
    requirements(ids?: string[] | null, opts?: { refresh?: boolean }): Promise<TinyRequirement[]>;
    missing(ids?: string[] | null): Promise<TinyRequirement[]>;
    battery(): Promise<TinyBattery | null>;
    wifi(): Promise<TinyWifi | null>;
    idleTime(): Promise<number>;
    /** null where the launcher has no locale reader yet (Windows, Linux) */
    locale(): Promise<TinyLocale | null>;
  };
  /** { dark } | null — changes arrive via the onSystem('theme', …) export */
  theme: {
    get(): Promise<{ dark: boolean } | null>;
  };
  /** macOS-only — same shape and names as tiny.macos.* in the page. These
   *  throw on Windows and Linux, except ai.availability() ('unsupported'). */
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
  /** find files — up to 100 paths; see tiny.app.spotlight */
  spotlight(query: string): Promise<string[]>;
  /** the system alert beep — the one portable sound */
  beep(): Promise<boolean>;
  /** 'info' | 'success' | 'alert' | 'error' (portable — mapped to each OS's
   *  nearest sound), a platform sound name ('Glass' macOS, 'SystemHand'
   *  Windows, 'complete' Linux — these do NOT port), or an audio file path.
   *  Resolves false if the name/file didn't load. */
  playSound(target?: TinySoundName | (string & {})): Promise<boolean>;
  /** screenshot a display (id from screens(); see tiny.app.captureScreen for
   *  the default) — png in
   *  the temp dir, caller owns the file; needs the 'screen' permission
   *  and macOS 14+, rejects with the reason otherwise */
  captureScreen(screenId?: number): Promise<TinyCapture>;
  /** system eyedropper — NO screen-recording permission needed;
   *  '#rrggbb', or null if the user cancels */
  pickColor(): Promise<string | null>;
  /** thumbnail png — see tiny.app.thumbnail (Linux: images only) */
  thumbnail(path: string, size?: number): Promise<TinyThumbnail>;
  secrets: TinySecrets;
  /** the webview's cookie jar, HttpOnly included (backend only) */
  cookies: TinyCookies;
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
  /** like tiny.app.info() (reading app.info.version as an object still
   *  works but is deprecated) */
  info: (() => Promise<TinyAppInfo>) & TinyAppInfoDeprecatedFields;
  /** resolves when the window process ends */
  done: Promise<{ exit_status: number }>;
}

/** Metadata passed to api handlers. */
declare interface TinyApiMeta {
  /** id of the window the call came from ('main' or a tiny.win.open id) */
  window: string;
  /** origin of the CALLING FRAME, stamped by the launcher from WebKit's own
   *  frame info (not page-claimed) — e.g. "https://airtable.com",
   *  "file://". All three launchers stamp it; undefined only with an older
   *  launcher. The tinyjs.json "api".origins gate keys off the same value. */
  origin?: string;
}

/** Signature for backend api methods:
 *  export const api = { myMethod: async (params, app, meta) => ... } */
declare type TinyApiHandler = (
  params: any,
  app: TinyApp,
  meta: TinyApiMeta,
) => unknown | Promise<unknown>;

/** 'navigate' page event / onNavigate backend hook (all three platforms).
 *  kind 'policy' reaches only the backend hook, for main-frame http(s)
 *  navigations before they run: return 'deny' or 'external' (hand the URL to
 *  the real browser) to stop them; anything else — or answering slower than
 *  ~400ms — allows. It also fires for any other URL scheme (mailto:,
 *  someapp://…) in ANY frame; the webview never loads those, and only
 *  'external' hands them to the OS. No answer denies. isMainFrame is null
 *  where the engine can't tell (other schemes on Linux and Windows). The
 *  rest narrate the load: start | commit | finish | fail (with error) |
 *  crash (web content process died). */
declare interface TinyNavEvent {
  window: string;
  kind: 'policy' | 'start' | 'commit' | 'finish' | 'fail' | 'crash';
  url: string;
  isMainFrame?: boolean | null;
  error?: string;
}

/** 'download' page event / onDownload backend hook (all three platforms).
 *  tinyjs.json "downloads": 'auto' (default; ~/Downloads, de-duped names) |
 *  'ask' (save panel) | 'deny'. 'progress' states carry bytes/total
 *  (total -1 when the response had no Content-Length), throttled to ~4/s. */
declare interface TinyDownloadEvent {
  id: number;
  url: string;
  filename: string;
  path: string | null;
  state: 'started' | 'progress' | 'done' | 'failed' | 'denied' | 'cancelled';
  bytes?: number;
  total?: number;
  error?: string;
}

/** onWindowOpen backend hook / 'popup' page event (all three platforms) —
 *  window.open / target=_blank, per tinyjs.json "popups": 'external'
 *  (default; handed to the browser) | 'window' (a real tinyjs window) |
 *  'deny'.
 *
 *  kind 'policy' reaches only the backend hook, BEFORE anything shows:
 *  return 'window' | 'external' | 'deny' to override the configured mode
 *  (anything else — or answering slower than ~400ms — keeps it). In
 *  "popups": "window" mode the popup already exists hidden under `window`
 *  while you decide; in other modes only 'external'/'deny' can be honored,
 *  because the webview had to be returned (or not) synchronously.
 *
 *  kind 'open' (and the 'popup' page event) reports the outcome; with
 *  action 'window' the popup is live under `window` and
 *  app.window(id).close() still vetoes it late. */
declare interface TinyPopupEvent {
  window: string | null;
  /** id of the window whose page called window.open */
  opener?: string;
  url: string;
  kind?: 'policy' | 'open';
  /** the configured "popups" mode (policy asks only) */
  mode?: string;
  action?: 'window' | 'external' | 'deny';
}
