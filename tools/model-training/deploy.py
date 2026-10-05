#!/usr/bin/env python3
"""Upload a trained model to the device and (optionally) switch config.ini to it.

    python deploy.py output/dig-class11_261005_s2/dig-class11_261005_s2_q.tflite \\
        --device 192.168.1.50 --activate

Steps:
  1. checks the model against the firmware contract (refuses a broken model),
  2. pauses processing and uploads the model to /config/<file>; the file is read back
     and compared byte-for-byte (size + MD5), with up to 3 attempts,
  3. with --activate:
     a. downloads config.ini and builds the new version in memory (only the
        "Model =" line of the [Digits] or [Analog] section changes) and sanity-checks
        it (non-empty, all section headers kept, new Model line present, size within
        +-20% of the original),
     b. writes a timestamped backup of the original to backups/ (fsync'ed),
     c. deletes and re-uploads config.ini (the device cannot overwrite files), reads it
        back and compares byte-for-byte; up to 3 attempts. If that never succeeds the
        ORIGINAL is uploaded again and the script aborts - it never reboots then,
     d. only after a clean compare: offers to reboot (the new model is loaded at boot;
        --yes skips the question).

Without --activate you can select the model on the device's configuration page.
"""

from __future__ import annotations

import argparse
import os
import re
import time
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from common import (Device, TFLiteModel, check_firmware_contract, die, log,  # noqa: E402
                    parse_config, replace_model_line, warn)

CONFIG = "/config/config.ini"


def section_headers(text: str) -> list[str]:
    return re.findall(r"^;?\[\w+\]", text, flags=re.M)


def sanity_check_config(old: str, new: str, section: str, remote: str) -> list[str]:
    """Problems that make the new config.ini unsafe to upload (empty list = OK)."""
    problems = []
    if not new.strip():
        problems.append("new config.ini is empty")
    if section_headers(new) != section_headers(old):
        problems.append("section headers differ from the original")
    if f"[{section}]" not in new:
        problems.append(f"section [{section}] missing")
    if not re.search(rf"^Model = {re.escape(remote)}\r?$", new, flags=re.M):
        problems.append(f"'Model = {remote}' line missing")
    if old and not (0.8 * len(old) <= len(new) <= 1.2 * len(old)):
        problems.append(f"size changed too much ({len(old)} -> {len(new)} bytes)")
    return problems


def write_backup(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    if path.stat().st_size != len(data):
        die(f"backup {path} was not written completely - aborting before touching the device")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Upload a .tflite model to an AI-on-the-edge device.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("model", help=".tflite model to upload")
    ap.add_argument("--device", required=True, help="device IP address or hostname")
    ap.add_argument("--section", choices=["Digits", "Analog"],
                    help="config.ini section to switch (default: from the model type)")
    ap.add_argument("--activate", action="store_true",
                    help="also point the section's 'Model =' line at the uploaded file")
    ap.add_argument("--yes", action="store_true", help="reboot without asking")
    ap.add_argument("--no-reboot", action="store_true", help="never reboot")
    ap.add_argument("--backup-dir", default="backups", help="where config.ini backups go")
    ap.add_argument("--skip-check", action="store_true",
                    help="upload even if the firmware contract check fails (not recommended)")
    args = ap.parse_args()

    model = Path(args.model)
    if not model.is_file() or model.stat().st_size == 0:
        die(f"{model} does not exist or is empty")
    problems = check_firmware_contract(model)
    for p in problems:
        warn(f"firmware contract: {p}")
    if problems and not args.skip_check:
        die("this model would not work on the device (use --skip-check to force)")

    mt = TFLiteModel(model).type
    section = args.section or mt.section
    if section != mt.section:
        warn(f"{mt.name} model is normally used in [{mt.section}], not [{section}]")
    log(f"{model.name}: {mt.name}, {model.stat().st_size / 1024:.0f} KiB -> [{section}]")

    dev = Device(args.device)
    remote = f"/config/{model.name}"
    config_changed = False
    with dev.paused():
        log(f"Uploading to {remote} ...")
        if not dev.upload(model, remote):
            die(f"could not upload and verify {remote}; config.ini was not touched")

        if args.activate:
            # latin-1 round-trips every byte, so only the Model line changes (line endings
            # and any non-ASCII characters are preserved).
            orig = dev.fetch_bytes(CONFIG)
            if not orig.strip():
                die("the device's config.ini is empty - fix that first (not touching it)")
            text = orig.decode("latin-1")
            sec = parse_config(text).get(section)
            if sec is not None and not sec["enabled"]:
                warn(f"[{section}] is disabled in config.ini (';[{section}]'); the model line is "
                     "changed anyway, but the device will not use it until you enable the section")
            try:
                new_text = replace_model_line(text, section, remote)
            except ValueError as e:
                die(f"{e}; the model was uploaded but config.ini is unchanged")
            new = new_text.encode("latin-1")
            if new == orig:
                log(f"config.ini already uses {remote}")
            else:
                bad = sanity_check_config(text, new_text, section, remote)
                if bad:
                    die("new config.ini failed the sanity check (" + "; ".join(bad) +
                        "); config.ini unchanged")
                stamp = time.strftime("%Y%m%d-%H%M%S")
                backup = Path(args.backup_dir) / f"config_{dev.name}_{stamp}.ini"
                write_backup(backup, orig)
                log(f"Backed up config.ini to {backup}")
                if dev.upload_verified(new, CONFIG, attempts=3):
                    log(f"config.ini: [{section}] Model = {remote}")
                    config_changed = True
                else:
                    warn("could not write the new config.ini - restoring the original")
                    if dev.upload_verified(orig, CONFIG, attempts=3):
                        die("config.ini upload failed; the ORIGINAL config.ini was restored "
                            "and verified. Device not rebooted.")
                    die("config.ini upload failed AND restoring the original failed!\n"
                        "  Do NOT reboot the device (it would start in setup mode). Upload\n"
                        f"  {backup.resolve()}\n  as /config/config.ini with the device's "
                        "file server as soon as possible.")

    if not args.activate:
        log("Uploaded. Select it on the device's configuration page, or rerun with --activate.")
        return
    if not config_changed:
        return
    if args.no_reboot:
        log("Reboot the device to load the new model.")
        return
    if not args.yes:
        ans = input("Reboot the device now to load the new model? [y/N] ").strip().lower()
        if ans not in ("y", "yes"):
            log("Not rebooted. The new model is used after the next reboot.")
            return
    dev.reboot()
    log("Reboot requested. Check the device's Overview page in a minute.")


if __name__ == "__main__":
    main()
