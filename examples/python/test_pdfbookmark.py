"""Test of the Python wrapper (and so of the C API from another language).

    python test_pdfbookmark.py <pdfbookmark library> <engine fixtures dir> <work dir>
"""

import json
import shutil
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdfbookmark  # noqa: E402


def main():
    library, fixtures, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    lib = pdfbookmark.Library(library)
    assert lib.version.count(".") == 2

    # Non-ASCII path, analysis with progress, plan editing, apply.
    book = work / "Bøøk – test.pdf"
    shutil.copyfile(fixtures / "boundary_outlined.pdf", book)
    stages = []
    report, plan = lib.analyze(book, mode="embedded",
                               progress=lambda stage, done, total: stages.append(stage))
    assert report["outcome"] == "plan_ready" and plan and stages, report["outcome"]

    edited = json.loads(plan)
    edited["nodes"][0]["title"] = "Édité — title"
    assert lib.validate_plan(edited)["valid"]
    assert lib.read_pdf_identity(book)["sha256"] == edited["input"]["sha256"]
    output = work / "Bøøk (bookmarked).pdf"
    result = lib.apply(book, output, edited)
    assert result["committed"] and result["verification"]["structure_matches"]
    assert result["verification"]["outline_items"] == len(edited["nodes"])

    # Errors carry the status name and message.
    try:
        lib.apply(book, output, edited)
        raise AssertionError("existing output must be refused")
    except pdfbookmark.PdfBookmarkError as error:
        assert error.name == "output_exists", error
    try:
        lib.analyze(book, colour=1)
        raise AssertionError("unknown option must be refused")
    except pdfbookmark.PdfBookmarkError as error:
        assert error.name == "invalid_argument" and "colour" in error.message

    # Cancellation from another thread (already cancelled before any page).
    token = pdfbookmark.CancelToken(lib)
    threading.Thread(target=token.cancel).start()
    threading.Event().wait(0.1)
    report, plan = lib.analyze(book, cancel=token)
    assert report["outcome"] == "cancelled" and plan is None

    # Analysis + metadata in one run (issue #3).
    early = []
    report, plan, meta = lib.analyze_book(book, mode="embedded", on_metadata=early.append)
    assert report["outcome"] == "plan_ready" and plan and meta["kind"] == "pdfbookmark.metadata"
    assert early == [meta], "metadata delivered early through the callback"

    text = lib.extract_text(fixtures / "boundary.pdf", pages=[39], mode="embedded")
    assert text["pages"][0]["page_index"] == 39
    print(f"pdfbookmark {lib.version} Python wrapper passed")


if __name__ == "__main__":
    main()
