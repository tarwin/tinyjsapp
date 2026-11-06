// tjs run test/embed-url/check.js — headless, any OS: which URLs tiny.embedURL
// accepts, what the loopback page contains, and what the server refuses.
import { embedTarget, embedPage, makeEmbedHost } from '../../runtime/bridge.js';

let fail = 0, n = 0;
const ok = (name, cond, extra = '') => {
  n++;
  if (!cond) { fail++; console.log('FAIL', name, extra); }
};
const accepted = (url, extra) => { try { embedTarget(url, extra); return true; } catch { return false; } };

// [url, extra hosts, accepted]
const cases = [
  ['https://www.youtube.com/embed/tgbNymZ7vqY', null, true],
  ['https://www.youtube.com/embed/x?start=30&rel=0', null, true],
  ['https://www.youtube-nocookie.com/embed/x', null, true],
  ['https://player.vimeo.com/video/1', null, true],
  ['HTTPS://WWW.YOUTUBE.COM/embed/x', null, true],
  ['https://open.spotify.com/embed/x', ['open.spotify.com'], true],
  ['https://open.spotify.com/embed/x', ['OPEN.spotify.com'], true],
  ['https://open.spotify.com/embed/x', null, false],
  ['https://open.spotify.com/embed/x', 'open.spotify.com', false],     // not an array
  ['http://www.youtube.com/embed/x', null, false],
  ['https://www.youtube.com:8443/embed/x', null, false],
  ['https://user:pw@www.youtube.com/embed/x', null, false],
  ['https://www.youtube.com@evil.example/embed/x', null, false],
  ['https://www.youtube.com.evil.example/embed/x', null, false],
  ['https://evil.example/www.youtube.com/embed/x', null, false],
  ['javascript:alert(1)', null, false],
  ['data:text/html,x', null, false],
  ['file:///etc/hosts', null, false],
  ['/embed/x', null, false],
  ['', null, false],
  [null, null, false],
  [undefined, null, false],
];
for (const [url, extra, want] of cases)
  ok('target ' + url, accepted(url, extra) === want, 'want ' + want);

// The target can't break out of the attribute.
const page = embedPage(embedTarget('https://www.youtube.com/embed/x"><script>alert(1)</script>\'&a=1'));
ok('page escapes <', !page.includes('<script'), page);
ok('page keeps one iframe src', (page.match(/ src="/g) || []).length === 1, page);
ok('page escapes quote', !/src="[^"]*"[^ ]*><script/.test(page), page);

const host = makeEmbedHost(['example.org']);
const a = host.url('https://www.youtube.com/embed/a');
const b = host.url('https://www.youtube.com/embed/b');
const m = a.match(/^http:\/\/127\.0\.0\.1:(\d+)\/e\/([0-9a-f]{32})$/);
ok('url shape', !!m, a);
const port = Number(m?.[1]);
ok('real port', port > 0, a);
ok('same target, same url', host.url('https://www.youtube.com/embed/a') === a);
ok('other target, other id', a !== b);
ok('server reused', b.startsWith(`http://127.0.0.1:${port}/`), b);

const base = `http://127.0.0.1:${port}`;
const res = await fetch(a);
const body = await res.text();
ok('serves page', res.status === 200, String(res.status));
ok('frames the target', body.includes('src="https://www.youtube.com/embed/a"'), body);
ok('names no other target', !body.includes('embed/b'), body);
ok('html type', (res.headers.get('content-type') || '').startsWith('text/html'));
ok('csp', (res.headers.get('content-security-policy') || '').includes("default-src 'none'"));

const status = async (url, init) => (await fetch(url, init)).status;
ok('root 404', await status(base + '/') === 404);
ok('unknown id 404', await status(base + '/e/' + '0'.repeat(32)) === 404);
ok('no query targets', await status(base + '/e/?u=https://www.youtube.com/embed/a') === 404);
ok('POST 404', await status(a, { method: 'POST', body: 'x' }) === 404);

let threw = false;
try { host.url('https://evil.example/'); } catch { threw = true; }
ok('disallowed host rejected', threw);

// A rebinding page reaches us with its own name in Host. Raw socket: fetch sets Host itself.
const rawStatus = async (hostHeader) => {
  const sock = new TCPSocket('127.0.0.1', port);
  const { readable, writable } = await sock.opened;
  const w = writable.getWriter();
  await w.write(new TextEncoder().encode(`GET ${new URL(a).pathname} HTTP/1.1\r\nHost: ${hostHeader}\r\nConnection: close\r\n\r\n`));
  const { value } = await readable.getReader().read();
  sock.close();
  return new TextDecoder().decode(value).split('\r\n')[0];
};
const good = await rawStatus(`127.0.0.1:${port}`);
ok('raw request with our Host 200', good.includes(' 200'), good);
const rebound = await rawStatus(`evil.example:${port}`);
ok('foreign Host 404', rebound.includes(' 404'), rebound);

await host.close();
console.log(fail ? `${fail} of ${n} FAILED` : `PASS ${n} checks`);
tjs.exit(fail ? 1 : 0);
