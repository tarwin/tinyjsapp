// tjs-compat stub: just enough surface for amp's module graph to LOAD in the
// island (function bodies that call deeper tjs surface run against the stub).
globalThis.tjs = {
  env: { HOME: "/tmp" },
  homeDir: "/tmp",
  tmpDir: "/tmp",
  cwd: "/tmp",
  args: [],
  platform: "darwin",
  version: "stub",
  exePath: "/tmp/stub",
  spawn: () => ({ wait: async () => ({ exit_status: 0 }), stdout: null }),
  readFile: async () => new Uint8Array(),
  writeFile: async () => {},
  stat: async () => { throw new Error("stub"); },
  remove: async () => {},
  makeDir: async () => {},
  rename: async () => {},
  makeTempDir: async () => "/tmp/stub",
  exit: () => {},
  listen: async () => { throw new Error("stub"); },
  connect: async () => { throw new Error("stub"); },
};
const main = await import("./main.js");
export function report() {
  const keys = Object.keys(main);
  const apiKeys = main.api ? Object.keys(main.api) : [];
  return JSON.stringify({ exports: keys, apiMethods: apiKeys.length });
}
