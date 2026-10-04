# Can a non-app page become a file:// document? (#30.7)

The launchers turn on file-access-from-file-URLs and universal-access-from-
file-URLs (Vite's `crossorigin` module scripts need them). WebKit applies
both only to `file://` documents, so this checks that a wrapped `http` page
can't become one.

1. In this folder: `python3 -m http.server 8765 --bind 127.0.0.1`
2. A scratch app with `"url": "http://127.0.0.1:8765/wrapfile.html"` in
   tinyjs.json (no `"api"`), run with `tinyjs dev`.
3. Read `"wf"` and `"wfnav"` from its store.json after about 8 s.

Expected: `wf.fetch` is an error, there's no `wf.iframe` (the frame never
loads), and `wfnav` is still the `http://…/wrapfile.html` URL.
