// SemVer update precedence, through the same manifest check on every OS.
// Headless: tjs run test/update-versions/check.js
const { checkForUpdate } = await import('../../runtime/update.js');

let manifest;
const server = tjs.serve({
  port: 0,
  fetch: () => new Response(JSON.stringify(manifest), {
    headers: { 'content-type': 'application/json' },
  }),
});
const url = `http://127.0.0.1:${server.port}/manifest.json`;
const asset = { url: 'https://example.invalid/app.zip', sha256: 'a'.repeat(64) };

const cases = [
  ['1.2.0-beta.39', '1.2.0-beta.40', true],
  ['1.2.0-beta.9', '1.2.0-beta.10', true],
  ['1.2.0-beta.2', '1.2.0-beta.10', true],
  ['1.2.0-alpha', '1.2.0-alpha.1', true],
  ['1.2.0-1', '1.2.0-alpha', true],
  ['1.2.0-beta.11', '1.2.0-rc.1', true],
  ['1.2.0-ALPHA', '1.2.0-alpha', true],
  ['1.2.0-beta.a', '1.2.0-beta.b', true],
  ['1.2.0-beta', '1.2.0', true],
  ['1.2.0', '1.2.0-beta', false],
  ['1.2.0-beta.10', '1.2.0-beta.2', false],
  ['1.2.0-beta.40', '1.2.0-beta.40', false],
  ['1.2.0+build.1', '1.2.0+build.2', false],
  ['1.2.0-beta+build.1', '1.2.0-beta+build.2', false],
  ['v1.2.0-beta', '1.2.0', true],
  ['1.2.0-beta.40', '1.2.1-beta.1', true],
  ['1.2.9', '1.3.0', true],
  ['1.9.9', '2.0.0', true],
  ['9007199254740992.0.0', '9007199254740993.0.0', true],
  ['1.0.0-beta.9007199254740992', '1.0.0-beta.9007199254740993', true],
  ['1.2', '1.3.0', false],
  ['1.2.0', '1.2.0-beta..1', false],
  ['1.2.0', '1.2.0-01', false],
  ['1.2.0-01', '1.3.0', false],
  ['1.2.0', '1.2.0+bad..meta', false],
  ['1.02.0', '1.3.0', false],
  ['1.2.0', '01.2.1', false],
  ['1.2.0', '1.2.0-extra!', false],
  ['1.2.0', '1.2.1 ', false],
];

let failures = 0;
for (const [current, latest, expected] of cases) {
  // Every platform has an entry, so this tests precedence, not availability
  // of an asset for this particular OS or CPU.
  manifest = { version: latest, ...asset,
    mac: { arm64: asset, x86_64: asset },
    win: { ...asset, arm64: asset, x86_64: asset },
    linux: { arm64: asset, x86_64: asset } };
  const result = await checkForUpdate({ url, version: current });
  if (result.available !== expected || result.latest !== latest) {
    console.log('FAIL', current, '→', latest, result.available, 'expected', expected);
    failures++;
  }
}

// An OS/CPU-specific version takes precedence over the manifest's top-level
// version, including when the builds are released on different schedules.
manifest = { version: '9.0.0', ...asset,
  mac: { arm64: { ...asset, version: '1.2.0-beta.40' },
         x86_64: { ...asset, version: '1.2.0-beta.40' } },
  win: { ...asset, version: '1.2.0-beta.40',
         arm64: asset, x86_64: asset },
  linux: { arm64: { ...asset, version: '1.2.0-beta.40' },
           x86_64: { ...asset, version: '1.2.0-beta.40' } } };
const overridden = await checkForUpdate({ url, version: '1.2.0-beta.39' });
if (!overridden.available || overridden.latest !== '1.2.0-beta.40') {
  console.log('FAIL platform version override', overridden);
  failures++;
}

server.close?.();
console.log(failures ? `${failures} FAILED` : `PASS ${cases.length + 1} cases`);
tjs.exit(failures ? 1 : 0);
