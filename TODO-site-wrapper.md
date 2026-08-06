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
findInPage` — true on macOS, on Windows since 2026-08-05 and on Linux
since 2026-08-06 (see those legs' sections).

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

## Windows leg: shipped 2026-08-05 (verified live same day, headless)

Implemented in launcher-win.cc (section "browser affordances for wrapped
sites" — same wire, same section name as the macOS twin) plus a two-line
tinyjs patch in the vendored win32_edge.hh (stashes the WebMessageReceived
Source so main-window CALLs can be origin-stamped; secondaries read
`get_Source` directly). Bridge changes were exactly the two predicted:
capabilities flip + dropping IS_WIN from the find guard. Verified by
rebuilding the macOS harness: wraptest (local frontend, two-origin gate,
inject, hook recorders, popups "window", downloads auto/ask), wrapurl
("url" app), a node static server for cross-origin targets + a 300MB file,
`TINYJS_TEST_AUTODLG` for every modal.

- **JS dialogs** — `put_AreDefaultScriptDialogsEnabled(FALSE)` +
  `ScriptDialogOpening`, answered synchronously (MessageBox / the DLG
  prompt template via a new `run_prompt_raw`). Verified both directions:
  confirm true/false, prompt returns typed text / null, alert returns.
- **Downloads** — `ICoreWebView2_4::add_DownloadStarting` (QI-guarded; a
  runtime too old keeps the engine's default download UI). auto: known
  Downloads folder (`SHGetKnownFolderPath`, GUID spelled inline for MinGW)
  + ` (n)` dedup — and note the suggested filename is just the leaf of
  WebView2's default ResultFilePath, the engine already did MIME→extension.
  ask: IFileSaveDialog seeded name+folder. deny: put_Cancel. Progress via
  `add_BytesReceivedChanged` throttled to 250ms; done/failed/cancelled via
  `add_StateChanged`. Verified: blob download byte-exact, 300MB with 3
  progress events (bytes/total sane), ask-mode save + cancel, and the
  files land on this box's REDIRECTED Downloads (`\\Mac\Home\Downloads`) —
  the known-folder call is what makes that work, don't hardcode
  `%USERPROFILE%\Downloads`.
- **Navigation events + policy** — `NavigationStarting` (start),
  `ContentLoading` (commit), `NavigationCompleted` (finish/fail),
  `ProcessFailed` (crash). **NavigationStarting has no deferral**, so a
  policy ask cancels the nav, sends NAVQ, and re-Navigates on 'allow'
  behind an allow-once marker; 400ms `SetTimer` default-allows. CAVEAT
  this creates: an asked (main-frame http) POST re-issues as a GET —
  macOS holds the original action and doesn't have this hole. fail names
  the last STARTED url (get_Source is the page still showing), and BOTH
  OPERATION_CANCELED and CONNECTION_ABORTED are suppressed as noise —
  the latter is how a navigation-turned-download reports itself
  (measured: the bogus fail arrived BEFORE DownloadStarting, so it can't
  be suppressed by looking the download up). Verified: deny provably
  stopped a load (no start/commit/finish after the ask), external
  shell-opened, policy asks flowed for every main-frame http(s) nav.
- **Popups** — `NewWindowRequested`. window mode: hidden TinyjsSecondary
  window + controller from the shared env; SecCtrlHandler grew
  popup_args/popup_deferral fields — the completion hands the webview to
  `put_NewWindow` INSTEAD of navigating (an explicit Navigate would sever
  window.opener) and completes the deferral; a fresh sec_shim with the
  popup's own winid prevents the cross-wired-RPC trap the macOS fresh-UCC
  dance solves. POPUPQ verdict shows or closes the hidden window.
  Registered as popupN in g_windows: targetable, WINCLOSE-able, emits
  WINCLOSED via the normal WM_DESTROY path; DocumentTitleChanged gives it
  the page's title. Verified: window.open returned a WindowProxy, the
  popup's cross-origin page used its own gated tiny, hook-denied popup
  produced `open deny` and never appeared, backend closed it by id.
  NOT handled: `WindowCloseRequested` — a popup page's own
  `window.close()` is a no-op today (close it via the backend).
- **Find** — WebView2 has no find API, so the page does both halves:
  Chromium's `window.find(t, cs, !fwd, wrap)` selects and scrolls, then
  the exact macOS text-walk counts (returned as an object so
  ExecuteScript's JSON needs no double-decode), answered via route_ret
  like DLG. Verified: 3 matches, activeMatch 1 → 2, miss 0/0/false.
- **Origin scoping** — CALLs now carry the origin second element on BOTH
  paths: secondaries in SecMsgHandler (`get_Source`), main via the
  vendored-handler stash + splice in on_invoke. `origin_from_uri` matches
  the macOS shape (scheme://host[:port], default ports elided, bare
  `file://`, literal `"null"` when unknown). Verified in one run:
  `file://*` got "all" while `http://127.0.0.1:8123` got a 3-name allow
  list (denied call rejects quoting the manifest; capabilities().api
  reported 0 vs 140 denied for the two origins). Note a popup's initial
  about:blank document runs the injected client whose load-time getState
  carries origin "null" → denied under an origins gate — fail-closed by
  design, shows up as one startup denial log line, harmless.
- **inject / url / config** — TINYJS_INJECT now sized dynamically (the
  fixed 8KB buffer silently dropped bigger bundles, both webview sites);
  TINYJS_DOWNLOADS/TINYJS_POPUPS read at startup (Windows is spawn-mode
  always, env is the whole story — no plist/registry equivalent needed).
  "url" apps needed nothing new (argv already carried it); verified the
  remote main window loads with policy ask + NAV events flowing, and
  app.reload re-loads the live URL (sessionStorage-marked second pass).
- **TINYJS_TEST_AUTODLG=ok|cancel ported** — polls for a visible #32770
  owned by this process, posts WM_COMMAND to the right button. THE TRAP
  IT COST (the twin of macOS's out-of-process `ok:` throw): **an MB_OK
  MessageBox's single OK button has ctrl id 2 (IDCANCEL)**, so that close
  works — ok-mode must fall back to IDCANCEL when no IDOK control exists
  or every `alert()` hangs the drill forever. Probed live on the stuck
  dialog. The IFileSaveDialog answers a posted `WM_COMMAND IDOK` fine
  (in-process, unlike macOS's out-of-process panel).

Capabilities: all five wrapper keys now `true` on Windows; Linux is the
only `false` column left.

## Linux leg: shipped 2026-08-06 (verified live same day, headless)

Implemented in launcher-linux.cc (section "browser affordances (Linux)" —
same wire, same section name as its twins) plus the call-token change to
the page shim below. Bridge: capabilities flip + dropping the IS_LINUX
throw from the find guard, exactly the two the Windows leg predicted.
Verified by rebuilding the harness (wraptest with a two-origin gate,
inject, hook recorders, popups window/deny, downloads auto/ask/deny;
wrapurl for `"url"`; a python static server for cross-origin targets and a
300MB file; `TINYJS_TEST_AUTODLG` for every modal).

- **JS dialogs** — the `script-dialog` signal, answered synchronously with
  the same GTK dialogs `do_dialog` builds, headlined by the page's host.
  WebKitGTK does ship default dialogs, so unlike macOS this replaces rather
  than fills a hole — the win is the origin headline, the app's styling and
  a testable path. Verified both directions: confirm true/false, prompt
  returns the typed text / null, alert returns.
- **Downloads** — `download-started` on the default web context, then
  `decide-destination` / `received-data` / `failed` / `finished` per
  download. auto: `G_USER_DIRECTORY_DOWNLOAD` (XDG, not a hardcoded
  ~/Downloads) + ` (n)` dedup + a small MIME→extension table (GLib knows
  content types but not preferred extensions, so unknown types keep the
  bare name). ask: a GtkFileChooser**Dialog**, deliberately NOT
  `gtk_file_chooser_native` — the native one rides the portal, out of
  process, where the test hook can't reach it (the twin of macOS's
  out-of-process save panel). deny: `webkit_download_cancel`. Progress via
  `received-data` throttled to 250ms. `set_destination` takes an absolute
  PATH here (4.1 also accepts a file:// URI; the 6.0 API asserts on
  anything but a path). Verified: blob download byte-exact, 300MB with 12
  progress events (bytes/total sane), ask-mode save and cancel, deny.
- **Navigation events + policy** — `load-changed`
  (started/committed/finished), `load-failed`, `web-process-terminated`.
  **The policy ask lives at the RESPONSE decision, not the navigation
  action**: WebKitGTK fires NAVIGATION_ACTION for subframe navigations too
  and gives no way to tell them apart (measured — `frame_name` is NULL for
  both), while the response decision has
  `is_main_frame_main_resource`. The decision is ref'd and held until NAVR
  or a 400ms default-allow. CAVEAT this stage creates: by ask time the
  request has been issued and answered, so a deny discards the response but
  the server saw the request (macOS holds the original action and has no
  such hole; Windows has a different one — asked POSTs re-issue as GETs).
  Noise suppressed: CANCELLED, FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE (a
  navigation-turned-download) and PLUGIN_WILL_HANDLE_LOAD. Verified: deny
  provably stopped a load (no commit/finish after the ask, page stayed
  put), external shell-opened via a private fake default browser
  (XDG_DATA_HOME/XDG_CONFIG_HOME override — the user's mimeapps.list is
  never touched), and an iframe that self-navigates produced two frame
  loads and ZERO policy asks.
- **Popups** — the `create` signal. window mode builds a hidden
  SecWin+webview, POPUPQ decides show or destroy, registered as popupN in
  g_secwins (targetable, WINCLOSE-able, WINCLOSED via the normal destroy
  path, page title once loaded, window.open's width/height/x/y from
  `ready-to-show`'s window properties). Unlike Windows, `window.close()`
  from the popup's own page works (the `close` signal) — verified
  alongside a backend close by id.
- **The call-token change, and why it exists.** A window-mode popup MUST be
  built with `related-view` (window.opener / postMessage / the WindowProxy
  need it, and a plain view returned from `create` trips an assertion in
  the web process — measured). But WebKit builds the new page from the
  OPENER's PageConfiguration, so the popup's own user content manager is
  ignored and it runs the opener's injected shim — with the opener's window
  id baked in. macOS solves the same trap with a fresh WKUserContentController;
  WebKitGTK gives no such lever, and its script-message signal carries no
  sender. Left alone this is not just cross-wired RPC: **a hostile popup's
  calls would be attributed to the OPENER's origin and inherit its `api`
  gate.** So the shim now tags every message with a per-DOCUMENT token that
  the launcher plants at commit (`assign_call_token`) and maps back to the
  real window; calls made before the token lands are queued rather than
  sent under the wrong identity, and untokened calls arriving on a manager
  known to be shared are dropped. A 1500ms fallback flushes untokened (the
  pre-existing attribution) so an unusual load can never hang an app's own
  boot. Verified: the popup's own gated tiny works (its origin, 140 denied
  vs main's 0, `clipboard.write` rejected quoting the manifest) while
  `window.opener.postMessage` reaches the opener.
- **Find** — `WebKitFindController`, the one launcher whose match counts
  come from the ENGINE (`counted-matches` semantics via found-text's count)
  instead of the mac/win JS text-walk approximation. The active index is
  tracked launcher-side: 1 after a fresh search (count when searching
  backwards), stepped with wraparound on repeats. THE TRAP: the signal is
  `failed-to-find-text`, not `failed-to-find` — the wrong name connects
  with only a GLib-GObject-CRITICAL on stderr and every miss then hangs the
  page's promise forever. Verified: 3 matches, activeMatch 1→2→1 stepping
  back, a case-sensitive miss and a nonsense term both 0/0/false.
- **Origin stamping** — CALLs carry the origin as the second element, from
  the sending window's URI (`origin_from_uri`, same shape as the other
  two). Frame-blind, unlike macOS's frameInfo and WebView2's Source: the
  shim only injects top-frame so every ordinary call IS main-frame, but a
  subframe's hand-rolled postMessage would be attributed to the top frame.
- **popups "window" needed one settings change** —
  `javascript_can_open_windows_automatically` is FALSE by default on
  WebKitGTK (macOS defaults it TRUE), so a second gesture-less
  `window.open` silently never reached `create`. The "popups" config plus
  the POPUPQ ask is the real gate.
- **`inject` / `url` / config** — TINYJS_DOWNLOADS/TINYJS_POPUPS read at
  startup (Linux is spawn-mode always, like Windows); `"url"` apps needed
  nothing new. Verified: the remote main window loads with the policy ask
  and NAV events flowing, the gate applies to the remote origin, inject
  lands, and `app.reload()` re-loads the live URL (sessionStorage-marked
  second pass).
- **TINYJS_TEST_AUTODLG=ok|cancel ported** — polls for a visible modal
  GtkDialog and answers it with `gtk_dialog_response` (ACCEPT/CANCEL for
  file choosers, OK/CANCEL otherwise). Polling for the same reason macOS
  does: `gtk_dialog_run` spins a nested main loop and timeouts keep
  dispatching inside it, but a chooser can take a second to appear.

Capabilities: all five wrapper keys are now `true` on all three platforms.

## Review sweep 2026-08-06 — two origin-spoofs, one fixed here

An adversarial read of both new legs against the macOS contract (protocol
conformance, engine lifetimes, and specifically "can the page influence the
origin the gate trusts?"). Both legs are otherwise sound — wire vocabulary
identical on all three, deferrals/decisions correctly completed and
timeout-bounded, no first-load deadlock, popups hidden until verdict. But
the origin stamp — the input the `"api"` origins keyhole trusts — was
spoofable on BOTH new platforms, in two completely different ways. Neither
was reachable through the cooperative `tiny.js` shim, which is exactly why
both legs' own verification runs passed.

**FIXED HERE (bridge, all platforms).** Windows' main-window RPC rides the
webview library's `bind()`, whose argument array is *whatever the page
passed* to `window.__invoke(...)` (`engine_base.hh` onBind →
`slice.call(arguments)`). The launcher appends the attested origin, but the
bridge read position `[1]` — so `window.__invoke(payload, "file://")` put a
page-chosen string exactly where the gate looked, letting a hostile wrapped
site claim any origin the manifest trusts (typically `file://*`, which
carries the widest grant) and inherit its keyhole. The bridge now reads the
origin as the LAST element, which every launcher appends, so page-supplied
extra arguments sit in the middle and are ignored. macOS and Linux build
their two-element array themselves and were never exposed. Verified: origin
gating still enforced on macOS (two origins, different grants, in one run).

### Linux box — ALL FIXED 2026-08-06, and probed on the box

Fixed in `launcher-linux.cc` (plus `runtime/tiny.js` for #2) and run against a
Linux wrapkit harness the same day, on a launcher rebuilt from source. Two of
the five reproduced exactly as predicted, two did not reproduce **on this
engine** (WebKitGTK 2.52.3) — and where a probe can't fail, the entry says so
rather than claiming a scalp. Per-probe evidence in TODO-verify.md.

1. **SECURITY: the origin stamp followed the ACTIVE uri, not the committed
   document.** FIXED: `g_call_tokens` is now `token → {winid, origin}` and the
   origin is captured in `assign_call_token`, at `WEBKIT_LOAD_COMMITTED`, where
   the active uri IS the committed document's. Message time never re-reads the
   view (only the untokened fallback still does). **The predicted race did not
   reproduce on WebKitGTK 2.52.3**: with `location.href` pointed at a 3s-slow
   page on the TRUSTED origin, all 24 in-flight calls from the still-live old
   document were denied on the pre-fix build too, and the `start` NAV event for
   that navigation still names the OLD url — i.e. this engine does not flip the
   active uri at provisional-load start. The fix stands anyway: it makes the
   stamp independent of *when* an engine chooses to flip.
2. **`tiny.win.id` was the opener's id inside a window-mode popup.**
   REPRODUCED and FIXED (in `runtime/tiny.js`): `win.id` is a **getter** now,
   so it reads `window.__TINY_WIN` at call time rather than baking in whatever
   the opener's document-start shim carried. Differential on the same harness:
   pre-fix the popup reported `main`, post-fix `popup1` — with `__TINY_WIN`
   already correct in both (the launcher's correction always landed; the client
   had simply read it too early). The change is in the SHARED client, so macOS
   and Windows pick it up on their next build — both give a popup its own shim,
   so `win.id` reads the same value there as before, just later.
3. **Shared-manager mark was cleaned up with the wrong manager.** REPRODUCED
   and FIXED: `SecWin` remembers the manager it marked (the OPENER's), and
   `g_shared_ucms` is a COUNT, so one opener with several popups un-marks only
   when the last is gone. Differential: an untokened raw message posted after
   every popup had closed was still dropped pre-fix (the opener stayed flagged
   for the process's life) and is processed post-fix — then denied by the gate
   on its real origin, which is the correct end state.
4. **Dropped calls hanging the page's promise.** The premise did not hold here:
   a `window.open('')` popup DOES commit its about:blank document on WebKitGTK
   (`navigate commit about:blank window popup3`), so it gets a token and the
   call settles — measured as `rejected` (origin `null` → the stranger gate) on
   both builds, inside the 3s timeout. Kept a guard anyway: a window-mode popup
   that has no token 120ms after `create` is given one for whatever document it
   is showing, so a document that genuinely never commits settles as an error
   instead of hanging.
5. Lower, all three addressed: **find state no longer survives a navigation**
   (`find_forget` at commit) — reproduced and differential: the same term
   searched on a fresh document reported `activeMatch 2` pre-fix (stepping
   through the previous page's match list) and `1` post-fix; **`gtk_dialog_run`
   reentrancy** — the handler now refs the view and the `WebKitScriptDialog`
   across the nested loop and skips the answer if the view was destroyed
   meanwhile (defensive; the crash was suspected, not seen, and dialogs still
   answer: confirm `true`, prompt returns its text, alert returns); and
   **`json_escape` now repairs invalid UTF-8** (`g_utf8_make_valid`) so a
   non-UTF-8 filename — legal on Linux — can't put an unparseable line on the
   wire. That last one is fixed by construction, not probed.

### Windows box — ALL SEVEN FIXED 2026-08-06, six of them verified live

Fixed in `launcher-win.cc` (plus the vendored `win32_edge.hh` for #1) and run
against the adversarial harness the same day; the per-item evidence is in
TODO-verify.md's probe section. Launcher rebuilt from source first, as always.

1. **Origin attribution could cross-attribute.** FIXED. The vendored slot
   comment claimed the callback ran synchronously inside Invoke — it does not:
   `on_message` does `dispatch([=]{...})` and `dispatch_impl` is
   `PostMessageW`, so `on_invoke` read the global on a LATER message-loop
   turn, and two documents posting back-to-back could swap origins (an iframe
   inheriting the top frame's keyhole is the direction that matters). The
   patch is now a QUEUE of `{message, source}` matched back by the JSON-RPC
   id, with everything ahead of the match discarded (undispatched messages —
   tinyjs posts drop-forwarding the same way — can't make it grow), a 64-entry
   backstop, and no entry pushed at all when `TryGetWebMessageAsString` fails.
   No match yields `"null"`, which denies under an origins gate: the failure
   direction is closed. Not directly probed — it needs two documents racing —
   so it rides on the forged-origin probes, which all still deny.
2. **A denied popup could open a WebView2 DEFAULT window, and leaked a
   controller.** FIXED and SEEN: `abandon_popup()` sets `put_Handled(TRUE)`
   before completing the deferral, the early return `ctrl->Close()`s the
   orphaned controller, and `SecCtrlHandler` has a destructor that abandons.
   A window census across a full run shows the denied popup producing no
   window of any kind.
3. **`filename` disagreed with the file written.** FIXED and SEEN in both
   arms (` (n)` dedup and an ask-mode rename): the reported name is derived
   from the final path, and it matches what is on disk.
4. **Cancel-and-re-Navigate broke back/forward.** FIXED and SEEN: the ask is
   now gated on `get_NavigationKind` — `BACK_OR_FORWARD` is never asked (it
   cannot be faithfully re-issued), `RELOAD` is asked and re-issued through
   `Reload()`. Asked POSTs re-issuing as GETs remains, and remains structural.
5. **Stale allow-once marker failed OPEN.** FIXED and SEEN: entries are
   stamped and dropped on match, on age (10s), or on any other navigation in
   that window; a re-visited url asks again.
6. **`beforeunload` was auto-accepted.** FIXED and SEEN: it runs a confirm
   box, and only OK accepts. Note it prompts TWICE on an asked navigation —
   beforeunload runs, the ask cancels, the re-issue runs it again.
7. Lower, all three FIXED: the ask-mode save panel now takes the download
   deferral and runs OUT of the `DownloadStarting` callback instead of
   spinning a nested modal loop inside it; `params: []` no longer produces a
   malformed `[,"null"]` body; `prompt()` is parented to the calling window.

### Open — Windows box (new, found by the harness 2026-08-06)

1. **A window-mode popup's committed document has a `tiny` whose RPC never
   returns.** The popup's page runs (it sets its own `document.title`, and
   `tiny.win.id` correctly reads `popup1`), but every call from it — through
   `tiny.*` or a raw `window.chrome.webview.postMessage`, which does not throw
   — produces **no `CALL` line at the launcher at all**, so the page's
   promises hang forever. Measured with a 4s race: `capabilities()` and
   `win.setTitle()` both report HUNG. What DOES arrive is the initial
   `about:blank` document's bootstrap (`client.hello`, `win.getState`, twice,
   both stamped origin `"null"`). So messages work at the popup's birth and
   stop before its real document is usable.
   **Pre-existing, not a regression from the fixes above** — confirmed by
   stashing them, rebuilding at `3e1158c`, and re-running: byte-identical
   behaviour. It does mean the Windows leg's "the popup's cross-origin page
   used its own gated tiny (140 denied vs main's 0)" claim needs re-reading;
   whatever answered there, it is not what a popup's committed document can do
   today. Window-mode popups are for OAuth returns, where the popup mostly
   just needs to exist and postMessage to its opener — untested here, and the
   opener side is a different path — but a popup that wants `tiny` has none.
   Not yet root-caused: script injection into that document demonstrably works
   (the shim and client are both there), while messages out of it do not
   arrive, and that asymmetry is the thing to explain first.
   Reproducer: `scratchpad/wrapkit`, `"url"` app at
   `http://127.0.0.1:8123/?fast=popup`, ~20s.
   **Windows-only, now that Linux has been measured the same way** (2026-08-06):
   a Linux window-mode popup's committed document has a fully working `tiny` —
   `capabilities()` returned its own origin's grant (0 denied against the
   wrapped site's 135), `win.setTitle` retitled the POPUP's window and not
   main, `store.set` landed, and `window.opener.postMessage` reached the
   opener. So the design is answerable; it is WebView2's message path out of
   that document that isn't.

## Remaining
- **`window` popups keep default chrome** — no `win.open`-style chrome
  options beyond the window features' width/height/x/y (all platforms).
- **Windows: `WindowCloseRequested` unhandled** — a window-mode popup
  page calling `window.close()` does nothing; the backend can close it by
  id. Small arm if it ever matters.
- **Windows: asked POSTs re-issue as GETs** — structural (NavigationStarting
  has no deferral; the ask is cancel + re-Navigate). Only main-frame http(s)
  form submissions are affected; fetch/XHR never hit the policy path.
- **Linux: a denied nav still made its request** — structural, see the
  navigation bullet above (the ask is at the response decision, the only
  place WebKitGTK distinguishes main-frame). Fine for "don't show me this
  page", not a request blocker.
- **Visual once-over** — every path is verified by assertion, but nobody has
  *looked* at the panels (native styling, origin in the headline) or at a
  popup window's placement, on any of the three platforms. Five minutes with
  the wraptest scratch app.

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

### Adversarial probes the harness MUST include

Every harness so far drove the cooperative `tiny.js` shim — one argument per
call, no navigation mid-call — and so all three legs' verification runs
passed while the origin stamp was spoofable on two of them (see the review
sweep above). A wrapper's gate is only worth what a HOSTILE page can't do to
it, so the harness has to act like one. Add these to the wraptest page; each
is one assertion and each maps to a real finding.

```js
// 1. Forged origin argument (found the Windows spoof). The page passes a
//    second argument where a naive bridge reads the origin from.
//    PASS = still gated as the page's REAL origin.
R.spoofArg = await tiny.api.call ? null : null; // (call __invoke directly:)
try {
  await window.__invoke(JSON.stringify({ method: 'shell.reveal', params: { path: '/tmp' } }),
                        'file://');            // <- claim the widest grant
  R.spoofArg = 'ALLOWED (bad)';
} catch (e) { R.spoofArg = 'denied'; }

// 2. Same, bypassing the shim entirely (the raw bound-call transport).
try {
  window.__webview__.post(JSON.stringify({ id: 'x', method: '__invoke',
    params: [JSON.stringify({ method: 'secrets.get', params: { key: 'k' } }), 'file://'] }));
} catch (e) {}   // PASS = a denial in the log, never a secret in the reply

// 3. Navigate-then-call race (found the Linux active-URI spoof). Point at a
//    SLOW url on a trusted origin, then call before it commits.
//    PASS = the in-flight call is still gated as the current origin.
location.href = 'http://127.0.0.1:8123/slow';   // server sleeps ~3s
try { await tiny.api.call('shell.reveal', { path: '/tmp' }); R.spoofNav = 'ALLOWED (bad)'; }
catch (e) { R.spoofNav = 'denied'; }

// 4. Popup identity: inside a window-mode popup, tiny.win.id must be the
//    POPUP's id, not the opener's (Linux finding 2 — app.window(tiny.win.id)
//    from a popup otherwise targets main).
tiny.store.set('popupWinId', tiny.win.id);      // PASS = 'popupN', not 'main'

// 5. A call from a document that never commits must not hang forever
//    (Linux finding 4): window.open('') then call with a 3s timeout.
//    PASS = resolves or rejects; FAIL = the promise never settles.
```

Non-JS assertions the runner should make, each from a real finding:

- **A denied popup opens NO window at all.** Count top-level windows after a
  `'deny'` verdict — on Windows an abandoned deferral completed with
  `Handled=FALSE` makes WebView2 open its OWN browser window, which looks
  like "the deny didn't work" and is invisible to a page-side assertion.
- **The reported `filename` names the file that exists on disk.** Download
  the same name twice (forcing ` (2)` dedup) and in ask-mode rename in the
  panel, then `stat` what `info.filename` claims. Windows reports the
  suggestion, not the result.
- **Back/forward still work after a policy ask** — `history.back()` must go
  back (Windows replays it as a forward `Navigate`, so the stack grows).
- **A second navigation to a previously-asked URL still asks** — the
  Windows allow-once marker can go stale and fails OPEN (no policy ask).
- **`beforeunload` prompts** rather than being auto-accepted (Windows).

The rule this encodes: a probe that only exercises the happy path through
our own client proves the client works, not that the gate holds.

Linux adds shapes of its own, because its identity is a per-document TOKEN
rather than an engine-attested frame (all seen 2026-08-06):

```js
// The raw transport, three ways. The shim posts "<token>|<seq>:<payload>"
// to webkit.messageHandlers.tiny — so a hostile page can post whatever it
// likes there, and the launcher's answer must be "not a window".
const T = window.webkit.messageHandlers.tiny;
T.postMessage('ffffffff-dead-beef-dead-ffffffffffff|9901:' + payload); // forged token
T.postMessage(String(window.__TINY_TOK).replace(/^./, 'a') + '|9903:' + payload); // mutated
T.postMessage('|9902:' + payload);   // untokened: the pre-token fallback shape
// PASS = the first two produce NO `CALL` line at all; the third is stamped
// with the page's REAL origin and denied by the gate.

// A popup must not be able to speak as its opener.
try { t = window.opener.__TINY_TOK; } catch (e) {}   // PASS = SecurityError

// The shared-manager mark must be lifted when the LAST popup closes: post
// an untokened message while one is alive (PASS = dropped) and again after
// they are all closed (PASS = processed, then denied on the real origin).

// Find state must not survive a navigation: same term, N matches, searched
// on page A and again on a fresh page — PASS = activeMatch 1, not 2.
```

And run every one of them against the UNFIXED build too. Two of the five
Linux findings did not reproduce on WebKitGTK 2.52.3; without the
differential they would have been written up as fixes that were never bugs.

Linux harness notes (2026-08-06), each of which cost a wrong result first:
- the page must live in `src/frontend/`, and `tiny.quit()` is the page's
  quit — `tiny.app.quit` doesn't exist, and awaiting it hangs the run in a
  way that reads exactly like a launcher hang;
- `app.reload()` is a BACKEND method, so a page can only reach it through
  an app api method;
- hook recorders that spread the info object (`{kind:'popup', ...info}`)
  have their `kind` overwritten by the info's own — filter on
  `policy`/`open`/`start`, not on the label you thought you set;
- to test the `external` verdict without touching the user's default
  browser, point XDG_DATA_HOME + XDG_CONFIG_HOME at a scratch dir holding
  a fake handler .desktop and a mimeapps.list (the app's store moves there
  too — read results from that copy).
