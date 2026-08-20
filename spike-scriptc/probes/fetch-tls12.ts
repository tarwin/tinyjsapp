// Probe: txiki Bug B analog — TLS 1.2-only host (rss.art19.com, old Fastly
// profile). txiki's mbedtls build fails the handshake; a correct client works.
const t0 = Date.now();
try {
  const r = await fetch("https://rss.art19.com/the-allusionist", { redirect: "follow" });
  console.log(`TLS1.2 host: status=${r.status} in ${Date.now() - t0}ms`);
  const body = await r.text();
  console.log(`body length: ${body.length}`);
} catch (e) {
  if (e instanceof Error) console.log(`TLS1.2 host FAILED: ${e.message}`);
}
