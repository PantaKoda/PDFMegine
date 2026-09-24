#!/usr/bin/env python3
"""Extract the embedded PP-OCR recognition character dictionary.

This intentionally parses only the ``PostProcess.character_dict`` block used
by PaddleOCR inference.yml files.  It has no third-party dependencies, which
keeps this ground-truth bootstrap script usable in a clean Python install.
"""

from __future__ import annotations

import argparse
import ast
from pathlib import Path


def _decode_yaml_scalar(source: str, line_number: int) -> str:
    if not source:
        raise ValueError(f"line {line_number}: empty character token")

    if source.startswith("'"):
        if len(source) < 2 or not source.endswith("'"):
            raise ValueError(f"line {line_number}: unterminated single-quoted scalar")
        return source[1:-1].replace("''", "'")

    if source.startswith('"'):
        if len(source) < 2 or not source.endswith('"'):
            raise ValueError(f"line {line_number}: unterminated double-quoted scalar")
        # YAML and Python share the escape forms used by this model file.  Use
        # literal_eval rather than hand-unescaping UTF-8 characters.
        value = ast.literal_eval(source)
        if not isinstance(value, str):
            raise ValueError(f"line {line_number}: character token is not a string")
        return value

    # Plain YAML scalars in this dictionary are literal character tokens.  Do
    # not strip them: file order and token bytes are part of the model ABI.
    return source


def extract_charset(yaml_path: Path) -> list[str]:
    lines = yaml_path.read_text(encoding="utf-8").splitlines()
    marker = "  character_dict:"
    try:
        start = lines.index(marker) + 1
    except ValueError as exc:
        raise ValueError(f"{yaml_path}: missing {marker.strip()!r}") from exc

    tokens: list[str] = []
    for index in range(start, len(lines)):
        line = lines[index]
        if not line.startswith("  - "):
            if line.startswith("  ") and not line.startswith("    "):
                break
            raise ValueError(
                f"line {index + 1}: expected a character_dict list entry"
            )
        token = _decode_yaml_scalar(line[4:], index + 1)
        if token == "":
            raise ValueError(f"line {index + 1}: decoded to a blank token")
        if "\n" in token or "\r" in token:
            raise ValueError(f"line {index + 1}: token contains a line ending")
        tokens.append(token)

    if not tokens:
        raise ValueError(f"{yaml_path}: character_dict is empty")
    return tokens


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("yaml", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    tokens = extract_charset(args.yaml)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as stream:
        for token in tokens:
            stream.write(token)
            stream.write("\n")

    print(f"wrote {len(tokens)} tokens to {args.output}")


if __name__ == "__main__":
    main()
