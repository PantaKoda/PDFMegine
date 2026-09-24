#!/usr/bin/env python3
"""Generate stage-by-stage PP-OCRv6 ONNX golden artifacts.

The numerical algorithms are loaded from the pinned PaddleOCR checkout.  A
minimal paddle module shim is sufficient because inference is performed by
ONNX Runtime and the selected upstream code only checks Tensor/get_device.
"""

from __future__ import annotations

import argparse
import ast
import importlib.util
import json
import math
import shutil
import subprocess
import sys
import types
from pathlib import Path
from typing import Any

import cv2
import numpy as np
import onnxruntime as ort
import pyclipper
import shapely
import yaml


PROFILE = {
    "limit_side_len": 736,
    "limit_type": "min",
    "max_side_limit": 4000,
    "bin_thresh": 0.2,
    "box_thresh": 0.45,
    "max_candidates": 3000,
    "unclip_ratio": 1.4,
    "use_dilation": False,
    "score_mode": "fast",
    "box_type": "quad",
    "rec_height": 48,
    "rec_base_width": 320,
    "rec_batch_size": 6,
    "drop_score": 0.5,
}


def install_paddle_shim() -> None:
    if "paddle" in sys.modules:
        return

    paddle = types.ModuleType("paddle")

    class Tensor:
        pass

    paddle.Tensor = Tensor
    paddle.get_device = lambda: "cpu"
    paddle_nn = types.ModuleType("paddle.nn")
    paddle_nn_functional = types.ModuleType("paddle.nn.functional")
    paddle_nn.functional = paddle_nn_functional
    paddle.nn = paddle_nn
    sys.modules["paddle"] = paddle
    sys.modules["paddle.nn"] = paddle_nn
    sys.modules["paddle.nn.functional"] = paddle_nn_functional


def load_module(name: str, path: Path) -> types.ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load Python module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def load_top_level_function(path: Path, name: str, globals_: dict[str, Any]) -> Any:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    functions = [
        node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == name
    ]
    if len(functions) != 1:
        raise RuntimeError(f"expected one {name} definition in {path}")
    module = ast.fix_missing_locations(ast.Module(body=functions, type_ignores=[]))
    namespace = dict(globals_)
    exec(compile(module, str(path), "exec"), namespace)
    return namespace[name]


def load_class_methods(
    path: Path, source_class: str, method_names: set[str], globals_: dict[str, Any]
) -> type:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    source = next(
        (
            node
            for node in tree.body
            if isinstance(node, ast.ClassDef) and node.name == source_class
        ),
        None,
    )
    if source is None:
        raise RuntimeError(f"missing class {source_class} in {path}")
    methods = [
        node
        for node in source.body
        if isinstance(node, ast.FunctionDef) and node.name in method_names
    ]
    found = {node.name for node in methods}
    if found != method_names:
        raise RuntimeError(f"missing methods {sorted(method_names - found)} in {path}")
    class_node = ast.ClassDef(
        name=f"Reference{source_class}",
        bases=[],
        keywords=[],
        body=methods,
        decorator_list=[],
    )
    module = ast.fix_missing_locations(ast.Module(body=[class_node], type_ignores=[]))
    namespace = dict(globals_)
    exec(compile(module, str(path), "exec"), namespace)
    return namespace[class_node.name]


def json_value(value: Any) -> Any:
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, dict):
        return {key: json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_value(item) for item in value]
    return value


def write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(json_value(value), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def make_session(path: Path) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    return ort.InferenceSession(
        str(path), sess_options=options, providers=["CPUExecutionProvider"]
    )


def verify_model_config(root: Path, rec_session: ort.InferenceSession) -> None:
    rec_yml = yaml.safe_load((root / "models/rec/inference.yml").read_text(encoding="utf-8"))
    embedded = rec_yml["PostProcess"]["character_dict"]
    charset = (root / "models/rec/charset.txt").read_text(encoding="utf-8").splitlines()
    if embedded != charset:
        raise RuntimeError("charset.txt differs from inference.yml character_dict")
    if len(charset) != 18708 or any(token == "" for token in charset):
        raise RuntimeError("charset must contain 18708 non-blank tokens")
    dummy = np.zeros((1, 3, PROFILE["rec_height"], PROFILE["rec_base_width"]), np.float32)
    output = rec_session.run(None, {rec_session.get_inputs()[0].name: dummy})[0]
    if output.shape[-1] != len(charset) + 2:
        raise RuntimeError(
            f"recognizer C={output.shape[-1]}, expected {len(charset) + 2}"
        )
    print(f"recognizer dummy output {output.shape}; C={output.shape[-1]}")


def write_readme(root: Path, golden: Path, corpus_count: int) -> None:
    commit = subprocess.check_output(
        ["git", "-C", str(root / "PaddleOCR"), "rev-parse", "HEAD"], text=True
    ).strip()
    readme = f"""# Golden PP-OCRv6 ONNX reference

Generated by `tools/generate_golden.py` over {corpus_count} images.

- PaddleOCR commit: `{commit}`
- Reference path: legacy `tools/infer/predict_system.py`, `predict_det.py`, and
  `predict_rec.py`; numerical helper implementations are loaded directly from
  the corresponding files under this checkout.
- ONNX Runtime: `{ort.__version__}` (CPUExecutionProvider, one intra-op and one
  inter-op thread, all graph optimisations enabled)
- NumPy: `{np.__version__}`
- OpenCV Python: `{cv2.__version__}`
- Shapely: `{shapely.__version__}`
- pyclipper: `{pyclipper.__version__}`

The parameter profile is the PP-OCRv6 medium model profile:

```json
{json.dumps(PROFILE, indent=2)}
```

`08_rec_input_NNN_indices.json` records the original sorted-crop index for
every sample in that recognition batch. An image with no detected text has no
08/09 batch files and has an empty `10_final.json`.
"""
    (golden / "README.md").write_text(readme, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    paddle_root = root / "PaddleOCR"
    golden = root / "golden"
    golden.mkdir(parents=True, exist_ok=True)

    install_paddle_shim()
    operators = load_module(
        "paddleocr_reference_operators", paddle_root / "ppocr/data/imaug/operators.py"
    )
    db_module = load_module(
        "paddleocr_reference_db", paddle_root / "ppocr/postprocess/db_postprocess.py"
    )
    rec_module = load_module(
        "paddleocr_reference_rec", paddle_root / "ppocr/postprocess/rec_postprocess.py"
    )
    detector_geometry_type = load_class_methods(
        paddle_root / "tools/infer/predict_det.py",
        "TextDetector",
        {"order_points_clockwise", "clip_det_res", "filter_tag_det_res"},
        {"np": np},
    )
    recognizer_preprocess_type = load_class_methods(
        paddle_root / "tools/infer/predict_rec.py",
        "TextRecognizer",
        {"resize_norm_img"},
        {"np": np, "cv2": cv2, "math": math},
    )
    sorted_boxes = load_top_level_function(
        paddle_root / "tools/infer/predict_system.py", "sorted_boxes", {"np": np}
    )
    get_rotate_crop_image = load_top_level_function(
        paddle_root / "tools/infer/utility.py",
        "get_rotate_crop_image",
        {"np": np, "cv2": cv2},
    )

    decode = operators.DecodeImage(img_mode="BGR", channel_first=False)
    resize = operators.DetResizeForTest(
        limit_side_len=PROFILE["limit_side_len"],
        limit_type=PROFILE["limit_type"],
        max_side_limit=PROFILE["max_side_limit"],
    )
    normalize = operators.NormalizeImage(
        scale="1./255.",
        mean=[0.485, 0.456, 0.406],
        std=[0.229, 0.224, 0.225],
        order="hwc",
    )
    chw = operators.ToCHWImage()
    db = db_module.DBPostProcess(
        thresh=PROFILE["bin_thresh"],
        box_thresh=PROFILE["box_thresh"],
        max_candidates=PROFILE["max_candidates"],
        unclip_ratio=PROFILE["unclip_ratio"],
        use_dilation=PROFILE["use_dilation"],
        score_mode=PROFILE["score_mode"],
        box_type=PROFILE["box_type"],
    )
    detector_geometry = detector_geometry_type()
    recognizer_preprocess = recognizer_preprocess_type()
    recognizer_preprocess.rec_image_shape = [
        3,
        PROFILE["rec_height"],
        PROFILE["rec_base_width"],
    ]
    recognizer_preprocess.rec_algorithm = "SVTR_LCNet"
    recognizer_preprocess.use_onnx = True
    recognizer_preprocess.input_tensor = types.SimpleNamespace(
        shape=["DynamicDimension.0", 3, 48, "DynamicDimension.1"]
    )
    decoder = rec_module.CTCLabelDecode(
        character_dict_path=str(root / "models/rec/charset.txt"), use_space_char=True
    )

    det_session = make_session(root / "models/det/inference.onnx")
    rec_session = make_session(root / "models/rec/inference.onnx")
    verify_model_config(root, rec_session)
    det_input_name = det_session.get_inputs()[0].name
    rec_input_name = rec_session.get_inputs()[0].name

    manifest = json.loads((root / "corpus/manifest.json").read_text(encoding="utf-8"))
    write_readme(root, golden, len(manifest))

    for image_number, record in enumerate(manifest, start=1):
        image_id = record["id"]
        output_dir = (golden / image_id).resolve()
        if output_dir.exists():
            if not output_dir.is_relative_to(golden.resolve()):
                raise RuntimeError(f"refusing to clear path outside golden: {output_dir}")
            shutil.rmtree(output_dir)
        output_dir.mkdir(parents=True)

        encoded = (root / "corpus" / record["file"]).read_bytes()
        decoded_data = decode({"image": encoded})
        if decoded_data is None:
            raise RuntimeError(f"could not decode corpus image {image_id}")
        image = decoded_data["image"]
        np.save(output_dir / "01_decoded.npy", image)

        resized_data = resize({"image": image.copy()})
        resized = resized_data["image"]
        shape = resized_data["shape"]
        normalized = normalize({"image": resized})["image"]
        det_input = np.expand_dims(chw({"image": normalized})["image"], 0).copy()
        np.save(output_dir / "02_det_input.npy", det_input)
        write_json(
            output_dir / "02_det_meta.json",
            {
                "src_h": int(shape[0]),
                "src_w": int(shape[1]),
                "ratio_h": float(shape[2]),
                "ratio_w": float(shape[3]),
                "resize_h": int(det_input.shape[2]),
                "resize_w": int(det_input.shape[3]),
            },
        )

        det_map = det_session.run(None, {det_input_name: det_input})[0]
        np.save(output_dir / "03_det_map.npy", det_map)
        pred = det_map[0, 0]
        bitmap = pred > PROFILE["bin_thresh"]
        raw_boxes, raw_scores = db.boxes_from_bitmap(
            pred, bitmap, int(shape[1]), int(shape[0])
        )
        write_json(
            output_dir / "04_boxes_raw.json",
            {"boxes": raw_boxes, "scores": raw_scores},
        )

        final_boxes = detector_geometry.filter_tag_det_res(raw_boxes, image.shape)
        write_json(output_dir / "05_boxes_final.json", {"boxes": final_boxes})
        ordered_boxes = sorted_boxes(final_boxes)
        write_json(output_dir / "06_boxes_sorted.json", {"boxes": ordered_boxes})

        crops_dir = output_dir / "07_crops"
        crops_dir.mkdir()
        crops: list[np.ndarray] = []
        for crop_index, box in enumerate(ordered_boxes):
            crop = get_rotate_crop_image(image, np.array(box, dtype=np.float32))
            crops.append(crop)
            ok, encoded_crop = cv2.imencode(".png", crop)
            if not ok:
                raise RuntimeError(f"could not encode crop {image_id}/{crop_index}")
            (crops_dir / f"{crop_index:03d}.png").write_bytes(encoded_crop.tobytes())

        rec_results: list[tuple[str, float]] = [("", 0.0)] * len(crops)
        if crops:
            widths = np.array(
                [crop.shape[1] / float(crop.shape[0]) for crop in crops], np.float64
            )
            indices = np.argsort(widths)
            batch_size = PROFILE["rec_batch_size"]
            for batch_number, begin in enumerate(range(0, len(crops), batch_size)):
                end = min(len(crops), begin + batch_size)
                batch_indices = indices[begin:end]
                max_wh_ratio = PROFILE["rec_base_width"] / PROFILE["rec_height"]
                for crop_index in batch_indices:
                    crop = crops[int(crop_index)]
                    max_wh_ratio = max(
                        max_wh_ratio, crop.shape[1] / float(crop.shape[0])
                    )
                batch = np.concatenate(
                    [
                        recognizer_preprocess.resize_norm_img(
                            crops[int(crop_index)], max_wh_ratio
                        )[np.newaxis, :]
                        for crop_index in batch_indices
                    ]
                ).copy()
                stem = f"{batch_number:03d}"
                np.save(output_dir / f"08_rec_input_{stem}.npy", batch)
                write_json(
                    output_dir / f"08_rec_input_{stem}_indices.json",
                    [int(index) for index in batch_indices],
                )
                logits = rec_session.run(None, {rec_input_name: batch})[0]
                np.save(output_dir / f"09_rec_logits_{stem}.npy", logits)
                decoded = decoder(logits)
                for batch_offset, result in enumerate(decoded):
                    rec_results[int(batch_indices[batch_offset])] = (
                        result[0],
                        float(result[1]),
                    )

        result = []
        for box, (recognized_text, confidence) in zip(ordered_boxes, rec_results):
            if confidence >= PROFILE["drop_score"]:
                result.append(
                    {
                        "quad": box,
                        "text": recognized_text,
                        "confidence": confidence,
                    }
                )
        write_json(output_dir / "10_final.json", result)
        print(
            f"[{image_number:02d}/{len(manifest)}] {image_id}: "
            f"{len(raw_boxes)} raw, {len(final_boxes)} final, {len(result)} retained"
        )


if __name__ == "__main__":
    main()
