"""Generate a small, deterministic native-text PDF for S2/S1 integration."""

from pathlib import Path

output = Path(__file__).with_name("acquired_toc.pdf")
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


def page_lines(lines: list[str]) -> str:
    commands = []
    for i, line in enumerate(lines):
        y = 740 - i * 32
        commands.append(f"BT /F1 14 Tf 40 {y} Td ({line}) Tj ET")
    return "\n".join(commands)


assert add("<< /Type /Catalog /Pages 2 0 R >>") == 1
assert add("<< /Type /Pages /Kids [5 0 R 7 0 R 9 0 R] /Count 3 >>") == 2
assert add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>") == 3

pages = [
    [
        "Contents",
        "Foundations ................ 1",
        "Methods .................... 17",
        "Experiments ................ 34",
        "Results .................... 52",
    ],
    [
        "Discussion ................. 70",
        "Applications ............... 84",
        "Appendix A ................. 95",
        "References ................ 110",
    ],
    [
        "Foundations",
        "This is body prose without a reference column.",
        "It should not look like another contents page.",
    ],
]
for index, lines in enumerate(pages):
    content_number = stream(page_lines(lines))
    assert content_number == 4 + index * 2
    assert add(
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] "
        "/Resources << /Font << /F1 3 0 R >> >> "
        f"/Contents {content_number} 0 R >>"
    ) == 5 + index * 2

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
output.write_bytes(document)
print(output, len(document))
