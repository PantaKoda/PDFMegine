# Generated S1 PDF fixtures

All physical page indices here are **zero based**. The PDFs are small, generated test artifacts; `make_fixture.py` builds `basic.pdf`. `image_only.pdf` embeds the repository's plain-screenshot corpus image as an image page at 300 DPI. Neither requires Python at application runtime.

| File | SHA-256 | Expected evidence |
| --- | --- | --- |
| `basic.pdf` | `830D477923C88ED11BE21B8B05A7F339624A105BF70B813F8882B8CA44C60D92` | 8 pages: 0 clean native `Hello World` with Roman viewer label `i` and local link to index 2; 1 blank; 2 native `Second page` with viewer label `A-1`; 3 rotated/cropped; 4 native header over large image; 5 ToUnicode with Chinese, emoji, é and one invalid surrogate; 6 `/UserUnit 2`; 7 hidden PDF text layer over image. |
| `image_only.pdf` | `7D4C5A6097B0F09CC75CB6793FD98F26D32FDB300E280D23B85D26F8D4E21BFE` | 1 physical page, no embedded text; existing OCR package and S1 adapter return the same 4 ordered lines on the same PDFium BGR raster. |

The S1 tests assert evidence and outcomes, not semantic TOC or bookmark decisions.
