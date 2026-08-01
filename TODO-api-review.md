# API review — round 2 (proposals, not decisions)

Questions raised 2026-07-26. Researched, not acted on: these are naming and
placement calls that want a human. Each has a recommendation and the evidence
behind it.

## 1. ~~`ocr` / `share` / `wifi` / `authenticate`~~ — DECIDED 2026-07-26

**Done:** `wifi` moved to `tiny.system.*`, and `battery` + `idleTime` moved
with it (they're the same kind of thing — leaving them behind would have made
the rule incoherent). `ocr`, `share` and `authenticate` stay on `tiny.app`.
The reasoning below is kept because it's the rule everything else gets
measured against.

First, the dividing line, because without one this keeps coming back:

- **`tiny.system.*` answers questions about the machine.** Today: `os()`,
  `info()`, `architecture()`, `capabilities()`, `requirements()`. All read-only
  facts, none of them *do* anything.
- **`tiny.app.*` is things this app does** — decorate its icon, open a dialog,
  read the clipboard.
- **`tiny.macos.*` is concepts no other OS has** (settled last round).

Against that line:

| call | verdict |
| --- | --- |
| `wifi()` | **Move to `tiny.system.wifi()`.** Pure machine state — SSID, signal, tx rate. It reads exactly like `battery()`, which has the same problem (see below). |
| `authenticate()` | **Keep on `app`.** It's an interaction, not a fact: it puts a sheet in front of the user and waits. Touch ID / Windows Hello / polkit all exist, so it stays intent-named. |
| `ocr()` | **Keep on `app`.** A service the app invokes on data it supplies. Windows has WinRT OCR and Linux has tesseract, so it's portable-in-principle and belongs with the other unbuilt-but-possible calls. |
| `share()` | **Keep on `app`.** Same shape as `ocr` — an action on app data. macOS share sheet, Windows share UI, Linux portal. |

If `wifi()` moves, **`battery()` and `idleTime()` should move with it** — they
are the same kind of thing (machine state), and leaving them behind makes the
rule incoherent. That's the real scope of this change: three calls, not one.

**Cost:** small. `battery` and `wifi` are used by menu-bar monitor examples;
`idleTime` by till. All three are one-line call sites.

## 2. ~~`tiny.win.openFile()` → ?~~ — DECIDED 2026-07-26

**Done:** moved to `tiny.dialog.*` (all seven: the four pickers plus
alert/confirm/prompt). Native dialogs kept on all three platforms; macOS
sheets stay possible later as an implementation change behind the same call,
not another rename.

**Recommendation: rename, but to `tiny.dialog.*`, not `tiny.system.*`.**

Evidence for moving them off `win`: the launcher runs these with `runModal`
(launcher-macos.cc:562+), i.e. **application-modal, not window-attached
sheets**. Nothing about them is scoped to the calling window — `win.openFile()`
on a satellite behaves identically to one on main. So `win.*` is misleading
today.

Evidence against `system.*`: a file picker isn't a fact about the machine, so
it breaks the rule proposed above the moment it's set.

`tiny.dialog.openFile()` / `.saveFile()` / `.chooseFolder()` / `.alert()` /
`.confirm()` / `.prompt()` reads better than all three current options and
groups things that are already documented together under a "dialogs" heading.
Note `alert`/`confirm`/`prompt` live on `tiny.win.*` too and have the same
mismatch.

**Caveat worth knowing before deciding:** macOS *can* attach these as sheets
(`beginSheetModal:`), which would make them genuinely window-scoped and argue
for keeping `win.*`. That would be a behaviour improvement — a sheet is more
native than a floating modal — so the question is really "do we want sheets
one day?" If yes, keep `win.*` and fix the implementation instead.

## 3. Kitchen-sink "Mac superpowers" — several of these aren't mac-only

Checked against the capability tables. **`pickColor`, `secrets` and
`spotlight` work on all three platforms** (portal / Secret Service / plocate on
Linux; Credential Manager on Windows). They are not superpowers, they're just
API.

Genuinely macOS-only in that section: `quickLook`, `applescript` (both now
`tiny.macos.*`), `ocr`, `recorder`, `share`, `ai`, `selectedText`,
`otherWindows`.

**Recommendation:** rename the section to something like "deep system access"
and move the three cross-platform ones into the ordinary API tabs. The section
title is currently teaching people the wrong thing about their own portability.

## 4. "Traffic lights"

It's the macOS term for the close/minimize/zoom buttons, and it is the actual
`setChrome({ trafficLights })` key — so the *card* name matches the *API* name.
The API name is the thing to decide first; renaming only the card would make
them disagree.

Windows and Linux have the same three-button concept (min/max/close) with no
shared name. Candidates: `windowButtons`, `titlebarButtons`, `controls`.

**Recommendation: leave it.** Unlike `dock`, this isn't misleading about
*portability* — `setChrome` is macOS-shaped anyway, and "traffic lights" is
unambiguous to anyone who has used a Mac. Low value, non-zero churn. Revisit
only if `setChrome` itself gets a cross-platform pass.

## 5. Kitchen-sink FFI card

It demonstrates `tjs:ffi` with two demos: `sysctlbyname()` for kernel values,
and zlib `compress2()`/`uncompress()` round-tripping text.

**It is macOS-only by construction** — the paths are hardcoded
`/usr/lib/libSystem.B.dylib` and `/usr/lib/libz.dylib`. On Windows and Linux
the `dlopen` fails and the whole tab is dead, with nothing saying why. It sits
behind a top-level tab (⌘8) presented like any other feature.

**Recommendation:** keep the demo (FFI is a real selling point) but make it
honest — branch the library path per platform (`libz.so.1` /`zlib1.dll`,
`kernel32.dll` for a Windows equivalent of the sysctl demo), or detect and
show "this demo is macOS-only" instead of a silently broken tab. The zlib half
ports easily; the sysctl half has no Windows analogue and would need a
different demo.

## Found while looking: a third capabilities over-claim

Not a proposal — a bug, fixed in this branch. Windows declared no `wifi` key,
so under the "absent = true" rule `capabilities().wifi` read **true**, while
launcher-win.cc answers `null` for a wifi query unconditionally
(launcher-win.cc:3710, the `-> null` list). Same class as the
`nowPlaying`/`haptic` over-claim, and the third time this table has lied by
omission.

**Worth doing once, properly:** the "absent = true" convention is what makes
this recur — every new unimplemented call is a claim of support until someone
remembers to deny it. An explicit table per platform (or a test that asserts
every capability key the runtime knows about appears in every platform's
column) would end the whole category.

## 6. Tool calling for `macos.ai` — feasible, measured, NOT built

Raised 2026-07-27: could the on-device model call app functions ("move the
window", "show a bubble")? Answered by experiment on macOS 26.5 rather than
from docs — three throwaway Swift programs, all compiled and run.

**Yes, and dynamically.** FoundationModels has first-class tool calling, and
crucially the schema does *not* have to be a compile-time `@Generable` Swift
type: `DynamicGenerationSchema` + `GenerationSchema(root:dependencies:)` build
one from values at runtime, and a tool can take `GeneratedContent` (untyped
JSON) as its arguments. Measured: a tool whose name, description and argument
schema were all constructed from strings was invoked correctly, arguments
arriving as `{"y":340,"x":120}` from the prompt "Move the window to 120, 340
please." That is exactly the shape a JS-defined tool needs.

Sketch of the plumbing, all of which is new work:
- JS declares `{ name, description, parameters, run }`; the bridge ships the
  spec with the generate call.
- Swift builds the schema, and its `call` bridges out through a C callback →
  the launcher writes `AITOOL <id> <name> <argsJson>` → the bridge runs the JS
  handler → `AITOOLRESULT <id> <json>` comes back.
- The obstacle: `tiny_ai_generate` currently blocks a thread on a semaphore.
  A tool call has to round-trip to JS *while* that thread is blocked. The
  socket I/O is on another thread so it should work, but the blocking design
  is the thing most likely to bite.

**The measurement that should shape the API, though, is the failure rate.**
Asked to do three things in one turn (move, resize, bubble), across four runs
the model called all three tools **once**. The other three runs it did two.
One run passed `y: 0` when the prompt said 40. Order varied every time.

And in **every single run — including the ones that skipped a tool — the prose
answer claimed all three had been done.** "The window has been moved, resized,
and a bubble shown" is what it says whether or not it moved anything.

So if this is built:
- The app's record of what happened must come from the **tool-call log**,
  never from the model's text. The text is confabulated confidently.
- Prefer one action per turn, or verify after each call and re-prompt.
- A demo that prints the model's summary next to what actually ran would
  teach this better than any prose — and is the honest way to show it.

## 7. CLI shims and file/folder opening — wishlist, 2026-07-27

Raised from outside: building a `tinyjs`-app CLI shim works today but only by
special-casing `open -a` on macOS and the `--open` launcher mode elsewhere.
Four changes would collapse the whole thing to `exec <exe> "$@"` on all three.
Each claim below was checked against the source.

**1. Deliver argv as `onOpenFiles` — cold start AND second launch.**
Confirmed: nothing in `bridge.js` reads process argv. The only mentions are a
`probeOk(argv)` spawn helper and a Windows console-routing comment. Paths reach
an app exclusively through the launcher — `OPENFILES <json-paths>` on macOS
(routed from `openURLs`), and the instance pipe on a re-launch, which already
carries `{ url }` or `{ paths }` and calls `onOpenFiles`. So the plumbing on
the receiving end exists; what's missing is reading argv at startup and
forwarding it into the same two paths. That's the highest-value item here: it
removes the per-platform launch dance entirely.

**2. `"openFolders": true` alongside `fileExtensions`.** Wants `public.folder`
in `CFBundleDocumentTypes` and `inode/directory` in the `.desktop`
`MimeType=`. Small and self-contained; the plist/desktop writers already take
a list.

**3. `tinyjs build --cli <name>` to emit the shim next to the app.** The
argument for it is real: the shim needs the launcher path and the instance-pipe
name, and only the build knows both. Worth deciding whether the shim is a
build artifact or something `tinyjs publish` installs.

**4. `app.setAsDefaultHandler(ext)`.** Confirmed one-line-shaped on Linux:
`bridge.js` already loops `xdg-mime default <app>.desktop
x-scheme-handler/<scheme>` for every `urlScheme`, while the file MIME types
derived from `fileExtensions` are written into `MimeType=` and never made
default. A second loop over those mimes is the whole Linux story. macOS wants
`LSSetDefaultRoleHandlerForContentType`, Windows a `HKCU\…\UserChoice` write
that Windows deliberately makes hard — so this one is "Linux now, the other two
are their own projects", and the API should probably answer `'unsupported'`
rather than pretend.

**5. ~~Stale Windows docs~~ — FIXED 2026-07-27.** The README listed deep links
/ file associations / single instance as unported while `bridge.js` registers
them, and it was worse than reported: `authenticate` (WinRT
`UserConsentVerifier`), `audioTap` (WASAPI loopback) and exe icons
(`--embed-icon`) were all listed as missing too, and `SKILL.md` contradicted
itself — claiming `audioTap` works on Windows in one paragraph and listing it
as macOS-only in the next. Both files corrected, and the Windows gap list
rewritten around what's actually missing (`app.badge`, `nowPlaying`/media keys,
`otherWindows`/`moveWindow`, `pickColor`, `spotlight`, `system.locale`).

**6. ~~`tiny.dialog.openFile({ types: ['md','markdown','txt'] })` — native
file-type filters~~ — BUILT 2026-07-31, same day.** Decision: `{ types }` is
**explicit only** — the proposed fileExtensions-as-default was rejected
(owner's call: an open panel that silently narrows because of an unrelated
file-association declaration is confusing, and the app can always pass its
own manifest values by hand). Shipped on all three launchers per the
mapping below; macOS machine-verified, Windows/Linux boxes in
TODO-verify.md. Original write-up, kept for the evidence:

Raised 2026-07-31. Today every open panel shows every file: `openFile`/`openFiles`/
`saveFile` take no arguments (`runtime/tiny.js:712`), and `DIALOG_OPS` in
`bridge.js:690` sends `DLG <id> open` with an empty arg list. All three
launchers run their dialog with no filter attached — NSOpenPanel at
`launcher-macos.cc:645` never sets `allowedContentTypes`, `run_file_dialog`
in `launcher-win.cc` never calls `SetFileTypes`, and the
`gtk_file_chooser_native_new` at `launcher-linux.cc:1516` adds no
`GtkFileFilter`. Each platform has a first-class mapping waiting:
`allowedContentTypes` (UTType from extension) on macOS, a
`COMDLG_FILTERSPEC` array on Windows, `GtkFileFilter` +
`gtk_file_filter_add_pattern` on Linux. The wire change is one extra
tab-separated field on the `DLG` line, and the default costs nothing new:
`createApp` already receives the manifest's `fileExtensions`
(`bridge.js:770`), which declares exactly this information — so an app that
registered `.md`/`.txt` associations gets a matching open panel for free,
and `{ types }` overrides per call. `saveFile` should take the same option
(NSSavePanel's `allowedContentTypes` and the same `COMDLG_FILTERSPEC` /
`GtkFileFilter` paths cover save too). Include an implicit "All files"
escape hatch on Windows/Linux, where a filter otherwise hard-hides
everything else.
