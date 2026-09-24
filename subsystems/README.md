# PDF Bookmark subsystems

The five domain folders below make ownership visible at the repository root. [Repository architecture](../docs/ARCHITECTURE.md) maps each folder to its public headers, sources, tests, and build target. Root [AGENTS.md](../AGENTS.md) defines the contracts and acceptance gates.

[S1 Text Acquisition](text/README.md), [S2 TOC Detection](toc_detection/README.md), [S3 TOC Parsing](toc_parsing/README.md), [S4 Page Mapping](page_mapping/README.md), and [S5 Bookmark Writing](bookmark_writing/README.md) are implemented.

[S6 Document Metadata Extraction](document_metadata/README.md) is an owner-directed extension beyond the five subsystems in `AGENTS.md`: a pure library that finds the title, contributors, edition and years from S1 page values (see `docs/handoffs/S6_HANDOFF.md`).
