"""Generate deterministic native-text PDFs for Engine analysis tests.

Standard library only. All indices below are zero-based physical pages.
Printed page numbers appear as footers; they are never physical indices.
"""

from pathlib import Path

HERE = Path(__file__).parent
WIDTH, HEIGHT = 600, 800


def esc(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def line(text: str, x: float, y: float, size: int = 12) -> str:
    return f"BT /F1 {size} Tf {x:.2f} {y:.2f} Td ({esc(text)}) Tj ET"


def toc_page(rows, heading=True, short_leaders=False):
    """rows: (title, indent_x, printed_ref). Numbers right-aligned at x=520."""
    out = [line("Contents", 40, 740, 18)] if heading else []
    y = 690
    for title, x, ref in rows:
        # Dot leaders run close to the right-aligned number, or (short_leaders)
        # stop early, leaving a wide empty gap before the number.
        dots = 8 if short_leaders else max(3, int((500 - x - 7.0 * len(title)) / 3.336))
        out.append(line(f"{title} {'.' * dots}", x, y))
        ref = str(ref)
        out.append(line(ref, 520 - 6.672 * len(ref), y))
        y -= 28
    return out


def body_page(printed=None, headings=()):
    out = []
    y = 740
    for heading in headings:
        out.append(line(heading, 40, y, 16))
        y -= 26
    out.append(line("Body text continues on this page without numbers.", 40, 600))
    if printed is not None:
        out.append(line(str(printed), 300, 40))
    return out


def prose_page(label):
    return [line(f"Front matter: {label}", 40, 740, 14),
            line("This page contains ordinary prose and no contents rows.", 40, 700)]


def build(pages, outline=None) -> bytes:
    objects: list[bytes] = []

    def add(value) -> int:
        objects.append(value.encode("latin-1") if isinstance(value, str) else value)
        return len(objects)

    def page_object(i):
        return 5 + i * 2

    outline_root = 4 + len(pages) * 2
    catalog = "<< /Type /Catalog /Pages 2 0 R >>"
    if outline:
        catalog = f"<< /Type /Catalog /Pages 2 0 R /Outlines {outline_root} 0 R >>"
    add(catalog)
    kids = " ".join(f"{page_object(i)} 0 R" for i in range(len(pages)))
    add(f"<< /Type /Pages /Kids [{kids}] /Count {len(pages)} >>")
    add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    for i, lines in enumerate(pages):
        data = "\n".join(lines).encode("latin-1")
        assert add(f"<< /Length {len(data)} >>\nstream\n".encode() + data +
                   b"\nendstream") == 4 + i * 2
        assert add(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {WIDTH} {HEIGHT}] "
                   "/Resources << /Font << /F1 3 0 R >> >> "
                   f"/Contents {4 + i * 2} 0 R >>") == page_object(i)
    if outline:
        first = outline_root + 1
        n = len(outline)
        assert add(f"<< /Type /Outlines /First {first} 0 R /Last {first + n - 1} 0 R "
                   f"/Count {n} >>") == outline_root
        for k, (title, target) in enumerate(outline):
            links = f"/Parent {outline_root} 0 R"
            if k:
                links += f" /Prev {first + k - 1} 0 R"
            if k + 1 < n:
                links += f" /Next {first + k + 1} 0 R"
            add(f"<< /Title ({esc(title)}) {links} /Dest [{page_object(target)} 0 R /Fit] >>")
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{number} 0 obj\n".encode() + body + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
    for offset in offsets:
        out += f"{offset:010d} 00000 n \n".encode()
    out += (f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
            f"startxref\n{xref}\n%%EOF\n").encode()
    return bytes(out)


def book(count, toc_pages, body_first, targets, short_leaders=False):
    """toc_pages: {index: rows or (rows, heading)}; body pages from body_first
    carry printed n = index - body_first + 1; targets: {printed: [headings]}."""
    pages = []
    for i in range(count):
        if i in toc_pages:
            spec = toc_pages[i]
            rows, heading = spec if isinstance(spec, tuple) else (spec, True)
            pages.append(toc_page(rows, heading, short_leaders))
        elif i >= body_first:
            printed = i - body_first + 1
            pages.append(body_page(printed, targets.get(printed, ())))
        else:
            pages.append(prose_page(f"page {i + 1}"))
    return pages


# 1. TOC spanning physical indices 39/40 (the first search batch ends at 39).
BOUNDARY_ROWS_39 = [("Part One", 40, 1), ("Getting Started", 64, 1),
                    ("Installation", 64, 3), ("Configuration", 64, 6)]
BOUNDARY_ROWS_40 = [("Part Two", 40, 10), ("Networking", 64, 11),
                    ("Storage", 64, 15), ("Appendix Notes", 40, 20)]
BOUNDARY_TARGETS = {1: ["Part One", "Getting Started"], 3: ["Installation"],
                    6: ["Configuration"], 10: ["Part Two"], 11: ["Networking"],
                    15: ["Storage"], 20: ["Appendix Notes"]}
boundary = book(70, {39: BOUNDARY_ROWS_39, 40: (BOUNDARY_ROWS_40, False)}, 41,
                BOUNDARY_TARGETS)
# Same book with short leaders: right-aligned numbers far from their titles
# (the layout that once made S2 drop TOC page 39; see S2-08).
boundary_short = book(70, {39: BOUNDARY_ROWS_39, 40: (BOUNDARY_ROWS_40, False)}, 41,
                      BOUNDARY_TARGETS, short_leaders=True)
# Misleading pre-existing outline: wrong titles/destinations, extra entry.
MISLEADING = [("Part One", 5), ("Networking", 2), ("Zeta Chapter", 66)]

# 2. Two tied TOC candidates (indices 1 and 3) separated by prose (index 2).
multi = book(30, {1: [("Part A", 40, 1), ("Part B", 40, 5), ("Part C", 40, 10),
                      ("Part D", 40, 15)],
                  3: [("Intro", 40, 1), ("Methods", 40, 4), ("Results", 40, 8),
                      ("Summary", 40, 12)]},
             5, {1: ["Part A", "Intro"], 4: ["Methods"], 5: ["Part B"],
                 8: ["Results"], 10: ["Part C"], 12: ["Summary"], 15: ["Part D"]})

# 3. Unknown hierarchy: "Beta" is indented 12 points (between root and child).
unknown = book(20, {1: [("Alpha", 40, 1), ("Beta", 52, 2), ("Gamma", 40, 3),
                        ("Delta", 40, 4)]},
               2, {1: ["Alpha"], 2: ["Beta"], 3: ["Gamma"], 4: ["Delta"]})

# 4. Unresolvable entry: "Beta" cites printed page 99 (beyond the document);
#    its child "Beta One" resolves and must be promoted under allow_partial.
unresolved = book(20, {1: [("Alpha", 40, 1), ("Beta", 40, 99),
                           ("Beta One", 64, 2), ("Gamma", 40, 3)]},
                  2, {1: ["Alpha"], 2: ["Beta One"], 3: ["Gamma"]})

# 4b. Numbered chapters, sections and appendices for title styles (E-18).
#     "A Short History" is a chapter whose title starts with the article "A";
#     it must never be styled as an appendix.
numbered = book(20, {1: [("1 Introduction", 40, 1), ("1.1 Scope", 64, 2),
                         ("2 Methods", 40, 3), ("A Short History", 40, 4),
                         ("A Tables", 40, 5), ("A.1 Units", 64, 6),
                         ("B Glossary", 40, 7)]},
                2, {1: ["1 Introduction"], 2: ["1.1 Scope"], 3: ["2 Methods"],
                    4: ["A Short History"], 5: ["A Tables"], 6: ["A.1 Units"],
                    7: ["B Glossary"]})

# 5. No TOC anywhere.
no_toc = [prose_page(f"page {i + 1}") for i in range(50)]

FIXTURES = {
    "boundary.pdf": build(boundary),
    "boundary_outlined.pdf": build(boundary, MISLEADING),
    "boundary_short_leaders.pdf": build(boundary_short),
    "multi.pdf": build(multi),
    "unknown_hierarchy.pdf": build(unknown),
    "unresolved.pdf": build(unresolved),
    "numbered.pdf": build(numbered),
    "no_toc.pdf": build(no_toc),
}

if __name__ == "__main__":
    import hashlib
    for name, data in FIXTURES.items():
        (HERE / name).write_bytes(data)
        print(f"{name} {len(data)} {hashlib.sha256(data).hexdigest()}")
