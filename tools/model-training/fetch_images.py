#!/usr/bin/env python3
"""Download the images your device has logged (ROI crops and, optionally, raw frames).

Incremental: files that already exist locally (size > 0) are skipped, so you can run
this again every few days and only new images are transferred. The device folder
structure is preserved below data/device/<device>/ , e.g.

    data/device/10.0.42.12/log/digit/20261004/13/7_c98_main_main3_20261004-131502.jpg

Enable image logging on the device first (config page: [Digits]/[Analog] ->
ROIImagesLocation = /log/digit or /log/analog, ROIImagesRetention > 0).

Examples:
    python fetch_images.py --device 192.168.1.50
    python fetch_images.py --device 192.168.1.50 --raw --pause
    python fetch_images.py --device 192.168.1.50 --folders /log/digit --zip
"""

from __future__ import annotations

import argparse
import io
import os
import sys
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from common import Device, Progress, die, fmt_duration, log, warn

DEFAULT_FOLDERS = ["/log/digit", "/log/analog"]
RAW_FOLDER = "/log/source/raw"


def local_path(root: Path, device_path: str) -> Path:
    return root / device_path.lstrip("/")


def have(path: Path) -> bool:
    try:
        return path.stat().st_size > 0
    except OSError:
        return False


def fetch_per_file(dev: Device, folder: str, root: Path, workers: int) -> tuple[int, int, int]:
    """List the folder recursively and download every missing file."""
    log(f"Listing {folder} ...")
    try:
        files = list(dev.walk(folder))
    except RuntimeError as e:
        warn(f"cannot list {folder}: {e}")
        return 0, 0, 1
    todo = [f for f in files if not have(local_path(root, f))]
    log(f"{folder}: {len(files)} files on the device, {len(files) - len(todo)} already here, "
        f"{len(todo)} to download")
    if not todo:
        return 0, len(files), 0
    done = failed = 0
    nbytes = 0
    prog = Progress(len(todo), "  downloading")
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {pool.submit(dev.download, f, local_path(root, f)): f for f in todo}
        try:
            for fut in as_completed(futs):
                try:
                    nbytes += fut.result()
                    done += 1
                except RuntimeError as e:
                    failed += 1
                    warn(f"\n{futs[fut]}: {e}")
                prog.update(extra=f"{nbytes / 1e6:.1f} MB")
        except KeyboardInterrupt:
            pool.shutdown(wait=False, cancel_futures=True)
            raise
    prog.done(f"{nbytes / 1e6:.1f} MB")
    return done, len(files) - len(todo), failed


def fetch_zip(dev: Device, folder: str, root: Path) -> tuple[int, int, int] | None:
    """Download the folder as one ZIP (newer firmware) and extract the missing files.

    Returns None when the device does not support ?zip=1, so the caller can fall back.
    The device builds the archive on its SD card first; that can take a while for big
    folders, hence the generous timeout.
    """
    log(f"{folder}: requesting ZIP (the device builds it first, please wait) ...")
    try:
        r = dev.request("GET", "/fileserver" + folder.rstrip("/") + "/?zip=1",
                        stream=True, timeout=600)
    except RuntimeError as e:
        warn(f"ZIP download not available ({e}); falling back to per-file download")
        return None
    if "zip" not in r.headers.get("Content-Type", ""):
        warn("device did not answer with a ZIP (old firmware?); falling back to per-file download")
        return None
    buf = io.BytesIO()
    t0 = time.time()
    for chunk in r.iter_content(1 << 16):
        buf.write(chunk)
        sys.stdout.write(f"\r  received {buf.tell() / 1e6:.1f} MB "
                         f"({buf.tell() / 1e6 / max(time.time() - t0, 1e-3):.2f} MB/s)")
        sys.stdout.flush()
    sys.stdout.write("\n")
    try:
        zf = zipfile.ZipFile(buf)
    except zipfile.BadZipFile:
        warn("received a broken ZIP; falling back to per-file download")
        return None
    new = skipped = 0
    for info in zf.infolist():
        if info.is_dir():
            continue
        name = info.filename.replace("\\", "/")
        if name.startswith("/") or ".." in name.split("/"):
            warn(f"ignoring suspicious path in ZIP: {name}")
            continue
        dest = local_path(root, folder.rstrip("/") + "/" + name)
        if have(dest):
            skipped += 1
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_name(dest.name + ".part")
        with zf.open(info) as src, open(tmp, "wb") as out:
            out.write(src.read())
        os.replace(tmp, dest)
        new += 1
    log(f"{folder}: {new} new files extracted, {skipped} already here")
    return new, skipped, 0


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Incrementally download logged images from an AI-on-the-edge device.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--device", required=True, help="device IP address or hostname")
    ap.add_argument("--name", help="local folder name for this device (default: the address)")
    ap.add_argument("--folders", nargs="+", default=DEFAULT_FOLDERS,
                    help="device folders to download")
    ap.add_argument("--raw", action="store_true",
                    help=f"also download raw full frames from {RAW_FOLDER} (large!)")
    ap.add_argument("--out", default="data/device", help="local base folder")
    ap.add_argument("--pause", action="store_true",
                    help="pause the device's processing during the transfer (always resumed "
                         "afterwards, also on Ctrl-C); makes transfers faster and more reliable")
    ap.add_argument("--zip", action="store_true",
                    help="download each folder as one ZIP (?zip=1, newer firmware); falls back "
                         "to per-file download if unsupported")
    ap.add_argument("--workers", type=int, default=2,
                    help="parallel downloads (the device web server is small; keep this low)")
    ap.add_argument("--retries", type=int, default=6, help="retries per request (with backoff)")
    ap.add_argument("-v", "--verbose", action="store_true", help="show every retry")
    args = ap.parse_args()

    dev = Device(args.device, retries=args.retries, verbose=args.verbose)
    root = Path(args.out) / (args.name or dev.name)
    folders = list(args.folders) + ([RAW_FOLDER] if args.raw else [])
    log(f"Device {dev.base} -> {root}")

    t0 = time.time()
    totals = [0, 0, 0]
    try:
        with dev.paused(args.pause):
            for folder in folders:
                folder = "/" + folder.strip("/")
                res = fetch_zip(dev, folder, root) if args.zip else None
                if res is None:
                    res = fetch_per_file(dev, folder, root, max(1, args.workers))
                totals = [a + b for a, b in zip(totals, res)]
    except KeyboardInterrupt:
        die("interrupted (already downloaded files are kept; run again to continue)", 130)
    except RuntimeError as e:
        die(str(e))
    log(f"Done in {fmt_duration(time.time() - t0)}: {totals[0]} downloaded, "
        f"{totals[1]} already present, {totals[2]} failed.")
    if totals[2]:
        sys.exit(1)


if __name__ == "__main__":
    main()
