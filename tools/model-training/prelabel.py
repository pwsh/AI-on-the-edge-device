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

Images that were already handled are skipped (compared by content hash, so a new
model with different predictions does not bring them back): anything in
data/labeled/<type>/, anything still under data/review/<type>/, and anything deleted
in label_tool.py (data/review/<type>/_trash/). --no-skip-handled turns this off.

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
                    fmt_duration, format_label, hashes_in, iter_images, load_images, log,
                    phash_images)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Predict labels for unlabelled crops and sort them for review.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--model", required=True, help=".tflite model used for pre-labelling")
    ap.add_argument("--input", nargs="+", required=True, help="crop files or folders (recursive)")
    ap.add_argument("--out", default="data/review",
                    help="output base folder (a sub-folder per model type is created)")
    ap.add_argument("--labeled", default="data/labeled",
                    help="labelled images base folder (label_tool.py --out); images already "
                         "in <labeled>/<type>/ are skipped")
    ap.add_argument("--no-skip-handled", action="store_true",
                    help="also process images that are already labelled, in review or deleted")
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

    out = Path(args.out) / mt.name
    skipped = {"labeled": 0, "review": 0, "deleted": 0}
    sha = [file_sha1(f) for f in files]
    if not args.no_skip_handled:
        labeled = hashes_in(Path(args.labeled) / mt.name)
        deleted = hashes_in(out / "_trash")
        review = hashes_in(out, exclude_trash=True)
        keep_files = []
        for f, h in zip(files, sha):
            why = ("labeled" if h in labeled else "deleted" if h in deleted
                   else "review" if h in review else None)
            if why:
                skipped[why] += 1
            else:
                keep_files.append((f, h))
        files, sha = [f for f, _ in keep_files], [h for _, h in keep_files]
        log(f"{len(files)} new, {sum(skipped.values())} already handled "
            f"({skipped['labeled']} labelled, {skipped['review']} in review, "
            f"{skipped['deleted']} deleted)")
        if not files:
            log(f"\nNothing new to pre-label: {skipped['labeled']} already labelled, "
                f"{skipped['review']} already in review, {skipped['deleted']} deleted earlier "
                f"({fmt_duration(time.time() - t0)})")
            return

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
            dest = out / sub / f"{labels[i]}_c{pct:02d}_{sha[i]}{src.suffix.lower()}"
            if dest.exists():
                counts["exists"] += 1
                continue
            (shutil.move if args.move else shutil.copy2)(src, dest)
            counts[sub] += 1
            per_label[labels[i]] = per_label.get(labels[i], 0) + 1
            w.writerow([str(dest.relative_to(out)), labels[i], f"{confs[i]:.4f}",
                        Path(args.model).name, str(src)])

    log(f"\n{counts['confident']} confident (>= {args.threshold}), {counts['unsure']} unsure, "
        f"{counts['exists']} already at the same name, {skipped['labeled']} already labelled, "
        f"{skipped['review']} already in review, {skipped['deleted']} deleted earlier")
    if per_label:
        log("Predicted label distribution: " +
            ", ".join(f"{k}: {v}" for k, v in sorted(per_label.items())))
    log(f"Predictions appended to {csv_path}")
    log(f"Next: python label_tool.py --dir {out}    ({fmt_duration(time.time() - t0)})")


if __name__ == "__main__":
    main()
