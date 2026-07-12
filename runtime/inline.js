// Frontend asset inliner: turns src/frontend/index.html plus its local
// scripts, stylesheets, and images into one self-contained HTML string
// (required because the launcher loads the page via webview_set_html).
//
// Handles the common shapes:
//   <script src="app.js"></script>          -> <script>…</script>
//   <link rel="stylesheet" href="x.css">    -> <style>…</style>
//   <img src="logo.png">                    -> <img src="data:…;base64,…">
//   url(img.png) inside inlined CSS         -> url(data:…;base64,…)
// Remote (http/https/data/protocol-relative) references are left untouched.

const dec = new TextDecoder();

const MIME = {
  png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif',
  svg: 'image/svg+xml', webp: 'image/webp', ico: 'image/x-icon',
  woff: 'font/woff', woff2: 'font/woff2', ttf: 'font/ttf', otf: 'font/otf',
};

function isRemote(url) {
  return /^(https?:)?\/\//.test(url) || url.startsWith('data:');
}

function mimeFor(path) {
  const ext = path.split('.').pop().toLowerCase();
  return MIME[ext] || 'application/octet-stream';
}

function toBase64(bytes) {
  let bin = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(bin);
}

async function readAsset(baseDir, rel, missing) {
  try {
    return await tjs.readFile(baseDir + '/' + rel);
  } catch {
    missing.push(rel);
    return null;
  }
}

async function toDataUri(baseDir, rel, missing) {
  const data = await readAsset(baseDir, rel, missing);
  return data ? `data:${mimeFor(rel)};base64,${toBase64(data)}` : null;
}

async function inlineCssUrls(css, baseDir, missing) {
  const refs = [...css.matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)];
  for (const m of refs) {
    if (isRemote(m[1])) continue;
    const uri = await toDataUri(baseDir, m[1], missing);
    if (uri) css = css.replace(m[0], `url(${uri})`);
  }
  return css;
}

// Returns { html, missing } — missing lists local references that didn't
// resolve (the build warns about them rather than failing).
export async function inlineHtml(indexPath) {
  const baseDir = indexPath.replace(/\/[^/]*$/, '');
  let html = dec.decode(await tjs.readFile(indexPath));
  const missing = [];

  // <script src="…"></script>
  for (const m of [...html.matchAll(/<script\b[^>]*\bsrc=["']([^"']+)["'][^>]*>\s*<\/script>/g)]) {
    if (isRemote(m[1])) continue;
    const data = await readAsset(baseDir, m[1], missing);
    if (data == null) continue;
    // A literal "</script>" inside inlined JS would truncate the tag.
    const js = dec.decode(data).replace(/<\/script/gi, '<\\/script');
    html = html.replace(m[0], `<script>\n${js}\n</script>`);
  }

  // <link rel="stylesheet" href="…">
  for (const m of [...html.matchAll(/<link\b[^>]*>/g)]) {
    const tag = m[0];
    if (!/rel=["']stylesheet["']/.test(tag)) continue;
    const href = tag.match(/href=["']([^"']+)["']/);
    if (!href || isRemote(href[1])) continue;
    const data = await readAsset(baseDir, href[1], missing);
    if (data == null) continue;
    const css = await inlineCssUrls(dec.decode(data), baseDir, missing);
    html = html.replace(tag, `<style>\n${css}\n</style>`);
  }

  // <img src="…"> (and any other src= attribute pointing at a local asset file)
  for (const m of [...html.matchAll(/<img\b[^>]*\bsrc=["']([^"']+)["']/g)]) {
    if (isRemote(m[1])) continue;
    const uri = await toDataUri(baseDir, m[1], missing);
    if (uri) html = html.replace(m[0], m[0].replace(m[1], uri));
  }

  return { html, missing };
}
