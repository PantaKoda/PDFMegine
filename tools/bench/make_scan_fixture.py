"""Image-only ("scanned") PDF fixture for OCR resource measurements.

Same generator as MyBooksLibrary tests/fixtures/make_fixtures.py
(write_image_pdf), so results are comparable with the reader harness in
PDFMegine issue #1: US Letter pages, each one 850x1100 grayscale image with
dark bars that look like text lines, and no text layer.

    python tools/bench/make_scan_fixture.py OUT.pdf [PAGES]   (default 4 pages)

Standard library only; output is deterministic (4 pages: SHA-256 below).
"""

import sys
import zlib
from pathlib import Path

FOUR_PAGE_SHA256 = "131ad654651215f39c37b308687c55098180049a971dfdb5c3cb7a7172236c05"


def stream_object(dictionary: bytes, data: bytes) -> bytes:
    return dictionary + b"\nstream\n" + data + b"\nendstream"


def write_objects(path: Path, objects):
    """objects: bytes bodies; object number = index + 1; object 1 is the catalog."""
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % number + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        out += b"%010d 00000 n \n" % offset
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    path.write_bytes(bytes(out))


def write_image_pdf(path: Path, page_count: int, width: int = 850, height: int = 1100):
    objects = []
    page_ids = [3 + 3 * i for i in range(page_count)]
    objects.append(b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = " ".join(f"{pid} 0 R" for pid in page_ids)
    objects.append(f"<< /Type /Pages /Kids [{kids}] /Count {page_count} >>".encode())
    for i in range(page_count):
        rows = bytearray()
        for y in range(height):
            row = bytearray([255]) * width
            if (y // 22) % 3 == 0 and 120 < y < 420:  # A few text-like lines.
                seed = (y // 22) * 31 + i * 7
                x = 90
                while x < width - 90:
                    end = min(x + 30 + (seed * 13 + x) % 70, width - 90)
                    row[x:end] = bytes([32]) * (end - x)
                    x = end + 14
            rows += row
        data = zlib.compress(bytes(rows), 9)
        image_id, content_id = page_ids[i] + 1, page_ids[i] + 2
        objects.append(
            (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
             f"/Resources << /XObject << /Im0 {image_id} 0 R >> >> /Contents {content_id} 0 R >>").encode())
        objects.append(stream_object(
            (f"<< /Type /XObject /Subtype /Image /Width {width} /Height {height} /ColorSpace /DeviceGray "
             f"/BitsPerComponent 8 /Filter /FlateDecode /Length {len(data)} >>").encode(), data))
        content = b"q 612 0 0 792 0 0 cm /Im0 Do Q"
        objects.append(stream_object(b"<< /Length %d >>" % len(content), content))
    write_objects(path, objects)


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    write_image_pdf(Path(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) == 3 else 4)
