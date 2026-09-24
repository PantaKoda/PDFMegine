# S1 — Text Acquisition

**Status: implemented and independently tested.** This folder owns the standalone OCR-disabled [CMake entry](CMakeLists.txt). The root build provides an opt-in OCR-enabled target. The former `text/CMakeLists.txt` remains a compatibility forwarding entry.

Public contract: `include/pdfbookmark/text/acquisition.hpp`. Implementation: `src/text/`. Tests and generated PDFs: `tests/text/`. Shared types: `include/pdfbookmark/core/types.hpp`. [S1 handoff](../../docs/handoffs/S1_HANDOFF.md) records behavior, test evidence, package identities and limitations.
