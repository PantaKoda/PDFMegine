# PP-OCRv6 ONNX C++ library

The PDF Bookmark project uses this completed OCR library as a dependency. See
[repository architecture](../ARCHITECTURE.md) for the five visible domain
subsystems and their current status.

> This file moved here from the repository root on 24 September 2026 (decision E-25). It describes the OCR package on its own; to build the whole project, use `docs/BUILDING.md`, whose presets fetch ONNX Runtime, OpenCV and the models automatically. `golden/` and the `PaddleOCR/` checkout are local-only and not in Git (E-24).

This project provides an embeddable C++17 OCR engine and a C ABI. It runs the
bundled PP-OCRv6 detector and recognizer with ONNX Runtime and OpenCV. PaddleOCR
is used only to define and validate the algorithms; it is not a runtime
dependency.

Consumers include [`ocr/ocr.hpp`](../../include/ocr/ocr.hpp) for C++ or
[`ocr/ocr.h`](../../include/ocr/ocr.h) for C. The public headers expose no OpenCV or
ONNX Runtime types. Raw image input is always 8-bit, three-channel **BGR**.

## Build

Provide OpenCV through its CMake package and an ONNX Runtime root containing
`include/onnxruntime_cxx_api.h`, the import/static library, and the shared
runtime:

```powershell
cmake -S . -B out/build -G Ninja `
  -DOpenCV_DIR=C:/path/to/opencv/build `
  -DONNXRUNTIME_ROOT=C:/path/to/onnxruntime
cmake --build out/build
cmake --install out/build --prefix out/install
```

`BUILD_SHARED_LIBS=OFF` builds `ocr.lib`/`libocr.a`; setting it to `ON`
builds the shared library. The install exports `ocr::ocr`, installs both
public headers, and places the ONNX Runtime and OpenCV runtime libraries in
`bin`.

An installed CMake consumer uses:

```cmake
find_package(ocr CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ocr::ocr)
```

The engine constructor takes detector model, recognizer model, and charset
paths. Models are not located relative to the process working directory.
[`ocr_demo`](../../tools/ocr_demo.cpp) demonstrates executable-relative deployment
and is built with a sample image and model assets beside the executable.

## C++ example

```cpp
#include <ocr/ocr.hpp>

ocr::Engine engine("models/det/inference.onnx",
                   "models/rec/inference.onnx",
                   "models/rec/charset.txt");
std::vector<ocr::TextLine> lines = engine.run_file("page.png");
```

The C API uses an opaque `ocr_engine` handle and status codes. Result arrays,
their UTF-8 strings, and error messages must be released with
`ocr_free_text_lines`, `ocr_free_quads`, and `ocr_free_error`.

## Reference and parity

[`tools/generate_golden.py`](../../tools/generate_golden.py) generates the staged
reference artifacts from PaddleOCR commit
`dab3fe35379033fdcb2d0e9572fac0b36c9a9ebf`.
[`tools/compare_golden.cpp`](../../tools/compare_golden.cpp) validates all stages and
the public `Engine` over the 30-image corpus:

```powershell
out/build/ocr_compare_golden golden `
  models/det/inference.onnx `
  models/rec/inference.onnx `
  models/rec/charset.txt
```

The current Windows x64 validation has exact detector tensors, maps, DB quads,
crops, recognition tensors, logits, decoded text, and end-to-end quads. The
maximum confidence difference is `2.980232e-07`. See
[`DECISIONS.md`](DECISIONS.md) for every compatibility choice.
