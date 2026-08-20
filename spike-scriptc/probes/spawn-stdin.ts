// Probe: child stdin via the temp-file-fd idiom (piped stdin is a compile
// fence in scriptc's static tier). This is nib's txiki-Bug-C workaround,
// expressed natively: stdin slot = an open fd.
import { spawn } from "node:child_process";
import { writeFileSync, openSync, closeSync } from "node:fs";

const tmp = "/tmp/scriptc-probe-stdin.txt";
writeFileSync(tmp, "abc def\n");
const fd = openSync(tmp, "r");

const p = spawn("tr", ["a-z", "A-Z"], { stdio: [fd, "pipe", "inherit"] });
let out = "";
const so = p.stdout;
if (so !== null) {
  so.on("data", (chunk) => { out += chunk.toString("utf8"); });
}
p.on("exit", (code, _signal) => {
  closeSync(fd);
  console.log(`child exited code=${code} output=${JSON.stringify(out)}`);
});
