// `tinyjs update --check` against a local server that misbehaves on purpose
// (#67). Runs the real cli.js from a throwaway "install" (a copy with a
// VERSION file) with TINYJS_UPDATE_URL pointed at the server. Pure tjs, so it
// runs the same on macOS, Linux and Windows:
//
//   bin/tjs run test/update-fetch/check.js          (Windows: bin\tjs.exe)
//   bin/tjs run test/update-fetch/check.js --quick  (skips the ~32 s tarpit case)
//
// The server picks its behaviour from the request path:
//   /nativedrop/*       drop anything carrying `pragma:` (native fetch sends it,
//                       curl doesn't), so the native fetch fails and curl
//                       succeeds, every time; else as /ok/*
//   /ok/releases/latest 302 → /ok/releases/tag/v9.9.9
//   /ok/releases/tag/*  200
//   /drop/*             close without answering
//   /tarpit/*           accept, never answer
//   /evil/*             302 → file:///etc/passwd

const enc = new TextEncoder(), dec = new TextDecoder();
const quick = tjs.args.includes('--quick');
const root = new URL('../..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');

// ── server ────────────────────────────────────────────────────────────────
const log = [];
const listener = await tjs.listen('tcp', '127.0.0.1', 0);
const info = await listener.opened;
const base = `http://127.0.0.1:${info.localPort}`;
const held = [];

async function handle(conn) {
  const { readable, writable } = await conn.opened;
  const reader = readable.getReader(), writer = writable.getWriter();
  let head = '';
  while (!head.includes('\r\n\r\n')) {
    const { value, done } = await reader.read().catch(() => ({ done: true }));
    if (done) return;
    head += dec.decode(value);
  }
  const line = head.split('\r\n')[0];
  const ua = /\r\nuser-agent: *([^\r]*)/i.exec(head)?.[1] || '';
  const path = line.split(' ')[1] || '';
  log.push(`${line} [${ua}]`);
  const reply = (status, extra = '') => writer.write(enc.encode(
    `HTTP/1.1 ${status}\r\n${extra}content-length: 0\r\nconnection: close\r\n\r\n`));
  if (path.startsWith('/drop/')) { /* close below */ }
  else if (path.startsWith('/nativedrop/') && /\r\npragma:/i.test(head)) { /* close below */ }
  else if (path.startsWith('/tarpit/')) { held.push(conn); return; }
  else if (path === '/ok/releases/latest' || path === '/nativedrop/releases/latest') {
    await reply('302 Found', 'location: /ok/releases/tag/v9.9.9\r\n');
  } else if (path.startsWith('/ok/releases/tag/')) await reply('200 OK');
  else if (path.startsWith('/evil/')) await reply('302 Found', 'location: file:///etc/passwd\r\n');
  else await reply('404 Not Found');
  await writer.close().catch(() => {});
}

(async () => {
  const accept = info.readable.getReader();
  for (;;) {
    const { value, done } = await accept.read();
    if (done) break;
    handle(value).catch(() => {});
  }
})();

// ── throwaway install ─────────────────────────────────────────────────────
const inst = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-update-test-XXXXXX');
await tjs.writeFile(inst + '/cli.js', await tjs.readFile(root + 'cli.js'));
// cli.js imports this at load (signed updates); without it every case dies
// before the update check even starts.
await tjs.makeDir(inst + '/runtime');
await tjs.writeFile(inst + '/runtime/update-signing.js', await tjs.readFile(root + 'runtime/update-signing.js'));
await tjs.writeFile(inst + '/VERSION', enc.encode('v0.0.1\n'));
const emptyBin = await tjs.makeTempDir(tjs.tmpDir + '/tinyjs-nocurl-XXXXXX');

async function readAll(s) {
  const r = s.getReader();
  let out = '';
  for (;;) {
    const { value, done } = await r.read();
    if (done) return out;
    out += dec.decode(value, { stream: true });
  }
}

async function cli(url, env = {}) {
  const t0 = Date.now();
  const p = tjs.spawn([tjs.exePath, 'run', inst + '/cli.js', 'update', '--check'], {
    env: { ...tjs.env, TINYJS_UPDATE_URL: url, ...env },
    stdin: 'ignore', stdout: 'pipe', stderr: 'pipe',
  });
  const [out, err] = await Promise.all([readAll(p.stdout), readAll(p.stderr)]);
  const st = await p.wait();
  return { code: st.exit_status, text: (out + err).trim(), secs: (Date.now() - t0) / 1000 };
}

// ── cases ─────────────────────────────────────────────────────────────────
let failed = 0;
async function check(name, url, expect, env) {
  const from = log.length;
  const r = await cli(url, env);
  const problems = expect(r);
  if (problems) failed++;
  console.log(`${problems ? 'FAIL' : 'ok  '} ${name}  (${r.secs.toFixed(1)}s, exit ${r.code})`);
  console.log('     ' + r.text.replace(/\n/g, '\n     '));
  for (const l of log.slice(from)) console.log('     > ' + l);
  if (problems) console.log('     !! ' + problems);
}

const available = (r) => r.code === 0 && /v9\.9\.9 is available/.test(r.text) ? null : 'expected "v9.9.9 is available"';
const failsWith = (...parts) => (r) =>
  r.code !== 0 && parts.every((p) => r.text.includes(p)) ? null
    : `expected a failure naming ${parts.map((p) => JSON.stringify(p)).join(', ')}`;

await check('native fetch works (one request: the tag rides the redirect)', base + '/ok/releases/latest', (r) =>
  available(r));

await check('native fetch fails, curl recovers', base + '/nativedrop/releases/latest', (r) => available(r));

await check('everything drops: error names each attempt', base + '/drop/releases/latest',
  failsWith('could not reach GitHub', 'fetch:', 'curl:'));

await check('no curl on PATH: still fails cleanly', base + '/drop/releases/latest',
  failsWith('could not reach GitHub', 'fetch:', 'curl:'), { PATH: emptyBin, Path: emptyBin });

await check('redirect to file:// is refused', base + '/evil/releases/latest',
  failsWith('could not reach GitHub'));

if (!quick) {
  await check('tarpit: gives up in bounded time', base + '/tarpit/releases/latest', (r) =>
    failsWith('could not reach GitHub', 'timed out')(r) || (r.secs > 40 ? `took ${r.secs}s` : null));
}

for (const c of held) c.close?.();
await tjs.remove(inst, { recursive: true }).catch(() => {});
await tjs.remove(emptyBin, { recursive: true }).catch(() => {});
console.log(failed ? `\n${failed} case(s) FAILED` : '\nall cases passed');
tjs.exit(failed ? 1 : 0);
