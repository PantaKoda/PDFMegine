"""Generate the tiny structural S1 fixture; no package dependencies."""

from pathlib import Path

objects: list[bytes] = []


def add(value: str | bytes) -> int:
    objects.append(value.encode("ascii") if isinstance(value, str) else value)
    return len(objects)


def stream(value: str) -> int:
    data = value.encode("ascii")
    return add(
        f"<< /Length {len(data)} >>\nstream\n".encode("ascii")
        + data
        + b"\nendstream"
    )


catalog = add("<< /Type /Catalog /Pages 2 0 R /PageLabels 12 0 R >>")
pages = add(
    "<< /Type /Pages /Kids [5 0 R 7 0 R 9 0 R 11 0 R "
    "15 0 R 20 0 R 22 0 R 24 0 R] /Count 8 >>"
)
font = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
assert (catalog, pages, font) == (1, 2, 3)

for index, text in enumerate(("Hello World", "", "Second page", "Rotated")):
    command = f"BT /F1 14 Tf 20 150 Td ({text}) Tj ET" if text else ""
    assert stream(command) == 4 + 2 * index
    page = (
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
        "/Resources << /Font << /F1 3 0 R >> >> "
        f"/Contents {4 + 2 * index} 0 R"
    )
    if index == 3:
        page += " /CropBox [20 10 180 190] /Rotate 90"
    if index == 0:
        page += " /Annots [13 0 R]"
    assert add(page + " >>") == 5 + 2 * index

assert add("<< /Nums [0 << /S /r >> 2 << /S /D /P (A-) >>] >>") == 12
assert add(
    "<< /Type /Annot /Subtype /Link /Rect [20 145 100 165] "
    "/Border [0 0 0] /Dest [9 0 R /Fit] >>"
) == 13
assert stream(
    "q 180 0 0 150 10 10 cm /Im0 Do Q "
    "BT /F1 14 Tf 20 180 Td (Header) Tj ET"
) == 14
assert add(
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
    "/Resources << /Font << /F1 3 0 R >> "
    "/XObject << /Im0 16 0 R >> >> /Contents 14 0 R >>"
) == 15
assert add(
    b"<< /Type /XObject /Subtype /Image /Width 1 /Height 1 "
    b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 3 >>\n"
    b"stream\n\xff\xff\xff\nendstream"
) == 16
assert add(
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
    "/Encoding /WinAnsiEncoding /ToUnicode 18 0 R >>"
) == 17
cmap = """/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /Adobe-Identity-UCS def
/CMapType 2 def
1 begincodespacerange
<00> <FF>
endcodespacerange
4 beginbfchar
<41> <4E2D>
<42> <D83DDE00>
<43> <00E9>
<44> <D800>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end end"""
assert stream(cmap) == 18
assert stream("BT /F2 16 Tf 20 150 Td <41424344> Tj ET") == 19
assert add(
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
    "/Resources << /Font << /F2 17 0 R >> >> /Contents 19 0 R >>"
) == 20
assert stream("BT /F1 14 Tf 20 150 Td (Scaled) Tj ET") == 21
assert add(
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
    "/UserUnit 2 /Resources << /Font << /F1 3 0 R >> >> "
    "/Contents 21 0 R >>"
) == 22
assert stream(
    "q 180 0 0 180 10 10 cm /Im0 Do Q "
    "BT /F1 14 Tf 3 Tr 20 150 Td (Hidden layer) Tj ET"
) == 23
assert add(
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
    "/Resources << /Font << /F1 3 0 R >> "
    "/XObject << /Im0 16 0 R >> >> /Contents 23 0 R >>"
) == 24
pdf = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
offsets = [0]
for number, body in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf.extend(f"{number} 0 obj\n".encode("ascii"))
    pdf.extend(body)
    pdf.extend(b"\nendobj\n")
xref = len(pdf)
pdf.extend(f"xref\n0 {len(offsets)}\n".encode("ascii"))
pdf.extend(b"0000000000 65535 f \n")
for offset in offsets[1:]:
    pdf.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
pdf.extend(
    f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\n"
    f"startxref\n{xref}\n%%EOF\n".encode("ascii")
)
target = Path(__file__).parent / "fixtures" / "basic.pdf"
target.parent.mkdir(exist_ok=True)
target.write_bytes(pdf)
print(target, len(pdf))
