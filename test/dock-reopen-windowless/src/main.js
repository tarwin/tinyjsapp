export function init(app) {
  app.tray.set({ title: 'Dock Test', icon: 'sf:camera', menu: [
    { id: 'open', label: 'Open editor' },
    { id: 'close', label: 'Close editor' },
    { id: 'quit', label: 'Quit' },
  ] });
}

export function onTray(id, app) {
  if (id === 'open') {
    app.win.open('editor', { page: 'editor.html', title: 'Editor', size: '400x300' });
    app.window('editor').show();
  }
  if (id === 'close') app.window('editor').close();
  if (id === 'quit') app.quit();
}
