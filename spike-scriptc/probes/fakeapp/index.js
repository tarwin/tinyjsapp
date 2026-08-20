// The whole plain-JS "app backend": dynamic dispatch, untyped bags — island code.
const state = { plays: 0, feeds: {} };
const api = {
  ping: (args) => ({ pong: args }),
  bump: () => ({ plays: ++state.plays }),
  stash: (kv) => { Object.assign(state.feeds, kv); return { n: Object.keys(state.feeds).length }; },
};
export function dispatch(name, args) {
  const fn = api[name];
  return JSON.stringify(fn ? fn(args) : { error: "no such method " + name });
}
export async function probeFetch(url) {
  const r = await fetch(url);
  return r.status;
}
