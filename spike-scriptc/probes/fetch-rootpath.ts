// Probe: txiki Bug A analog — does fetch of a root-path URL emit "GET //"?
// We ARE the raw listener: a net server that captures the request bytes.
import { createServer } from "node:net";

const server = createServer((conn) => {
  let raw = "";
  conn.on("data", (chunk) => {
    raw += chunk.toString("utf8");
    if (raw.indexOf("\r\n\r\n") >= 0) {
      const reqLine = raw.split("\r\n")[0];
      console.log(`REQUEST LINE: ${reqLine}`);
      conn.write("HTTP/1.1 200 OK\r\ncontent-length: 2\r\nconnection: close\r\n\r\nok");
      conn.end();
      server.close();
    }
  });
});

async function go(): Promise<void> {
  try {
    const r = await fetch("http://127.0.0.1:8899"); // pathless — txiki sends "GET //"
    const t = await r.text();
    console.log(`fetch completed: ${t}`);
  } catch (e) {
    if (e instanceof Error) console.log(`fetch failed: ${e.message}`);
  }
}

server.listen({ port: 8899, host: "127.0.0.1" }, () => {
  void go();
});
