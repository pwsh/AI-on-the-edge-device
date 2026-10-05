"""Shared helpers for the AI-on-the-edge-device model-training toolkit.

Everything that more than one script needs lives here:

* the firmware contract (model types, input sizes, output sizes, supported ops),
* filename label parsing / formatting (upstream-compatible conventions),
* firmware-identical image preprocessing and output decoding,
* a small HTTP client for the device (retries with backoff, pause/resume),
* the metrics used by train.py and evaluate.py.

Only the Python standard library is imported at module level, so lightweight tools
(label_tool.py) can import this file without TensorFlow, numpy or requests installed.
Heavy dependencies are imported lazily inside the functions that need them.
"""

from __future__ import annotations

import contextlib
import hashlib
import math
import os
import re
import signal
import sys
import time
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote

# ---------------------------------------------------------------------------
# Firmware contract
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ModelType:
    """Static description of one model family the firmware understands."""

    name: str        # e.g. "dig-class11"
    height: int      # model input height (pixels)
    width: int       # model input width (pixels)
    outputs: int     # output vector length; the firmware infers the model kind from it
    kind: str        # "class11", "class100", "cont" (sin/cos) or "cont10" (dig-cont)
    section: str     # config.ini section the model belongs to ("Digits" or "Analog")


MODEL_TYPES: dict[str, ModelType] = {
    "dig-class11": ModelType("dig-class11", 32, 20, 11, "class11", "Digits"),
    "dig-class100": ModelType("dig-class100", 32, 20, 100, "class100", "Digits"),
    "ana-cont": ModelType("ana-cont", 32, 32, 2, "cont", "Analog"),
    "ana-class100": ModelType("ana-class100", 32, 32, 100, "class100", "Analog"),
}

# Older digit model family the firmware still runs ("DoubleHyprid10": 10 outputs, the
# reading is interpolated between the best class and its stronger neighbour). It can be
# pre-labelled with / evaluated, but this toolkit does not train it.
LEGACY_TYPES: dict[str, ModelType] = {
    "dig-cont": ModelType("dig-cont", 32, 20, 10, "cont10", "Digits"),
}
ALL_TYPES: dict[str, ModelType] = {**MODEL_TYPES, **LEGACY_TYPES}

# Output sizes the firmware accepts (anything else is rejected when the model is loaded).
# train.py only produces 2, 11 and 100.
VALID_OUTPUT_SIZES = (2, 10, 11, 100)

# TFLite builtin ops registered in the firmware's static op resolver
# (code/components/jomjol_tfliteclass/CTfLiteClass.cpp, MakeStaticResolver()).
# A model that uses any other op fails to load on the device.
FIRMWARE_OPS = frozenset({
    "FULLY_CONNECTED", "RESHAPE", "SOFTMAX", "CONV_2D", "MAX_POOL_2D",
    "QUANTIZE", "MUL", "ADD", "LEAKY_RELU", "DEQUANTIZE",
})

# Class index 10 of a class11 model means "not a number" (digit between two positions).
NAN_CLASS = 10

IMAGE_EXTENSIONS = (".jpg", ".jpeg", ".png", ".bmp")


def model_type_from_io(input_shape, output_size: int) -> ModelType:
    """Infer the model type exactly like the firmware does, from the tensor shapes.

    11 outputs -> digit class11; 100 outputs -> class100 (analog when the input is
    32x32, digit otherwise); 2 outputs -> analog continuous (sin/cos).
    """
    h, w = int(input_shape[1]), int(input_shape[2])
    if output_size == 11:
        return ModelType("dig-class11", h, w, 11, "class11", "Digits")
    if output_size == 100:
        if (h, w) == (32, 32):
            return ModelType("ana-class100", h, w, 100, "class100", "Analog")
        return ModelType("dig-class100", h, w, 100, "class100", "Digits")
    if output_size == 2:
        return ModelType("ana-cont", h, w, 2, "cont", "Analog")
    if output_size == 10:
        return ModelType("dig-cont", h, w, 10, "cont10", "Digits")
    raise ValueError(f"output size {output_size} is not supported by the firmware "
                     f"(must be one of {VALID_OUTPUT_SIZES})")


# ---------------------------------------------------------------------------
# Small console helpers
# ---------------------------------------------------------------------------


def log(msg: str = "") -> None:
    print(msg, flush=True)


def warn(msg: str) -> None:
    print(f"WARNING: {msg}", file=sys.stderr, flush=True)


def die(msg: str, code: int = 1) -> None:
    print(f"ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def fmt_duration(seconds: float) -> str:
    seconds = int(max(0, seconds))
    h, rem = divmod(seconds, 3600)
    m, s = divmod(rem, 60)
    return f"{h}h{m:02d}m{s:02d}s" if h else f"{m}m{s:02d}s"


class Progress:
    """Minimal single-line progress indicator with rate and ETA (no dependencies)."""

    def __init__(self, total: int, label: str = "", every: float = 0.5):
        self.total = max(0, total)
        self.label = label
        self.every = every
        self.start = time.time()
        self.last = self.start      # first line after `every` seconds, then every `every` s
        self.n = 0

    def update(self, n: int = 1, extra: str = "", force: bool = False) -> None:
        self.n += n
        now = time.time()
        if not force and now - self.last < self.every:
            return
        self.last = now
        elapsed = now - self.start
        rate = self.n / elapsed if elapsed > 0 else 0.0
        eta = (self.total - self.n) / rate if rate > 0 and self.total else 0
        tot = f"/{self.total}" if self.total else ""
        line = (f"\r{self.label} {self.n}{tot}  {rate:6.1f}/s  "
                f"elapsed {fmt_duration(elapsed)}  ETA {fmt_duration(eta)} {extra}")
        sys.stdout.write(line[:150].ljust(80))
        sys.stdout.flush()

    def done(self, extra: str = "") -> None:
        """Print the final state once and end the line."""
        self.update(0, extra, force=True)
        sys.stdout.write("\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# Filename label conventions
# ---------------------------------------------------------------------------
#
# Labelled image:   <label>_<anything>.jpg
#   class11:        label is one character "0".."9" or "N" (device logs may write "10" for N)
#   class100/cont:  label is "x.y" (first 3 characters), e.g. "3.7_...jpg";
#                   a legacy single digit "3_...jpg" is accepted as 3.0
# Device log image: <pred>_[c<NN>_]<number>_<roi>_<YYYYMMDD-HHMMSS>.jpg
# Unlabelled crop:  unlabeled_<...>.jpg  (never used for training)

_XY_RE = re.compile(r"^(\d)\.(\d)")
_CONF_RE = re.compile(r"^c(\d\d)$")
_HASH_RE = re.compile(r"^[0-9a-f]{16}$")


def label_token(filename: str) -> str:
    """Return the first '_'-separated token of a file's base name."""
    return Path(filename).name.split("_", 1)[0]


def parse_label(filename: str, kind: str):
    """Parse the ground-truth label encoded in a filename.

    Returns an int class (0..10, 10 = N) for kind "class11", a float 0.0..9.9 for
    "class100"/"cont", or None when the name carries no usable label.
    """
    token = label_token(filename)
    if not token or token.lower() == "unlabeled":
        return None
    if kind == "class11":
        if token in ("N", "n", "10", "NaN", "nan"):
            return NAN_CLASS
        if len(token) >= 1 and token[0].isdigit() and (len(token) == 1 or token[1] == "."):
            # "7" or the class100-style "7.0" (only an exact .0 maps to a digit class)
            if len(token) == 1 or token[:3] == f"{token[0]}.0":
                return int(token[0])
        return None
    # class100 / cont: "x.y" in the first three characters
    m = _XY_RE.match(token)
    if m:
        return int(m.group(1)) + int(m.group(2)) / 10.0
    if len(token) == 1 and token.isdigit():   # legacy single-digit label -> x.0
        return float(token)
    return None


def parse_confidence(filename: str):
    """Return the c<NN> confidence (0..0.99) written by newer firmware / prelabel.py, or None."""
    parts = Path(filename).stem.split("_")
    if len(parts) > 1:
        m = _CONF_RE.match(parts[1])
        if m:
            return int(m.group(1)) / 100.0
    return None


def format_label(value, kind: str) -> str:
    """Format a label the way it is written into file names ("7", "N" or "3.7")."""
    if kind == "class11":
        return "N" if int(value) == NAN_CLASS else str(int(value))
    v = round(float(value) * 10) % 100        # wrap 10.0 -> 0.0
    return f"{v // 10}.{v % 10}"


def label_to_class(value, kind: str) -> int:
    """Map a parsed label to a class index (class11: 0..10, class100: 0..99)."""
    if kind == "class11":
        return int(value)
    return int(round(float(value) * 10)) % 100


def file_sha1(path, n: int = 16) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()[:n]


def hash_from_name(filename: str):
    """Return the 16-hex-digit content hash at the end of a privacy-renamed file, or None."""
    last = Path(filename).stem.split("_")[-1]
    return last if _HASH_RE.match(last) else None


def iter_images(paths, recursive: bool = True):
    """Yield image files under the given files/folders, sorted, skipping hidden/trash folders."""
    for p in paths:
        p = Path(p)
        if p.is_file():
            if p.suffix.lower() in IMAGE_EXTENSIONS:
                yield p
            continue
        if not p.is_dir():
            warn(f"not found: {p}")
            continue
        it = p.rglob("*") if recursive else p.glob("*")
        for f in sorted(it):
            if any(part.startswith((".", "_trash")) for part in f.relative_to(p).parts):
                continue
            if f.is_file() and f.suffix.lower() in IMAGE_EXTENSIONS:
                yield f


def load_labeled(paths, kind: str):
    """Collect (file, label) pairs from labelled folders; returns (files, labels, skipped)."""
    files, labels, skipped = [], [], []
    for f in iter_images(paths):
        lab = parse_label(f.name, kind)
        if lab is None:
            skipped.append(f)
        else:
            files.append(f)
            labels.append(lab)
    return files, labels, skipped


# ---------------------------------------------------------------------------
# Firmware-identical preprocessing and output decoding
# ---------------------------------------------------------------------------


# How crops are scaled to the model input. "firmware" reproduces the device; the others
# are offered for comparison and for compatibility with upstream-trained models.
RESIZE_METHODS = ("firmware", "mitchellcubic", "bilinear")
DEFAULT_RESIZE = "firmware"


def _fma32(a, b, c):
    """float32 fused multiply-add a*b + c with a single rounding (Xtensa madd.s).

    Computed in float64 and rounded once to float32. For the operands that occur in
    firmware_resize() (pixel values 0..255, fractions in [0, 1), small coordinates) the
    float64 result is exact, so this equals a true FMA; only a pathological case in the
    final lerp could round twice (never observed, see resize_check.py).
    """
    import numpy as np

    return (np.asarray(a, np.float64) * np.asarray(b, np.float64)
            + np.asarray(c, np.float64)).astype(np.float32)


def firmware_resize(img, new_w: int, new_h: int):
    """Bit-exact numpy port of the firmware's ROI -> model-input downscale.

    Mirrors CImageBasis::Resize(int, int, CImageBasis*) in
    code/components/jomjol_image_proc/CImageBasis.cpp (commit c05dcebc), a 2-tap
    bilinear without anti-aliasing:

        scale = (float)src / (float)dst                        (per axis, float32)
        s     = (d + 0.5f) * scale - 0.5f                      (pixel-centre mapping)
        i0    = (int)floorf(s);  f = s - (float)i0;  i0/i0+1 clamped to the image
        top   = p00 + (p01 - p00) * fx;  bot = p10 + (p11 - p10) * fx
        out   = (uint8_t)(top + (bot - top) * fy + 0.5f)       (round half up, truncate)

    Every channel is interpolated independently with the same weights. All maths is
    float32, and the ESP32/ESP32-S3 GCC build fuses each "a + b * c" into one madd.s
    (verified by disassembling the S3 firmware: 5 madd.s, no mul.s), so those steps
    use a single-rounding FMA here too.

    img: uint8 array (H, W, C). Returns a uint8 array (new_h, new_w, C).
    """
    import numpy as np

    img = np.asarray(img, np.uint8)
    sh, sw = img.shape[:2]
    f32 = np.float32

    def axis(n_dst, n_src):
        scale = f32(n_src) / f32(n_dst)
        d = np.arange(n_dst, dtype=np.float32) + f32(0.5)
        s = _fma32(d, scale, f32(-0.5))
        i0 = np.floor(s).astype(np.int64)
        frac = (s - i0.astype(np.float32)).astype(np.float32)
        return np.clip(i0, 0, n_src - 1), np.clip(i0 + 1, 0, n_src - 1), frac

    x0, x1, fx = axis(new_w, sw)
    y0, y1, fy = axis(new_h, sh)
    src = img.astype(np.float32)
    fx = fx[None, :, None]
    fy = fy[:, None, None]
    p00 = src[y0][:, x0]
    p01 = src[y0][:, x1]
    p10 = src[y1][:, x0]
    p11 = src[y1][:, x1]
    top = _fma32(p01 - p00, fx, p00)
    bot = _fma32(p11 - p10, fx, p10)
    val = _fma32(bot - top, fy, top)
    return (val + f32(0.5)).astype(np.float32).astype(np.uint8)   # utrunc.s


def load_images(files, height: int, width: int, resize: str = DEFAULT_RESIZE,
                batch: int = 256, progress: str = ""):
    """Load crops the way the firmware feeds them to the model.

    Each crop is decoded to RGB and scaled to the model input size, then kept as
    float32 *without* normalisation (the firmware copies the raw 0..255 byte values
    into the float input tensor).

    resize: "firmware"      bit-exact port of the device's bilinear downscale (default)
            "mitchellcubic" tf.image.resize Mitchell-cubic, as the upstream training
                            notebooks do (not rounded to bytes)
            "bilinear"      tf.image.resize bilinear (half-pixel centres, not rounded)
    Returns a float32 numpy array of shape (N, height, width, 3).
    """
    import numpy as np

    if resize not in RESIZE_METHODS:
        raise ValueError(f"unknown resize method {resize!r} (use one of {RESIZE_METHODS})")
    files = [str(f) for f in files]
    out = np.zeros((len(files), height, width, 3), np.float32)
    if not files:
        return out
    prog = Progress(len(files), progress) if progress else None

    if resize == "firmware":
        from concurrent.futures import ThreadPoolExecutor

        from PIL import Image

        def _one(path):
            with Image.open(path) as im:
                return firmware_resize(np.asarray(im.convert("RGB")), width, height)

        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            for i, img in enumerate(pool.map(_one, files, chunksize=64)):
                out[i] = img
                if prog:
                    prog.update()
    else:
        import tensorflow as tf

        def _load(path):
            img = tf.io.decode_image(tf.io.read_file(path), channels=3, expand_animations=False)
            img = tf.image.resize(tf.cast(img, tf.float32), (height, width), method=resize)
            return tf.clip_by_value(img, 0.0, 255.0)

        ds = (tf.data.Dataset.from_tensor_slices(files)
              .map(_load, num_parallel_calls=tf.data.AUTOTUNE)
              .batch(batch).prefetch(2))
        i = 0
        for b in ds:
            b = b.numpy()
            out[i:i + len(b)] = b
            i += len(b)
            if prog:
                prog.update(len(b))
    if prog:
        prog.done()
    return out


def decode_outputs(raw, kind: str):
    """Turn raw model outputs (N, K) into (values, confidences) like the firmware does.

    class11:  value = argmax (10 = N); confidence = max / sum (firmware formula)
    class100: value = argmax / 10;     confidence = max / sum
    cont10:   (dig-cont) argmax over 10 classes, interpolated towards the stronger
              neighbour; confidence = the firmware's "fit" (best + neighbour output)
    cont:     value = fmod(atan2(o0, o1) / 2pi + 2, 1) * 10; the firmware has no
              confidence for this type, so we report how close |(o0, o1)| is to 1
              (a heuristic, only used to sort images for review).
    """
    import numpy as np

    raw = np.asarray(raw, np.float64)
    if kind == "cont":
        vals = np.mod(np.arctan2(raw[:, 0], raw[:, 1]) / (2 * math.pi) + 2.0, 1.0) * 10.0
        norm = np.sqrt(raw[:, 0] ** 2 + raw[:, 1] ** 2)
        conf = np.clip(1.0 - np.abs(1.0 - norm), 0.0, 1.0)
        return vals, conf
    if kind == "cont10":
        n = np.arange(len(raw))
        num = np.argmax(raw[:, :10], axis=1)
        val = raw[n, num]
        vp, vm = raw[n, (num + 1) % 10], raw[n, (num + 9) % 10]
        with np.errstate(divide="ignore", invalid="ignore"):
            up = num + np.nan_to_num(vp / (vp + val))
            down = num - np.nan_to_num(vm / (val + vm))
        vals = np.mod(np.where(vp > vm, up, down), 10.0)
        conf = np.clip(np.where(vp > vm, val + vp, val + vm), 0.0, 1.0)
        return vals, conf
    idx = np.argmax(raw, axis=1)
    mx = raw[np.arange(len(raw)), idx]
    s = raw.sum(axis=1)
    conf = np.where(s > 0, mx / np.where(s > 0, s, 1.0), mx)
    if kind == "class100":
        return idx / 10.0, conf
    return idx, conf


class TFLiteModel:
    """Thin wrapper around tf.lite.Interpreter that runs a model like the firmware."""

    def __init__(self, path, threads: int | None = None):
        import warnings

        import tensorflow as tf

        self.path = Path(path)
        # TF 2.18 warns that tf.lite.Interpreter moves to the ai_edge_litert package; the
        # bundled interpreter is what we want here (same kernels as the converter).
        warnings.filterwarnings("ignore", message=".*LiteRT interpreter.*")
        # Plain builtin kernels, no XNNPACK delegate: closer to the reference kernels of
        # TFLite-Micro on the device, and every op stays visible for the op check below.
        self.interpreter = tf.lite.Interpreter(
            model_path=str(path), num_threads=threads or os.cpu_count(),
            experimental_op_resolver_type=(
                tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES))
        self.interpreter.allocate_tensors()
        self.inp = self.interpreter.get_input_details()[0]
        self.out = self.interpreter.get_output_details()[0]
        out_size = int(self.out["shape"][-1])
        self.type = model_type_from_io(self.inp["shape"], out_size)

    @property
    def height(self) -> int:
        return self.type.height

    @property
    def width(self) -> int:
        return self.type.width

    def op_names(self) -> set[str]:
        """Builtin op names used by the model (uses a private but stable TF API)."""
        try:
            return {d["op_name"] for d in self.interpreter._get_ops_details()}
        except Exception:   # pragma: no cover - API missing in some TF versions
            return set()

    def predict_raw(self, images, progress: str = ""):
        """Run float32 images (N, H, W, 3) one by one (the device uses batch size 1)."""
        import numpy as np

        it = self.interpreter
        out = np.zeros((len(images), int(self.out["shape"][-1])), np.float32)
        prog = Progress(len(images), progress) if progress else None
        for i, img in enumerate(images):
            it.set_tensor(self.inp["index"], img[None].astype(np.float32))
            it.invoke()
            out[i] = it.get_tensor(self.out["index"])[0]
            if prog:
                prog.update()
        if prog:
            prog.done()
        return out

    def predict(self, images, progress: str = ""):
        return decode_outputs(self.predict_raw(images, progress), self.type.kind)


def check_firmware_contract(path, expect: ModelType | None = None) -> list[str]:
    """Check a .tflite file against the firmware contract; return a list of problems."""
    import numpy as np

    problems = []
    try:
        m = TFLiteModel(path)
    except ValueError as e:
        return [str(e)]
    inp, out = m.inp, m.out
    if inp["dtype"] != np.float32:
        problems.append(f"input dtype is {inp['dtype'].__name__}, firmware needs float32")
    if out["dtype"] != np.float32:
        problems.append(f"output dtype is {out['dtype'].__name__}, firmware needs float32")
    shape = list(inp["shape"])
    if len(shape) != 4 or shape[0] != 1 or shape[3] != 3:
        problems.append(f"input shape {shape} is not (1, H, W, 3)")
    if int(out["shape"][-1]) not in VALID_OUTPUT_SIZES:
        problems.append(f"output size {out['shape'][-1]} not in {VALID_OUTPUT_SIZES}")
    if expect is not None:
        if (shape[1], shape[2]) != (expect.height, expect.width):
            problems.append(f"input is {shape[1]}x{shape[2]}, expected {expect.height}x{expect.width}")
        if m.type.name != expect.name:
            problems.append(f"firmware would treat this as {m.type.name}, expected {expect.name}")
    ops = m.op_names()
    if not ops:
        problems.append("could not list the model's ops (TensorFlow API changed?)")
    bad = sorted(ops - FIRMWARE_OPS)
    if bad:
        problems.append(f"uses ops not in the firmware resolver: {', '.join(bad)}")
    return problems


# ---------------------------------------------------------------------------
# Near-duplicate detection
# ---------------------------------------------------------------------------


def phash_images(files, progress: str = ""):
    """Return a uint64 perceptual hash (imagehash.phash, 8x8) per image file."""
    import imagehash
    import numpy as np
    from PIL import Image

    hashes = np.zeros(len(files), np.uint64)
    prog = Progress(len(files), progress) if progress else None
    for i, f in enumerate(files):
        with Image.open(f) as im:
            bits = imagehash.phash(im.convert("RGB")).hash.flatten()
        hashes[i] = np.uint64(int("".join("1" if b else "0" for b in bits), 2))
        if prog:
            prog.update()
    if prog:
        prog.done()
    return hashes


def _popcount(a):
    """Bit count per uint64 element (np.bitwise_count needs numpy >= 2.0)."""
    import numpy as np

    if hasattr(np, "bitwise_count"):
        return np.bitwise_count(a)
    return np.unpackbits(np.asarray(a, np.uint64).view(np.uint8).reshape(-1, 8),
                         axis=1).sum(axis=1)


def dedupe_indices(hashes, groups=None, max_dist: int = 2):
    """Greedy near-duplicate removal: keep the first image of every phash cluster.

    hashes:   uint64 array from phash_images()
    groups:   optional per-image key (e.g. the label); images are only considered
              duplicates of each other inside the same group, so a "3" is never
              dropped because it looks like an "8" with one segment missing.
    max_dist: Hamming distance at or below which two images count as duplicates.
    Returns the sorted list of indices to keep.
    """
    import numpy as np

    if groups is None:
        groups = [0] * len(hashes)
    kept_by_group: dict = {}
    keep = []
    for i, (h, g) in enumerate(zip(hashes, groups)):
        kept = kept_by_group.setdefault(g, [])
        if kept:
            arr = np.asarray(kept, np.uint64)
            if int(_popcount(arr ^ h).min()) <= max_dist:
                continue
        kept.append(h)
        keep.append(i)
    return keep


# ---------------------------------------------------------------------------
# Metrics (shared by train.py and evaluate.py)
# ---------------------------------------------------------------------------


def circular_diff(a, b, period: float = 10.0):
    """Smallest absolute difference between values on a circle of the given period."""
    import numpy as np

    d = np.abs(np.asarray(a, np.float64) - np.asarray(b, np.float64)) % period
    return np.minimum(d, period - d)


def compute_metrics(kind: str, labels, preds) -> dict:
    """Metrics for one model kind.

    class11:  accuracy + confusion matrix (11x11) + most frequent confusions
    class100: exact-class accuracy, fraction within +-0.1 (with wrap at 10) and confusions
    cont:     fraction within +-0.1 with wrap ("Accepted_Deviation" in upstream), MAE
    """
    import numpy as np

    labels = np.asarray(labels)
    preds = np.asarray(preds)
    n = len(labels)
    res: dict = {"samples": int(n)}
    if n == 0:
        return res
    if kind == "class11":
        ok = labels.astype(int) == preds.astype(int)
        res["accuracy"] = float(ok.mean())
        cm = np.zeros((11, 11), int)
        for t, p in zip(labels.astype(int), preds.astype(int)):
            cm[t, p] += 1
        res["confusion_matrix"] = cm.tolist()
        res["per_class_accuracy"] = {
            format_label(c, kind): (float(cm[c, c] / cm[c].sum()) if cm[c].sum() else None)
            for c in range(11)}
        names = [format_label(c, kind) for c in range(11)]
    else:
        tcls = np.round(labels.astype(float) * 10).astype(int) % 100
        pcls = np.round(preds.astype(float) * 10).astype(int) % 100
        diff = circular_diff(labels, preds)
        res["within_0.1"] = float((diff <= 0.1 + 1e-6).mean())
        res["within_0.2"] = float((diff <= 0.2 + 1e-6).mean())
        res["mae"] = float(diff.mean())
        res["max_error"] = float(diff.max())
        if kind == "class100":
            res["accuracy"] = float((tcls == pcls).mean())
        cm = None
        names = None
    # Most frequent label -> prediction confusions (top 15)
    pairs: dict = {}
    for t, p in zip(labels, preds):
        a, b = format_label(t, kind), format_label(p, kind)
        if kind in ("cont", "cont10") and float(circular_diff([t], [p])[0]) <= 0.1 + 1e-6:
            continue                     # within tolerance is not a mistake
        if a != b:
            pairs[(a, b)] = pairs.get((a, b), 0) + 1
    res["top_confusions"] = [{"label": a, "prediction": b, "count": c}
                             for (a, b), c in sorted(pairs.items(), key=lambda kv: -kv[1])[:15]]
    if names:
        res["class_names"] = names
    return res


def headline(kind: str, metrics: dict) -> str:
    """One-line summary of the most important metric."""
    if not metrics.get("samples"):
        return "no samples"
    if kind == "class11":
        return f"accuracy {metrics['accuracy'] * 100:.2f}% ({metrics['samples']} images)"
    s = f"within +-0.1: {metrics['within_0.1'] * 100:.2f}%"
    if "accuracy" in metrics:
        s = f"exact {metrics['accuracy'] * 100:.2f}%, " + s
    return s + f", MAE {metrics['mae']:.3f} ({metrics['samples']} images)"


def print_confusions(kind: str, metrics: dict, max_rows: int = 10) -> None:
    if kind == "class11" and "confusion_matrix" in metrics:
        names = metrics["class_names"]
        log("Confusion matrix (rows = label, columns = prediction):")
        log("      " + "".join(f"{n:>6}" for n in names))
        for i, row in enumerate(metrics["confusion_matrix"]):
            if sum(row):
                log(f"{names[i]:>6}" + "".join(f"{v:>6}" for v in row))
    tc = metrics.get("top_confusions", [])[:max_rows]
    if tc:
        log("Most frequent mistakes (label -> prediction: count): " +
            ", ".join(f"{c['label']}->{c['prediction']}: {c['count']}" for c in tc))


def write_false_predictions(path, files, labels, preds, kind: str, confs=None,
                            splits=None) -> int:
    """Write every wrong prediction (class mismatch, or > 0.1 off for analog) to a CSV.

    splits: optional per-file tag (e.g. "train"/"val") written as an extra column.
    """
    import csv

    n = 0
    with open(path, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["file", "label", "prediction", "confidence"] +
                   (["split"] if splits is not None else []))
        for i, (fn, t, p) in enumerate(zip(files, labels, preds)):
            if kind == "class11":
                wrong = int(t) != int(p)
            elif kind == "class100":
                wrong = label_to_class(t, kind) != label_to_class(p, kind)
            else:   # cont / cont10: continuous readings
                wrong = float(circular_diff([t], [p])[0]) > 0.1 + 1e-6
            if wrong:
                c = "" if confs is None else f"{float(confs[i]):.3f}"
                pred = (format_label(p, kind) if kind in ("class11", "class100")
                        else f"{float(p):.2f}")
                w.writerow([str(fn), format_label(t, kind), pred, c] +
                           ([splits[i]] if splits is not None else []))
                n += 1
    return n


# ---------------------------------------------------------------------------
# Device HTTP client
# ---------------------------------------------------------------------------


class _LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            for k, v in attrs:
                if k == "href" and v:
                    self.links.append(v)


def _raise_interrupt(*_):
    """SIGTERM handler: turn a kill into KeyboardInterrupt so cleanup (resume) runs."""
    raise KeyboardInterrupt()


class Device:
    """HTTP client for one device.

    The device web server drops connections while a processing round runs, so every
    request is retried with exponential backoff. Use `with dev.paused(): ...` to pause
    the flow for bulk transfers; it always resumes, also on Ctrl-C / SIGTERM.
    """

    def __init__(self, host: str, retries: int = 6, timeout: float = 30.0, verbose: bool = False):
        import requests

        host = host.strip().rstrip("/")
        if not host.startswith(("http://", "https://")):
            host = "http://" + host
        self.base = host
        self.name = re.sub(r"^https?://", "", host).replace(":", "_").replace("/", "_")
        self.retries = retries
        self.timeout = timeout
        self.verbose = verbose
        self.session = requests.Session()
        self.session.headers["User-Agent"] = "aiot-model-training/1.0"

    # -- low level ---------------------------------------------------------
    def request(self, method: str, path: str, ok=(200,), stream=False, timeout=None, **kw):
        """HTTP request with retries; returns the response or raises RuntimeError."""
        import requests

        url = self.base + path
        delay = 1.0
        last = None
        for attempt in range(1, self.retries + 1):
            try:
                r = self.session.request(method, url, stream=stream, allow_redirects=False,
                                         timeout=timeout or self.timeout, **kw)
                if r.status_code in ok:
                    return r
                last = f"HTTP {r.status_code}: {r.text[:200].strip() if not stream else ''}"
                if 400 <= r.status_code < 500 and r.status_code not in (408, 429):
                    break                      # client errors are not retried
            except requests.RequestException as e:
                last = f"{type(e).__name__}: {e}"
            if attempt < self.retries:
                if self.verbose:
                    warn(f"{method} {path} failed ({last}); retry {attempt}/{self.retries - 1} "
                         f"in {delay:.0f}s")
                time.sleep(delay)
                delay = min(delay * 2, 30)
        raise RuntimeError(f"{method} {url} failed: {last}")

    def get_text(self, path: str) -> str:
        return self.request("GET", path).text

    # -- file server ------------------------------------------------------
    def list_dir(self, path: str):
        """List a device folder; returns (subfolders, files) as absolute device paths."""
        path = "/" + path.strip("/") + "/"
        if path == "//":
            path = "/"
        html = self.get_text("/fileserver" + path)
        p = _LinkParser()
        p.feed(html)
        dirs, files = [], []
        for href in p.links:
            if not href.startswith("/fileserver/") or "?" in href:
                continue
            dev = unquote(href[len("/fileserver"):])
            if not dev.startswith(path) or dev == path:
                continue
            rest = dev[len(path):]
            if rest.endswith("/") and "/" not in rest[:-1]:
                dirs.append(dev.rstrip("/"))
            elif "/" not in rest:
                files.append(dev)
        return sorted(set(dirs)), sorted(set(files))

    def walk(self, path: str):
        """Recursively yield every file below a device folder."""
        dirs, files = self.list_dir(path)
        yield from files
        for d in dirs:
            yield from self.walk(d)

    def download(self, device_path: str, dest: Path, timeout=None) -> int:
        """Download one file to dest (atomically via a .part file); returns bytes written."""
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_name(dest.name + ".part")
        r = self.request("GET", "/fileserver" + device_path, stream=True, timeout=timeout)
        n = 0
        with open(tmp, "wb") as f:
            for chunk in r.iter_content(1 << 16):
                f.write(chunk)
                n += len(chunk)
        os.replace(tmp, dest)
        return n

    def fetch_bytes(self, device_path: str) -> bytes:
        return self.request("GET", "/fileserver" + device_path, timeout=120).content

    def upload_bytes(self, data: bytes, device_path: str) -> None:
        """Delete-then-upload (the device answers 400 "File already exists" otherwise).

        Refuses empty data: an empty upload of e.g. config.ini leaves an unusable device.
        Does NOT verify; use upload_verified() for anything that matters.
        """
        if not data:
            raise RuntimeError(f"refusing to upload empty data to {device_path}")
        with contextlib.suppress(RuntimeError):     # a missing file is fine
            self.request("POST", "/delete" + device_path, ok=(200, 303))
        self.request("POST", "/upload" + device_path, ok=(200, 303), data=data, timeout=120)

    def upload_verified(self, data: bytes, device_path: str, attempts: int = 3) -> bool:
        """Upload, read the file back and compare byte-for-byte (size + MD5).

        Retries up to `attempts` times; returns True only after a clean compare.
        """
        want = hashlib.md5(data).hexdigest()
        for attempt in range(1, attempts + 1):
            try:
                self.upload_bytes(data, device_path)
                back = self.fetch_bytes(device_path)
                if back == data:
                    log(f"  verified {device_path}: {len(back)} bytes, md5 {want}")
                    return True
                warn(f"verify {device_path}: got {len(back)} bytes md5 "
                     f"{hashlib.md5(back).hexdigest()}, expected {len(data)} bytes md5 {want} "
                     f"(attempt {attempt}/{attempts})")
            except RuntimeError as e:
                warn(f"upload {device_path} failed: {e} (attempt {attempt}/{attempts})")
            time.sleep(2 * attempt)
        return False

    def upload(self, local: Path, device_path: str, attempts: int = 3) -> bool:
        """Upload a local file with verification; the file must exist and be non-empty."""
        local = Path(local)
        if not local.is_file() or local.stat().st_size == 0:
            raise RuntimeError(f"{local} does not exist or is empty - not uploading")
        return self.upload_verified(local.read_bytes(), device_path, attempts)

    # -- flow control -----------------------------------------------------
    def pause_state(self, action: str | None = None) -> str:
        q = f"?status={action}" if action else ""
        return self.get_text("/pause" + q).strip()

    @contextlib.contextmanager
    def paused(self, enabled: bool = True):
        """Pause the processing flow for the duration of the block; always resume."""
        if not enabled:
            yield
            return
        was = None
        with contextlib.suppress(RuntimeError):
            was = self.pause_state()
        if was == "paused":
            log("Device processing is already paused (will be left paused).")
            yield
            return
        state = self.pause_state("pause")
        log(f"Device processing: {state}")
        old_term = signal.getsignal(signal.SIGTERM)
        signal.signal(signal.SIGTERM, _raise_interrupt)
        try:
            yield
        finally:
            signal.signal(signal.SIGTERM, old_term)
            for attempt in range(5):
                try:
                    log(f"Device processing: {self.pause_state('resume')}")
                    break
                except RuntimeError as e:
                    warn(f"resume failed ({e}); retrying")
                    time.sleep(2)
            else:
                warn(f"could not resume processing; open {self.base}/pause?status=resume")

    def reboot(self) -> None:
        with contextlib.suppress(RuntimeError):
            self.request("GET", "/reboot", ok=(200, 303), timeout=10)


# ---------------------------------------------------------------------------
# config.ini parsing
# ---------------------------------------------------------------------------


@dataclass
class Roi:
    number: str
    name: str
    x: int
    y: int
    w: int
    h: int


def parse_config(text: str) -> dict:
    """Parse the parts of config.ini this toolkit needs.

    Returns {"sections": {name: {"enabled": bool, "lines": [...]}}, ...}. A section
    header written as ";[Analog]" is a disabled section; its lines are kept but the
    section is marked disabled.
    """
    sections: dict = {}
    current = None
    for raw in text.splitlines():
        line = raw.strip()
        m = re.match(r"^(;?)\[(\w+)\]\s*$", line)
        if m:
            current = m.group(2)
            sections[current] = {"enabled": m.group(1) == "", "lines": []}
            continue
        if current is not None:
            sections[current]["lines"].append(line)
    return sections


def config_value(sections: dict, section: str, key: str):
    for line in sections.get(section, {}).get("lines", []):
        if line.startswith(";"):
            continue
        k, sep, v = line.partition("=")
        if sep and k.strip().lower() == key.lower():
            return v.strip()
    return None


def config_rois(sections: dict, section: str) -> list[Roi]:
    """ROI lines look like '<number>.<roi> x y w h [flags]'."""
    rois = []
    for line in sections.get(section, {}).get("lines", []):
        if not line or line.startswith(";") or "=" in line:
            continue
        parts = line.split()
        if len(parts) >= 5 and "." in parts[0]:
            try:
                x, y, w, h = (int(float(v)) for v in parts[1:5])
            except ValueError:
                continue
            number, _, name = parts[0].partition(".")
            rois.append(Roi(number, name, x, y, w, h))
    return rois


def replace_model_line(text: str, section: str, model_path: str) -> str:
    """Replace the (first, uncommented) 'Model =' line in a section; keeps line endings."""
    out = []
    current = None
    done = False
    for raw in text.splitlines(keepends=True):
        line = raw.strip()
        m = re.match(r"^;?\[(\w+)\]\s*$", line)
        if m:
            current = m.group(1)
        elif (not done and current == section and not line.startswith(";")
              and line.partition("=")[0].strip().lower() == "model" and "=" in line):
            eol = raw[len(raw.rstrip("\r\n")):]
            raw = f"Model = {model_path}{eol}"
            done = True
        out.append(raw)
    if not done:
        raise ValueError(f"no 'Model =' line found in section [{section}]")
    return "".join(out)
