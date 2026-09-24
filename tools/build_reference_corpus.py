#!/usr/bin/env python3
"""Build the fixed 30-image parity corpus.

The first 25 fixtures are deterministic synthetic images targeting pipeline
edge cases.  The remaining five are resized copies of images in the checked
out PaddleOCR repository and retain their source path in the manifest.
"""

from __future__ import annotations

import json
from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "corpus"
FONT = cv2.FONT_HERSHEY_SIMPLEX


def save(name: str, image: np.ndarray, manifest: list[dict[str, object]], **meta: object) -> None:
    path = CORPUS / f"{name}.png"
    ok, encoded = cv2.imencode(".png", image)
    if not ok:
        raise RuntimeError(f"could not encode {path}")
    path.write_bytes(encoded.tobytes())
    record: dict[str, object] = {
        "id": name,
        "file": path.name,
        "width": int(image.shape[1]),
        "height": int(image.shape[0]),
    }
    record.update(meta)
    manifest.append(record)


def canvas(width: int, height: int, value: int = 248) -> np.ndarray:
    return np.full((height, width, 3), value, dtype=np.uint8)


def text(
    image: np.ndarray,
    value: str,
    origin: tuple[int, int],
    scale: float = 1.0,
    thickness: int = 2,
    color: tuple[int, int, int] = (24, 24, 24),
) -> None:
    cv2.putText(
        image,
        value,
        origin,
        FONT,
        scale,
        color,
        thickness,
        cv2.LINE_AA,
    )


def add_rotated_text(
    image: np.ndarray,
    value: str,
    center: tuple[int, int],
    angle: float,
    scale: float,
) -> None:
    layer = np.zeros_like(image)
    mask = np.zeros(image.shape[:2], dtype=np.uint8)
    size, baseline = cv2.getTextSize(value, FONT, scale, 3)
    origin = ((image.shape[1] - size[0]) // 2, (image.shape[0] + size[1]) // 2)
    cv2.putText(layer, value, origin, FONT, scale, (20, 20, 20), 3, cv2.LINE_AA)
    cv2.putText(mask, value, origin, FONT, scale, 255, 3, cv2.LINE_AA)
    transform = cv2.getRotationMatrix2D(
        (image.shape[1] / 2.0, image.shape[0] / 2.0), angle, 1.0
    )
    transform[:, 2] += np.array(center) - np.array(
        [image.shape[1] / 2.0, image.shape[0] / 2.0]
    )
    rotated = cv2.warpAffine(layer, transform, (image.shape[1], image.shape[0]))
    rotated_mask = cv2.warpAffine(mask, transform, (image.shape[1], image.shape[0]))
    image[rotated_mask > 0] = rotated[rotated_mask > 0]


def synthetic_fixtures(manifest: list[dict[str, object]]) -> None:
    image = canvas(800, 480, 255)
    cv2.rectangle(image, (0, 0), (799, 58), (45, 52, 62), -1)
    text(image, "Settings", (28, 41), 1.0, 2, (245, 245, 245))
    text(image, "Account overview", (48, 130), 1.25, 2)
    text(image, "Email address", (48, 210), 0.8, 2, (80, 80, 80))
    text(image, "hello@example.com", (340, 210), 0.8, 2)
    text(image, "Save changes", (48, 340), 0.9, 2, (255, 255, 255))
    cv2.rectangle(image, (35, 300), (260, 365), (165, 92, 35), -1)
    save("00_plain_screenshot", image, manifest, category="plain screenshot")

    rng = np.random.default_rng(606)
    photo = rng.normal(154, 24, (600, 800, 3)).clip(0, 255).astype(np.uint8)
    photo = cv2.GaussianBlur(photo, (0, 0), 5.0)
    cv2.rectangle(photo, (105, 145), (695, 455), (226, 220, 202), -1)
    add_rotated_text(photo, "ROTATED CAFE", (400, 300), 17.0, 1.65)
    save("01_rotated_photo", photo, manifest, category="photo with rotated text")

    scan = canvas(4201, 600, 250)
    for x in range(0, 4201, 120):
        cv2.line(scan, (x, 0), (x, 599), (246, 246, 246), 1)
    text(scan, "LARGE FORMAT SCAN", (90, 165), 2.1, 4)
    text(scan, "edge coordinates must survive the 4000 pixel cap", (90, 330), 1.5, 3)
    text(scan, "right edge", (3710, 515), 1.0, 2)
    save("02_large_scan", scan, manifest, category="over 4000 pixels")

    tiny = canvas(20, 20, 255)
    text(tiny, "A", (2, 16), 0.55, 1)
    save("03_tiny", tiny, manifest, category="h+w under 64")

    tie = canvas(96, 64, 255)
    text(tie, "TIE", (8, 43), 1.1, 2)
    save(
        "04_rounding_tie",
        tie,
        manifest,
        category="34.5 grid tie: 1104/32 rounds to even 34",
    )

    tall = canvas(280, 680, 255)
    strip = canvas(520, 90, 255)
    text(strip, "VERTICAL", (12, 63), 1.7, 3)
    strip = cv2.rotate(strip, cv2.ROTATE_90_COUNTERCLOCKWISE)
    y = (tall.shape[0] - strip.shape[0]) // 2
    x = (tall.shape[1] - strip.shape[1]) // 2
    tall[y : y + strip.shape[0], x : x + strip.shape[1]] = strip
    save("05_tall_crop", tall, manifest, category="crop aspect ratio over 1.5")

    wide = canvas(2200, 150, 255)
    text(wide, "THIS IS A VERY WIDE RECOGNITION CROP 0123456789", (25, 105), 1.8, 3)
    save("06_very_wide", wide, manifest, category="very wide crop")

    doubled = canvas(1000, 240, 255)
    text(doubled, "balloon coffee success", (35, 105), 1.75, 3)
    text(doubled, "letters: ll oo ff ss", (35, 195), 1.2, 2)
    save("07_doubled_letters", doubled, manifest, category="adjacent duplicate CTC tokens")

    empty = canvas(640, 360, 255)
    for y in range(empty.shape[0]):
        empty[y, :, :] = 235 + (y * 19 // empty.shape[0])
    save("08_no_text", empty, manifest, category="no text")

    labels = [
        "one line only",
        "TWO ROWS",
        "numbers 0123456789",
        "punctuation !? #42",
        "MixedCase Example",
        "low contrast text",
        "WHITE ON BLACK",
        "near left border",
        "near right border",
        "small glyphs abcdef",
        "large glyphs OCR",
        "slanted minus twelve",
        "row tolerance alpha",
        "row tolerance beta",
        "spaces between words",
        "thin stroke sample",
    ]
    for offset, label in enumerate(labels, start=9):
        width = 520 + (offset % 4) * 95
        height = 210 + (offset % 3) * 55
        background = 32 if offset == 15 else 250
        image = canvas(width, height, background)
        foreground = (242, 242, 242) if background < 100 else (28, 28, 28)
        if offset == 14:
            foreground = (172, 172, 172)
        scale = 0.62 if offset == 18 else (1.65 if offset == 19 else 1.0)
        thickness = 1 if offset == 24 else 2
        origin_x = 2 if offset == 16 else 24
        text(image, label, (origin_x, 82), scale, thickness, foreground)
        if offset in {10, 21, 22}:
            second_y = 93 if offset in {21, 22} else 155
            text(image, f"second row {offset}", (42, second_y), 0.82, 2, foreground)
        if offset == 20:
            center = (width // 2, height // 2 + 35)
            add_rotated_text(image, "angled", center, -12.0, 1.0)
        save(f"{offset:02d}_synthetic", image, manifest, category="synthetic coverage")


def real_fixtures(manifest: list[dict[str, object]]) -> None:
    sources = [
        "PaddleOCR/test_tipc/web/test.jpg",
        "PaddleOCR/tests/test_files/book.jpg",
        "PaddleOCR/tests/test_files/doc_with_formula.png",
        "PaddleOCR/langchain-paddleocr/tests/data/sample_img.jpg",
        "PaddleOCR/docs/version2.x/static/images/demo.jpg",
    ]
    for index, relative in enumerate(sources, start=25):
        source = ROOT / relative
        image = cv2.imdecode(np.frombuffer(source.read_bytes(), np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError(f"could not decode {source}")
        longest = max(image.shape[:2])
        if longest > 900:
            ratio = 900.0 / longest
            image = cv2.resize(
                image,
                (int(image.shape[1] * ratio), int(image.shape[0] * ratio)),
                interpolation=cv2.INTER_AREA,
            )
        save(
            f"{index:02d}_paddle_sample",
            image,
            manifest,
            category="PaddleOCR source fixture",
            source=relative,
        )


def main() -> None:
    CORPUS.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, object]] = []
    synthetic_fixtures(manifest)
    real_fixtures(manifest)
    if len(manifest) != 30:
        raise RuntimeError(f"expected 30 fixtures, built {len(manifest)}")
    (CORPUS / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"wrote {len(manifest)} images to {CORPUS}")


if __name__ == "__main__":
    main()
