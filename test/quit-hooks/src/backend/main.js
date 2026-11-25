// Survives the app quitting, so onQuit's lines can be read afterwards.
const LOG = tjs.env.QUIT_LOG || tjs.tmpDir + '/quittest.log';
let veto = true, app_;
const log = async (s) => {
  const line = new Date().toISOString().slice(11, 23) + '  ' + s;
  const f = await tjs.open(LOG, 'a'); await f.write(new TextEncoder().encode(line + '\n')); await f.close();
  try { app_?.push('qlog', line); } catch {}
};
export const api = {
  qlog: async ({ s }) => (await log(s), true),
  setVeto: async ({ on }) => { veto = !!on; await log('backend veto = ' + veto); return true; },
};
export async function onBeforeQuit(info, app) {
  await log('BACKEND onBeforeQuit reason=' + info.reason + ' -> ' + (veto ? 'CANCEL' : 'allow'));
  return !veto;
}
export async function onQuit(app) {
  await log('onQuit started (window gone)');
  await new Promise((r) => setTimeout(r, 1000));
  await log('onQuit finished after 1s async work');
}
export function init(app) { app_ = app; log('--- app started'); }
