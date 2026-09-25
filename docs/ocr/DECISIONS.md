# Implementation decisions

## Parameter profile

The implementation and golden corpus use the PP-OCRv6 medium model profile
from `models/det/inference.yml`: detector limit 736 with type `min`, absolute
side cap 4000, binarisation threshold 0.2, box threshold 0.45, 3000 contour
candidates, unclip ratio 1.4, fast scoring, quadrilateral boxes, and no
dilation. The recognition cutoff is the legacy pipeline value 0.5. All remain
runtime options.

## Golden corpus

`tools/build_reference_corpus.py` creates exactly 30 deterministic fixtures.
Its numeric constants only define test images; they are deliberately varied to
exercise the required edge cases and are not OCR algorithm constants. Five
fixtures are derived from files in the pinned PaddleOCR checkout; the generated
manifest records those source paths.

Recognition batch artifacts use zero-padded batch numbers:
`08_rec_input_000.npy`, `08_rec_input_000_indices.json`, and
`09_rec_logits_000.npy`. This makes the unspecified `B` placeholder in the
implementation guide stable and sortable.

## Python reference environment

The reference harness uses the versions pinned in
`tools/reference-requirements.txt`. Paddle itself is not installed: the harness
loads the relevant preprocessing, DB post-processing, and CTC decoder source
files from the pinned PaddleOCR checkout and supplies only the minimal module
shim needed for their `paddle.Tensor` and `paddle.get_device` checks. ONNX
execution is through the pinned `onnxruntime` package.

## Polygon offset

Clipper2 commit `f9c5eb6e14a59f6f5d65fbfb3564519a561cf4fd` is vendored under
`third_party/clipper2` with its Boost Software License. Only
`clipper.engine.cpp` and `clipper.offset.cpp` are compiled.

Upstream pyclipper accepts the floating-point mini-box vertices even though its
offset engine uses integer coordinates. A direct probe with positive and
negative fractional coordinates showed truncation toward zero (for example,
`1.9 -> 1` and `-1.9 -> -1`). The C++ path therefore uses an explicit
`static_cast<int64_t>` before adding the closed polygon to Clipper2. The
offset distance itself remains double precision and round joins are used.

Clipper2 is constructed with pyclipper's default miter limit 2.0 and arc
tolerance 0.25. Clipper2 normally takes the ceiling of each round join's step
count, while Clipper 6.4.2 as wrapped by pyclipper 1.4.0 rounds that count to
the nearest integer. The vendored `clipper.offset.cpp` carries one documented
compatibility change from `ceil` to `round`, with a lower bound of one,
matching Clipper 6.4.2's `DoRound`. Without it, intermediate arc vertices
shifted fitted mini-boxes by one pixel and changed recognition results in the
dense fixtures.

`Options::unclip_ratio` is a `float` as required by the public API, whereas
the Python reference receives YAML/CLI values as Python double-precision
numbers. Directly promoting the default float produced
`1.399999976158142`; for a 10-by-10 box that made the offset just less than
3.5 and changed Clipper's integer rounding. The implementation converts the
float through `std::to_chars`' shortest round-tripping decimal form and parses
that decimal as double. Thus the configured default is evaluated as decimal
1.4, matching the reference, while other representable option values retain
their shortest decimal value.

## C header linkage

The no-preprocessor-branch rule precludes the conventional
`#ifdef __cplusplus` wrapper in a dual C/C++ header. `include/ocr/ocr.h` is
therefore a pure C header with no conditional compilation. The C++ definition
includes it inside an `extern "C"` block, and the milestone consumer compiles
it as C. C++ consumers use `include/ocr/ocr.hpp`.

## Equal recognition aspect ratios

The Python reference calls NumPy's default, unstable `argsort`. Stable sorting
changes batch membership when crops have identical integer dimensions, which
was observed in the golden corpus. NumPy 2.5.3 dispatches arrays of at most 256
double values to x86-simd-sort's four-lane AVX2 bitonic network on the reference
machine. The C++ implementation has a scalar version of that network, including
its rule that equal keys retain the item in the first compare-exchange input.
This exactly reproduces all golden batch-index files, including the 65-crop and
145-crop tie-heavy cases. Larger arrays use an unstable comparison sort; equal
ratios at a batch boundary cannot change the batch width or restored final
result, but their intermediate batch index order is NumPy implementation and
CPU-dispatch dependent.

## Memory arena and thread count (PDFMegine issue #1, 25 September 2026)

The detector and recognizer sessions disable ONNX Runtime's CPU memory arena and memory-pattern planning (`src/ort_util.cpp`).

- **Why:** the arena grows to the largest tensor shapes a session has seen and never shrinks. Full-page detection plus variable-width recognition batches made a process peak at 8.7 GB for four 300-DPI pages (4.45 GB for one page). Without the arena the peak is 2.3 GB, whatever the page count, with byte-identical outputs and about 13% more time at one thread.
- **Verified:** `golden_parity` passes unchanged (single-threaded, the validated baseline).

`ocr_compare_golden` accepts an optional fifth argument, the ONNX Runtime thread count (default 1). At 8 threads all 30 images keep identical boxes, crops, recognition tensors and decoded text. The maximum confidence difference is 5.96e-7, and the maximum recognition-logit difference 2.86e-6, from floating-point summation order.
- `ocr::Options::threads` still defaults to 1. PDF Bookmark's S1 adapter chooses the thread count; see `docs/IMPLEMENTATION_DECISIONS.md` E-31.
