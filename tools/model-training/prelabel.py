#!/usr/bin/env python3
"""Pre-label crops with an existing model so you only have to *check* labels.

Runs a .tflite model exactly like the firmware (Mitchell-cubic resize to the model
input, raw 0..255 float pixels) over a folder of crops and sorts the images into

    data/review/<type>/confident/<pred>_c<NN>_<hash>.jpg   (confidence >= --threshold)
    data/review/<type>/unsure/<pred>_c<NN>_<hash>.jpg
    data/review/<type>/predictions.csv                     (file, label, confidence, model, source)

The file is renamed to its content hash (first 16 hex digits of the SHA-1) - the
privacy convention of the upstream community image collections - so neither the
date nor the meter position leaks. Then open label_tool.py to confirm/correct.

--dedupe drops near-duplicates (perceptual hash distance <= 2, compared only among
images with the same predicted label): a meter that sits still for hours produces
thousands of identical crops, which is the most common dataset problem.

Examples:
    python prelabel.py --model ../../sd-card/config/dig-class11_1910_s2_q.tflite \\
        --input data/crops/digits --dedupe
    python prelabel.py --model ana-cont_1500_s2_q.tflite --input data/device --move
"""

from __future__ import annotations

import argparse
import csv
import shutil
import time
from pathlib import Path

from common import (DEFAULT_RESIZE, RESIZE_METHODS, TFLiteModel, dedupe_indices, die, file_sha1,
                    fmt_duration, format_label, iter_images, load_images, log, phash_images)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Predict labels for unlabelled crops and sort them for review.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--model", required=True, help=".tflite model used for pre-labelling")
    ap.add_argument("--input", nargs="+", required=True, help="crop files or folders (recursive)")
    ap.add_argument("--out", default="data/review",
                    help="output base folder (a sub-folder per model type is created)")
    ap.add_argument("--threshold", type=float, default=0.9,
                    help="confidence at or above which an image goes to confident/")
    ap.add_argument("--dedupe", action="store_true", help="drop near-duplicate images")
    ap.add_argument("--dedupe-distance", type=int, default=2,
                    help="max perceptual-hash distance that counts as duplicate")
    ap.add_argument("--move", action="store_true",
                    help="move the images instead of copying them")
    ap.add_argument("--limit", type=int, default=0, help="only process the first N images")
    ap.add_argument("--resize", choices=RESIZE_METHODS, default=DEFAULT_RESIZE,
                    help="how crops are scaled to the model input: 'firmware' = exactly what the "
                         "device does (default); 'mitchellcubic' = upstream training notebooks")
    args = ap.parse_args()

    t0 = time.time()
    model = TFLiteModel(args.model)
    mt = model.type
    log(f"Model {Path(args.model).name}: {mt.name} (input {mt.height}x{mt.width}, "
        f"{mt.outputs} outputs)")

    files = list(iter_images(args.input))
    if args.limit:
        files = files[:args.limit]
    if not files:
        die("no images found")

    log(f"{len(files)} images, resize method: {args.resize}")
    images = load_images(files, mt.height, mt.width, resize=args.resize, progress="loading")
    values, confs = model.predict(images, progress="predicting")
    del images
    labels = [format_label(v, mt.kind) for v in values]

    keep = list(range(len(files)))
    if args.dedupe:
        hashes = phash_images(files, progress="hashing")
        keep = dedupe_indices(hashes, groups=labels, max_dist=args.dedupe_distance)
        n_dup = len(files) - len(keep)
        log(f"Dedupe: {n_dup} of {len(files)} images are near-duplicates "
            f"({100 * n_dup / len(files):.1f}%); {len(keep)} unique images kept")

    out = Path(args.out) / mt.name
    for sub in ("confident", "unsure"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    csv_path = out / "predictions.csv"
    new_csv = not csv_path.exists()
    counts = {"confident": 0, "unsure": 0, "exists": 0}
    per_label: dict[str, int] = {}
    with open(csv_path, "a", newline="") as fcsv:
        w = csv.writer(fcsv, lineterminator="\n")
        if new_csv:
            w.writerow(["file", "label", "confidence", "model", "source"])
        for i in keep:
            src = files[i]
            sub = "confident" if confs[i] >= args.threshold else "unsure"
            pct = min(99, int(confs[i] * 100))
            dest = out / sub / f"{labels[i]}_c{pct:02d}_{file_sha1(src)}{src.suffix.lower()}"
            if dest.exists():
                counts["exists"] += 1
                continue
            (shutil.move if args.move else shutil.copy2)(src, dest)
            counts[sub] += 1
            per_label[labels[i]] = per_label.get(labels[i], 0) + 1
            w.writerow([str(dest.relative_to(out)), labels[i], f"{confs[i]:.4f}",
                        Path(args.model).name, str(src)])

    log(f"\n{counts['confident']} confident (>= {args.threshold}), {counts['unsure']} unsure, "
        f"{counts['exists']} already in {out}/")
    if per_label:
        log("Predicted label distribution: " +
            ", ".join(f"{k}: {v}" for k, v in sorted(per_label.items())))
    log(f"Predictions appended to {csv_path}")
    log(f"Next: python label_tool.py --dir {out}    ({fmt_duration(time.time() - t0)})")


if __name__ == "__main__":
    main()
