"""Adds bookmarks to a PDF book from its printed table of contents.

    python add_bookmarks.py "My Book.pdf" [--allow-partial] [--chapter-titles]

Writes "My Book (bookmarked).pdf" next to the input; the input is never
modified. Needs pdfbookmark.py and the pdfbookmark library (see
pdfbookmark.py for how the library is found).
"""

import argparse
import sys
from pathlib import Path

import pdfbookmark


def main():
    # Never fail on printing a file name the console encoding can't show.
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pdf", type=Path)
    parser.add_argument("--allow-partial", action="store_true",
                        help="leave out entries that cannot be placed reliably")
    parser.add_argument("--chapter-titles", action="store_true",
                        help='top-level titles as "Chapter 1: ..."')
    parser.add_argument("--library", help="path to the pdfbookmark library")
    args = parser.parse_args()

    lib = pdfbookmark.Library(args.library)
    print(f"pdfbookmark {lib.version}")
    report, plan = lib.analyze(
        args.pdf, allow_partial=args.allow_partial,
        titles="chapter" if args.chapter_titles else "printed",
        progress=lambda stage, done, _total: print(f"  {stage}: {done} pages read",
                                                   file=sys.stderr))
    print(f"outcome: {report['outcome']}")
    if plan is None:
        for blocker in report["plan"]["blockers"]:
            print(f"  {blocker}")
        return 3

    output = args.pdf.with_name(f"{args.pdf.stem} (bookmarked).pdf")
    result = lib.apply(args.pdf, output, plan)
    print(f"wrote {result['output']} ({result['verification']['outline_items']} bookmarks)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except pdfbookmark.PdfBookmarkError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
