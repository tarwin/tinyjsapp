// Probe: a plain-JS "app backend" shape — dynamic dispatch, untyped bags,
// timers, fetch, fs — the way real tinyjs app main.js files are written.
import { writeFileSync, readFileSync } from "node:fs";

const state = { plays: 0, feeds: {} };
const api = {
  ping: (args) => ({ pong: args }),
  bump: () => ({ plays: ++state.plays }),
  stash: (kv) => { Object.assign(state.feeds, kv); return { n: Object.keys(state.feeds).length }; },
};

function dispatch(name, args) {
  const fn = api[name];
  return fn ? fn(args) : { error: "no such method " + name };
}

console.log(JSON.stringify(dispatch("ping", { hello: 1 })));
console.log(JSON.stringify(dispatch("bump")));
console.log(JSON.stringify(dispatch("stash", { a: "x", b: "y" })));
console.log(JSON.stringify(dispatch("nope")));

writeFileSync("/tmp/scriptc-dyn-store.json", JSON.stringify(state));
const back = JSON.parse(readFileSync("/tmp/scriptc-dyn-store.json", "utf8"));
console.log("store round-trip plays =", back.plays);

const r = await fetch("https://rss.art19.com/the-allusionist");
console.log("island fetch status", r.status);
setTimeout(() => console.log("island timer fired"), 100);
