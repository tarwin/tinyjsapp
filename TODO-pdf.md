# PDF output — pagination, and headers/footers

**Low priority** (parked 2026-07-26). Nothing here is broken for the common
case of "save what's on screen"; it's wrong for the case the docs advertise,
which is invoices and reports.

## The finding: macOS doesn't paginate, the other two do

`win.printToPDF(path)` goes through a different pipeline on each platform, and
only one of them produces pages:

| | how | output |
| --- | --- | --- |
| macOS | `WKPDFConfiguration` + `createPDFWithConfiguration:` | **one page**, the size of the whole content |
| Windows | WebView2 `PrintToPdf(path, nullptr, handler)` | real paper-sized pages (default settings) |
| Linux | `webkit_print_operation` + GtkPrintSettings "Print to File" | real paper-sized pages (printer default) |

Measured on macOS 2026-07-26 with a 120-paragraph A4-styled test page:
`pages: 1`, media box **1083 × 4424 pt** — about a metre and a half tall, with
`@page { size: A4; margin: 20mm 15mm }` ignored entirely. It is a faithful
vector capture of the document; it is not a document anyone can print or send.

So the shortest description of the gap: **on macOS there are no pages, and a
header/footer needs pages to sit on.**

## Chrome's `headerTemplate` has no equivalent in either engine

The ask that started this was "custom header/footer HTML, like headless
Chrome". `Page.printToPDF`'s `headerTemplate`/`footerTemplate` are a Blink/CDP
feature — a separate mini-document rendered into the page margin, with magic
classes (`pageNumber`, `totalPages`, `date`, `title`, `url`).

- **WebKit** (macOS *and* WebKitGTK) exposes nothing comparable.
- **WebView2** exposes `ICoreWebView2PrintSettings` with
  `ShouldPrintHeaderAndFooter`, `HeaderTitle` and `FooterUri` — plain text and
  a URI, not HTML, and not a template.

Anything richer has to be built by us.

## Three routes, cheapest first

### 1. Make macOS paginate (the prerequisite for everything else)

Switch macOS to `[wv printOperationWithPrintInfo:]` with
`jobDisposition = NSPrintSaveJob` and `NSPrintJobSavingURL` — the same path
`win.print()`'s dialog already uses, so the saved PDF matches what the user
would get from the print panel. All three platforms would then emit paper-sized
pages honouring `@page`.

Once pages exist, **running headers cost no new API**: `@page { margin }` to
reserve the strip, plus `position: fixed` header/footer blocks, which WebKit
repeats on every printed page. That's full HTML and CSS in the app's own
document — less restricted than Chrome's template context, which can't see the
page's stylesheet. **Unverified here** — the harness below never produced a
valid file, so someone has to actually confirm the repeat.

> **Warning for whoever picks this up.** A quick standalone harness around
> `printOperationWithPrintInfo:` ran away: it wrote a **1 GB** PDF in seconds
> and kept going, with *and* without fixed-position elements, so it is not the
> CSS. Most likely the webview's frame has to be set to the printable width
> *before* the operation is created (setting `op.view.frame` afterwards is too
> late). Guard any experiment with a file-size ceiling and a kill — the first
> run filled a lot of disk before it was noticed.

### 2. A print options object, with stamped page numbers

`printToPDF(path, { paper, margins, landscape, header, footer })`, where
header/footer are **text templates** — `{page}`, `{pages}`, `{title}`,
`{date}` — drawn into the margin by the launcher: PDFKit/Core Graphics on
macOS, `HeaderTitle`/`FooterUri` covering part of it on Windows, a Cairo pass
on Linux.

This is the only route to **"Page 3 of 12"**. No engine implements `@page`
margin boxes or `counter(page)`, so CSS cannot number pages on any platform —
that is the one thing route 1 can never give you.

### 3. Full HTML header/footer, Chrome-style

Render the header/footer HTML in an offscreen webview, then composite the
result onto every page of the finished PDF, substituting the page number per
page. Closest to headless Chrome and the most work by a distance: three
different PDF libraries (PDFKit / PDFium or a bundled writer / Poppler+Cairo)
and a second webview to keep alive.

## Docs to correct when any of this lands

Both currently sell the macOS behaviour as something it isn't:

- `README.md` and `docs/docs.html` — `printToPDF` as "a real **vector** PDF …
  that's the one for invoices and reports". True about the vectors, misleading
  about the shape: today an invoice comes out as one very tall page.
- `kitchen-sink` Desktop ▸ Sharing & print card repeats the same line.

## Reproducing the measurement

The page: any tall document with `@page { size: A4; margin: … }`. The
inspector: ~20 lines of Objective-C over PDFKit — open the file as a
`PDFDocument`, print `pageCount`, then each page's `boundsForBox:
kPDFDisplayBoxMediaBox` and the first line of `page.string`. Build with
`clang -fobjc-arc -framework Foundation -framework PDFKit -framework Quartz`.
Page count and media box are the whole test: 1 page at four thousand points
tall is the bug, ~9 pages at 842 pt tall is the fix.
