# Real test books (local only)

Put real PDF books here to check PDF Bookmark on real documents (guide
step A11). These files are **never** packaged (the end-user package is an
allow-list; see `packaging/`) and should not be published: books are usually
copyrighted.

| Folder | Put here |
| --- | --- |
| `native/` | PDFs with real selectable text (you can highlight words in a viewer) |
| `scanned/` | Image-only scans (no selectable text) |
| `mixed/` | Scans with a hidden OCR text layer, or partly scanned books |
| `unsupported/` | Anything you expect to fail (e.g. number-before-title contents pages) |

Most useful: books whose contents page looks like `Chapter title ...... 12`, with
page numbers printed at the bottom of pages. The TCP/IP book mentioned in the
implementation guide belongs in `native/` or `scanned/`, depending on its kind.

## Optional: expected bookmarks

To let results be checked automatically later, add a text file next to a PDF
named `<same name>.expected.txt` with one line per chapter you verified by hand:

```
<page shown in the PDF viewer's page box, counting from 1><TAB><chapter title>
```

For example `13	Introduction`. Indent the title with two spaces per nesting level
if you want hierarchy checked.

## Run them all

Double-click `run_books.cmd` in this folder (or run it from a terminal). It
uses the packaged program in `dist/pdfbookmark/`, processes every PDF under
this folder, and writes reports, plans, bookmarked copies and a summary to
`out/books-results/`. Your PDFs here are only read, never changed.
