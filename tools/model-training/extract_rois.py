#!/usr/bin/env python3
"""Cut the ROIs (digits / analog dials) out of raw full camera frames.

For people who only have raw frames (/log/source/raw/<YYYYMMDD>/<HH>/raw_<ts>.jpg)
instead of ROI images logged by the device. The ROI coordinates are read from your
config.ini ([Digits] / [Analog] sections); every ROI of every frame is saved as

    data/crops/digits/<number>_<roi>/unlabeled_<number>_<roi>_<timestamp>.jpg
    data/crops/analog/<number>_<roi>/unlabeled_<number>_<roi>_<timestamp>.jpg

IMPORTANT: the device draws ROIs on the *aligned* image (after rotation and the
alignment step). Raw frames are taken *before* alignment, so the crops are only
correct when alignment is disabled (AlignmentAlgo = off) and no rotation/flip is
configured. Otherwise the script refuses to run unless --force is given.

Examples:
    python extract_rois.py --config config.ini --raw data/device/192.168.1.50/log/source/raw
    python extract_rois.py --device 192.168.1.50 --raw ~/meter-raw --limit 1500
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image

from common import (Device, Progress, config_rois, config_value, die, iter_images, log,
                    parse_config, warn)

SECTIONS = {"Digits": "digits", "Analog": "analog"}


def timestamp_of(path: Path) -> str:
    """raw_20261004-131502.jpg -> 20261004-131502 (falls back to the file stem)."""
    m = re.search(r"(\d{8}-\d{6})", path.name)
    return m.group(1) if m else path.stem


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Crop the ROIs defined in config.ini out of raw full frames.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--config", help="path to a local config.ini")
    src.add_argument("--device", help="read config.ini from this device (read-only)")
    ap.add_argument("--raw", nargs="+", required=True,
                    help="raw frame files or folders (searched recursively)")
    ap.add_argument("--out", default="data/crops", help="output base folder")
    ap.add_argument("--sections", nargs="+", default=list(SECTIONS), choices=list(SECTIONS),
                    help="which config sections to extract")
    ap.add_argument("--limit", type=int, default=0,
                    help="only use the first N frames (0 = all)")
    ap.add_argument("--every", type=int, default=1,
                    help="only use every N-th frame (consecutive frames are near-identical)")
    ap.add_argument("--quality", type=int, default=95, help="JPEG quality of the crops")
    ap.add_argument("--force", action="store_true",
                    help="extract even though alignment/rotation is enabled (crops may be off)")
    args = ap.parse_args()

    if args.device:
        text = Device(args.device).get_text("/fileserver/config/config.ini")
    else:
        text = Path(args.config).read_text(errors="replace")
    sections = parse_config(text)

    # --- the alignment check (raw frames are pre-alignment) -------------------
    problems = []
    algo = (config_value(sections, "Alignment", "AlignmentAlgo") or "default").lower()
    if algo != "off":
        problems.append(f"AlignmentAlgo = {algo} (not 'off')")
    rot = config_value(sections, "Alignment", "InitialRotate")
    try:
        if rot and float(rot) != 0.0:
            problems.append(f"InitialRotate = {rot}")
    except ValueError:
        pass
    flip = config_value(sections, "Alignment", "FlipImageSize")
    if flip and flip.lower() in ("true", "1"):
        problems.append(f"FlipImageSize = {flip}")
    for line in sections.get("Alignment", {}).get("lines", []):
        if line.split("=")[0].split()[:1] == ["Crop"] and len(line.split()) > 3:
            problems.append(f"crop is enabled ({line})")
    if problems:
        bar = "!" * 78
        print(f"\n{bar}\nWARNING: the ROI coordinates in config.ini refer to the ALIGNED image, but\n"
              f"raw frames are taken BEFORE rotation/alignment. Your config has:\n  - " +
              "\n  - ".join(problems) +
              "\nThe crops will be shifted/rotated relative to what the device reads and are\n"
              f"probably useless for training. Prefer ROI images logged by the device\n"
              f"(fetch_images.py).\n{bar}\n", file=sys.stderr)
        if not args.force:
            die("refusing to continue without --force")

    jobs = []
    for sec in args.sections:
        if sec in sections and not sections[sec]["enabled"]:
            log(f"[{sec}] is disabled in config.ini - skipped")
            continue
        rois = config_rois(sections, sec)
        if sec == "Analog":
            for r in rois:
                if r.w != r.h:
                    warn(f"analog ROI {r.number}.{r.name} is not square ({r.w}x{r.h}); "
                         "the device expects square analog ROIs")
        log(f"[{sec}] {len(rois)} ROIs: " + ", ".join(f"{r.number}.{r.name}" for r in rois))
        jobs += [(SECTIONS[sec], r) for r in rois]
    if not jobs:
        die("no ROIs found in config.ini")

    frames = [f for f in iter_images(args.raw) if f.name.lower().startswith("raw")] or \
        list(iter_images(args.raw))
    frames = frames[::max(1, args.every)]
    if args.limit:
        frames = frames[:args.limit]
    if not frames:
        die("no raw frames found")
    log(f"{len(frames)} frames x {len(jobs)} ROIs = {len(frames) * len(jobs)} crops -> {args.out}")

    out = Path(args.out)
    for kind, r in jobs:
        (out / kind / f"{r.number}_{r.name}").mkdir(parents=True, exist_ok=True)

    written = skipped = 0
    prog = Progress(len(frames), "frames")
    for f in frames:
        ts = timestamp_of(f)
        try:
            with Image.open(f) as im:
                im = im.convert("RGB")
                W, H = im.size
                for kind, r in jobs:
                    dest = out / kind / f"{r.number}_{r.name}" / \
                        f"unlabeled_{r.number}_{r.name}_{ts}.jpg"
                    if dest.exists():
                        skipped += 1
                        continue
                    if r.x < 0 or r.y < 0 or r.x + r.w > W or r.y + r.h > H:
                        warn(f"ROI {r.number}.{r.name} is outside the {W}x{H} frame {f.name}")
                        continue
                    im.crop((r.x, r.y, r.x + r.w, r.y + r.h)).save(dest, quality=args.quality)
                    written += 1
        except OSError as e:
            warn(f"cannot read {f}: {e}")
        prog.update()
    prog.done()
    log(f"Wrote {written} crops ({skipped} already existed) to {out}/")


if __name__ == "__main__":
    main()
