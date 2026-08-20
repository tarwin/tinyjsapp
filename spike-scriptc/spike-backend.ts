// Spike: drive the REAL tinyjs launcher-macos from a scriptc-compiled native
// backend — no txiki, no QuickJS, no interpreter.
//
// The launcher speaks newline-delimited commands over a Unix socket; scriptc's
// net stack is TCP-only, so an `nc` pair relays the Unix socket to our TCP
// listener. That relay is a stand-in for the real fix (AF_UNIX in scriptc, or
// a TCP+token transport in the launcher) — everything else is the real wire.
//
// argv: spike-backend <launcherPath> <pagePath> [--stay]
import { createServer } from "node:net";
import { spawn } from "node:child_process";
import { appendFileSync, writeFileSync } from "node:fs";

if (process.argv.length < 4) {
  console.log("usage: spike-backend <launcherPath> <pagePath> [--stay]");
  process.exit(1);
}
const launcher = process.argv[2];
const page = process.argv[3];
const stay = process.argv.length > 4 && process.argv[4] === "--stay";

const PORT = 8897;
const SOCK = "/tmp/scriptc-spike.sock";
const FIFO = "/tmp/scriptc-spike.fifo";
const LOG = "/tmp/scriptc-spike-transcript.txt";
writeFileSync(LOG, "");

function record(dir: string, line: string): void {
  const entry = `${dir} ${line}`;
  console.log(entry);
  appendFileSync(LOG, entry + "\n");
}

// The wire escape the launcher's wire_unescape reverses (bridge.js esc()).
function esc(s: string): string {
  let out = "";
  for (const ch of s) {
    if (ch === "\\") out += "\\\\";
    else if (ch === "\t") out += "\\t";
    else if (ch === "\r") out += "\\r";
    else if (ch === "\n") out += "\\n";
    else out += ch;
  }
  return out;
}

const server = createServer((conn) => {
  record("--", "relay attached to TCP side");
  let buf = "";
  let sawLauncher = false;
  let greeted = false;

  function send(line: string): void {
    record("->", line);
    conn.write(line + "\n");
  }

  function greet(): void {
    if (greeted) return;
    greeted = true;
    send("TITLE driven by scriptc ✨");
    const js =
      'document.getElementById("status").textContent = ' +
      '"backend says hello — compiled TypeScript, " + (2 ** 10) + " bytes of runtime between us";';
    send("EVAL " + esc(js));
  }

  conn.on("data", (chunk) => {
    buf += chunk.toString("utf8");
    let i = buf.indexOf("\n");
    while (i >= 0) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      record("<-", line);
      if (!sawLauncher) {
        sawLauncher = true;
        greet();
      }
      i = buf.indexOf("\n");
    }
  });

  // The launcher may connect silently (nothing unprompted); greet on a timer
  // as well so the demo works either way.
  setTimeout(() => { greet(); }, 2500);

  if (!stay) {
    setTimeout(() => {
      send("QUIT");
      setTimeout(() => { process.exit(0); }, 1500);
    }, 9000);
  }
});

server.listen({ port: PORT, host: "127.0.0.1" }, () => {
  record("--", `backend listening on 127.0.0.1:${PORT}`);

  // Unix-socket-to-TCP relay: launcher connects to SOCK; bytes flow
  // launcher -> nc -lU -> pipe -> nc TCP -> us, and back through the fifo.
  const relayCmd =
    `rm -f ${SOCK} ${FIFO}; mkfifo ${FIFO}; ` +
    `nc -lU ${SOCK} < ${FIFO} | nc 127.0.0.1 ${PORT} > ${FIFO}`;
  const relay = spawn("sh", ["-c", relayCmd], { stdio: "ignore" });
  record("--", `relay pid ${relay.pid ?? -1}`);

  setTimeout(() => {
    const child = spawn(launcher, [page, SOCK, "scriptc spike", "760x520", "0.0.0"], {
      stdio: ["ignore", "inherit", "inherit"],
    });
    record("--", `launcher pid ${child.pid ?? -1}`);
    child.on("exit", (code, _signal) => {
      record("--", `launcher exited code=${code}`);
      relay.kill("SIGTERM");
      process.exit(0);
    });
  }, 500);
});
