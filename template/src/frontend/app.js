const $ = (id) => document.getElementById(id);
// Escape anything that goes into innerHTML — a filename like
// "<img src=x onerror=…>" must never become markup in a page that holds
// an RPC channel to the backend.
const esc = (s) => String(s).replace(/[&<>"']/g,
  (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

tiny.api.on('tick', ({ time, uptime }) => {
  $('clock').textContent = time;
  $('uptime').textContent = uptime;
});

async function init() {
  await tiny.api.call('ping');
  $('dot').classList.add('ok');

  const info = await tiny.api.call('sysinfo');
  $('sysinfo').innerHTML = Object.entries(info)
    .map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`)
    .join('');

  $('path').value = info.home;
  await listDir(info.home);

  // Custom menu bar (About/Quit and Edit are always there).
  await tiny.menu.set([
    {
      title: 'Actions',
      items: [
        { id: 'open', label: 'Open File…', key: 'o' },
        { id: 'rename', label: 'Rename Window', key: 'r' },
        { separator: true },
        { id: 'hello', label: 'Say Hello' },
      ],
    },
  ]);
  tiny.menu.on((id) => {
    if (id === 'open') openFile();
    if (id === 'rename') renameWindow();
    if (id === 'hello') tiny.win.alert('Hello!', 'This came from a native menu item.');
  });
}

async function listDir(path) {
  $('dirErr').textContent = '';
  try {
    const { entries } = await tiny.api.call('listDir', { path });
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

async function openFile() {
  const path = await tiny.win.openFile();
  if (!path) return;
  const head = await tiny.api.call('readFileHead', { path });
  $('picked').textContent = path + '\n\n' + head;
}

let renames = 0;
function renameWindow() {
  tiny.win.setTitle('renamed ' + ++renames + '×');
}

$('dir').addEventListener('click', (ev) => {
  const li = ev.target.closest('li');
  if (li && li.classList.contains('dir')) listDir(li.dataset.p);
});
$('path').addEventListener('keydown', (ev) => {
  if (ev.key === 'Enter') listDir($('path').value);
});

// Window controls
$('retitle').addEventListener('click', renameWindow);
$('grow').addEventListener('click', () => tiny.win.setSize(1200, 800));
$('shrink').addEventListener('click', () => tiny.win.setSize(960, 640));
$('quit').addEventListener('click', async () => {
  if (await tiny.win.confirm('Quit the app?', { detail: 'This is tiny.win.confirm().' })) tiny.quit();
});

// Native dialogs
$('openfile').addEventListener('click', openFile);
$('savefile').addEventListener('click', async () => {
  const path = await tiny.win.saveFile();
  $('picked').textContent = path ? 'would save to: ' + path : '(cancelled)';
});
$('alert').addEventListener('click', () => tiny.win.alert('Heads up', 'This is tiny.win.alert().'));
$('promptBtn').addEventListener('click', async () => {
  const name = await tiny.win.prompt('What is your name?', { default: 'world' });
  $('picked').textContent = name == null ? '(cancelled)' : 'hello, ' + name + '!';
});

init().catch((e) => { $('dirErr').textContent = 'init failed: ' + e; });
