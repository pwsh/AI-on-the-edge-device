#!/usr/bin/env python3
"""Measure how well any .tflite model reads YOUR labelled images.

Runs the model exactly like the firmware (same resize and raw 0..255 float input) and
reports the same metrics as train.py. Use it to compare the stock model with a newly
trained one before deploying:

    python evaluate.py ../../sd-card/config/dig-class11_1910_s2_q.tflite data/labeled/dig-class11
    python evaluate.py output/dig-class11_261005_s2/dig-class11_261005_s2_q.tflite \\
        data/labeled/dig-class11 --out eval-new

The model type (and therefore how the file name labels are read) is detected from the
model's output size, like the firmware does.
"""

from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from common import (DEFAULT_RESIZE, RESIZE_METHODS, TFLiteModel,  # noqa: E402
                    check_firmware_contract, compute_metrics, die,
                    fmt_duration, headline, load_images, load_labeled, log, print_confusions,
                    warn, write_false_predictions)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Evaluate a .tflite model on labelled images (firmware-identical).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("model", help=".tflite model")
    ap.add_argument("data", nargs="+", help="labelled image folders (<label>_<anything>.jpg)")
    ap.add_argument("--out", help="folder for false_predictions.csv and metrics.json "
                                  "(default: eval/<model name>)")
    ap.add_argument("--resize", choices=RESIZE_METHODS, default=DEFAULT_RESIZE,
                    help="how crops are scaled to the model input: 'firmware' = exactly what the "
                         "device does (default); 'mitchellcubic' = upstream training notebooks")
    args = ap.parse_args()

    t0 = time.time()
    model = TFLiteModel(args.model)
    mt = model.type
    log(f"Model {Path(args.model).name}: firmware treats it as {mt.name} "
        f"(input {mt.height}x{mt.width}, {mt.outputs} outputs)")
    for p in check_firmware_contract(args.model):
        warn(f"firmware contract: {p}")

    files, labels, skipped = load_labeled(args.data, mt.kind)
    if skipped:
        log(f"{len(skipped)} files without a valid {mt.kind} label skipped")
    if not files:
        die("no labelled images found")
    log(f"{len(files)} images, resize method: {args.resize}")
    x = load_images(files, mt.height, mt.width, resize=args.resize, progress="loading")
    preds, confs = model.predict(x, progress="predicting")
    metrics = compute_metrics(mt.kind, labels, preds)

    out = Path(args.out or f"eval/{Path(args.model).stem}")
    out.mkdir(parents=True, exist_ok=True)
    n_false = write_false_predictions(out / "false_predictions.csv", files, labels, preds,
                                      mt.kind, confs)
    metrics.update({"model": str(args.model), "type": mt.name, "data": args.data,
                    "resize": args.resize})
    (out / "metrics.json").write_text(json.dumps(metrics, indent=1) + "\n")

    log(f"\n{Path(args.model).name}: {headline(mt.kind, metrics)}")
    print_confusions(mt.kind, metrics)
    log(f"{n_false} wrong predictions -> {out / 'false_predictions.csv'}  "
        f"({fmt_duration(time.time() - t0)})")


if __name__ == "__main__":
    main()
