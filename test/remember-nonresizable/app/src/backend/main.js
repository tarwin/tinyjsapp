// #113: setResizable(false) + rememberWindows must not save "maximized".
// See ../../README.md. Prints `remember-nonresizable: PASS|FAIL|INFO …` lines.
const out = (kind, msg) => console.log(`remember-nonresizable: ${kind} ${msg}`);
const check = (ok, msg) => out(ok ? 'PASS' : 'FAIL', msg);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let fixed = false;
const kidMaxEvents = [];

export const api = {
  madeFixed() { fixed = true; return true; },
};

export function onWindowState(info) {
  if (info.win !== 'kid') return;
  out('INFO', 'kid state: ' + JSON.stringify(info));
  if (info.maximized) {
    kidMaxEvents.push(info);
    out('FAIL', 'kid reported maximized:true — it was never maximized (the #113 bug)');
  }
}

// kid's saved entry from the most recently used screen layout, read before
// this run writes anything
async function savedKid(app) {
  try {
    const dir = (await app.paths()).data;
    const st = JSON.parse(new TextDecoder().decode(await tjs.readFile(dir + '/window-state.json')));
    const layouts = Object.values(st.layouts ?? {}).sort((a, b) => (b.used ?? 0) - (a.used ?? 0));
    return layouts.find((l) => l.windows?.kid)?.windows.kid ?? null;
  } catch { return null; }
}

export function init(app) {
  (async () => {
    const saved = await savedKid(app);
    const first = !saved;
    out('INFO', 'saved kid: ' + JSON.stringify(saved && { rect: saved.rect, maximized: saved.maximized }));
    check(!saved || saved.maximized === false, 'saved state does not say kid is maximized');
    await wait(600);
    app.win.open('kid', { page: 'kid.html', title: 'kid (setResizable false)', size: '400x300' });
    for (let i = 0; i < 40 && !fixed; i++) await wait(100);
    check(fixed, 'kid page called setResizable(false)');
    await wait(500);
    const s = await app.window('kid').getState().catch(() => null);
    out('INFO', 'kid opened at: ' + JSON.stringify(s && { x: s.x, y: s.y, width: s.width, height: s.height }));
    if (!first) {
      check(s && s.width <= 410 && s.height <= 310, 'kid came back at its own size (400x300), not maximized');
      const r = saved.rect;
      check(s && r && Math.abs(s.x - r.x) <= 2 && Math.abs(s.y - r.y) <= 2,
        `kid came back where it was left (${r?.x},${r?.y})`);
      out('INFO', 'kid is left where it came back — drag it again, quit, and rerun as often as you like.');
    } else {
      // First run only: a move made AFTER setResizable(false) is what saved
      // maximized:true. Later runs leave kid alone so a hand drag is what's tested.
      app.window('kid').setPosition(320, 220);
      await wait(1200);                     // rememberWindows saves 500ms after a change
      check(kidMaxEvents.length === 0, 'moved kid to 320,220: no maximized:true reported');
      out('INFO', 'now DRAG kid by hand a few times (watch for FAIL lines), then quit and run again.');
    }
  })().catch((e) => out('FAIL', 'threw: ' + (e?.message ?? e)));
}
