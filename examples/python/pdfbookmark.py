"""pdfbookmark for Python: a small ctypes wrapper over the library's C API.

No build step and no dependencies: copy this file next to your script, and
point it at the pdfbookmark library (argument, PDFBOOKMARK_LIBRARY, or the
library placed next to this file).

    import pdfbookmark
    lib = pdfbookmark.Library()                 # or Library("C:/sdk/bin/pdfbookmark.dll")
    report, plan = lib.analyze("book.pdf", allow_partial=True)
    if plan:
        lib.apply("book.pdf", "book (bookmarked).pdf", plan)

Options are the keyword arguments documented for the C API in docs/API.md
(e.g. mode="embedded", titles="chapter", models=None to disable OCR).
Results are the library's JSON reports, decoded into dicts. Failures raise
PdfBookmarkError. Calls release the GIL, so a CancelToken can be cancelled
from another thread.
"""

import ctypes
import json
import os
import sys
from pathlib import Path

__all__ = ["Library", "CancelToken", "PdfBookmarkError"]

C_API_VERSION = 1

_PROGRESS = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
                             ctypes.c_size_t)
_OUT = ctypes.POINTER(ctypes.c_void_p)


class PdfBookmarkError(Exception):
    """A failed library call. `status` is the C status number and `name` its
    stable name, e.g. "output_exists" or "input_changed"."""

    def __init__(self, status, name, message):
        super().__init__(f"{name}: {message}")
        self.status = status
        self.name = name
        self.message = message


def _default_library_name():
    if sys.platform == "win32":
        return "pdfbookmark.dll"
    if sys.platform == "darwin":
        return "libpdfbookmark.dylib"
    return "libpdfbookmark.so"


class CancelToken:
    """Cooperative cancellation: pass it to an operation, call cancel() from
    any thread. Checked between pages."""

    def __init__(self, library):
        self._lib = library._c
        self._handle = self._lib.pdfb_cancel_token_new()
        if not self._handle:
            raise MemoryError("pdfb_cancel_token_new failed")

    def cancel(self):
        self._lib.pdfb_cancel_token_cancel(self._handle)

    def __del__(self):
        handle, self._handle = getattr(self, "_handle", None), None
        if handle:
            self._lib.pdfb_cancel_token_free(handle)


class Library:
    """The loaded pdfbookmark library."""

    def __init__(self, path=None):
        if path is None:
            path = os.environ.get("PDFBOOKMARK_LIBRARY")
        if path is None:
            path = Path(__file__).resolve().parent / _default_library_name()
        # Dependencies (PDFium, qpdf, ...) are found next to the library.
        self._c = ctypes.CDLL(str(path))
        self._declare()
        if self._c.pdfb_c_api_version() != C_API_VERSION:
            raise RuntimeError(f"{path}: C API version {self._c.pdfb_c_api_version()}, "
                               f"this wrapper needs {C_API_VERSION}")

    # ------------------------------------------------------------ info

    @property
    def version(self):
        return self._c.pdfb_version().decode()

    def find_models(self):
        """OCR model paths found by the default search, or None."""
        return self._call_json(self._c.pdfb_find_models)

    # ------------------------------------------------------------ operations

    def extract_text(self, pdf, pages=None, progress=None, cancel=None, **options):
        """Positioned text of `pages` (zero-based; None = every page)."""
        if pages is not None:
            options["pages"] = list(pages)
        return self._call_json(self._c.pdfb_extract_text, _path(pdf), _options(options),
                               _token(cancel), _callback(progress), None)

    def analyze(self, pdf, progress=None, cancel=None, **options):
        """Returns (report, plan). `plan` is the ready plan's JSON text for
        apply(), or None when the report's plan is not ready (see
        report["plan"]["blockers"])."""
        report, plan = _OutString(self._c), _OutString(self._c)
        callback = _callback(progress)  # Kept alive during the call.
        self._check(self._c.pdfb_analyze(_path(pdf), _options(options), _token(cancel),
                                         callback, None, report.ref(), plan.ref()))
        return json.loads(report.text()), plan.text()

    def analyze_book(self, pdf, progress=None, cancel=None, **options):
        """analyze() and extract_metadata() in one run: pages read for the
        contents are reused for the metadata (a scan is OCR'd once).
        Returns (report, plan, metadata); plan is None when not ready."""
        report, plan, meta = _OutString(self._c), _OutString(self._c), _OutString(self._c)
        callback = _callback(progress)  # Kept alive during the call.
        self._check(self._c.pdfb_analyze_book(_path(pdf), _options(options), _token(cancel),
                                              callback, None, report.ref(), plan.ref(),
                                              meta.ref()))
        return json.loads(report.text()), plan.text(), json.loads(meta.text())

    def apply(self, pdf, output, plan, replace_existing_output=False, cancel=None):
        """Writes a NEW PDF with the plan's bookmarks. `plan` is JSON text or
        a dict. Returns the write result."""
        if not isinstance(plan, str):
            plan = json.dumps(plan, ensure_ascii=False)
        options = {"replace_existing_output": True} if replace_existing_output else {}
        return self._call_json(self._c.pdfb_apply, _path(pdf), _path(output),
                               plan.encode("utf-8"), _options(options), _token(cancel))

    def extract_metadata(self, pdf, cancel=None, **options):
        return self._call_json(self._c.pdfb_extract_metadata, _path(pdf), _options(options),
                               _token(cancel))

    def validate_plan(self, plan):
        if not isinstance(plan, str):
            plan = json.dumps(plan, ensure_ascii=False)
        return self._call_json(self._c.pdfb_validate_plan, plan.encode("utf-8"))

    def read_pdf_identity(self, pdf):
        return self._call_json(self._c.pdfb_read_pdf_identity, _path(pdf))

    # ------------------------------------------------------------ plumbing

    def _call_json(self, function, *args):
        out = _OutString(self._c)
        self._check(function(*args, out.ref()))
        text = out.text()
        return None if text is None else json.loads(text)

    def _check(self, status):
        if status != 0:
            raise PdfBookmarkError(status, self._c.pdfb_status_name(status).decode(),
                                   self._c.pdfb_last_error().decode("utf-8", "replace"))

    def _declare(self):
        c = self._c
        s, i, v, p = ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p, _PROGRESS
        signatures = {
            "pdfb_version": (s, []),
            "pdfb_c_api_version": (i, []),
            "pdfb_status_name": (s, [i]),
            "pdfb_last_error": (s, []),
            "pdfb_free": (None, [v]),
            "pdfb_cancel_token_new": (v, []),
            "pdfb_cancel_token_cancel": (None, [v]),
            "pdfb_cancel_token_free": (None, [v]),
            "pdfb_find_models": (i, [_OUT]),
            "pdfb_extract_text": (i, [s, s, v, p, v, _OUT]),
            "pdfb_analyze": (i, [s, s, v, p, v, _OUT, _OUT]),
            "pdfb_analyze_book": (i, [s, s, v, p, v, _OUT, _OUT, _OUT]),
            "pdfb_apply": (i, [s, s, s, s, v, _OUT]),
            "pdfb_extract_metadata": (i, [s, s, v, _OUT]),
            "pdfb_validate_plan": (i, [s, _OUT]),
            "pdfb_read_pdf_identity": (i, [s, _OUT]),
        }
        for name, (restype, argtypes) in signatures.items():
            function = getattr(c, name)
            function.restype = restype
            function.argtypes = argtypes


class _OutString:
    """A `char**` output owned by the library, freed after decoding."""

    def __init__(self, c):
        self._c = c
        self._pointer = ctypes.c_void_p()

    def ref(self):
        return ctypes.byref(self._pointer)

    def text(self):
        if not self._pointer.value:
            return None
        try:
            return ctypes.string_at(self._pointer.value).decode("utf-8")
        finally:
            self._c.pdfb_free(self._pointer)
            self._pointer = ctypes.c_void_p()


def _path(path):
    return os.fsdecode(os.fspath(path)).encode("utf-8")


def _options(options):
    return json.dumps(options, ensure_ascii=False).encode("utf-8") if options else None


def _token(cancel):
    return cancel._handle if cancel is not None else None


def _callback(progress):
    if progress is None:
        return _PROGRESS()  # NULL function pointer.
    return _PROGRESS(lambda _user, stage, done, total:
                     progress(stage.decode("utf-8", "replace"), done, total))
