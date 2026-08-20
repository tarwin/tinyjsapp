// Probe: the tinyjs newline-delimited wire protocol over TCP loopback
// (scriptc's net has no Unix-socket support — this tests the protocol shape
// on the transport scriptc DOES have).
import { createServer, createConnection } from "node:net";

const server = createServer((conn) => {
  let buf = "";
  conn.on("data", (chunk) => {
    buf += chunk.toString("utf8");
    let i = buf.indexOf("\n");
    while (i >= 0) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      console.log(`server got: ${line}`);
      conn.write(`ACK ${line}\n`);
      if (line === "QUIT") {
        conn.end();
        server.close();
      }
      i = buf.indexOf("\n");
    }
  });
});

server.listen({ port: 8898, host: "127.0.0.1" }, () => {
  const client = createConnection({ port: 8898, host: "127.0.0.1" }, () => {
    client.write("READY 1x1\n");
    client.write("EVAL@2 console.log(1)\n");
    client.write("QUIT\n");
  });
  let cbuf = "";
  client.on("data", (chunk) => {
    cbuf += chunk.toString("utf8");
  });
  client.on("close", () => {
    console.log(`client saw:\n${cbuf}`);
  });
});
