PDF BOOKMARK - add clickable bookmarks to PDF books
=====================================================

What it does
------------
Many PDF books have a printed table of contents ("Contents") but no
clickable bookmarks in the PDF viewer's side panel. PDF Bookmark reads the
printed table of contents, works out which page each chapter really starts
on, and saves a NEW copy of the PDF with bookmarks for every chapter.

  * Your original PDF is never changed.
  * Everything runs on your own computer. Nothing is uploaded; no internet
    connection is needed.
  * Scanned books work too: text on scanned pages is read with built-in
    text recognition (OCR).
  * It never guesses. If it cannot place a chapter reliably, it tells you
    instead of creating a wrong bookmark.


What you need
-------------
  * Windows 10 or 11, 64-bit.
  * This whole folder (keep all files together; do not move pdfbookmark.exe
    out of it). Nothing else needs to be installed.


Install as a command (recommended)
----------------------------------
1. Extract the downloaded pdfbookmark-<version>-win64.zip.
2. Double-click  Install.cmd  (no administrator rights needed).
   It copies the program to  %LOCALAPPDATA%\Programs\PDF Bookmark  and adds
   that folder to your PATH.
3. Open a NEW Command Prompt or PowerShell window and type:
       pdfbookmark --version
       pdfbookmark --help
   The program now works from any folder, like any other command.
4. To remove it: run  Uninstall.cmd  in the install folder
   (%LOCALAPPDATA%\Programs\PDF Bookmark). It removes the PATH entry and
   the folder.

You can also skip installing and use the extracted folder directly
(drag-and-drop below works either way).


Quick start (no typing needed)
------------------------------
1. Unzip / copy this folder anywhere, for example to your Desktop.
2. Drag one or more PDF files onto  "Add bookmarks.cmd".
3. A black window shows progress. When it says "Finished", press a key.
4. Next to each original PDF you will find:
       My Book (bookmarked).pdf       <- the new PDF with bookmarks
       pdfbookmark-reports\           <- details (you can ignore these)
5. Open "My Book (bookmarked).pdf" and show the bookmarks panel in your PDF
   viewer (in most viewers: the ribbon/bookmark icon on the left side).

Large or scanned books can take a few minutes. Please wait for "Finished".


If a file says "Not bookmarked"
-------------------------------
PDF Bookmark only writes bookmarks it can verify. Common reasons:

  "a complete, verified set of bookmarks could not be made"
      Some chapters could not be placed with certainty. Try dragging the
      same PDF onto  "Add bookmarks (allow partial).cmd".  That version
      writes the chapters it IS sure about and simply leaves out the rest
      (it lists them in the report). Chapters whose nesting is unclear are
      placed at the top level.

  "No TOC candidate ... pages searched"
      No table of contents was recognised in the first pages of the PDF.
      Either the PDF has none, or its layout is not supported yet (see
      "What kinds of PDFs work" below).

  "too close; explicit selection required"
      The PDF has two similar tables of contents (for example a short one
      and a detailed one). See "Choosing between two tables of contents".

  "already exists"
      "<name> (bookmarked).pdf" is already there. Delete or rename it and
      try again.

  "encrypted" / "Password-protected" / "Signed PDFs are not supported"
      Protected or digitally signed PDFs cannot be changed by this tool.


What kinds of PDFs work
-----------------------
Works best with:
  * Books whose contents page lists titles followed by page numbers, e.g.
        Introduction ................ 1
        Getting Started ............. 12
  * Normal page numbers (1, 2, 3 ...) printed at the bottom of the pages.
  * Text PDFs and scanned PDFs.

Not supported yet (you will get "Not bookmarked", never wrong bookmarks):
  * Contents pages that print the page number BEFORE the title
        (for example "12  Introduction").
  * Chapters numbered with Roman numerals (i, ii, iv ...) or with prefixes
    such as "A-12" (these entries are left out in "allow partial" mode).
  * Tables of contents that appear after page 200 of the PDF.
  * Password-protected or digitally signed PDFs.


Existing bookmarks
------------------
If your PDF already has bookmarks, they are ignored while reading, and the
new copy gets exactly the bookmarks found from the printed contents page.
The original file keeps its old bookmarks.


Using the command line (optional)
---------------------------------
After "Install as a command" above, open any Command Prompt or PowerShell
window. (Without installing, open a terminal in this folder instead: in
File Explorer type  cmd  in the address bar and press Enter.) Examples:

  One step (writes "My Book (bookmarked).pdf" next to the original):
    pdfbookmark add "C:\Books\My Book.pdf"
    pdfbookmark add "C:\Books\My Book.pdf" --allow-partial

  Two steps (to check or edit the plan before writing):
    pdfbookmark analyze "C:\Books\My Book.pdf" --plan plan.json --report report.json
    pdfbookmark apply   "C:\Books\My Book.pdf" --plan plan.json --output "C:\Books\My Book (bookmarked).pdf"

  Note: 'apply' needs the plan that 'analyze' writes; use 'add' to do both.

  Bookmark title style (add and analyze):
    --titles printed     as printed in the contents (default): "1 Modern processors"
    --titles chapter     "Chapter 1: Modern processors", "Appendix A: ..."
                         (sections such as "1.2.3 Pipelining" stay as printed)
    Example:  pdfbookmark add "My Book.pdf" --titles chapter

  Useful options for analyze:
    --allow-partial      write only the chapters that can be placed
    --flat-outline       put chapters with unclear nesting at the top level
    --candidate ID       choose which table of contents to use (see below)
    --mode embedded      use only the PDF's own text (fastest, no OCR)
    --mode ocr           always use text recognition

  Book details (title, authors, edition, years):
    pdfbookmark metadata "C:\Books\My Book.pdf"
    Reads the first pages (cover, title and copyright pages) and shows
    what was found and where. A copyright year is shown separately and is
    never presented as the publication year. Add --json FILE for all the
    evidence. The PDF is not changed.

  Other commands:
    pdfbookmark text "My Book.pdf" --pages 1-5      shows the text found on pages 1-5
    pdfbookmark --help                               lists the commands
    pdfbookmark help analyze                         all options of one command
                                                     (also: analyze --help)

  Page numbers you type (like --pages 1-5) count from 1 = first page of the
  file. Inside the .json reports, page_index counts from 0 = first page.

  Result codes (for scripts): 0 = done, 3 = not complete (no bookmarks
  written), 1 = error, 2 = wrong command, 4 = cancelled (Ctrl+C).


Choosing between two tables of contents
----------------------------------------
If the message lists IDs such as  toc-p1-r2  and  toc-p3-r4 , the number
after "p" is the page (counting from 0) where that contents list starts.
Run analyze again choosing one, for example:

    pdfbookmark analyze "My Book.pdf" --candidate toc-p3-r4 --plan plan.json
    pdfbookmark apply   "My Book.pdf" --plan plan.json --output "My Book (bookmarked).pdf"


Editing bookmark titles before saving (optional)
------------------------------------------------
plan.json is a text file. You may open it in Notepad and change a "title"
before running apply. Do not change the "sha256" or "page_count" lines: they
make sure the plan is only used with the exact PDF it was made for.


Privacy and files created
-------------------------
Only these files are created: "<name> (bookmarked).pdf" and the report and
plan files in "pdfbookmark-reports". The reports contain the text read from
the pages that were examined; delete them if you do not need them.


Third-party software
--------------------
This program includes PDFium, qpdf, zlib, libjpeg-turbo, ONNX Runtime,
OpenCV and PaddleOCR (PP-OCR) models. Their licences are in the
"licenses" folder.
