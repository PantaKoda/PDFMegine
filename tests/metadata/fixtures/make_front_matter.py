"""Generate front_matter.pdf for S6/Engine metadata tests (standard library).

Physical pages (zero based):
  0 cover        large two-line title, authors lower down, publisher line
  1 title page   title, subtitle, "Second Edition", "by ..." statement
  2 copyright    copyright years, publication history, printing line, ISBN
  3 contents     a short table of contents
  4-11 body      ordinary text
"""

from pathlib import Path

HERE = Path(__file__).parent


def esc(text):
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def line(text, x, y, size):
    return f"BT /F1 {size} Tf {x} {y} Td ({esc(text)}) Tj ET"


pages = [
    [line("Example Press", 60, 740, 12), line("Parallel", 60, 640, 44),
     line("Worlds", 60, 590, 44), line("Jane Q. Doe", 60, 200, 24),
     line("John Smith", 60, 170, 24)],
    [line("Parallel Worlds", 60, 620, 30), line("A Practical Guide", 60, 580, 16),
     line("Second Edition", 60, 520, 12), line("by Jane Q. Doe and John Smith", 60, 460, 12),
     line("Example Press", 60, 100, 10)],
    [line("Example Press, 1 Main Street, Springfield", 60, 700, 8),
     line("Copyright (c) 2005, 2012 by Example Press", 60, 685, 8),
     line("First edition published 2005", 60, 670, 8),
     line("Second edition published 2012", 60, 655, 8),
     line("Printed in the United States of America 10 9 8 7 6 5 4 3 2 1", 60, 640, 8),
     line("ISBN 978-1-2345-6789-7", 60, 625, 8)],
    [line("Contents", 60, 700, 18), line("1 Introduction ........ 1", 60, 640, 11),
     line("2 Methods ........ 12", 60, 620, 11)],
] + [[line(f"Body text of page {i}, ordinary prose without metadata.", 60, 600, 10)]
     for i in range(4, 12)]

objects = []


def add(value):
    objects.append(value.encode("latin-1") if isinstance(value, str) else value)
    return len(objects)


add("<< /Type /Catalog /Pages 2 0 R >>")
kids = " ".join(f"{5 + i * 2} 0 R" for i in range(len(pages)))
add(f"<< /Type /Pages /Kids [{kids}] /Count {len(pages)} >>")
add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
for i, lines in enumerate(pages):
    data = "\n".join(lines).encode("latin-1")
    add(f"<< /Length {len(data)} >>\nstream\n".encode() + data + b"\nendstream")
    add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 500 760] "
        f"/Resources << /Font << /F1 3 0 R >> >> /Contents {4 + i * 2} 0 R >>")
out = bytearray(b"%PDF-1.4\n")
offsets = []
for number, body in enumerate(objects, start=1):
    offsets.append(len(out))
    out += f"{number} 0 obj\n".encode() + body + b"\nendobj\n"
xref = len(out)
out += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
for offset in offsets:
    out += f"{offset:010d} 00000 n \n".encode()
out += f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
(HERE / "front_matter.pdf").write_bytes(bytes(out))
print("front_matter.pdf", len(out))
