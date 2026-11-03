---
title: "gotchas"
---

# gotchas

<p class="tagline">things that cost real debugging</p>

Sharp edges in txiki.js (26.6.0, the runtime tinyjs ships) and the webviews, collected so you don't have to rediscover them. Each feature page lists its own platform differences; this page is for the ones that cut across everything.

## txiki.js {#txiki}

**File calls are async; SQLite is sync.** Every `tjs.*` file system call returns a promise, while `tjs:sqlite` doesn't. A missing `await` on `makeDir` before opening a database is a race that only fails sometimes:

```js
await tjs.makeDir(dir, { recursive: true });
const db = new Database(dir + '/app.db');   // no await here
```

**`tjs.cwd` is a property**, not a function — and in a built app it's rarely your project folder, so build paths from [`app.paths()`](./resources) instead.

**To silence a child process, use `'ignore'`.** `stdout: 'null'` doesn't silence anything — the child's output goes to yours:

```js
tjs.spawn(argv, { stdout: 'ignore', stderr: 'ignore' });
```

**Writing to a child's stdin is unreliable.** Several writes to `stdin: 'pipe'` may not all arrive. Pass input as arguments or through a file instead.

**`import.meta.url` throws in a built app**, where the backend is compiled into one executable. Find shipped files through `app.paths()` ([resources](./resources), [executables](./executables)).

## the webview {#webview}

**Hidden windows are slowed down.** The webview throttles `requestAnimationFrame` and timers in a window that's covered, minimized or off-screen, so a hidden window can't reliably drive a visible one. Do continuous work in the visible window, or in the [backend](./backend) (which isn't a webview) and [push](./backend#push) the results.

**On macOS, local media is limited to the page's folder.** `<img>`, `<audio>` and `<video>` there can only load files under the frontend folder; widen that with [`readAccess`](./config#readAccess), and build the URL with [`tiny.fileURL(path)`](./api#fileURL). Windows and Linux already let a `file://` page load other local files. Don't build `file://` URLs by hand: a file on another disk (`/Volumes/…`) never loads over `file://`, even with `"readAccess": "/"`, so `tiny.fileURL` hands out a `tiny-media://file/` URL for those.

**Pages are real `file://` documents**, not HTML injected into `about:blank`. That's what makes relative paths work, and it's a secure context, so APIs that need one (WebGPU's `navigator.gpu`, for instance) are there. On macOS 15 and earlier, tinyjs also turns on WebKit's WebGPU switch for you.

**`fetch` from the page follows browser rules** — CORS included. For requests a browser would block, use [`tiny.fetch`](./fetch), which goes through the backend.

## Linux {#linux}

**Web Audio crackles.** Audio sent to `ctx.destination` under WebKitGTK is rendered on a normal-priority thread and stutters under load. That's measured, not theoretical, and it's why tinyjs has native [audio filters](./audio) and [sound effects](./sampler) instead of relying on Web Audio there.

**Wayland limits what an app can do** with windows and input: no placing windows yourself (see [`windowPlacement`](./config#windowPlacement)), global hotkeys through a system prompt ([hotkeys](./hotkeys)), and no global cursor position without permission ([window](./window#cursor)).
