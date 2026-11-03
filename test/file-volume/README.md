# tiny.fileURL across volumes (tinyjsapp-examples#2)

WebKit's file:// read grant stops at the volume the page sits on, so even
`"readAccess": "/"` couldn't load `/Volumes/<other disk>/…`. `tiny.fileURL`
now hands out `tiny-media://file/<path>` for a path on another volume, served
by the macOS launcher (`serveFile:` in launcher-macos.cc). macOS only.

1. Make a volume with a track on it:
   ```
   say -o /tmp/t.aiff hello && afconvert -f m4af -d aac /tmp/t.aiff /tmp/t.m4a
   hdiutil create -size 20m -fs APFS -volname TinyVolTest -o /tmp/tvt.dmg
   hdiutil attach /tmp/tvt.dmg -nobrowse && cp /tmp/t.m4a /Volumes/TinyVolTest/
   cp /tmp/t.m4a ~/tvt.m4a
   ```
2. From a scratch app dir whose tinyjs.json has `"readAccess": "/"`:
   `TINYJS_HTML=$PWD/../tinyjsapp/test/file-volume/main.html tinyjs dev`
3. After ~3 s read `"fv"` from the app's store.json.

| key | expected |
|---|---|
| `vurl` | `tiny-media://file/Volumes/TinyVolTest/t.m4a` |
| `vol` | `ok` (was `error 4` before the fix) |
| `range` | `206 10` |

Reverse direction (an app run off a dmg): copy main.html onto the volume,
set `H` in that copy to `/Users/<you>/tvt.m4a` and point TINYJS_HTML at it
— `hurl` is a `tiny-media://file/Users/…` URL, `home` is `ok`.

Refusals (stderr names each): with `"readAccess": true` instead, `vol` is
`error 4` and stderr says `tiny-media file refused: … is outside
readAccess`; a symlink in ~ pointing at the volume, and `/Users/x/../../etc/hosts`,
are refused the same way; a cross-origin iframe's fetch gets `refused for
http://… (not one of the app's own pages)`; an iframe navigated to a
tiny-media://file URL gets `(document load)`.
