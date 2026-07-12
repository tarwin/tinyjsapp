const $ = (id) => document.getElementById(id);
// Escape anything that goes into innerHTML — a filename like
// "<img src=x onerror=…>" must never become markup in a page that holds
// an RPC channel to the backend.
const esc = (s) => String(s).replace(/[&<>"']/g,
  (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

api.on('tick', ({ time, uptime }) => {
  $('clock').textContent = time;
  $('uptime').textContent = uptime;
});

async function init() {
  await api.call('ping');
  $('dot').classList.add('ok');

  const info = await api.call('sysinfo');
  $('sysinfo').innerHTML = Object.entries(info)
    .map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`)
    .join('');

  $('path').value = info.home;
  await listDir(info.home);
}

async function listDir(path) {
  $('dirErr').textContent = '';
  try {
    const { entries } = await api.call('listDir', { path });
    $('path').value = path;
    const up = path.replace(/\/[^/]+\/?$/, '') || '/';
    $('dir').innerHTML =
      `<li class="dir" data-p="${esc(up)}">..</li>` +
      entries.map((e) =>
        `<li class="${e.isDir ? 'dir' : ''}" data-p="${esc(path.replace(/\/$/, '') + '/' + e.name)}">` +
        `${e.isDir ? '▸ ' : '  '}${esc(e.name)}</li>`
      ).join('');
  } catch (e) {
    $('dirErr').textContent = String(e);
  }
}

$('dir').addEventListener('click', (ev) => {
  const li = ev.target.closest('li');
  if (li && li.classList.contains('dir')) listDir(li.dataset.p);
});
$('path').addEventListener('keydown', (ev) => {
  if (ev.key === 'Enter') listDir($('path').value);
});

// Window controls & native dialogs
let n = 0;
$('retitle').addEventListener('click', () => api.call('win.setTitle', { title: 'renamed ' + ++n + '×' }));
$('grow').addEventListener('click', () => api.call('win.setSize', { width: 1200, height: 800 }));
$('shrink').addEventListener('click', () => api.call('win.setSize', { width: 960, height: 640 }));
$('openfile').addEventListener('click', async () => {
  const path = await api.call('win.openFile');
  if (!path) return;
  const head = await api.call('readFileHead', { path });
  $('picked').textContent = path + '\n\n' + head;
});
$('savefile').addEventListener('click', async () => {
  const path = await api.call('win.saveFile');
  $('picked').textContent = path ? 'would save to: ' + path : '(cancelled)';
});
$('quit').addEventListener('click', () => api.call('quit'));

init().catch((e) => { $('dirErr').textContent = 'init failed: ' + e; });
