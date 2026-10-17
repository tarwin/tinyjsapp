# Dock reopening (macOS)

Requires a logged-in macOS GUI session and this checkout's rebuilt launcher.
From the repository root:

```sh
./setup.sh
cd test/dock-reopen
../../tinyjs dev
```

Enter `unsaved draft` in the text field, then check:

1. **Closed with the red button:** click the red close button, then the
   **Dock Reopen Test** icon in the Dock. The window must return with
   `unsaved draft` intact. Repeat once with different text.
2. **Already visible:** leave the test window visible, switch to Terminal,
   then click the test app's Dock icon. The same window must come to the
   front with its text intact. No additional window should appear.
3. **Minimized:** click the yellow minimize button, then the test app's
   Dock icon. The window must return with its text intact.

Quit via the app menu or the Dock icon's **Quit** item before the next run.

Repeat the same steps against a packaged app, from this fixture directory:

```sh
../../tinyjs build
'./dist/Dock Reopen Test.app/Contents/MacOS/dock-reopen-test'
```

For the app without a main window, use the
[windowless Dock test](../dock-reopen-windowless/README.md).
