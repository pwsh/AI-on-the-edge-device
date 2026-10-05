#!/usr/bin/env python3
"""Show why the resize method matters, and self-check the firmware resize port.

The device scales every ROI crop to the model input (20x32 digits, 32x32 dials) with
its own 2-tap bilinear downscale (CImageBasis::Resize). A model should be trained on
crops scaled the same way. This script resizes one crop with

    firmware        common.firmware_resize (numpy port, used by default everywhere)
    reference       a plain scalar re-implementation of the C code with exactly
                    rounded fused multiply-adds (slow; proves the port is bit-exact)
    no-FMA          the same C code without fused multiply-add (what you would get
                    from a naive port; shows how small the FMA effect is)
    tf bilinear     tf.image.resize(method="bilinear"), rounded and unrounded
    mitchellcubic   tf.image.resize(method="mitchellcubic"), as upstream training does

and prints how many pixels differ from the firmware result and by how much. With
--model it also shows what the model reads for each variant.

    python resize_check.py data/review/dig-class11/confident/3_c99_0123456789abcdef.jpg
    python resize_check.py crop.jpg --size 32x32 --model ana-cont_1500_s2_q.tflite
"""

from __future__ import annotations

import argparse
import math
import os
from fractions import Fraction

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

from common import firmware_resize  # noqa: E402


def _round_f32(q: Fraction) -> np.float32:
    """Round an exact rational to the nearest float32 (ties to even)."""
    c = np.float32(float(q))
    best = None
    for cand in (np.nextafter(c, np.float32(-np.inf)), c, np.nextafter(c, np.float32(np.inf))):
        err = abs(Fraction(float(cand)) - q)
        key = (err, int(np.array(cand, np.float32).view(np.uint32)) & 1)
        if best is None or key < best[0]:
            best = (key, cand)
    return np.float32(best[1])


def _f32(x) -> np.float32:
    return np.float32(x)


def reference_resize(img, new_w: int, new_h: int, fused: bool = True):
    """Scalar transcription of the C loop (slow). fused=False: separate mul + add."""
    sh, sw, ch = img.shape

    def mad(a, b, c):        # a * b + c
        if fused:
            return _round_f32(Fraction(float(a)) * Fraction(float(b)) + Fraction(float(c)))
        return _f32(_f32(_f32(a) * _f32(b)) + _f32(c))

    scale_x = _f32(_f32(sw) / _f32(new_w))
    scale_y = _f32(_f32(sh) / _f32(new_h))
    out = np.zeros((new_h, new_w, ch), np.uint8)
    for dy in range(new_h):
        sy = mad(_f32(_f32(dy) + _f32(0.5)), scale_y, _f32(-0.5))
        y0 = math.floor(sy)
        fy = _f32(sy - _f32(y0))
        y0c, y1c = min(max(y0, 0), sh - 1), min(max(y0 + 1, 0), sh - 1)
        for dx in range(new_w):
            sx = mad(_f32(_f32(dx) + _f32(0.5)), scale_x, _f32(-0.5))
            x0 = math.floor(sx)
            fx = _f32(sx - _f32(x0))
            x0c, x1c = min(max(x0, 0), sw - 1), min(max(x0 + 1, 0), sw - 1)
            for c in range(ch):
                p00, p01 = _f32(img[y0c, x0c, c]), _f32(img[y0c, x1c, c])
                p10, p11 = _f32(img[y1c, x0c, c]), _f32(img[y1c, x1c, c])
                top = mad(_f32(p01 - p00), fx, p00)
                bot = mad(_f32(p11 - p10), fx, p10)
                val = mad(_f32(bot - top), fy, top)
                out[dy, dx, c] = int(_f32(val + _f32(0.5)))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Compare the firmware resize with TensorFlow's on one crop.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("image", nargs="+", help="crop image(s) (e.g. a logged ROI image)")
    ap.add_argument("--size", default="20x32",
                    help="model input WIDTHxHEIGHT (digits 20x32, analog 32x32)")
    ap.add_argument("--model", help="optional .tflite: also show its reading per variant")
    ap.add_argument("--no-reference", action="store_true",
                    help="skip the slow exact scalar reference")
    args = ap.parse_args()
    w, h = (int(v) for v in args.size.lower().split("x"))

    import tensorflow as tf

    model = None
    if args.model:
        from common import TFLiteModel
        model = TFLiteModel(args.model)
        if (model.width, model.height) != (w, h):
            w, h = model.width, model.height
            print(f"(using the model's input size {w}x{h})")

    for path in args.image:
        img = np.asarray(Image.open(path).convert("RGB"))
        fw = firmware_resize(img, w, h)
        variants = {}
        if not args.no_reference:
            variants["reference (exact FMA)"] = reference_resize(img, w, h, fused=True)
            variants["no-FMA C port"] = reference_resize(img, w, h, fused=False)
        t = tf.convert_to_tensor(img.astype(np.float32))
        bil = tf.image.resize(t, (h, w), method="bilinear").numpy()
        variants["tf bilinear (unrounded)"] = bil
        variants["tf bilinear (rounded)"] = np.floor(bil + 0.5)
        variants["tf mitchellcubic (upstream)"] = np.clip(
            tf.image.resize(t, (h, w), method="mitchellcubic").numpy(), 0, 255)

        print(f"\n{path}: {img.shape[1]}x{img.shape[0]} -> {w}x{h}")
        print(f"  {'variant':30s} {'max |diff|':>10s} {'mean |diff|':>11s} {'pixels differing':>17s}")
        for name, v in variants.items():
            d = np.abs(v.astype(np.float64) - fw.astype(np.float64))
            print(f"  {name:30s} {d.max():10.2f} {d.mean():11.3f} {100 * (d > 0).mean():16.1f}%")
        if model is not None:
            reads = {"firmware": fw, **variants}
            print("  model reading per variant:")
            for name, v in reads.items():
                val, conf = model.predict(v.astype(np.float32)[None])
                print(f"    {name:30s} {float(val[0]):5.2f}  (confidence {float(conf[0]):.3f})")


if __name__ == "__main__":
    main()
