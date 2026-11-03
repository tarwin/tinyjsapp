// The txiki.js fixes tinyjs relies on, and the redirect rules bridge.js adds on
// top. Pure tjs, so it runs the same on macOS, Linux and Windows:
//
//   bin/tjs run test/fetch/check.js            (Windows: bin\tjs.exe)
//   bin/tjs run test/fetch/check.js --offline  (skips the real hosts)
//
// The listeners are raw TCP on purpose: tjs.serve normalizes `//` to `/`, which
// hid the root-path bug (TODO-txiki.md). Two ports means two origins.
//
// "upstream:" lines are report-only. They say whether native fetch still has a
// bug bridge.js works around; once one reads "fixed", that rule can go.

await import('../../runtime/bridge.js');

const enc = new TextEncoder(), dec = new TextDecoder();
const offline = tjs.args.includes('--offline');
const IS_WIN = tjs.env.OS === 'Windows_NT';
const seen = [];

function serve(info, other) {
  const handle = async (conn) => {
    const { readable, writable } = await conn.opened;
    const rd = readable.getReader(), wr = writable.getWriter();
    let buf = new Uint8Array(0), at = -1;
    const more = async () => {
      const { value, done } = await rd.read().catch(() => ({ done: true }));
      if (done) return false;
      const n = new Uint8Array(buf.length + value.length);
      n.set(buf); n.set(value, buf.length); buf = n;
      return true;
    };
    while ((at = dec.decode(buf).indexOf('\r\n\r\n')) < 0) if (!(await more())) return;
    const head = dec.decode(buf.subarray(0, at));
    const len = +(/\r\ncontent-length: *(\d+)/i.exec(head)?.[1] ?? 0);
    while (buf.length < at + 4 + len) if (!(await more())) break;
    const body = dec.decode(buf.subarray(at + 4, at + 4 + len));
    const line = head.split('\r\n')[0], path = line.split(' ')[1];
    const rec = { port: info.localPort, line, head: head.toLowerCase(), body };
    seen.push(rec);
    const reply = (status, extra = '', text = '') => wr.write(enc.encode(
      `HTTP/1.1 ${status}\r\n${extra}content-length: ${enc.encode(text).length}\r\nconnection: close\r\n\r\n${text}`));
    const to = (status, where) => reply(status, `location: ${where}\r\n`);
    if (path === '/cross') await to('302 Found', `http://127.0.0.1:${other()}/landed`);
    else if (path === '/cross307') await to('307 Temporary Redirect', `http://127.0.0.1:${other()}/landed`);
    else if (path === '/same') await to('302 Found', '/landed');
    else if (path === '/toroot') await to('302 Found', '/');
    else if (path === '/p303') await to('303 See Other', '/landed');
    else if (path === '/p301') await to('301 Moved Permanently', '/landed');
    else if (path === '/p308') await to('308 Permanent Redirect', '/landed');
    else if (path === '/tofile') await to('302 Found', 'file:///etc/passwd');
    else if (path === '/loop') await to('302 Found', '/loop');
    else if (path === '/stream') {
      await wr.write(enc.encode('HTTP/1.1 200 OK\r\ncontent-type: audio/mpeg\r\nconnection: close\r\n\r\n'));
      try { for (;;) { await wr.write(new Uint8Array(4096)); await new Promise((r) => setTimeout(r, 20)); } } catch {}
    } else if (path === '/hold') return;
    else await reply('200 OK', '', `${line}|${body.length}`);
    await wr.close().catch(() => {});
  };
  (async () => {
    const accept = info.readable.getReader();
    for (;;) {
      const { value, done } = await accept.read();
      if (done) break;
      handle(value).catch(() => {});
    }
  })();
}

const a = await (await tjs.listen('tcp', '127.0.0.1', 0)).opened;
const b = await (await tjs.listen('tcp', '127.0.0.1', 0)).opened;
serve(a, () => b.localPort);
serve(b, () => a.localPort);
const A = `http://127.0.0.1:${a.localPort}`;

let failed = 0;
const within = (p, ms) => Promise.race([p, new Promise((_, r) => setTimeout(() => r(new Error(`timed out after ${ms / 1000}s`)), ms))]);
// fn returns null when fine, or what was wrong
async function check(name, fn, ms = 8000) {
  let problem;
  try { problem = await within(fn(), ms); } catch (e) { problem = 'threw ' + e?.message; }
  if (problem) failed++;
  console.log(`${problem ? 'FAIL' : 'ok  '} ${name}${problem ? '\n     !! ' + problem : ''}`);
}
const last = () => seen[seen.length - 1];
const rejects = (f, re) => f().then(() => 'resolved', (e) => (re.test(String(e?.message)) ? null : 'rejected with: ' + e?.message));
const expect = (got, want) => (got === want ? null : `got ${JSON.stringify(got)}, wanted ${JSON.stringify(want)}`);
const CRED = { authorization: 'Bearer s3cret', cookie: 'a=b', 'proxy-authorization': 'x', 'x-keep': '1' };
const leaked = (r) => ['authorization:', 'cookie:', 'proxy-authorization:'].filter((h) => r.head.includes(h));

console.log(`tjs ${tjs.version}, fetch is ${fetch.name}\n`);

console.log('-- the wire');
await check('root path goes out as /', async () => { await fetch(A + '/'); return expect(last().line, 'GET / HTTP/1.1'); });
await check('pathless url goes out as /', async () => { await fetch(A); return expect(last().line, 'GET / HTTP/1.1'); });
await check('POST to root path, small body', async () => {
  const r = await fetch(A + '/', { method: 'POST', body: '{"a":1}' });
  return expect(await r.text(), 'POST / HTTP/1.1|7');
});
await check('redirect onto a root path', async () => { await fetch(A + '/toroot'); return expect(last().line, 'GET / HTTP/1.1'); });
await check('array headers sent once, as themselves', async () => {
  await fetch(A + '/h', { headers: [['x-a', '1']] });
  return last().head.includes('length: ') && !last().head.includes('content-length') ? 'sent the array\'s own properties'
    : last().head.includes('x-a: 1') ? null : 'x-a missing';
});
await check('CR/LF in a header value rejected', () => rejects(() => fetch(A + '/h', { headers: { 'x-a': 'v\r\nInjected: yes' } }), /header/i));
await check('NUL in a header value rejected', () => rejects(() => fetch(A + '/h', { headers: { 'x-a': 'v\0z' } }), /header/i));
await check('bad header name rejected', () => rejects(() => fetch(A + '/h', { headers: { 'a:b': '1' } }), /header/i));

console.log('\n-- redirect rules (bridge.js)');
await check('cross-origin 302 drops credentials', async () => {
  const r = await fetch(A + '/cross', { headers: CRED });
  return (r.redirected !== true && 'redirected not set') || (leaked(last()).length ? 'leaked ' + leaked(last()) : null)
    || (last().head.includes('x-keep: 1') ? null : 'dropped x-keep too');
});
await check('cross-origin 307 keeps the body, drops credentials', async () => {
  await fetch(A + '/cross307', { method: 'POST', body: 'hello', headers: CRED });
  return expect(last().body, 'hello') || (leaked(last()).length ? 'leaked ' + leaked(last()) : null);
});
await check('same-origin redirect keeps credentials', async () => {
  await fetch(A + '/same', { headers: CRED });
  return leaked(last()).length === 3 ? null : 'lost some: ' + leaked(last());
});
await check('303 POST becomes GET without body headers', async () => {
  await fetch(A + '/p303', { method: 'POST', body: 'hello', headers: { 'content-type': 'x/y' } });
  return expect(last().line, 'GET /landed HTTP/1.1') || (last().head.includes('content-type') ? 'kept content-type' : null);
});
await check('301 POST becomes GET', async () => { await fetch(A + '/p301', { method: 'POST', body: 'hello' }); return expect(last().line, 'GET /landed HTTP/1.1'); });
await check('308 keeps POST and body', async () => { await fetch(A + '/p308', { method: 'POST', body: 'hello' }); return expect(last().body, 'hello'); });
await check('redirect to file:// rejects, process survives', () => rejects(() => fetch(A + '/tofile'), /non-http/));
await check('redirect loop rejects', () => rejects(() => fetch(A + '/loop'), /too many/));
await check('redirect: "error" rejects', () => rejects(() => fetch(A + '/same', { redirect: 'error' }), /redirected/));
await check('redirect: "manual" returns the 3xx', async () => {
  const r = await fetch(A + '/same', { redirect: 'manual' });
  return expect(r.status, 302) || expect(r.headers.get('location'), '/landed');
});

console.log('\n-- bodies and streams');
await check('Uint8Array body', async () => expect(await (await fetch(A + '/', { method: 'POST', body: new Uint8Array(65536) })).text(), 'POST / HTTP/1.1|65536'));
await check('FormData body', async () => {
  const f = new FormData(); f.append('a', '1');
  await fetch(A + '/fd', { method: 'POST', body: f });
  return last().head.includes('multipart/form-data') ? null : 'not multipart';
});
await check('Request object: body arrives', async () => {
  await fetch(new Request(A + '/req', { method: 'POST', body: 'abc', headers: { 'x-keep': '9' } }));
  return expect(last().body, 'abc') || (last().head.includes('x-keep: 9') ? null : 'lost its header');
});
await check('Request object: redirect rules apply', async () => {
  await fetch(new Request(A + '/cross', { headers: CRED }));
  return leaked(last()).length ? 'leaked ' + leaked(last()) : null;
});
await check('endless body streams, then aborts', async () => {
  const ac = new AbortController();
  const r = await fetch(A + '/stream', { signal: ac.signal });
  const rd = r.body.getReader();
  for (let i = 0; i < 5; i++) await rd.read();
  ac.abort();
  return rd.read().then(({ done }) => (done ? null : rd.read().then(() => null)), (e) => expect(e.name, 'AbortError'));
});
await check('abort before the response head', () => {
  const ac = new AbortController();
  const p = fetch(A + '/hold', { signal: ac.signal });
  setTimeout(() => ac.abort(), 200);
  return p.then(() => 'resolved', (e) => expect(e.name, 'AbortError'));
});

console.log('\n-- processes (txiki#1028, #1164)');
const readAll = async (s) => {
  const r = s.getReader();
  let out = '';
  for (;;) {
    const { value, done } = await r.read();
    if (done) return out;
    out += dec.decode(value, { stream: true });
  }
};
const dir = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-fetch-check-XXXXXX');
await tjs.writeFile(dir + '/cat.js', enc.encode('await tjs.stdin.pipeTo(tjs.stdout);\n'));
await check('several small writes to a child\'s stdin', async () => {
  const p = tjs.spawn([tjs.exePath, 'run', dir + '/cat.js'], { stdin: 'pipe', stdout: 'pipe', stderr: 'ignore' });
  const out = readAll(p.stdout), w = p.stdin.getWriter();
  for (const s of ['abc\n', 'def\n', 'ghi\n']) await w.write(enc.encode(s));
  await w.close();
  const got = await out;
  await p.wait();
  return expect(got, 'abc\ndef\nghi\n');
});
await check('non-ASCII script path and argument', async () => {
  // No single ANSI code page holds all of these, so without #1164 Windows
  // mangles them on any locale.
  const name = 'ünïcödé-中文-русский-😀';
  const sep = IS_WIN ? '\\' : '/';
  const sub = dir + sep + name;
  await tjs.makeDir(sub);
  await tjs.writeFile(sub + sep + 'a.js', enc.encode('console.log(JSON.stringify(tjs.args[tjs.args.length - 1]));\n'));
  const p = tjs.spawn([tjs.exePath, 'run', sub + sep + 'a.js', name], { stdin: 'ignore', stdout: 'pipe', stderr: 'pipe' });
  const [out, err] = await Promise.all([readAll(p.stdout), readAll(p.stderr)]);
  await p.wait();
  return expect(out.trim(), JSON.stringify(name)) && `${out.trim()} ${err.trim()}`;
});

if (!offline) {
  console.log('\n-- real hosts: [status, bytes, final url]');
  // root path on S3/CloudFront, TLS 1.2 ceiling, redirect chains into both,
  // and #1165's Windows connect and h2 cases (literal IP, anchor.fm's 302 over
  // h2, repeated fetches to a front end that PINGs).
  const hosts = [
    ['https://feed.articlesofinterest.club/', 200],
    ['https://feed.proxypodcast.com/', 200],
    ['https://rss.art19.com/the-allusionist', 200],
    ['http://rss.art19.com/the-allusionist', 200],
    ['https://rss.pdrl.fm/4c9e6e/feed.hyperfixedpod.com', 200],
    ['https://anchor.fm/', 200],
    ['https://podcasters.spotify.com/', 200],
    ['http://151.101.2.133/', null],
    ['https://www.cloudflare.com/', 200],
  ];
  for (const [u, want] of hosts) {
    await check(u, async () => {
      const r = await fetch(u);
      const n = (await r.arrayBuffer()).byteLength;
      console.log(`     [${r.status}, ${n}, ${r.url}]`);
      return want === null || r.status === want ? null : `status ${r.status}`;
    }, 30000);
  }
  await check('anchor.fm redirect: "manual" over h2 stays a 302', async () => {
    const r = await fetch('https://anchor.fm/', { redirect: 'manual' });
    await r.arrayBuffer();
    return expect(r.status, 302);
  }, 30000);
  await check('6 fetches in a row to a pooled h2 host', async () => {
    for (let i = 0; i < 6; i++) {
      const t0 = Date.now();
      const r = await fetch('https://www.google.com/');
      await r.arrayBuffer();
      if (Date.now() - t0 > 4000) return `fetch ${i} took ${Date.now() - t0} ms`;
    }
    return null;
  }, 40000);
}

console.log('\n-- upstream: what native fetch still gets wrong (report only)');
const native = async (expr) => {
  await tjs.writeFile(dir + '/n.js', enc.encode(`const A='${A}';\n${expr}\ntjs.exit(0);\n`));
  const p = tjs.spawn([tjs.exePath, 'run', dir + '/n.js'], { stdin: 'ignore', stdout: 'pipe', stderr: 'pipe' });
  const [out] = await Promise.all([readAll(p.stdout), readAll(p.stderr)]);
  return { code: (await p.wait()).exit_status, out: out.trim() };
};
const crash = await native(`await fetch(A + '/tofile').catch(() => {});`);
console.log(`upstream: redirect to file:// ${crash.code ? 'still kills the process' : 'fixed'}`);
await native(`await fetch(A + '/cross', { headers: { authorization: 'x' } });`);
console.log(`upstream: Authorization across origins ${last().head.includes('authorization:') ? 'still forwarded' : 'fixed'}`);
await native(`await fetch(new Request(A + '/up', { method: 'POST', body: 'abc' }));`);
console.log(`upstream: Request body ${last().body === 'abc' ? 'fixed' : 'still mangled (the text "[object Blob]", no content-length)'}`);

await tjs.remove(dir, { recursive: true }).catch(() => {});
console.log(failed ? `\n${failed} case(s) FAILED` : '\nall cases passed');
tjs.exit(failed ? 1 : 0);
