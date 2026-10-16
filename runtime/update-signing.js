// Exact-byte Ed25519 update envelopes, shared by publisher and runtime.
// Only the baked key is authoritative; no key is taken from the envelope.
const enc = new TextEncoder();
const PREFIX = 'tinyjs-update-v1\n';

export function base64(bytes) {
  return btoa(Array.from(new Uint8Array(bytes), b => String.fromCharCode(b)).join(''));
}

export function unbase64(value, length, what) {
  if (typeof value !== 'string' || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    throw new Error('invalid ' + what + ' base64');
  const bytes = Uint8Array.from(atob(value), c => c.charCodeAt(0));
  if ((length !== null && bytes.length !== length) || base64(bytes) !== value)
    throw new Error('invalid ' + what + ' encoding or length');
  return bytes;
}

function manifestObject(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || typeof value.version !== 'string')
    throw new Error('invalid signed update manifest');
  return value;
}

export async function verifyManifest(envelope, publicKey) {
  // Snapshot attacker-controlled values before the first asynchronous call.
  const payload = envelope?.payload;
  const signature = envelope?.signature;
  if (envelope?.format !== 'tinyjs-update-v1' || envelope?.algorithm !== 'Ed25519' ||
      typeof payload !== 'string' || Object.keys(envelope).sort().join(',') !== 'algorithm,format,payload,signature')
    throw new Error('signed Ed25519 update manifest required');
  const sig = unbase64(signature, 64, 'update signature');
  const raw = unbase64(publicKey, 32, 'update publicKey');
  const bytes = enc.encode(PREFIX + payload);
  const key = await crypto.subtle.importKey('raw', raw, 'Ed25519', false, ['verify']);
  if (!(await crypto.subtle.verify('Ed25519', key, sig, bytes)))
    throw new Error('update manifest signature verification failed');
  // Parse only after authentication, before any platform overlays.
  return manifestObject(JSON.parse(payload));
}

export async function signManifest(manifest, privateKey, publicKey) {
  const payload = JSON.stringify(manifestObject(manifest));
  const signature = base64(await crypto.subtle.sign('Ed25519', privateKey, enc.encode(PREFIX + payload)));
  const envelope = { format: 'tinyjs-update-v1', algorithm: 'Ed25519', payload, signature };
  // A missing/mismatched configured key must fail before publishing anything.
  await verifyManifest(envelope, publicKey);
  return envelope;
}
