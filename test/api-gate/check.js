// The "api" capability gate, every form tinyjs.json can give it.
// Headless, any OS:   bin/tjs run test/api-gate/check.js
//
// Each case: [spec, method, calling origin (undefined = no stamp), allowed].
// The rows marked FIXED failed open before (2026-10-21): "api": false and an
// enable list with nothing disabled both meant "allow everything".
const { compileApiGate } = await import(tjs.env.BRIDGE_JS || '../../runtime/bridge.js');

const A = 'https://a.com', B = 'https://b.com';
const cases = [
  // tiny.embedURL: off for a wrapper and for any list that doesn't name it
  ['wrapper', 'embed.url', A, false],
  [{ disable: ['*'], enable: ['win.*'] }, 'embed.url', A, false],
  [{ disable: ['*'], enable: ['embed.url'] }, 'embed.url', A, true],
  // no gate
  [undefined, 'store.get', A, true],
  [null, 'store.get', A, true],
  ['all', 'store.get', A, true],
  [true, 'store.get', A, true],
  // nothing
  ['none', 'store.get', A, false],
  ['none', 'client.hello', A, true],               // bootstrap is always allowed
  [false, 'store.get', A, false],                  // FIXED: was allow-all
  [false, 'store.get', undefined, false],          // FIXED
  ['', 'store.get', A, false],                     // FIXED: unknown preset ""
  [0, 'store.get', A, false],                      // FIXED: not a gate value
  // presets
  ['wrapper', 'notify', A, true],
  ['wrapper', 'shell.open', A, true],
  ['wrapper', 'fetch', A, false],
  ['wrapper', 'saveDraft', A, false],              // the app's own api: off
  [{ preset: 'wrapper', enable: ['saveDraft'] }, 'saveDraft', A, true],
  ['wrapperz', 'notify', A, false],                // unknown preset fails closed
  [{ preset: 'nope' }, 'notify', A, false],
  // lists
  [['notify'], 'notify', A, true],
  [['notify'], 'store.get', A, false],
  [{ enable: ['notify'] }, 'notify', A, true],
  [{ enable: ['notify'] }, 'store.get', A, false], // FIXED: was allowed
  [{ enable: ['store.*'] }, 'store.get', A, true],
  [{ enable: ['store.*'] }, 'store', A, false],     // 'x.*' doesn't cover bare 'x'
  [{ disable: ['store.*'] }, 'store.get', A, false],
  [{ disable: ['store.*'] }, 'notify', A, true],
  [{ disable: ['*'], enable: ['notify'] }, 'notify', A, true],
  [{ enable: ['notify'], disable: ['notify'] }, 'notify', A, true],   // enable wins
  // origins
  [{ origins: { [A]: ['notify'] } }, 'notify', A, true],
  [{ origins: { [A]: ['notify'] } }, 'store.get', A, false],
  [{ origins: { [A]: ['notify'] } }, 'notify', B, false],           // stranger, no top-level lists
  [{ origins: { [A]: ['notify'] } }, 'client.hello', B, true],
  [{ origins: { [A]: ['notify'] } }, 'store.get', undefined, true], // no stamp: top-level (none)
  [{ origins: { 'https://*.a.com': 'all' } }, 'store.get', 'https://x.y.a.com', true],
  [{ origins: { [A]: false } }, 'notify', A, false],
  [{ origins: { [A]: { enable: ['notify'] } } }, 'store.get', A, false], // FIXED
  // what `tinyjs wrap` writes: wrapper for the site, window controls elsewhere
  [{ disable: ['*'], enable: ['win.close'], origins: { [A]: 'wrapper' } }, 'notify', A, true],
  [{ disable: ['*'], enable: ['win.close'], origins: { [A]: 'wrapper' } }, 'notify', B, false],
  [{ disable: ['*'], enable: ['win.close'], origins: { [A]: 'wrapper' } }, 'win.close', B, true],
  [{ enable: ['win.close'], origins: { [A]: 'wrapper' } }, 'notify', B, false], // FIXED
  // first matching key wins
  [{ origins: { [A]: 'none', 'https://*': 'all' } }, 'notify', A, false],
];

let fail = 0;
for (const [spec, m, origin, want] of cases) {
  const gate = compileApiGate(spec);
  const got = gate ? gate(m, origin) : true;
  if (got !== want) {
    fail++;
    console.log('FAIL', JSON.stringify(spec), m, origin ?? '(no stamp)', 'want', want, 'got', got);
  }
}
console.log(fail ? `${fail} of ${cases.length} FAILED` : `PASS ${cases.length} cases`);
tjs.exit(fail ? 1 : 0);
