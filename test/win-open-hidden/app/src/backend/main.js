// win.open({ show: false }) check (#102). See ../../README.md.
// Prints `win-open-hidden: PASS|FAIL|INFO …` lines to the dev console.
const out = (kind, msg) => console.log(`win-open-hidden: ${kind} ${msg}`);
const check = (ok, msg) => out(ok ? 'PASS' : 'FAIL', msg);
const st = async (app, id) => {
  const s = await app.window(id).getState().catch(() => null);
  return s && { visible: s.visible, x: s.x, y: s.y, width: s.width, height: s.height };
};
let ranHidden = null, hiddenAt = null, pageVis = null;

export const api = {
  // called by kid.html before it shows itself
  async ranWhileHidden(_, app) { ranHidden = await st(app, 'kid'); return true; },
  // called by kid.html ~0.7s after its own show(): what the page itself sees
  afterShow(vis) { pageVis = vis; return true; },
};

export function init(app) {
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  (async () => {
    await wait(800);
    app.win.open('kid', { page: 'kid.html', title: 'kid (show:false)', size: '400x300', parent: true, show: false });
    await wait(1500);
    hiddenAt = await st(app, 'kid');
    out('INFO', 'kid while hidden: ' + JSON.stringify(hiddenAt));
    check(hiddenAt && hiddenAt.visible === false, 'kid stays hidden after open');
    check(ranHidden && ranHidden.visible === false, "kid's page ran JS + an RPC while hidden");
    // kid.html shows itself ~2.5s after load, then reports in (a fixed wait
    // here raced the page on a slow Windows start)
    for (let i = 0; i < 40 && pageVis === null; i++) await wait(250);
    const shown = await st(app, 'kid');
    out('INFO', 'kid after show(): ' + JSON.stringify(shown));
    check(shown && shown.visible === true, 'tiny.win.show() from the page shows it');
    check(pageVis === 'visible', 'the page sees itself shown (visibilityState: ' + pageVis + ')');
    const prev = await app.store.get('phase');
    if (prev === 'saved') {
      // run 2: the remembered rect was 300,200 (normal), then maximized
      check(hiddenAt && Math.abs(hiddenAt.x - 300) <= 2 && Math.abs(hiddenAt.y - 200) <= 2,
        'run 2: opened hidden AT the remembered position (300,200) — no jump later');
      check(shown && shown.width > 400, 'run 2: remembered maximize applied on show (wider than 400)');
      out('INFO', 'run 2 done. Delete window-state.json + store.json (or run with a fresh id) to start over.');
    } else {
      check(shown && Math.abs(shown.x - hiddenAt.x) <= 2 && Math.abs(shown.y - hiddenAt.y) <= 2,
        'run 1: shown where it was placed while hidden');
      app.window('kid').setPosition(300, 200);
      await wait(700);
      app.window('kid').zoom();
      await wait(1200);                     // rememberWindows saves 500ms after a change
      await app.store.set('phase', 'saved');
      out('INFO', 'run 1 done: kid saved at 300,200 then maximized. Quit and run again for run 2.');
    }
    out('INFO', 'by eye: kid should appear ONCE, never at another spot first. parent: ' +
      'drag main — kid moves with it on macOS only; stays above main everywhere.');
  })().catch((e) => out('FAIL', 'threw: ' + (e?.message ?? e)));
}
