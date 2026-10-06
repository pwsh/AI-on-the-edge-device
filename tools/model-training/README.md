# Model training toolkit

Train your own digit / analog-dial models for AI-on-the-edge-device on your own
computer (no Colab, no Jupyter, CPU is enough). All scripts have `--help`.

## Setup

```bash
cd tools/model-training
python3.12 -m venv .venv && . .venv/bin/activate     # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

TensorFlow must stay at 2.18 and Keras at <= 3.10: newer versions produce models the
firmware cannot load. On macOS replace `tensorflow-cpu` with `tensorflow` in
`requirements.txt`.

## Quick start (digits)

```bash
# 1. get images: ROI images logged by the device ...
python fetch_images.py --device 192.168.1.50 --pause
#    ... or, if you only have raw frames, cut the ROIs yourself
python extract_rois.py --device 192.168.1.50 --raw data/device/192.168.1.50/log/source/raw

# 2. let the current model pre-label them, dropping near-duplicates
python prelabel.py --model ../../sd-card/config/dig-class11_1910_s2_q.tflite \
    --input data/device/192.168.1.50/log/digit --dedupe

# 3. check / correct the labels in the browser (http://127.0.0.1:8765)
python label_tool.py --dir data/review/dig-class11

# 4. train (optionally add a community image collection as a second --data folder)
python train.py --type dig-class11 --data data/labeled/dig-class11 --name 2610

# 5. compare the stock model with yours on your images
python evaluate.py ../../sd-card/config/dig-class11_1910_s2_q.tflite data/labeled/dig-class11
python evaluate.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite data/labeled/dig-class11

# 6. upload, switch config.ini to it and reboot
python deploy.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite \
    --device 192.168.1.50 --activate
```

## File names = labels

Labelled images are named `<label>_<anything>.jpg`, compatible with the upstream
community image collections:

| Model type | Label | Example |
|---|---|---|
| `dig-class11` | one character `0`..`9` or `N` (not a number) | `7_3f9c0a1b2c3d4e5f.jpg` |
| `dig-class100`, `ana-cont`, `ana-class100` | `x.y`, 0.0 .. 9.9 | `3.7_3f9c0a1b2c3d4e5f.jpg` |

A single-digit label (`7_...`) is read as `7.0` by the class100/analog types. Files
starting with `unlabeled_` are never used for training.

## Two things that matter for good models

**Resize like the device.** The firmware scales each ROI crop to the model input
(20x32 digits, 32x32 dials) with its own simple bilinear downscale. A model sees
slightly different pixels if it is trained on crops scaled another way: up to ~5 grey
levels on blurry LCD digits, and up to ~18 on sharp analog dials. So `prelabel.py`,
`train.py` and `evaluate.py` use a bit-exact copy of the firmware's resize by default
(`--resize firmware`). `--resize mitchellcubic` reproduces the upstream training
notebooks. `python resize_check.py <crop.jpg>` shows the difference on one of your crops.

**Device file names are not ground truth.** ROI images logged by the device are named
`<reading>_c<confidence>_<roi>_<time>.jpg`, and the first token is the device's
*post-processed* result. In particular `N` / `10` often just means "confidence below
`DigitConfidenceThreshold`" for a perfectly readable digit, not an unreadable one.
Always pass such images through `prelabel.py` and check them in `label_tool.py`
instead of training on the device names directly.

## Scripts

**`fetch_images.py`** downloads `/log/digit` and `/log/analog` (and with `--raw` the raw
frames in `/log/source/raw`) into `data/device/<device>/`, keeping the device's folder
structure. Files you already have are skipped, so run it as often as you like.
`--pause` pauses the device's processing during the transfer (it is always resumed,
also on Ctrl-C); `--zip` downloads each folder as one ZIP on firmware that supports it.

**`extract_rois.py`** cuts the `[Digits]` / `[Analog]` ROIs from your `config.ini`
(`--config file` or `--device ip`) out of raw full frames into
`data/crops/<digits|analog>/<number>_<roi>/`. ROI coordinates refer to the *aligned*
image, so this is only correct with `AlignmentAlgo = off` and no rotation; otherwise it
stops (override with `--force`). `--every N` / `--limit N` thin out long time series.

**`prelabel.py`** runs a `.tflite` model exactly like the firmware over a folder of
crops, writes `data/review/<type>/predictions.csv` and copies (`--move`: moves) the
images to `data/review/<type>/confident/` or `unsure/` (`--threshold`, default 0.9),
renamed to `<prediction>_c<confidence>_<hash>.jpg`. `--dedupe` drops near-duplicate
images, the most common dataset problem (a meter that does not move produces thousands
of identical crops). Images you have already handled are skipped by content hash, so
re-running it with new images or a new model only shows new ones: anything in
`data/labeled/<type>/` (`--labeled`), anything still in `data/review/<type>/` and anything
deleted in `label_tool.py`; `--no-skip-handled` turns this off.

**`label_tool.py`** is a small local web page for labelling with the keyboard: `0`-`9`
and `n` (digits) or two digits like `3` `7` = 3.7 / `+` `-` / slider (class100 and
analog), `space`/`enter` accepts the shown value, `d` deletes, arrow keys navigate,
`u` undoes. Accepted images are moved to `data/labeled/<type>/<label>_<hash>.jpg`;
"Accept all remaining with confidence >= X" accepts the easy ones in one go. `d` moves
the image to `data/review/<type>/_trash/` (not deleted), so `prelabel.py` remembers it and
does not offer it again (empty that folder to have them offered again).

*Fixing wrong labels.* Point `--dir` at a labelled folder and `label_tool.py` works in
place (relabel mode, automatic for `<label>_<hash>.jpg` names, or force it with
`--relabel`): the page shows the *current label*, a different label renames the file
in the same folder (`3_<hash>.jpg` -> `5_<hash>.jpg`, the hash is kept),
`space`/`enter` keeps the current label, `d` moves the file to `_trash/` inside that
folder (train.py ignores it) and `u` undoes. `--from-csv` only shows the images in a
`false_predictions.csv` from `train.py` / `evaluate.py`, the most confident
disagreements first (those are the likely label errors), with what the model said:

```bash
python label_tool.py --dir data/labeled/dig-class11 --from-csv output/<run>/false_predictions.csv
python label_tool.py --dir data/labeled/dig-class11      # go through the whole folder
```

`--model <file.tflite>` additionally runs a model over the shown images and shows its
reading as a hint (needs the packages from `requirements.txt`; everything else in
`label_tool.py` runs on plain Python).

**`train.py`** trains `--type dig-class11 | dig-class100 | ana-cont | ana-class100`
(`--size s0..s3` for `ana-cont`, `s2` otherwise) from one or more `--data` folders. It
removes duplicates, makes a stratified 80/20 split (`--val-split`), trains with
augmentation and early stopping, and writes to `output/<type>_<name>_<size>/`: the float
`.tflite`, the int8 `_q.tflite` (recommended for the device), `metrics.json`,
`false_predictions.csv` (check those images with `label_tool.py --from-csv`, they are
often labelled wrong),
`model-card.md` and the Keras model. Both `.tflite` files are checked against what the
firmware accepts and evaluated, so the quantisation loss is visible before deploying.
`--optimizer adadelta` reproduces the original digit training; `--balance` weights rare
classes up; `--seed` makes runs repeatable.

**`resize_check.py`** resizes a crop with the firmware method, an exact scalar
re-implementation of the C code (a self-check of the port) and TensorFlow's bilinear /
Mitchell-cubic, and prints how many pixels differ and by how much (`--model` also shows
the reading for each variant).

`prelabel.py` and `evaluate.py` also accept the older `dig-cont` models (10 outputs),
which the firmware still runs; `train.py` does not train that type.

**`evaluate.py`** reports the same metrics for any `.tflite` on your labelled images
(e.g. stock model vs. yours) and writes `false_predictions.csv` and `metrics.json` to
`eval/<model>/`.

**`deploy.py`** uploads a model to `/config/` on the device and reads it back to verify
it byte-for-byte. With `--activate` it also backs up `config.ini` to `backups/`, points
the `[Digits]` or `[Analog]` `Model =` line at the new file, uploads and verifies it, and
asks before rebooting (`--yes` skips the question). If the new `config.ini` cannot be
written correctly, the original is put back and the device is not rebooted.

`common.py` holds the code shared by all scripts (device client, label parsing,
firmware-identical preprocessing, metrics).
