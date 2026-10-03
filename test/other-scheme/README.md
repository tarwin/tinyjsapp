# Other URL schemes reach onNavigate (#30.2)

`nav.html` makes an iframe load `tinyjstest-a://sub`, then sets the main
frame's location to `tinyjstest-b://main`, then saves `"alive"` to the store
2 s later (proof the page didn't go anywhere).

Scratch app (`tinyjs new navapp`), with this appended to `src/main.js`
(change the log path to suit):

```js
const navLog = [];
export async function onNavigate(info) {
  if (info.kind !== 'policy') return;
  navLog.push({ url: info.url, isMainFrame: info.isMainFrame });
  await tjs.writeFile('/tmp/navlog.json', new TextEncoder().encode(JSON.stringify(navLog)));
  return tjs.env.NAV_VERDICT || undefined;
}
```

Run it with `TINYJS_HTML=/abs/path/nav.html tinyjs dev`, and again with
`NAV_VERDICT=deny`, `allow` and `external`.

Expected:
- The log has both URLs. `isMainFrame` is `false` and `true` on macOS, and
  `null` for both on Linux and Windows.
- `"alive"` is saved with the page's own `file://` href.
- With no verdict, `deny` or `allow`: nothing opens and no OS prompt
  appears.
- With `external`: the OS is asked to open both URLs. With these made-up
  schemes, macOS shows "There is no application set to open the URL…"
  twice; Cancel them. On Linux nothing visible happens (GIO finds no
  handler and fails silently) — to see the launch, register a throwaway
  `x-scheme-handler/tinyjstest-b` handler with `xdg-mime default`.

Windows: before #30.2, WebView2 showed its own "open this app?" prompt for
these. Now it shouldn't show one at all.
