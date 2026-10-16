// RFC 8032 section 7.1, TEST 1 (public test vector, never a release key).
import { base64 } from '../../runtime/update-signing.js';
const hex = s => Uint8Array.from(s.match(/../g), x => parseInt(x, 16));
const pk = hex('d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a');
const sk = hex('302e020100300506032b657004220420' +
  '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60');
const sig = hex('e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f' +
  'b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b');
const pub = await crypto.subtle.importKey('raw', pk, 'Ed25519', false, ['verify']);
const priv = await crypto.subtle.importKey('pkcs8', sk, 'Ed25519', false, ['sign']);
const actual = new Uint8Array(await crypto.subtle.sign('Ed25519', priv, new Uint8Array()));
if (base64(actual) !== base64(sig) || !(await crypto.subtle.verify('Ed25519', pub, sig, new Uint8Array())))
  throw new Error('RFC 8032 vector failed');
const bad = Uint8Array.from(sig);
bad[0] ^= 1;
if (await crypto.subtle.verify('Ed25519', pub, bad, new Uint8Array())) throw new Error('tamper accepted');
console.log('PASS RFC 8032 Ed25519 + PKCS8/raw import + tamper rejection (txiki ' + tjs.version + ')');
// The integration runner requests fresh disposable keys, outside the project.
const dir = tjs.args[3];
if (dir) {
  const pair = await crypto.subtle.generateKey('Ed25519', true, ['sign', 'verify']);
  await tjs.writeFile(dir + '/private.der', new Uint8Array(await crypto.subtle.exportKey('pkcs8', pair.privateKey)));
  await tjs.writeFile(dir + '/public.txt', new TextEncoder().encode(base64(await crypto.subtle.exportKey('raw', pair.publicKey))));
}
