// tiny.capabilities: the bridge's per-name verdicts, and the page side that
// maps tiny.* paths onto them. Headless, any OS:
//   bin/tjs run test/capabilities/check.js
const { capabilityVerdicts, mediaTrusted, mediaTrustLines, compileApiGate, eventGateName } =
  await import(tjs.env.BRIDGE_JS || '../../runtime/bridge.js');
let fail = 0;
const check = (name, ok, extra = '') => { if (!ok) fail++; if (!ok || tjs.env.VERBOSE) console.log(ok ? 'ok  ' : 'FAIL', name, extra); };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ── bridge: capabilityVerdicts ──────────────────────────────────────────────
const S = 'https://s.com';
const gate = compileApiGate({ origins: { [S]: 'wrapper' } });
const verdicts = (uses, platform = {}, linux = false) => capabilityVerdicts(uses, {
  callAllowed: (n) => gate(n, S),
  eventAllowed: (e) => gate(eventGateName(e), S),
  platform, linux,
});

let v = verdicts({ calls: ['notify', 'clip.read', 'app.badge', 'saveDraft'], events: ['menu', 'drop'] },
  { badge: false });
check('allowed + supported', v.calls.notify === true);
check('gate denies, names the key', same(v.calls['clip.read'], { why: 'gate', needs: 'clip.read' }));
check('platform says no', same(v.calls['app.badge'], { why: 'platform', feature: 'badge' }));
check('app method: just "gate"', same(v.calls.saveDraft, { why: 'gate', needs: 'saveDraft' }));
check('event allowed', v.events.menu === true);
check('event gate names event.<x>', same(v.events.drop, { why: 'gate', needs: 'event.drop' }));
check('only what was asked', same(Object.keys(v.calls).sort(), ['app.badge', 'clip.read', 'notify', 'saveDraft']));

// The gate first: a denied name says nothing about the machine.
v = verdicts({ calls: ['app.captureScreen'] }, { captureScreen: false });
check('denied hides platform', same(v.calls['app.captureScreen'], { why: 'gate', needs: 'app.captureScreen' }));

// Linux: the X11-only keys read 'session', the rest 'platform'.
const open = { callAllowed: () => true, eventAllowed: () => true };
v = capabilityVerdicts({ calls: ['win.setPosition', 'system.wifi'] },
  { ...open, platform: { windowPosition: false, wifi: false }, linux: true });
check('wayland → session', v.calls['win.setPosition'].why === 'session');
check('linux non-session → platform', v.calls['system.wifi'].why === 'platform');
v = capabilityVerdicts({ calls: ['win.setLevel', 'win.setAlwaysOnTop', 'win.setAllSpaces', 'win.setClickThrough'] },
  { ...open, platform: { windowLevel: false, setAllSpaces: false }, linux: true });
check('wayland: level + always-on-top', v.calls['win.setLevel'].why === 'session'
  && same(v.calls['win.setAlwaysOnTop'], { why: 'session', feature: 'windowLevel' }));
check('wayland: all spaces', v.calls['win.setAllSpaces'].why === 'session');
check('click-through has no key', v.calls['win.setClickThrough'] === true);
v = capabilityVerdicts({ calls: ['win.setAllSpaces'] }, { ...open, platform: { setAllSpaces: false } });
check('windows: all spaces → platform', v.calls['win.setAllSpaces'].why === 'platform');
v = capabilityVerdicts({ calls: ['win.setPosition'] }, { ...open, platform: { windowPosition: false } });
check('off Linux → platform', v.calls['win.setPosition'].why === 'platform');
v = capabilityVerdicts({ calls: ['win.setPosition'], events: ['hotkey'] },
  { ...open, platform: { globalHotkeys: true, sampler: 'page' } });
check('absent key = supported', v.calls['win.setPosition'] === true);
check('event feature true', v.events.hotkey === true);
v = capabilityVerdicts({ events: ['media-key'] }, { ...open, platform: { mediaKeys: false } });
check('event feature false', same(v.events['media-key'], { why: 'platform', feature: 'mediaKeys' }));

// Junk in, nothing thrown, nothing echoed.
v = capabilityVerdicts({ calls: [1, null, 'x'.repeat(500), 'notify'], events: 'menu' }, { ...open, platform: {} });
check('junk names dropped', same(Object.keys(v.calls), ['notify']) && same(v.events, {}));
v = capabilityVerdicts(undefined, { ...open, platform: {} });
check('no uses', same(v, { calls: {}, events: {} }));

// The call itself is always allowed, like client.hello.
check('client.capabilities under "none"', compileApiGate('none')('client.capabilities', S));
check('client.capabilities for a stranger',
  compileApiGate({ origins: { [S]: 'all' } })('client.capabilities', 'https://evil.net'));

// media.proxy: the same lines the launcher gets.
const lines = mediaTrustLines({ origins: { 'https://*.a.com': { enable: ['media.proxy'] }, [S]: 'wrapper' } }, ['file://']);
check('proxy: listed key', mediaTrusted(lines, 'proxy', 'https://x.a.com'));
check('proxy: wrapper key lacks it', !mediaTrusted(lines, 'proxy', S));
check('proxy: stranger', !mediaTrusted(lines, 'proxy', 'https://evil.net'));
check('proxy: no stamp', !mediaTrusted(lines, 'proxy', undefined));
check('proxy: kind matters', !mediaTrusted(lines, 'camera', 'https://x.a.com'));
check('file: own page', mediaTrusted(lines, 'file', 'file://'));
check('file: no api key grants it', !mediaTrusted(lines, 'file', 'https://x.a.com') && !mediaTrusted(lines, 'file', S));

// ── page: tiny.js maps paths onto wire names ────────────────────────────────
const here = new URL('.', import.meta.url).pathname;
const src = new TextDecoder().decode(await tjs.readFile(tjs.env.TINY_JS || here + '../../runtime/tiny.js'));
let asked = null;
const answer = { calls: {}, events: {} };
const fakeWindow = {
  __invoke: (j) => {
    const m = JSON.parse(j);
    if (m.method !== 'client.capabilities') return Promise.resolve(null);
    asked = m.params;
    for (const c of asked.calls) answer.calls[c] = true;
    for (const e of asked.events) answer.events[e] = true;
    answer.calls['clip.read'] = { why: 'gate', needs: 'clip.read' };
    answer.calls['app.badge'] = { why: 'platform', feature: 'badge' };
    answer.events.drop = { why: 'gate', needs: 'event.drop' };
    return Promise.resolve(answer);
  },
  addEventListener() {},
};
globalThis.navigator ??= { userAgent: 'Macintosh' };
globalThis.document ??= { addEventListener() {}, documentElement: { hasAttribute: () => false } };
new Function('window', src)(fakeWindow);
const caps = fakeWindow.tiny.capabilities;

check('pending before ready', !caps.can('notify') && caps.get('notify').why === 'pending' && caps.list().length === 0);
let changed = null;
caps.onChange((d) => { changed = d; });
await caps.ready;
check('asked with wire names', asked.calls.includes('clip.write') && asked.calls.includes('notify'));
check('asked with event names', asked.events.includes('drop') && asked.events.includes('menu'));
check('asked about fetch + proxy (hand-named)', asked.calls.includes('fetch') && asked.calls.includes('media.proxy'));
check('never asks about itself', !asked.calls.includes('client.capabilities'));
check('first snapshot fires onChange', changed && changed.added.includes('notify') && changed.removed.length === 0);

check('can: allowed', caps.can('notify') && caps.can('clipboard.write'));
check('can: tiny. prefix', caps.can('tiny.clipboard.write'));
check('can: gated', !caps.can('clipboard.read'));
check('get: gate + needs', same(caps.get('clipboard.read'), { ok: false, why: 'gate', needs: ['clip.read'] }));
check('get: platform', same(caps.get('app.badge'), { ok: false, why: 'platform', feature: 'badge' }));
check('get: event gate', same(caps.get('win.onDrop'), { ok: false, why: 'gate', needs: ['event.drop'] }));
check('get: ok', same(caps.get('notify'), { ok: true }));
check('get: unknown', same(caps.get('nope'), { ok: false, why: 'unknown' }));
check('local fns always work', caps.can('system.os') && caps.can('fileURL'));
check('wildcard: any', caps.can('clipboard.*') && caps.get('clipboard.*').ok);
check('wildcard: unknown ns', !caps.can('nope.*') && caps.get('nope.*').why === 'unknown');
check('list excludes the broken', !caps.list().includes('clipboard.read') && caps.list().includes('notify'));
check('list excludes itself', !caps.list().some((p) => p.startsWith('capabilities.')));
check('via another tiny fn', caps.can('system.architecture'));  // calls system.info

changed = null;
// a fresh answer object, as the wire would give
const next = { calls: { ...answer.calls, notify: { why: 'gate', needs: 'notify' } }, events: answer.events };
const realInvoke = fakeWindow.__invoke;
fakeWindow.__invoke = (j) => (JSON.parse(j).method === 'client.capabilities' ? Promise.resolve(structuredClone(next)) : realInvoke(j));
await caps.refresh();
check('refresh: diff', changed && same(changed.removed, ['notify']) && changed.added.length === 0);
changed = null;
await caps.refresh();
check('refresh: no change, no event', changed === null);

// A call that never returns (Windows window-mode popups) must not hang
// ready; the answer that lands late still fires onChange.
let release;
const hung = {
  __invoke: (j) => (JSON.parse(j).method === 'client.capabilities'
    ? new Promise((r) => { release = () => r(structuredClone(next)); })
    : Promise.resolve(null)),
  addEventListener() {},
};
new Function('window', src)(hung);
const t0 = Date.now();
// A ready that never settles would just end the run (exit 0, no output), so
// race it against a deadline that fails loudly instead.
const settled = await Promise.race([hung.tiny.capabilities.ready.then(() => true),
  new Promise((r) => setTimeout(() => r(false), 6000))]);
const waited = Date.now() - t0;
check('ready settles', settled, `${waited}ms`);
check('ready caps out', waited >= 2500 && waited < 4500, `${waited}ms`);
check('nothing claimed after the cap', !hung.tiny.capabilities.can('system.os') && hung.tiny.capabilities.get('notify').why === 'pending');
let late = null;
hung.tiny.capabilities.onChange((d) => { late = d; });
release();
await new Promise((r) => setTimeout(r, 0));
check('late answer fires onChange', late && late.added.includes('clipboard.write') && hung.tiny.capabilities.can('clipboard.write'));

if (fail) { console.log(`${fail} failed`); tjs.exit(1); }
console.log('capabilities: all ok');
