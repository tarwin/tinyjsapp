---
title: "fetch"
---

# fetch

<p class="tagline">tiny.fetch and tiny.proxyURL — networking without CORS</p>

A page's own `fetch()` is a browser fetch: another origin's API answers only if it sends CORS headers, and the page's CSP and mixed-content rules apply. `tiny.fetch` has the same shape as `window.fetch`, but the request runs in the [backend](./backend), so none of those limits apply and the page can reach any http(s) server. `tiny.proxyURL` does the same for `<audio>` and `<video>`, so a stream from another origin can feed Web Audio.

Both are page-only. The backend doesn't need them: its own `fetch()` already has no CORS, and it's the same fetch `tiny.fetch` runs on.

## tiny.fetch {#request}

`tiny.fetch(url, init?)` resolves to a real `Response`, so `ok`, `status`, `headers`, `json()`, `text()`, `arrayBuffer()` and `body` all work as usual.

```js
const r = await tiny.fetch('https://api.example.com/search', {
  method: 'POST',
  headers: { 'content-type': 'application/json', authorization: `Bearer ${token}` },
  body: JSON.stringify({ q: 'hello' }),
});
if (!r.ok) throw new Error(`search failed: ${r.status}`);
const results = await r.json();
```

| `init` field | |
|---|---|
| `method` | as in `fetch` |
| `headers` | a plain object, an array of pairs, or a `Headers` |
| `body` | a string, `ArrayBuffer`, typed array, `Blob` or `URLSearchParams` |
| `redirect` | `'follow'` (default), `'manual'` or `'error'` |
| `stream` | `true` for a live body — see [streaming](#stream) |

Other `fetch` options (`signal`, `credentials`, `mode`, `cache`, …) are ignored.

How it differs from `window.fetch`:

- **Pass an absolute URL.** The backend doesn't know the page's address, so `'/api/x'` isn't resolved against it.
- **It's a separate client.** The webview's cookies aren't sent and no `Origin` header is added. You may set headers a browser forbids, such as `Cookie`.
- **Errors reject with a string**, like every `tiny` call: a network failure or an invalid header gives `catch (msg)` the message, not a `TypeError`. An HTTP error status (404, 500) is not an error: it resolves with `r.ok === false`.
- **The whole body is read first.** Without `stream`, the backend downloads the full response before the promise resolves. For a large download or a response that never ends, use `stream: true`.
- **No abort.** `signal` isn't supported. A streaming body can be stopped with `reader.cancel()`.
- **`r.url` and `r.redirected`** report the address after redirects, as in a browser.

### redirects {#redirects}

Redirects are followed hop by hop, up to 20, the way a browser does: a 303, or a 301/302 after a `POST`, becomes a `GET` without the body; 307 and 308 keep both. `Authorization`, `Cookie` and `Proxy-Authorization` are dropped when a redirect leads to another origin, and stay dropped for the rest of the chain. A redirect to anything but http or https rejects.

With `redirect: 'manual'`, the 3xx response itself resolves, and unlike in a browser you can read its `status` and `Location` header. `redirect: 'error'` rejects on the first redirect.

### HTTP versions and TLS {#network}

The backend's runtime speaks HTTP/1.1 and HTTP/2 over TLS 1.2 and 1.3, and can move to HTTP/3 on hosts that advertise it. It checks certificates against its own bundled list, not the operating system's, so a certificate only your system trusts (a corporate TLS-inspecting proxy, for example) fails with a network error. To trust one, start the app with `SSL_CERT_FILE` set to a PEM file of CA certificates. Proxies come from the usual `http_proxy` and `https_proxy` environment variables. The same applies to `fetch()` in the backend.

On HTTP/2, a large response from a far-away server can be slower than on HTTP/1.1: a 20 MB download took about 40 s from a host 200 ms away, where curl took 5 s. Nearby CDNs aren't affected.

## streaming a response {#stream}

With `{ stream: true }` the promise resolves as soon as the headers arrive, and `body` is a live stream. The backend holds the connection and sends a chunk each time the page reads one, so a source that never ends, like internet radio, doesn't pile up in memory.

```js
const r = await tiny.fetch('https://ice1.somafm.com/groovesalad-128-mp3', { stream: true });
const reader = r.body.getReader();
for (;;) {
  const { value, done } = await reader.read();   // value: Uint8Array
  if (done) break;
  handleChunk(value);
}
```

`reader.cancel()` closes the upstream connection. So does closing the window or reloading the page that started it. A connection error while streaming makes `reader.read()` reject.

## under an api gate {#gate}

`tiny.fetch` is the call named `fetch` to the [`api`](./config#api) gate in tinyjs.json; the `"wrapper"` preset leaves it off. Streaming also uses `fetch.pull` and `fetch.cancel`, so a gate that should allow streaming needs both entries:

```json
{ "api": { "preset": "wrapper", "enable": ["fetch", "fetch.*"] } }
```

A page that can call `tiny.fetch` has the app's whole network reach, including servers on the local network. For your own pages that's no new risk: they can already call your backend, which can do anything. Think about it before you enable `fetch` for a site you [wrap](./site-wrappers).

## a remote stream into Web Audio {#proxyURL}

A `MediaElementSource` on an `<audio>` element playing from another origin outputs silence, by spec, so a radio stream can't drive an EQ or an analyser. `tiny.proxyURL(url)` returns a `tiny-media://` URL that streams the remote resource through the native layer with permissive CORS headers. The element isn't tainted, and the Web Audio graph gets real samples.

```js
const audio = document.querySelector('audio');
audio.crossOrigin = 'anonymous';
audio.src = tiny.proxyURL('https://ice1.somafm.com/groovesalad-128-mp3');

const ctx = new AudioContext();
const analyser = ctx.createAnalyser();
ctx.createMediaElementSource(audio).connect(analyser).connect(ctx.destination);
audio.play();
```

`tiny.proxyURL` only builds the URL; it returns a string at once and never fails. The native layer makes the request when the element loads it, follows redirects, and accepts http and https URLs only.

- **Live streams play.** An icecast or shoutcast stream answers `200` with no `Content-Length`. The proxy gives an audio or video response like that a very large fake length, so the media engine plays it progressively. `audio.duration` and `currentTime` are meaningless for such a stream, so don't wire a seek bar to one.
- **Seeking**: on macOS the element's range requests pass through, so a finite file stays seekable. Linux always fetches from the start.
- **Your pages only.** Because the proxy reads any URL with CORS out of the way, only the app's own pages (and the dev server) may use it. A wrapped site, an iframe from elsewhere, or a page the app navigated to is refused unless its `api` `origins` key allows the name `media.proxy` (see [`api`](./config#api)). A proxied response is never served as a page.

**macOS and Linux only.** Windows has no `tiny-media://` handler, so a proxied URL doesn't load there. Check before you use it:

```js
const caps = await tiny.system.capabilities();
audio.src = caps.proxyURL === false ? streamUrl : tiny.proxyURL(streamUrl);
```

To read the rendered audio without routing it through Web Audio at all, see [audio tap](./audiotap).
