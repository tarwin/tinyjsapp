# Signed app updates (opt in)

`update.publicKey` pins an Ed25519 publisher key in the compiled backend.
The runtime authenticates the entire manifest before selecting a platform or
reading release notes, then retains the existing transport, archive SHA-256,
macOS code-signature/Team ID checks and rollback behavior. The signature is
independent of Apple/Windows executable signing. This protects against a
compromised update host when the signing key remains private.

Apps without `update.publicKey` keep the existing unsigned manifest behavior.
A configured but invalid key (including `null` or an empty string), missing
signature, unsupported envelope or incorrect signature fails closed. An engine
without Ed25519 support also fails closed; there is no unsigned fallback.

## Create and keep the key outside the app project

Use WebCrypto in the same bundled `bin/tjs` runtime used by the publisher.
This example writes a PKCS8 **DER** private key and a raw-public-key base64 file
in an existing private directory. Run it outside your app project; restrict
access to that directory first (for example `umask 077` on macOS/Linux).

```js
// save as keygen.js in the private directory; run: /path/to/bin/tjs run keygen.js
const pair = await crypto.subtle.generateKey('Ed25519', true, ['sign', 'verify']);
await tjs.writeFile('update-private.der',
  new Uint8Array(await crypto.subtle.exportKey('pkcs8', pair.privateKey)));
const raw = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
const publicKey = btoa(Array.from(raw, b => String.fromCharCode(b)).join(''));
await tjs.writeFile('update-public.txt', new TextEncoder().encode(publicKey));
```

Put only the public file's contents into `tinyjs.json`:

```json
{
  "update": {
    "url": "https://example.com/updates/signed-manifest.json",
    "publicKey": "BASE64_OF_32_RAW_PUBLIC_KEY_BYTES"
  }
}
```

Never put a private key in `tinyjs.json`, source/resource folders, version
control or an uploaded release. The publisher reads it only from an explicitly
provided file path; no secret is added to generated app configuration. Keep
that file outside the project: tinyjs copies backend sources when building.
Use a secret file provisioned by your CI secret store when publishing in CI.

```sh
TINYJS_UPDATE_PRIVATE_KEY_FILE=/private/path/update-private.der tinyjs publish --notes "Release notes"
```

In PowerShell, set `$env:TINYJS_UPDATE_PRIVATE_KEY_FILE` to the external file
path, then run `tinyjs publish`. The variable names a file, not the secret
itself. A configured public key requires a matching private key before the
build touches `dist/`. Supplying the variable without `update.publicKey` is an
error. Ordinary `tinyjs build` and `tinyjs dev` need no private key.

Upload the archive and generated `dist/publish/manifest.json`. The configured
URL may use a different filename, such as `signed-manifest.json`; upload the
manifest under that name. All download URLs must remain HTTPS (localhost HTTP
is allowed for testing).

## Exact signed format

The hosted JSON contains exactly these four fields:

```json
{
  "format": "tinyjs-update-v1",
  "algorithm": "Ed25519",
  "payload": "{\"version\":\"2.0.0\",\"url\":\"https://example.com/App.zip\",\"sha256\":\"...\",\"notes\":\"...\"}",
  "signature": "BASE64_OF_64_SIGNATURE_BYTES"
}
```

The signed bytes are UTF-8 encoding of `"tinyjs-update-v1\n" + payload`.
`payload` is the complete plain JSON manifest as a string: version, notes,
URLs, hashes and every platform/architecture block are authenticated together.
There is no JSON canonicalization step during verification: changing even
whitespace inside the payload invalidates the signature. Outer envelope
whitespace and field order are irrelevant. Base64 must be standard canonical
base64 with padding; a raw key is 32 bytes and a signature is 64 bytes.
The server cannot provide or replace the trusted public key.

`check()` verifies before platform overlays. `install()` fetches and verifies
again using the baked key, even if a caller supplies a previous check result
or a fabricated manifest. It refuses an older/current version or an update
missing this platform. A changed, correctly signed newer release between
check and install can therefore be installed. The existing unsigned API
behavior remains unchanged for apps with no pinned key.

## Several platforms or architectures

Consecutive macOS `publish --arch arm64` / `publish --arch x86_64` operations
verify the previous signed envelope, merge the arch entries for the same
version, and sign the whole result again. An invalid prior envelope is not
trusted or merged; publish that architecture again if it must be included.

For independent platform builds, retain the archives and extract each
manifest's `payload` string as JSON in a trusted release workspace. Merge the
plain manifests as before: macOS owns top-level `url`/`sha256` and `mac`,
Windows owns `win`, and Linux owns per-architecture `linux` entries. Review the
final version, notes and every URL/hash, then sign **after the final merge**:

```sh
TINYJS_UPDATE_PRIVATE_KEY_FILE=/private/path/update-private.der tinyjs publish --sign-manifest /release/merged-manifest.json
```

Run from the app project containing the pinned public key. This command does
not build; it replaces that plain manifest with an envelope only after a
successful signature/matching-key check. Keep a copy of the plain merged
manifest if you need to edit it later. Never copy a signature from one
platform into a merged manifest or edit the signed payload after signing.

## Migrating existing apps

Legacy apps cannot read the envelope. Use a **separate signed-manifest URL**.
Publish a final ordinary manifest at the old URL whose archive contains the
new public-key-enabled app pointing to the signed URL. Once installed,
subsequent updates require signatures. The first migration update still uses
the legacy trust model; enabling this feature cannot retroactively secure it.
Keep the old endpoint available for clients that have not migrated.

To rotate the key, release an app baking the replacement key and signed using
the old key, while coordinating a new endpoint for subsequent releases.
This initial implementation has one pinned key, no revocation service, and no
protection from replay of a previously signed version newer than the current
installed version. Keep the private key backed up securely: losing it can
require distributing a replacement app through another trusted channel.

## Verification

```sh
./bin/tjs run test/signed-updates/crypto-probe.js
./bin/tjs run test/signed-updates/runtime-check.js
python3 -u test/signed-updates/check.py
```

The first two exercise RFC 8032, actual WebCrypto imports and adversarial
verification in bundled txiki. The Python runner also checks publisher key
failure before build, signing a merged manifest, and CLI `new`/`version`.
On macOS with the launcher built it publishes a real archive, launches a
packaged old app, rejects tampering and a check/install substitution, retains
checksum validation, installs and relaunches the new version, and repeats
the installation with a legacy unsigned app. The integration fixture uses
only localhost and temporary directories/keys. Packaged Windows and Linux
installations require separate verification on those systems; the script
explicitly skips that portion there.
