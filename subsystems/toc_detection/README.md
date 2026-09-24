# S2 — TOC Detection

**Status: implemented and independently tested.** This folder owns the [standalone CMake entry](CMakeLists.txt) for a backend-free `pdfbookmarkTocDetection` library. The existing root build exposes it through `PDFBOOKMARK_BUILD_DETECTION=ON`.

Public contract: `include/pdfbookmark/detection/detection.hpp`. Implementation: `src/detection/`. Pure fixtures, independent consumers, and an S1-acquired PDF example: `tests/detection/`. [S2 handoff](../../docs/handoffs/S2_HANDOFF.md) records the score policy, commands, results, and limitations.

S2 only inspects supplied S1 values. It does not acquire new pages, parse authoritative TOC entries, resolve destinations, or write bookmarks.
