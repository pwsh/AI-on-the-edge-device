#!/usr/bin/env python3
"""Train a meter-reading model and export it for the AI-on-the-edge-device firmware.

Model types (the firmware tells them apart by their output size):

    dig-class11    digits, 32x20 input, 11 classes (0-9 and N)               --size s2
    dig-class100   digits, 32x20 input, 100 classes (0.0 .. 9.9)             --size s2
    ana-cont       analog dials, 32x32 input, 2 outputs (sin/cos of value)   --size s0..s3
    ana-class100   analog dials, 32x32 input, 100 classes (0.0 .. 9.9)       --size s2

Labelled images are read from one or more folders (file name = "<label>_<anything>.jpg",
see README.md), so you can mix your own images with the community image collections.
The script de-duplicates them, makes a stratified train/validation split, trains with
the upstream architectures and augmentation, and exports two models into --out:

    <type>_<name>_<size>.tflite      float model
    <type>_<name>_<size>_q.tflite    int8 post-training-quantised (float input/output)

Both are verified against the firmware contract (float32 input of the right shape,
output size 2/11/100, only ops the firmware's resolver knows) and evaluated on the
validation set, so you see the quantisation loss before deploying anything.

Examples:
    python train.py --type dig-class11 --data data/labeled/dig-class11 --epochs 100
    python train.py --type ana-cont --size s2 --data mine/ collection/ana-cont --name 2610
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import math
import os
import platform
import shlex
import sys
import time
import warnings
from pathlib import Path

# Quieter TensorFlow start-up (must be set before TensorFlow is imported).
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

from common import (DEFAULT_RESIZE, MODEL_TYPES, RESIZE_METHODS, ModelType,  # noqa: E402
                    TFLiteModel, check_firmware_contract,
                    compute_metrics, decode_outputs, dedupe_indices, die, file_sha1,
                    fmt_duration, format_label, headline, label_to_class, load_images,
                    load_labeled, log, phash_images, print_confusions, warn,
                    write_false_predictions)

ANA_CONT_SIZES = {      # conv filters per block, dense units
    "s0": ((16, 32, 64, 128), (128, 64)),
    "s1": ((16, 32, 64, 64), (128, 64)),
    "s2": ((16, 32, 32, 64), (128, 64)),
    "s3": ((16, 32, 32, 32), (64, 32)),
}

# Augmentation per model kind (applied on the fly to the training batches only; it is
# NOT part of the exported model). Values mirror the upstream training notebooks.
AUGMENT = {
    "digit": dict(shift=1, rotation=5.0, zoom=(0.7, 1.3), shear=0.0,
                  brightness=(0.8, 1.2), channel_shift=0.0, white_balance=None, invert=0.0),
    # No rotation for dials: rotating the image changes the reading.
    "analog": dict(shift=1, rotation=0.0, zoom=(0.95, 1.05), shear=1.0,
                   brightness=(0.8, 1.2), channel_shift=5.0, white_balance=(0.8, 1.2),
                   invert=0.2),
}


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------


def stratified_split(keys, val_split: float, rng):
    """Split indices so every class (key) is represented ~val_split in validation."""
    import numpy as np

    keys = np.asarray(keys)
    train, val = [], []
    for k in np.unique(keys):
        idx = np.flatnonzero(keys == k)
        rng.shuffle(idx)
        n_val = int(round(len(idx) * val_split))
        if len(idx) >= 2:
            n_val = max(1, min(n_val, len(idx) - 1))
        else:
            n_val = 0                      # a single example must be trained on
        val += idx[:n_val].tolist()
        train += idx[n_val:].tolist()
    return np.array(sorted(train)), np.array(sorted(val))


def targets_for(mt: ModelType, labels):
    import numpy as np

    labels = np.asarray(labels, np.float64)
    if mt.kind == "cont":
        a = 2 * math.pi * labels / 10.0
        return np.stack([np.sin(a), np.cos(a)], axis=1).astype(np.float32)
    return np.array([label_to_class(v, mt.kind) for v in labels], np.int32)


def make_augment(cfg: dict, h: int, w: int):
    """Return a batched tf augmentation function (affine + photometric), keras-like."""
    import tensorflow as tf

    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
    deg = math.pi / 180.0

    def aug(x, y):
        b = tf.shape(x)[0]

        def uni(lo, hi, shape=None):
            return tf.random.uniform(shape if shape is not None else [b], lo, hi)

        # --- geometry: one projective transform per image (output -> input mapping)
        th = uni(-cfg["rotation"], cfg["rotation"] + 1e-9) * deg
        sh = uni(-cfg["shear"], cfg["shear"] + 1e-9) * deg
        zx = uni(*cfg["zoom"])
        zy = uni(*cfg["zoom"])
        s = cfg["shift"]
        tx = tf.cast(tf.random.uniform([b], -s, s + 1, dtype=tf.int32), tf.float32)
        ty = tf.cast(tf.random.uniform([b], -s, s + 1, dtype=tf.int32), tf.float32)
        c, si = tf.cos(th), tf.sin(th)
        m00 = c * zx
        m01 = (-c * tf.sin(sh) - si * tf.cos(sh)) * zy
        m10 = si * zx
        m11 = (-si * tf.sin(sh) + c * tf.cos(sh)) * zy
        a2 = cx - m00 * cx - m01 * cy + tx
        b2 = cy - m10 * cx - m11 * cy + ty
        zeros = tf.zeros_like(m00)
        tr = tf.stack([m00, m01, a2, m10, m11, b2, zeros, zeros], axis=1)
        x = tf.raw_ops.ImageProjectiveTransformV3(
            images=x, transforms=tr, output_shape=[h, w], fill_value=0.0,
            interpolation="BILINEAR", fill_mode="NEAREST")

        # --- photometric
        x = x * uni(*cfg["brightness"])[:, None, None, None]
        if cfg["channel_shift"]:
            x = x + uni(-cfg["channel_shift"], cfg["channel_shift"])[:, None, None, None]
        if cfg["white_balance"]:
            x = x * uni(*cfg["white_balance"], shape=[b, 1, 1, 3])
        x = tf.clip_by_value(x, 0.0, 255.0)
        if cfg["invert"]:
            inv = uni(0.0, 1.0) < cfg["invert"]
            x = tf.where(inv[:, None, None, None], 255.0 - x, x)
        return x, y

    return aug


# ---------------------------------------------------------------------------
# Models (the first layer is always BatchNormalization on the raw 0..255 input)
# ---------------------------------------------------------------------------


def build_model(mt: ModelType, size: str):
    from tensorflow import keras
    from tensorflow.keras import layers, regularizers

    inp = keras.Input(shape=(mt.height, mt.width, 3), name="image")
    if mt.kind == "class11":
        x = layers.BatchNormalization()(inp)
        for _ in range(3):
            x = layers.Conv2D(32, 3, padding="same", activation="relu")(x)
            x = layers.MaxPooling2D(2)(x)
        x = layers.Flatten()(x)
        x = layers.Dense(256, activation="relu")(x)
        out = layers.Dense(11, activation="softmax")(x)
    elif mt.kind == "cont":
        filters, dense = ANA_CONT_SIZES[size]
        x = inp
        for f in filters:
            x = layers.BatchNormalization()(x)
            x = layers.Conv2D(f, 3, padding="same", activation="relu")(x)
            x = layers.MaxPooling2D(2)(x)
        x = layers.Flatten()(x)
        x = layers.BatchNormalization()(x)
        x = layers.Dense(dense[0], activation="relu",
                         kernel_regularizer=regularizers.l2(1e-4))(x)
        x = layers.Dropout(0.3)(x)
        x = layers.Dense(dense[1], activation="relu",
                         kernel_regularizer=regularizers.l2(1e-4))(x)
        out = layers.Dense(2, activation="linear")(x)
    else:  # class100
        # Pooling uses padding="same": with the 32x20 digit input a valid-padded chain
        # (pool 4 -> 4 -> 2) collapses the width to zero (20 -> 5 -> 1 -> 0). With "same"
        # the chain is 32x20 -> 8x5 -> 2x2 -> 1x1; for 32x32 it is identical to "valid"
        # (32 -> 8 -> 2 -> 1).
        x = layers.BatchNormalization()(inp)
        x = layers.Conv2D(32, 5, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.MaxPooling2D(4, padding="same")(x)
        x = layers.Dropout(0.15)(x)
        x = layers.Conv2D(32, 3, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.Dropout(0.15)(x)
        x = layers.Conv2D(48, 3, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.MaxPooling2D(4, padding="same")(x)
        x = layers.Dropout(0.15)(x)
        x = layers.Conv2D(48, 3, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.Dropout(0.15)(x)
        x = layers.Conv2D(64, 3, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.MaxPooling2D(2, padding="same")(x)
        x = layers.Dropout(0.15)(x)
        x = layers.Conv2D(64, 3, padding="same", activation="relu")(x)
        x = layers.BatchNormalization()(x)
        x = layers.Flatten()(x)
        x = layers.Dropout(0.3)(x)
        out = layers.Dense(100, name="logits")(x)   # trained on logits (stable CE)
    return keras.Model(inp, out, name=f"{mt.name}_{size}")


def export_model(model, mt: ModelType):
    """Model that is converted to .tflite.

    class100 networks are trained on logits; for the device a Softmax is appended, because
    the firmware computes its confidence as max/sum and a margin as log(p1/p2), which
    only make sense for probabilities. argmax (the reading) is unchanged.
    """
    from tensorflow import keras

    if mt.kind != "class100":
        return model
    return keras.Model(model.input, keras.layers.Softmax()(model.output), name=model.name)


def cont_within_01(y_true, y_pred):
    """Keras metric for ana-cont: fraction of readings within +-0.1 (wrapping at 10)."""
    import tensorflow as tf

    two_pi = 2 * math.pi
    vt = tf.math.floormod(tf.atan2(y_true[:, 0], y_true[:, 1]) / two_pi, 1.0) * 10.0
    vp = tf.math.floormod(tf.atan2(y_pred[:, 0], y_pred[:, 1]) / two_pi, 1.0) * 10.0
    d = tf.abs(vt - vp)
    d = tf.minimum(d, 10.0 - d)
    return tf.reduce_mean(tf.cast(d <= 0.1 + 1e-6, tf.float32))


cont_within_01.__name__ = "within_0.1"


# ---------------------------------------------------------------------------
# Export / verification
# ---------------------------------------------------------------------------


def convert(model, mt: ModelType, quantize: bool, rep_images=None) -> bytes:
    import numpy as np
    import tensorflow as tf

    # from_keras_model (as upstream). The resulting input tensor is (1, H, W, 3); a
    # concrete-function conversion would keep the variables as resources under Keras 3,
    # which breaks int8 calibration (READ_VARIABLE).
    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    if quantize:
        conv.optimizations = [tf.lite.Optimize.DEFAULT]

        def rep():
            for img in rep_images:
                yield [img[None].astype(np.float32)]

        conv.representative_dataset = rep
        conv._experimental_disable_per_channel_quantization_for_dense_layers = True
        # inference_input_type / inference_output_type are deliberately NOT set: the
        # firmware feeds and reads float32 tensors.
    # Keras prints a long "Saved artifact at ..." listing while converting; hide it.
    with contextlib.redirect_stdout(io.StringIO()), warnings.catch_warnings():
        warnings.simplefilter("ignore")
        return conv.convert()


def keras_predict(model, mt: ModelType, x):
    import tensorflow as tf

    raw = model.predict(x, batch_size=256, verbose=0)
    if mt.kind == "class100":
        raw = tf.nn.softmax(raw).numpy()
    return decode_outputs(raw, mt.kind)


class EpochLogger:
    """Keras callback that prints one line per epoch with timing and ETA."""

    def __new__(cls, total_epochs: int, metric: str):
        from tensorflow import keras

        class _Logger(keras.callbacks.Callback):
            def on_train_begin(self, logs=None):
                self.t0 = time.time()

            def on_epoch_begin(self, epoch, logs=None):
                self.te = time.time()

            def on_epoch_end(self, epoch, logs=None):
                logs = logs or {}
                now = time.time()
                per = (now - self.t0) / (epoch + 1)
                eta = per * (total_epochs - epoch - 1)
                lr = float(self.model.optimizer.learning_rate)
                m, vm = logs.get(metric), logs.get("val_" + metric)
                ms = (f"  {metric} {m:.4f} / val {vm:.4f}" if m is not None and vm is not None
                      else "")
                print(f"epoch {epoch + 1:>4}/{total_epochs}  loss {logs.get('loss', 0):.4f}  "
                      f"val_loss {logs.get('val_loss', 0):.4f}{ms}  lr {lr:.2g}  "
                      f"{now - self.te:5.1f}s  ETA {fmt_duration(eta)}", flush=True)

        return _Logger()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Train and export a model for AI-on-the-edge-device.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--type", required=True, choices=list(MODEL_TYPES), help="model type")
    ap.add_argument("--size", default="s2",
                    help="network size: s2 for class11/class100; s0 (largest) .. s3 (smallest) "
                         "for ana-cont")
    ap.add_argument("--data", nargs="+",
                    help="labelled image folders (default: data/labeled/<type>)")
    ap.add_argument("--epochs", type=int, default=100,
                    help="maximum epochs (training stops earlier when it no longer improves)")
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--val-split", type=float, default=0.2,
                    help="fraction of each class held out for validation")
    ap.add_argument("--optimizer", choices=["adam", "adadelta"], default="adam",
                    help="adam (faster on CPU) or adadelta (lr 1.0, rho 0.95; upstream class11)")
    ap.add_argument("--lr", type=float,
                    help="learning rate (default: adam 1e-3, 5e-4 for class100; adadelta 1.0)")
    ap.add_argument("--patience", type=int, default=40,
                    help="early-stopping patience in epochs (best weights are restored)")
    ap.add_argument("--no-augment", action="store_true", help="disable data augmentation")
    ap.add_argument("--resize", choices=RESIZE_METHODS, default=DEFAULT_RESIZE,
                    help="how crops are scaled to the model input: 'firmware' = exactly what the "
                         "device does (default); 'mitchellcubic' = upstream training notebooks")
    ap.add_argument("--no-dedupe", action="store_true", help="keep near-duplicate images")
    ap.add_argument("--dedupe-distance", type=int, default=2,
                    help="max perceptual-hash distance for near-duplicates (same label only)")
    ap.add_argument("--balance", action="store_true",
                    help="weight classes inversely to their frequency (class models only)")
    ap.add_argument("--representative", type=int, default=500,
                    help="number of real training images used to calibrate int8 quantisation")
    ap.add_argument("--name", default=time.strftime("%y%m%d"),
                    help="version tag used in the output file names")
    ap.add_argument("--out", help="output folder (default: output/<type>_<name>_<size>)")
    ap.add_argument("--seed", type=int, default=42, help="random seed (split, init, augmentation)")
    args = ap.parse_args()

    mt = MODEL_TYPES[args.type]
    if mt.kind == "cont":
        if args.size not in ANA_CONT_SIZES:
            die(f"--size for ana-cont must be one of {', '.join(ANA_CONT_SIZES)}")
    elif args.size != "s2":
        die(f"{mt.name} only has the upstream size s2")
    data_dirs = args.data or [f"data/labeled/{mt.name}"]
    stem = f"{mt.name}_{args.name}_{args.size}"
    out = Path(args.out or f"output/{stem}")
    out.mkdir(parents=True, exist_ok=True)
    t_start = time.time()

    import numpy as np
    import tensorflow as tf
    from tensorflow import keras

    keras.utils.set_random_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    log(f"TensorFlow {tf.__version__}, Keras {keras.__version__}, "
        f"{os.cpu_count()} CPU threads")
    kver = tuple(int(p) for p in keras.__version__.split(".")[:2])
    if kver > (3, 10):
        warn("Keras > 3.10 may emit ops the firmware cannot run; use requirements.txt")

    # ---------------------------------------------------------------- data
    files, labels, per_dir = [], [], {}
    for d in data_dirs:
        f, lab, skipped = load_labeled([d], mt.kind)
        per_dir[d] = {"images": len(f), "skipped_unlabelled_or_invalid": len(skipped)}
        log(f"{d}: {len(f)} labelled images" +
            (f" ({len(skipped)} without a valid {mt.kind} label skipped)" if skipped else ""))
        files += f
        labels += lab
    if not files:
        die("no labelled images found (file names must start with '<label>_')")
    n_loaded = len(files)

    # Exact duplicates (same bytes) across all folders, then near-duplicates per label.
    seen, keep = set(), []
    for i, f in enumerate(files):
        h = file_sha1(f)
        if h not in seen:
            seen.add(h)
            keep.append(i)
    n_exact = len(files) - len(keep)
    files = [files[i] for i in keep]
    labels = [labels[i] for i in keep]
    n_near = 0
    if not args.no_dedupe:
        keys = [format_label(v, mt.kind) for v in labels]
        hashes = phash_images(files, progress="dedupe (hashing)")
        keep = dedupe_indices(hashes, groups=keys, max_dist=args.dedupe_distance)
        n_near = len(files) - len(keep)
        files = [files[i] for i in keep]
        labels = [labels[i] for i in keep]
    log(f"{n_loaded} images -> {len(files)} after removing {n_exact} exact and "
        f"{n_near} near duplicates")

    dist: dict[str, int] = {}
    for v in labels:
        k = format_label(v, mt.kind) if mt.kind != "cont" else str(int(v))
        dist[k] = dist.get(k, 0) + 1
    if len(dist) > 20:      # class100: summarise per integer part, 100 entries are unreadable
        coarse: dict[str, int] = {}
        for k, v in dist.items():
            coarse[k[0] + ".x"] = coarse.get(k[0] + ".x", 0) + v
        log(f"Label distribution ({len(dist)} of 100 classes present): " +
            ", ".join(f"{k}: {v}" for k, v in sorted(coarse.items())))
    else:
        log("Label distribution: " + ", ".join(f"{k}: {v}" for k, v in sorted(dist.items())))
    if mt.kind == "class11":
        missing = [format_label(c, "class11") for c in range(11)
                   if format_label(c, "class11") not in dist]
        if missing:
            warn(f"no training images for class(es) {', '.join(missing)} - the model cannot "
                 "learn them")
    if len(dist) > 1 and max(dist.values()) > 10 * min(dist.values()):
        warn(f"strong class imbalance (max {max(dist.values())} vs min {min(dist.values())} "
             "images per class); consider --balance or adding images of rare classes")

    log(f"Resize method: {args.resize}")
    x = load_images(files, mt.height, mt.width, resize=args.resize, progress="loading images")
    y = targets_for(mt, labels)
    strat = [label_to_class(v, mt.kind) if mt.kind != "cont" else int(v) for v in labels]
    tr, va = stratified_split(strat, args.val_split, rng)
    if len(va) == 0:
        die("not enough images for a validation split")
    log(f"Split: {len(tr)} training / {len(va)} validation images")
    x_tr, y_tr, x_va, y_va = x[tr], y[tr], x[va], y[va]
    lab_va = np.asarray(labels, np.float64)[va]

    # ---------------------------------------------------------------- model
    model = build_model(mt, args.size)
    if args.optimizer == "adadelta":
        opt = keras.optimizers.Adadelta(learning_rate=args.lr or 1.0, rho=0.95)
    else:
        opt = keras.optimizers.Adam(learning_rate=args.lr or
                                    (5e-4 if mt.kind == "class100" else 1e-3))
    lr0 = float(opt.learning_rate)
    if mt.kind == "class11":
        loss, metric, mname = "sparse_categorical_crossentropy", ["accuracy"], "accuracy"
    elif mt.kind == "class100":
        loss = keras.losses.SparseCategoricalCrossentropy(from_logits=True)
        metric, mname = ["accuracy"], "accuracy"
    else:
        loss, metric, mname = "mse", [cont_within_01], "within_0.1"
    model.compile(optimizer=opt, loss=loss, metrics=metric)
    model.summary(print_fn=lambda s, **_: log(s))

    aug_cfg = AUGMENT["analog" if mt.section == "Analog" else "digit"]
    ds_tr = tf.data.Dataset.from_tensor_slices((x_tr, y_tr)) \
        .shuffle(len(x_tr), seed=args.seed, reshuffle_each_iteration=True) \
        .batch(args.batch_size)
    if not args.no_augment:
        ds_tr = ds_tr.map(make_augment(aug_cfg, mt.height, mt.width),
                          num_parallel_calls=tf.data.AUTOTUNE)
    ds_tr = ds_tr.prefetch(tf.data.AUTOTUNE)
    ds_va = tf.data.Dataset.from_tensor_slices((x_va, y_va)).batch(256)

    class_weight = None
    if args.balance and mt.kind != "cont":
        cls, cnt = np.unique(y_tr, return_counts=True)
        class_weight = {int(c): float(len(y_tr) / (len(cls) * n)) for c, n in zip(cls, cnt)}

    callbacks = [
        keras.callbacks.EarlyStopping(monitor="val_loss", patience=args.patience,
                                      restore_best_weights=True),
        EpochLogger(args.epochs, mname),
    ]
    if args.optimizer == "adam":
        callbacks.insert(0, keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss", factor=0.5, patience=max(3, args.patience // 4), min_lr=1e-5))

    log(f"\nTraining {model.name} for up to {args.epochs} epochs "
        f"(batch {args.batch_size}, {args.optimizer}, augmentation "
        f"{'off' if args.no_augment else 'on'}) ...")
    t_train = time.time()
    hist = model.fit(ds_tr, validation_data=ds_va, epochs=args.epochs, verbose=0,
                     callbacks=callbacks, class_weight=class_weight)
    t_train = time.time() - t_train
    epochs_run = len(hist.history["loss"])
    best_epoch = int(np.argmin(hist.history["val_loss"])) + 1
    log(f"Training finished after {epochs_run} epochs in {fmt_duration(t_train)} "
        f"(best epoch {best_epoch})")
    model.save(out / f"{stem}.keras")

    # ---------------------------------------------------------------- evaluation
    results = {}
    k_pred, _ = keras_predict(model, mt, x_va)
    results["keras"] = compute_metrics(mt.kind, lab_va, k_pred)
    log(f"\nKeras model, validation: {headline(mt.kind, results['keras'])}")

    # ---------------------------------------------------------------- export
    exp = export_model(model, mt)
    n_rep = min(args.representative, len(x_tr))
    rep = x_tr[rng.choice(len(x_tr), n_rep, replace=False)]
    paths = {"float": out / f"{stem}.tflite", "quantized": out / f"{stem}_q.tflite"}
    log(f"\nExporting float and int8 models (quantisation calibrated on {n_rep} real "
        "training images) ...")
    paths["float"].write_bytes(convert(exp, mt, quantize=False))
    paths["quantized"].write_bytes(convert(exp, mt, quantize=True, rep_images=rep))

    failed = False
    contract = {}
    for kind_, p in paths.items():
        problems = check_firmware_contract(p, expect=mt)
        m = TFLiteModel(p)
        contract[kind_] = {"file": p.name, "bytes": p.stat().st_size,
                           "ops": sorted(m.op_names()), "problems": problems}
        if problems:
            failed = True
            for pr in problems:
                print(f"FIRMWARE CONTRACT VIOLATED ({p.name}): {pr}", file=sys.stderr)
        pv, _ = m.predict(x_va)
        results[f"tflite_{kind_}"] = compute_metrics(mt.kind, lab_va, pv)
        log(f"{p.name} ({p.stat().st_size / 1024:.0f} KiB), validation: "
            f"{headline(mt.kind, results[f'tflite_{kind_}'])}")

    main_metric = "within_0.1" if mt.kind == "cont" else "accuracy"
    q_loss = results["keras"][main_metric] - results["tflite_quantized"][main_metric]
    log(f"Quantisation loss: {q_loss * 100:+.2f} percentage points of {main_metric}")
    print_confusions(mt.kind, results["tflite_quantized"])

    # False predictions of the deployed (quantised) model on ALL images: the training
    # rows are the quickest way to find wrongly labelled images.
    qm = TFLiteModel(paths["quantized"])
    all_pred, all_conf = qm.predict(x)
    split = np.array(["train"] * len(files), dtype=object)
    split[va] = "val"
    n_false = write_false_predictions(out / "false_predictions.csv", files, labels, all_pred,
                                      mt.kind, all_conf, splits=split)
    log(f"{n_false} wrong predictions (train + val) written to {out / 'false_predictions.csv'}")

    metrics = {
        "type": mt.name, "size": args.size, "name": args.name, "resize": args.resize,
        "data": {"folders": per_dir, "loaded": n_loaded, "exact_duplicates": n_exact,
                 "near_duplicates": n_near, "used": len(files), "train": int(len(tr)),
                 "validation": int(len(va)), "label_distribution": dist},
        "training": {"epochs_max": args.epochs, "epochs_run": epochs_run,
                     "best_epoch": best_epoch, "seconds": round(t_train, 1),
                     "optimizer": args.optimizer, "learning_rate": lr0,
                     "batch_size": args.batch_size, "augmentation": None if args.no_augment
                     else aug_cfg, "balance": args.balance, "seed": args.seed,
                     "history": {k: [float(v) for v in vs] for k, vs in hist.history.items()}},
        "validation": results, "quantization_loss": q_loss, "export": contract,
    }
    (out / "metrics.json").write_text(json.dumps(metrics, indent=1) + "\n")
    write_model_card(out / "model-card.md", args, mt, metrics, results, contract, n_rep,
                     tf.__version__, keras.__version__)
    log(f"\nWrote {out}/: {paths['float'].name}, {paths['quantized'].name}, metrics.json, "
        f"false_predictions.csv, model-card.md, {stem}.keras  "
        f"(total {fmt_duration(time.time() - t_start)})")
    if failed:
        die("the exported model violates the firmware contract (see above) - do NOT deploy it")
    log(f"Deploy: python deploy.py {paths['quantized']} --device <ip> --activate")


def write_model_card(path: Path, args, mt: ModelType, m: dict, results: dict, contract: dict,
                     n_rep: int, tf_ver: str, keras_ver: str) -> None:
    d, t = m["data"], m["training"]
    main_metric = "within_0.1" if mt.kind == "cont" else "accuracy"

    def pct(r, k):
        v = r.get(k)
        return "-" if v is None else f"{v * 100:.2f}%"

    rows = "\n".join(
        f"| {name} | {pct(results[key], 'accuracy') if mt.kind != 'cont' else '-'} | "
        f"{pct(results[key], 'within_0.1') if mt.kind != 'class11' else '-'} | "
        f"{results[key]['samples']} |"
        for name, key in (("Keras", "keras"), ("TFLite float", "tflite_float"),
                          ("TFLite int8 (_q)", "tflite_quantized")))
    dist = ", ".join(f"{k}: {v}" for k, v in sorted(d["label_distribution"].items()))
    folders = "\n".join(f"- `{k}`: {v['images']} images" for k, v in d["folders"].items())
    aug = "off" if t["augmentation"] is None else ", ".join(
        f"{k}={v}" for k, v in t["augmentation"].items())
    ops = ", ".join(contract["quantized"]["ops"])
    text = f"""# Model card: {mt.name} {args.name} {args.size}

Trained {time.strftime('%Y-%m-%d %H:%M')} with tools/model-training/train.py
(TensorFlow {tf_ver}, Keras {keras_ver}, Python {platform.python_version()}).

Command: `{' '.join(shlex.quote(a) for a in sys.argv)}`

## Data

{folders}

- Loaded {d['loaded']} images; removed {d['exact_duplicates']} exact and {d['near_duplicates']}
  near duplicates (perceptual hash distance <= {args.dedupe_distance}, same label only)
- Used {d['used']} images: {d['train']} training / {d['validation']} validation
  (stratified, validation fraction {args.val_split}, seed {args.seed})
- Label distribution: {dist}

## Training

- Architecture: upstream {mt.name} {args.size}; input {mt.height}x{mt.width}x3 float32,
  raw 0..255 (first layer BatchNormalization), {mt.outputs} outputs
- Preprocessing: crops scaled to {mt.width}x{mt.height} with resize method `{args.resize}`
  ({'bit-exact port of the firmware bilinear downscale' if args.resize == 'firmware'
    else 'tf.image.resize, differs from what the device feeds the model'}); the same
  method is used for training, validation, int8 calibration and evaluation
- Optimizer {t['optimizer']}, learning rate {t['learning_rate']:.2g} (initial), batch {t['batch_size']},
  class weighting {'on' if t['balance'] else 'off'}
- Epochs: {t['epochs_run']} of max {t['epochs_max']} (early stopping patience {args.patience},
  best epoch {t['best_epoch']} restored), {fmt_duration(t['seconds'])}
- Augmentation (training only, not in the model): {aug}

## Validation metrics

| Model | Accuracy (exact class) | Within +-0.1 | Images |
|---|---|---|---|
{rows}

Quantisation loss ({main_metric}): {m['quantization_loss'] * 100:+.2f} percentage points.

## Export

- `{contract['float']['file']}` ({contract['float']['bytes']} bytes): float32 model
- `{contract['quantized']['file']}` ({contract['quantized']['bytes']} bytes): post-training
  int8 quantisation, `optimizations=[Optimize.DEFAULT]`, representative dataset =
  {n_rep} random real training images (no augmentation),
  `_experimental_disable_per_channel_quantization_for_dense_layers=True`, input/output
  left as float32 (inference_input_type/inference_output_type not set)
- Converted with `TFLiteConverter.from_keras_model`; input tensor (1, {mt.height}, {mt.width}, 3)
{'- A Softmax layer is appended to the class100 logits for export' + chr(10)
 if mt.kind == 'class100' else ''}- Ops: {ops}
- Firmware contract check: {'PASSED' if not (contract['float']['problems'] or
                                             contract['quantized']['problems']) else 'FAILED'}
"""
    path.write_text(text)


if __name__ == "__main__":
    main()
