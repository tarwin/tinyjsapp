---
title: "txiki.js"
---

# txiki.js

<p class="tagline">the runtime your backend runs on</p>

The [backend](./backend) runs on [txiki.js](https://txikijs.org/), a small JavaScript runtime: the QuickJS engine, libuv for I/O, with SQLite and WebAssembly built in. tinyjs ships **txiki.js 26.11.0**. Everything txiki.js offers is available to your backend — this page is a map of it, with links to [txiki.js's own reference](https://txikijs.org/docs/api-reference) for the details.

It isn't Node.js: there's no `require`, no `process`, no `Buffer` and no `node:*` modules. Use the `tjs` global, the `tjs:*` modules and standard Web APIs instead. npm packages work in a TypeScript backend (it's bundled with esbuild) as long as they don't need Node's built-ins.

The page has none of this — it runs in the system webview, with no `tjs`. Expose what it needs through [`api` functions](./backend#api).

## the tjs global {#tjs}

| area | calls | reference |
|---|---|---|
| files | `readFile`, `writeFile`, `open`, `stat`, `lstat`, `readDir`, `makeDir`, `makeTempDir`, `makeTempFile`, `remove`, `rename`, `copyFile`, `symlink`, `readLink`, `realPath`, `chmod`, `chown`, `utime`, `statFs`, `watch` | [filesystem](https://txikijs.org/docs/api/global.tjs.Function.open) |
| processes | `spawn`, `exec`, `kill`, `exit`, `pid`, `ppid`, `addSignalListener` | [process](https://txikijs.org/docs/api/global.tjs.Function.spawn) |
| network | `connect`, `listen`, `lookup` (DNS) | [networking](https://txikijs.org/docs/api/global.tjs.Function.connect) |
| HTTP server | `serve` | [HTTP server](https://txikijs.org/docs/api/global.tjs.Function.serve) |
| system | `args`, `env`, `cwd`, `chdir`, `homeDir`, `tmpDir`, `hostName`, `exePath`, `system`, `version` | [system](https://txikijs.org/docs/api/global.tjs.Variable.args) |
| console & I/O | `stdin`, `stdout`, `stderr`, `createConsole` | [utilities](https://txikijs.org/docs/api/global.tjs.Function.createConsole) |
| engine | `engine` — versions, GC, features | [engine](https://txikijs.org/docs/api/global.tjs.Namespace.engine) |
| modules | `setImportMap` | [modules](https://txikijs.org/docs/api/global.tjs.Function.setImportMap) |

```js
const text = new TextDecoder().decode(await tjs.readFile(path));
await tjs.writeFile(out, new TextEncoder().encode(text.toUpperCase()));

const p = tjs.spawn(['git', 'status', '--short'], { stdout: 'pipe' });
let output = '';
for await (const chunk of p.stdout) output += new TextDecoder().decode(chunk);
await p.wait();
```

On Windows, use [`app.spawnHidden()`](./backend#app) rather than `tjs.spawn()` for console programs, so each one doesn't flash a terminal window.

## tjs: modules {#modules}

| module | for | reference |
|---|---|---|
| `tjs:sqlite` | a SQLite database — see [sqlite](./sqlite) | [tjs:sqlite](https://txikijs.org/docs/api/tjs-sqlite) |
| `tjs:path` | joining and splitting paths | [tjs:path](https://txikijs.org/docs/api/tjs-path) |
| `tjs:hashing` | MD5, SHA-1, SHA-256 and other digests | [tjs:hashing](https://txikijs.org/docs/api/tjs-hashing) |
| `tjs:uuid` | UUIDs | [tjs:uuid](https://txikijs.org/docs/api/tjs-uuid) |
| `tjs:ffi` | calling C libraries directly, through `dlopen()` | [tjs:ffi](https://txikijs.org/docs/api/tjs-ffi) |
| `tjs:wasi` | running WASI WebAssembly modules | [tjs:wasi](https://txikijs.org/docs/api/tjs-wasi) |
| `tjs:posix-socket` | raw sockets | [tjs:posix-socket](https://txikijs.org/docs/api/tjs-posix-socket) |
| `tjs:ipaddr` | parsing IP addresses | [tjs:ipaddr](https://txikijs.org/docs/api/tjs-ipaddr) |
| `tjs:readline`, `tjs:getopts` | terminal input and command-line options, for command-line tools | [readline](https://txikijs.org/docs/api/tjs-readline), [getopts](https://txikijs.org/docs/api/tjs-getopts) |
| `tjs:assert`, `tjs:utils` | assertions; `format` and `inspect` | [assert](https://txikijs.org/docs/api/tjs-assert), [utils](https://txikijs.org/docs/api/tjs-utils) |

```js
import path from 'tjs:path';
import { createHash } from 'tjs:hashing';

const file = path.join((await app.paths()).data, 'notes.json');
const digest = createHash('sha256').update(text).digest();
```

## Web APIs {#web}

The backend also has the standard Web APIs txiki.js implements, including `fetch`, `WebSocket`, `EventSource`, `Worker`, `MessageChannel`, `BroadcastChannel`, `crypto` (with `crypto.subtle` and `randomUUID`), `TextEncoder`/`TextDecoder`, `URL`, `Blob`, `FormData`, `AbortController`, `CompressionStream`, `structuredClone`, `performance` and `localStorage`. See txiki.js's [Web Platform APIs](https://txikijs.org/docs/features/web-platform-apis) for the full list.

For an app's settings, prefer [`app.store`](./store) to `localStorage`: it's the same store the page sees, saved in the app's data folder.

## types {#types}

Every project gets txiki.js's own type definitions as `types/tjs.d.ts` (`backend/tjs.d.ts` in the Vite templates), so editors complete and check `tjs.*` calls and `tjs:*` imports — no npm needed. They match the txiki.js version tinyjs ships, and [`tinyjs update-project`](./cli#update-project) brings them up to date after you update tinyjs.

They're for the backend only: the Vite templates' `src/` is the page, which has no `tjs`, so it doesn't get them.

## versions {#versions}

txiki.js's website documents its latest release, which can be newer than the version tinyjs ships. If something there doesn't exist in your backend, check `tjs.version` — or `tjs.engine.versions` for QuickJS, libuv and SQLite. A tinyjs release names the txiki.js version it moves to in the [changelog](/changelog).

Sharp edges worth knowing — async file calls next to sync SQLite, `stdout: 'ignore'` vs `'null'` — are on [gotchas](./gotchas#txiki).
