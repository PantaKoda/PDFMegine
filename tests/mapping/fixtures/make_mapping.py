"""Generate deterministic native-text PDFs for S1/S2/S3/S4 handoff testing.

`acquired_mapping.pdf` has no outline. `acquired_mapping_outlined.pdf` has
identical page content plus a deliberately misleading pre-existing outline,
used to verify that existing bookmarks never influence S1-S4 results.
"""

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


def text_line(value: str, x: int, y: int) -> str:
    return f"BT /F1 14 Tf {x} {y} Td ({value}) Tj ET"


pages = [
    [
        text_line("Contents", 40, 740),
        text_line("Alpha ................ 1", 40, 680),
        text_line("Beta ................. 2", 40, 648),
        text_line("Gamma ................ 3", 40, 616),
    ],
    [
        text_line("Preface", 40, 740),
        text_line("This is body prose, not a contents entry.", 40, 700),
    ],
    [text_line("Alpha", 40, 740), text_line("1", 300, 40)],
    [text_line("Beta", 40, 740), text_line("2", 300, 40)],
    [text_line("Gamma", 40, 740), text_line("3", 300, 40)],
    [text_line("End matter", 40, 740), text_line("4", 300, 40)],
]

# Titles and physical destinations deliberately disagree with the printed TOC
# and footers (correct: Alpha->2, Beta->3, Gamma->4; Zeta does not exist).
MISLEADING_OUTLINE = [("Alpha", 5), ("Beta", 1), ("Zeta", 3)]


def page_object(index: int) -> int:
    return 5 + index * 2


def build(outline: bool) -> bytes:
    objects.clear()
    # Objects: catalog, pages, font, then (content, page) per page.
    outline_root = 4 + len(pages) * 2
    catalog = "<< /Type /Catalog /Pages 2 0 R >>"
    if outline:
        catalog = (f"<< /Type /Catalog /Pages 2 0 R "
                   f"/Outlines {outline_root} 0 R >>")
    assert add(catalog) == 1
    kids = " ".join(f"{page_object(i)} 0 R" for i in range(len(pages)))
    assert add(f"<< /Type /Pages /Kids [{kids}] /Count {len(pages)} >>") == 2
    assert add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>") == 3
    for index, lines in enumerate(pages):
        content_number = stream("\n".join(lines))
        assert content_number == 4 + index * 2
        assert add(
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] "
            "/Resources << /Font << /F1 3 0 R >> >> "
            f"/Contents {content_number} 0 R >>"
        ) == page_object(index)
    if outline:
        first = outline_root + 1
        count = len(MISLEADING_OUTLINE)
        assert add(
            f"<< /Type /Outlines /First {first} 0 R "
            f"/Last {first + count - 1} 0 R /Count {count} >>"
        ) == outline_root
        for i, (title, target) in enumerate(MISLEADING_OUTLINE):
            links = f"/Parent {outline_root} 0 R"
            if i > 0:
                links += f" /Prev {first + i - 1} 0 R"
            if i + 1 < count:
                links += f" /Next {first + i + 1} 0 R"
            assert add(f"<< /Title ({title}) {links} "
                       f"/Dest [{page_object(target)} 0 R /Fit] >>") == first + i

    document = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for number, body in enumerate(objects, start=1):
        offsets.append(len(document))
        document += f"{number} 0 obj\n".encode("ascii") + body + b"\nendobj\n"
    xref = len(document)
    document += f"xref\n0 {len(objects) + 1}\n".encode("ascii")
    document += b"0000000000 65535 f \n"
    for offset in offsets[1:]:
        document += f"{offset:010d} 00000 n \n".encode("ascii")
    document += (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref}\n%%EOF\n"
    ).encode("ascii")
    return bytes(document)


for name, outline in (("acquired_mapping.pdf", False),
                      ("acquired_mapping_outlined.pdf", True)):
    output = Path(__file__).with_name(name)
    data = build(outline)
    output.write_bytes(data)
    print(output, len(data))
