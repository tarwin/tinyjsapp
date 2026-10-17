# Dock reopening without a main window (macOS)

Requires a logged-in macOS GUI session and this checkout's rebuilt launcher.
Quit any earlier test run, then start from the repository root:

```sh
./setup.sh
cd test/dock-reopen-windowless
../../tinyjs dev
```

The app has a Dock icon and a **Dock Test** camera item on the right-hand
side of the menu bar. It starts without a window.

1. Click the app's Dock icon. No window should appear; the app must stay running.
2. Choose **Dock Test → Open editor** from the menu bar. An **Editor** window
   must appear.
3. Choose **Dock Test → Close editor**. The editor must disappear. Click the
   Dock icon again: no window should appear.
4. Open and close the editor once more through the tray menu, then choose
   **Dock Test → Quit**. The app must exit.

From this fixture directory, build the packaged app and repeat steps 1–4:

```sh
../../tinyjs build
'./dist/Dock Windowless Test.app/Contents/MacOS/dock-windowless-test'
```
