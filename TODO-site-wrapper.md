# Site wrappers — browser affordances for wrapping hosted web apps

Born from the tiny-airtable spike (2026-08-04): wrapping a hosted app means the
main frame is a third-party origin, not our own `index.html`, and it needs
everything a **browser** does that a local-page app never did — dialogs,
downloads, navigation, popups, find — plus a way to stop handing a third-party
origin the keys to the machine.

## Status: shipped on macOS 2026-08-04 (all verified live same day)

Implemented in one body of work: a real `WKNavigationDelegate` +
`WKDownloadDelegate` (`TinyWebDelegate`, browser-affordances section of
launcher-macos.cc) applied to every webview, JS-dialog / `createWebView`
handlers bolted onto the vendored `WebviewWKUIDelegate` at runtime (the
`install_media_capture_hook` pattern), gating in the bridge's `handleCall`,
config plumbing in cli.js. Verified with a self-driving app exercising each
item (dev + packaged .app, local http server for cross-origin/main-frame
paths).

- **JS dialogs** — `alert`/`confirm`/`prompt` get native panels (headline =
  page origin, falling back to the app name). Verified: `confirm()` blocks and
  returned `true` through the panel; previously it silently returned `false`.
- **Downloads** — `<a download>`, blob URLs, and any un-renderable MIME type
  become `WKDownload`s. `"downloads"`: `auto` (default; `~/Downloads`,
  MIME-derived extension when the name has none, de-duplicated `name (2).ext`)
  | `ask` (save panel) | `deny`. `DOWNLOAD` events → `onDownload(info, app)` +
  `'download'` page event with `{id, url, filename, path, state:
  started|done|failed|denied|cancelled, error?}`. Verified: blob download
  landed in `~/Downloads` with correct content and both events fired.
- **Navigation events + policy** — `NAV` events (`start|commit|finish|fail|
  crash`) → `onNavigate(info, app)` + `'navigate'` page event. Policy:
  main-frame http(s) navigations ask the backend first (`NAVQ`/`NAVR` wire);
  `onNavigate` gets `kind: 'policy'` and may return `'deny'` or `'external'`
  (shell-open); anything else — or answering slower than 400ms — allows, so a
  wrapper can't deadlock its own first load. Verified: a `'deny'` verdict
  provably stopped a popup's load (no start/commit/finish followed the ask).
- **`window.open` / `target=_blank`** — `"popups"`: `external` (default; hand
  http(s) to the browser) | `window` | `deny`. `window` mode returns a real
  webview built on the WebKit-provided configuration, so `window.open`'s
  return value / `window.opener` / `postMessage` survive (OAuth popups) — but
  with a FRESH `WKUserContentController`: the inherited one carries the
  parent's tiny shim with the parent's window id baked in, which would
  cross-wire the two windows' RPC promise tables. The popup registers in
  `g_windows` as `popupN` (targetable, `WINCLOSE`-able, emits `WINCLOSED`).
  `POPUP` event → `onWindowOpen(info, app)` + `'popup'` page event. Verified:
  `window.open` returned a WindowProxy and the popup's page used its own
  `tiny` (store write observed).
  - GOTCHA (cost the first run): WebKit snapshots which delegate methods
    exist at `setUIDelegate:` time. Runtime-added methods are invisible until
    the delegate is re-set — main() re-sets the main webview's UI delegate
    after `install_page_dialog_hooks()`. Same trap `install_open_handlers`
    documents for NSApp.
- **Find-in-page** — `tiny.win.find(term, {forward, matchCase})` →
  `{found}`, `tiny.win.stopFind()`. Call-style over the wire (`FIND`/
  `STOPFIND`, resolved like DLG). WebKit's public API has no match counts.
- **Capability gating** — tinyjs.json `"api"`: `{disable, enable}` lists of
  wire method names (`"ns.*"` wildcards, bare `"*"`; **enable wins over
  disable**; absent key = allow all), `"wrapper"` preset (OS chrome, windows,
  dialogs, store in; filesystem, clipboard read, secrets, capture out — the
  app's own api methods gated too, enable by name), or `{preset, enable}` to
  layer. Enforced in `handleCall` BEFORE the dialog/find short-circuits (the
  paths that skip `methods`). Denials REJECT with a readable reason and log
  in dev (`TINYJS_DEBUG`). `client.hello`/`debug.get` always pass (client
  bootstrap). Verified: denied call rejected with the manifest-quoting
  message; allowed calls unaffected; `wrapper` preset let a third-party
  origin use exactly the enabled surface.
- **`"inject"`** — path in tinyjs.json, bundled at build (`.ts` via esbuild);
  dev rides `TINYJS_INJECT`, packaged .apps ship `Resources/app/inject.js`
  (launcher reads it at startup into `g_inject_js`). Verified at
  document-start in dev; packaged file placement verified.
- **`"url"`** — the main window starts at a remote URL; frontend dir becomes
  optional. Spawn mode passes it as the launcher's page argument (the devUrl
  branch); packaged .apps carry the `TinyjsUrl` plist key. Verified: packaged
  wrapper loaded the remote page with policy ask + NAV events flowing.

Capabilities report the lot: `jsDialogs, downloads, navigation, popups,
findInPage` — true on macOS, false on Windows/Linux until the legs below.

## Round 2: shipped on macOS 2026-08-05 (verified live same day)

- **Download progress** — KVO on WKDownload's NSProgress, throttled to ~4/s,
  emits `state: 'progress'` with `bytes`/`total` (`total` −1 without a
  Content-Length). Verified against a 300MB local download.
- **Origin-scoped gating (v2)** — the launcher stamps every CALL with the
  calling frame's origin as a SECOND JSON array element, taken from
  `WKScriptMessage.frameInfo.securityOrigin` (WebKit-attested — a hostile
  page can't spoof it; launchers that don't stamp leave it undefined =
  no origin scoping). `"api": { "origins": { pattern: sub-gate } }`:
  patterns are origins with `*` wildcards (`"file://*"`,
  `"https://*.airtable.com"`), first matching key in manifest order wins;
  sub-gates are `"all"`, `"none"`, a preset, a bare array of names
  (deny-by-default + that enable list), or `{disable, enable}`. An origin
  matching NO key falls back to the top-level lists if any, else gets only
  the client bootstrap (origins present = strangers deny-by-default, so a
  redirect to an unlisted domain inherits nothing). Unknown preset names
  now FAIL CLOSED (deny everything, loudly) — a typo in a security setting
  must not allow-by-accident. `meta.origin` reaches app api handlers too.
  Verified: `file://` and `http://127.0.0.1:8123` got different gates in
  one run, denial logs name the origin.
- **Denials in `capabilities()`** — `system.capabilities` now returns
  `api: { gated, denied: [names] }` computed FOR THE CALLING ORIGIN.
  Verified: main page saw its 6 denied names, the keyhole popup saw 138.
- **`onWindowOpen` as a real policy hook** — `POPUPQ`/`POPUPR` wire. The
  hook gets `kind: 'policy'` `{window, opener, url, mode}` BEFORE anything
  shows and may return `'window' | 'external' | 'deny'`; unanswered after
  400ms = the configured mode. The sync `createWebView` contract is worked
  around by building window-mode popups HIDDEN (loading, but never painted)
  until the verdict — `'deny'` closes them unseen. In external/deny modes
  only `'external'`/`'deny'` can be honored (the webview had to be returned
  synchronously — `'window'` needs `"popups": "window"`). Outcome events
  keep flowing as `kind: 'open'` + the `'popup'` page event. Verified: a
  hook-denied popup produced `open deny` and never appeared; note popup
  policy and nav policy COMPOSE — a popup allowed by `onWindowOpen` whose
  URL `onNavigate` then denies shows as an empty window.
- **Find match counts** — `{found, matches, activeMatch}` via a JS
  text-walk layered on the native find (walks text nodes skipping
  script/style into one string, so matches spanning inline elements count;
  active index derived from where the find selection starts). Approximate
  by construction: hidden text counts, shadow DOM doesn't. Verified: 3
  matches, activeMatch stepping 1 → 2, miss = 0/0.
- **Nav-fail correctness** — fail events now name the FAILING url (from
  `NSURLErrorFailingURLStringErrorKey`; `wv.URL` is the page still being
  looked at), and "frame load interrupted" (WebKitErrorDomain 102 — how a
  navigation-turned-download reports itself) is suppressed: painting an
  offline screen because the user exported a CSV would be exactly wrong.
  Found because the 300MB test download emitted a bogus fail for the app's
  own file:// page.
- **Polish** — popup windows honor window.open's left/top and take the
  page's title once loaded; `RELOAD` (app.reload) works for `"url"` apps
  (reloads the live URL instead of no-op'ing on the empty html path); the
  dev hot-reload watcher skips `"url"` apps (their frontend dir may not
  exist and the main window is remote anyway).

Behavioral note from testing (browser-consistent, not a bug): two
downloads/navigations triggered in the SAME tick cancel each other's
provisional action — space programmatic `click()`s.

## Round 3: dialogs + ask-mode verified, and the crash they were hiding (2026-08-05)

Closing the "needs a human at the screen" gap turned up a shipping bug, which
is the whole argument for having closed it.

- **`prompt()` crashed the app, always.** The result string was `autorelease`d
  inside the handler's `@autoreleasepool`, which drains before WebKit's
  completion block runs — so every `prompt()` that returned a value handed
  WebKit a freed CFString. Crash report: `EXC_BREAKPOINT` in `__CF_IS_OBJC`
  via `CFStringGetLength` ← `runJavaScriptPrompt`. Now an owned `copy`,
  released after `done()`. Verified: OK path returns the typed text, Cancel
  returns null, app survives both.
- **Download `"ask"` verified** — save panel appears with the suggested
  filename, Save writes the chosen path and emits `started`→`done`, Cancel
  emits `cancelled` and writes nothing.
- **`alert()`/`confirm()` re-verified** in both directions (OK/Cancel) after
  the fixes.

**Test hook: `TINYJS_TEST_AUTODLG=ok|cancel`** (launcher, env-gated, inert
otherwise). Polls for `[NSApp modalWindow]` and answers it the way a user
would, so dialog paths run headless — reusable for the Windows/Linux legs.
Three traps it cost, all documented in the code:
  - single-shot timing is not enough: an NSAlert is up in ~100ms but an
    NSSavePanel takes over a second, so it polls (a single 600ms shot found
    nothing and left the ask-mode panel waiting for a human).
  - a block capturing a `const std::string&` bound to a temporary is
    dangling when it fires — the mode compared as garbage and the "cancel"
    run silently exercised the OK path. It captures an `NSString *` now.
  - save/open panels are hosted OUT OF PROCESS: their buttons aren't in our
    view tree, `cancel:` works but `ok:` is declared-and-throws
    (`-[NSSavePanel ok:] : not implemented` took the app down). Confirm with
    a Return key event, fall back to `stopModalWithCode:`.

Whether that hook stays in the shipped launcher is a judgement call — it is
~60 lines, unreachable without the env var, and the alternative is that
nobody ever tests dialogs on any platform again.

## Remaining

- **Windows leg** — WebView2: `ScriptDialogOpening` (+ `Deferral`) for
  dialogs, `DownloadStarting` for downloads, `NavigationStarting`/`Completed`
  + `NewWindowRequested` for nav/popups. No find API — needs a page-side
  `CSS.highlights` fallback. No CALL origin stamp yet (origin scoping
  inert there). Bridge/wire/config are already platform-neutral; only
  launcher-win.cc arms are missing.
- **Linux leg** — WebKitGTK: `script-dialog` signal, `download-started`,
  `decide-policy` + `create`, `WebKitFindController` (full-featured, has
  counts). Same: wire is ready.
- **`window` popups keep default chrome** — no `win.open`-style chrome
  options beyond WKWindowFeatures' width/height/x/y.
- **Visual once-over** — every path is verified by assertion, but nobody has
  *looked* at the panels (native styling, origin in the headline) or at a
  popup window's placement. Five minutes with the wraptest scratch app.

## Test bed

`~/all/development/tiny-airtable/app` (live site). The self-driving harness
from the 2026-08-04/05 verification — worth rebuilding rather than
reinventing when the Windows/Linux legs land:

- a scratch app with `inject` + an `api` gate with two `origins` entries +
  `onNavigate`/`onWindowOpen` deny-by-URL-substring + hook-to-file recorders
  (the backend appends every hook firing to a JSON log the harness asserts
  on);
- a page exercising find/gate/capabilities/blob-download/`window.open`,
  writing results via `tiny.store.set` — read them from the app's
  `store.json`;
- a local `python3 -m http.server` serving the cross-origin popup targets
  and a 300MB file (the only way to see download progress events);
- `TINYJS_TEST_AUTODLG=ok|cancel` for the modal paths, so a full run needs
  nobody at the keyboard.

Two behaviours that look like bugs and aren't: programmatic `click()`s in the
same tick cancel each other's provisional navigation (space them), and a
popup allowed by `onWindowOpen` whose URL `onNavigate` then denies shows as
an empty window (the two policies compose).
