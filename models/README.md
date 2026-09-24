# OCR models

PP-OCRv6 medium detector and recognizer (ONNX) with the recognizer's
character set. The files are not in Git (139 MB): configuring the project
downloads them from this repository's `models-v1` release and checks their
SHA-256 (`cmake/PdfbookmarkDependencies.cmake`). Offline alternative:
`-DPDFBOOKMARK_MODELS_ARCHIVE=<path to pdfbookmark-models-v1.zip>`.

    det/inference.onnx   text detector
    det/inference.yml    detector parameter profile
    rec/inference.onnx   text recognizer
    rec/inference.yml    recognizer parameter profile
    rec/charset.txt      recognition character set (18,708 tokens)

The models are PaddleOCR PP-OCR models (Apache-2.0).
