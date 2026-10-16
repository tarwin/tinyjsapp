import { base64, signManifest, verifyManifest } from '../../runtime/update-signing.js';
import { checkForUpdate, installUpdate } from '../../runtime/update.js';
const pair = await crypto.subtle.generateKey('Ed25519', true, ['sign', 'verify']);
const other = await crypto.subtle.generateKey('Ed25519', true, ['sign', 'verify']);
const key = base64(await crypto.subtle.exportKey('raw', pair.publicKey));
const wrong = base64(await crypto.subtle.exportKey('raw', other.publicKey));
const copy = x => JSON.parse(JSON.stringify(x));
let passed = 0;
function assert(ok, what) { if (!ok) throw new Error(what); passed++; }
async function rejects(fn, what, match) {
  try { await fn(); } catch (e) {
    assert(!match || String(e).includes(match), what + ': unexpected error ' + e);
    return;
  }
  throw new Error('accepted ' + what);
}
const payload = { version: '2.0.0', notes: 'release — привет', url: 'https://host/mac.zip', sha256: 'a'.repeat(64),
  mac: { arm64: { url: 'https://host/a.zip', sha256: 'b'.repeat(64) }, x86_64: { url: 'https://host/x.zip', sha256: 'c'.repeat(64) } },
  win: { version: '2.0.1', notes: 'win notes', url: 'https://host/w.zip', sha256: 'd'.repeat(64) },
  linux: { notes: 'linux notes', arm64: { url: 'https://host/l-a.tar.gz', sha256: 'e'.repeat(64) }, x86_64: { url: 'https://host/l-x.tar.gz', sha256: 'f'.repeat(64) } } };
const signed = await signManifest(payload, pair.privateKey, key);
assert(JSON.stringify(await verifyManifest(signed, key)) === JSON.stringify(payload), 'round trip');
await rejects(() => verifyManifest(signed, wrong), 'wrong pin');
const badSig = Uint8Array.from(atob(signed.signature), c => c.charCodeAt(0));
badSig[0] ^= 1;
await rejects(() => verifyManifest({ ...signed, signature: base64(badSig) }, key), 'tampered signature');
await rejects(() => signManifest(payload, other.privateKey, key), 'publisher mismatched key');
for (const field of ['version', 'notes', 'url', 'sha256', 'mac.arm64.url', 'mac.x86_64.sha256',
  'win.version', 'win.notes', 'win.url', 'win.sha256', 'linux.notes', 'linux.arm64.sha256', 'linux.x86_64.url']) {
  const m = copy(payload), path = field.split('.'), last = path.pop();
  let at = m; for (const p of path) at = at[p]; at[last] += '-tampered';
  await rejects(() => verifyManifest({ ...signed, payload: JSON.stringify(m) }, key), field);
}
for (const value of [null, '', wrong + '\n', '!!!!', base64(new Uint8Array(31))])
  await rejects(() => verifyManifest(signed, value), 'malformed pin');
for (const env of [payload, { ...signed, algorithm: 'none' }, { ...signed, format: 'v2' },
  { ...signed, signature: 'AA==' }, { ...signed, signature: signed.signature.slice(0, -1) + '!' },
  { ...signed, url: 'https://host/evil' }, { ...signed, payload: '{' }])
  await rejects(() => verifyManifest(env, key), 'invalid envelope');
const envelope = copy(signed);
const verifying = verifyManifest(envelope, key);
envelope.payload = '{}'; envelope.signature = '';
assert((await verifying).version === '2.0.0', 'snapshot across crypto await');
let response = signed, requests = [];
globalThis.fetch = async url => {
  requests.push(url);
  return { ok: true, url, json: async () => copy(response) };
};
const opts = { url: 'https://host/manifest.json', version: '1.0.0', publicKey: key };
const checked = await checkForUpdate(opts);
assert(checked.available && checked.latest.startsWith('2.'), 'signed check');
checked.manifest.url = 'https://host/evil';
response = payload;
await rejects(() => installUpdate({ ...opts, manifest: checked.manifest }), 'check/install unsigned substitution', 'signed Ed25519');
assert(requests.every(url => url === opts.url), 'no download before authentication');
response = { ...signed, payload: JSON.stringify({ ...payload, notes: 'evil' }) };
await rejects(() => installUpdate({ ...opts, manifest: payload }), 'direct install tamper', 'signature verification failed');
assert(requests.every(url => url === opts.url), 'no effects from supplied manifest');
const old = await signManifest({ ...payload, version: '0.1.0', win: undefined, mac: undefined, linux: undefined }, pair.privateKey, key);
response = old;
await rejects(() => installUpdate(opts), 'old signed version', 'no newer signed update');
// Pin cannot be substituted by mutating caller options during fetch.
response = await signManifest(payload, other.privateKey, wrong);
const changing = { ...opts };
const pending = checkForUpdate(changing); changing.publicKey = wrong;
await rejects(() => pending, 'pin substitution across fetch', 'signature verification failed');
response = payload;
assert((await checkForUpdate({ url: opts.url, version: opts.version })).available, 'legacy unsigned check');
console.log('PASS ' + passed + ' signed-update adversarial assertions (actual txiki)');
