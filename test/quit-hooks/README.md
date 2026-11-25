# quit-hooks (#121)

Hand-test app for `onBeforeQuit` / `tiny.app.onBeforeQuit` / `onQuit`.
Checklist and results: the #121 section of TODO-verify.md.

    cd test/quit-hooks && tinyjs dev      # or: tinyjs build, then run dist/

Toggles: the backend cancels every quit (on at start), the page asks with a
confirm dialog, hide-on-close. A `tiny.quit()` button checks that a
programmatic quit isn't asked.

Every line also goes to a log that outlives the app, so `onQuit` can be
checked after the window is gone: `$QUIT_LOG`, else `quittest.log` in the
temp dir (`$TMPDIR` on macOS, `/tmp` on Linux, `%TEMP%` on Windows).

macOS without clicking: `osascript -e 'tell application id
"com.example.quittest" to quit'` takes the Dock Quit path; add
`«event aevtquit» given «class why?»:«constant ****rlgo»` instead of `quit`
for a logout's quit event (reason `'session'`).
