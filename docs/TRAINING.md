# Training your own models

This guide takes you from "the stock model misreads my meter" to a `.tflite` model trained on
images of **your** meter, running on your device. It needs no Jupyter, no Colab and no GPU — a
laptop CPU trains these small networks in minutes.

The whole loop is:

1. **Collect** — the device saves cropped ROI images, pre-labelled with what the current model read
   and how confident it was.
2. **Download** them to your PC.
3. **Pre-label and de-duplicate** — a script sorts the crops by prediction and removes near-identical
   frames.
4. **Label** — you confirm or correct labels in a keyboard-driven browser tool. Thanks to step 3
   this is mostly pressing *Enter*.
5. **Train** — one command produces a float and a quantised `.tflite`, verified against the firmware's
   requirements.
6. **Evaluate** the new model against the stock one on *your* images.
7. **Deploy** to the device and switch the config to the new model.
8. **Iterate** — keep collecting; the device keeps saving the reads it was unsure about.

All scripts live in [`tools/model-training/`](../tools/model-training/) and have `--help`.

---

## 0. Which model type do you need?

| Your meter | Model type | Output | Train with |
| --- | --- | --- | --- |
| 7-segment LCD, or mechanical counter where you only need whole digits | **dig-class11** | 11 classes: `0`–`9` + `N` (not readable / mid-roll) | `train.py --type dig-class11` |
| Mechanical rolling counter where you want tenths (e.g. `3.7` = the 3 rolling to 4) | **dig-class100** | 100 classes `0.0`–`9.9` | `train.py --type dig-class100` |
| Analog pointer dial | **ana-cont** (recommended) | 2 outputs (sin/cos of the pointer angle) | `train.py --type ana-cont` |
| Analog pointer dial, classification variant | **ana-class100** | 100 classes `0.0`–`9.9` | `train.py --type ana-class100` |

The firmware decides the model type from the **output tensor size** (11, 100, 2) and the input size
(digits 20 × 32, analog 32 × 32). `train.py` enforces this, so a model it exports will always be
accepted by the device.

> **LCD / 7-segment meters:** use `dig-class11`. Label a digit `N` only when it is genuinely
> unreadable (glare, mid-update flicker). Do not use class100 — there are no in-between states.
>
> **Rolling counters:** the stock `dig-class100` / `dig-cont` models already handle most of them well.
> Train your own only if the font, lighting or camera angle is unusual.

---

## 1. Collect images on the device

Open **Configuration → Digits** (and/or **Analog**) in the web UI, enable *Expert mode*, and set:

| Parameter | Value | Notes |
| --- | --- | --- |
| `ROIImages` | `true` | Enables saving the ROI crops. |
| `ROIImagesMode` | `changed` | **New.** Saves a crop only when the read **changes** for that ROI, or the model was **unsure** (low confidence, rejected, `N`, overruled by the temporal vote). A static meter writes almost nothing; every transition and every doubtful read is still captured. `all` saves every inferred ROI every round (the old behaviour — lots of near-duplicates and SD wear). |
| `ROIImagesLocation` | `/log/digit` (or `/log/analog`) | Default if left empty. |
| `ROIImagesRetention` | `3`–`7` days | Folders older than this are deleted automatically. Download before they expire. |

Leave `RawImages` **off** unless you specifically want full frames (see §2b) — a full 640 × 480 JPEG
per round is the biggest SD-card load the firmware can produce.

What gets written, per inferred ROI:

```
/log/digit/<YYYYMMDD>/<HH>/<label>_c<NN>_<number>_<roi>_<YYYYMMDD-HHMMSS>.jpg
                            │       │     │        │
                            │       │     │        └ ROI name from your config (e.g. main4)
                            │       │     └ number/sequence name (e.g. main)
                            │       └ confidence 00–99 % (absent on very old firmware)
                            └ what the current model read: 0–9, 10 (= N) for class11 models,
                              or x.y for class100 / analog / continuous models
```

The saved image is the **raw crop** at the ROI's configured size (not the 20 × 32 model input), so
you can retrain at full fidelity and the training script does the resize exactly like the firmware.

**How long to collect?** You want every digit value in every position. On a water/gas meter the
last digit cycles in hours; the second-to-last in days; higher digits in weeks or months. In
`changed` mode this is cheap, so just leave it on. For the slow positions, mix in the community
image sets (§5) — they cover all ten digits in many fonts.

> **FastRead / PredictiveRead:** digits that are *not* re-inferred in a round (cached because the
> lower digit hasn't rolled over) are not saved — there is nothing new to learn from them.

> **ESP32-CAM with a slow SD card:** per-round SD writes are what caused the "rounds hang" problem
> on some boards. `changed` mode keeps writes rare; still, if you see rounds stalling, lower
> `ROIImagesRetention`, or collect in shorter sessions.

---

## 2. Download the images

### 2a. From the device's ROI log (normal case)

```bash
cd tools/model-training
python fetch_images.py --device 10.0.42.12 --pause
```

* Downloads `/log/digit` and `/log/analog` (add `--raw` for the full frames in `/log/source/raw`) into
  `data/device/10.0.42.12/…`, keeping the device's folder layout. Re-running only fetches **new** files.
* `--pause` pauses the device's processing for the duration (and always resumes, also on Ctrl-C).
  The device drops HTTP requests while a round is running, so this makes bulk transfers 3–5× faster
  and the script retries anyway.
* `--zip` uses the file browser's **Download folder as ZIP** (one streamed archive instead of
  thousands of requests). Firmware from this release streams the ZIP without a temp file, so it
  works for folders with tens of thousands of files; older firmware builds the whole ZIP on the SD
  card first and may time out — the script falls back to per-file downloads.

You can also just click the 📥 icon next to a folder in **System → File server** and unzip by hand.

### 2b. From raw full frames (if you only logged `RawImages`)

If you have full-frame logs (`/log/source/raw/…`) but no ROI crops, cut the ROIs out offline using
your `config.ini`:

```bash
python extract_rois.py --device 10.0.42.12 --raw data/device/10.0.42.12/log/source/raw
# or: --config /path/to/config.ini --raw <folder with raw_*.jpg>
# (--every 5 thins out long time series; consecutive frames are near-identical)
```

Crops land in `data/crops/<digits|analog>/<number>_<roi>/`.

> ⚠️ This is only correct when **`AlignmentAlgo = off`** (the ROI coordinates then apply directly to
> the camera frame). With alignment enabled, raw frames are *pre-alignment* and the ROI boxes would
> be offset; the script refuses to run unless you pass `--force`.

---

## 3. Pre-label and de-duplicate

Let the current model do the first pass:

```bash
python prelabel.py --model ../../sd-card/config/dig-class11_1910_s2_q.tflite --dedupe \
                   --input data/device/10.0.42.12/log/digit
```

* Runs the model exactly like the firmware: the crop is downscaled with a re-implementation of the
  firmware's own resize (not a generic image-library filter — the difference is visible to the model),
  raw 0–255 pixels, no scaling. `--resize mitchellcubic` switches to the upstream notebooks' filter if
  you want to reproduce their results. `python resize_check.py <crop.jpg>` shows the pixel difference
  between the two for one of your images (small for blurry LCD digits, up to ~18 grey levels on sharp
  analog dials).
* `--dedupe` drops near-identical crops (perceptual hash). On a real `ROIImagesMode = all` collection
  from a water meter, **98.6 % of 16,500 crops were duplicates** (235 unique images left). Near-
  duplicates in a training set inflate accuracy numbers and teach the model nothing. For LCD/7-segment
  meters the default distance (2) is aggressive; `--dedupe-distance 0` keeps about 3× more.
* **The label in a device filename is the model's prediction, not the truth** — that is the whole
  point of labelling. In particular an `N` (`10_…`) from the device often just means "confidence was
  below `DigitConfidenceThreshold`", not "unreadable"; those are usually perfectly readable digits and
  the most valuable ones to label correctly.
* Output goes to `data/review/<type>/confident/` (prediction ≥ 0.90) and `…/unsure/`, each file
  renamed `<prediction>_c<NN>_<hash>.jpg`. The hash rename is the upstream convention — it removes
  timestamps so you can share images without leaking when you were home.
* `predictions.csv` lists every file with prediction and confidence.

The model type (class11 / class100 / analog) is detected from the model file. For crops from §2b use
the same command with `--input data/crops/digits`.

---

## 4. Label

```bash
python label_tool.py --dir data/review/dig-class11
```

Open <http://127.0.0.1:8765>. The tool shows one crop at a time, enlarged, with the predicted label.

| Key | Action |
| --- | --- |
| `Enter` / `Space` | accept the predicted label |
| `0`–`9`, `n` | set the label (class11) |
| two digits, or `+` / `-` | set `x.y` in 0.1 steps (class100 / analog) |
| `d` | delete (unusable image) |
| `←` / `→` | previous / next |
| `u` | undo |
| *Accept all ≥ X %* button | bulk-accept the confident remainder |

Labelled files are **moved** to `data/labeled/<type>/<label>_<hash>.jpg`. Do the `unsure/` folder by
hand (those are the valuable ones); for `confident/`, skim a few pages and then bulk-accept.

### Labelling rules

* **dig-class11:** the digit you see. `N` only if a human can't tell either. Partially rolled
  digits on a mechanical counter: label the digit that occupies **more than half** of the window;
  if it's a coin-flip, `N`.
* **dig-class100:** `x.y` where `x` is the digit leaving and `y` is how far the roll has progressed
  in tenths (`3.0` = a clean 3, `3.5` = halfway between 3 and 4). Upstream has a
  [visual guide](https://github.com/jomjol/neural-network-digital-counter-readout#labeling).
* **Analog:** the value the pointer indicates, `0.0`–`9.9`, read clockwise. Be consistent to
  ±0.1 — the evaluation metric is "within ±0.1". **Counter-clockwise dials:** the model always
  works in clockwise space and the firmware mirrors the result (`CCW` flag), so label such images
  with the value a *clockwise* scale would show at that pointer position (`10 − reading`). Note the
  device's own filenames for CCW ROIs carry the mirrored reading.

Aim for **at least 50 images per class** for your own meter before expecting a measurable
improvement; a few hundred per class is better. Class balance matters more than raw count.

---

## After labelling: from images to a running model

Once `label_tool.py` has moved your images into `data/labeled/<type>/`, three commands take you to a
model running on the device. Everything below assumes the digit case; swap the type and section for
analog.

Run these from `tools/model-training` with the venv active (one command per line — the comments
say what each produces):

```bash
# 1. Train  -> output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite (+ float model, metrics, model card)
python train.py --type dig-class11 --name 2610 --data data/labeled/dig-class11 --balance

# 2. Check  -> accuracy + false_predictions.csv; run it on the stock model too and compare
python evaluate.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite data/labeled/dig-class11
python evaluate.py ../../sd-card/config/dig-class11_1910_s2_q.tflite        data/labeled/dig-class11

# 3. Deploy -> model in /config/ on the device, config.ini pointing at it, device rebooted
python deploy.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite --device <ip> --section Digits --activate
```

Then confirm on the device (§7). The three sections below explain each step.

`--name` is just the version tag in the file name (`2610` = October 2026 — any short string works; the
device shows the file name, so pick something you'll recognise in the Model dropdown).

## 5. Train

```bash
python train.py --type dig-class11 --name 2610 \
                --data data/labeled/dig-class11 /path/to/community/images
#                     ^ your images          ^ optional extra folders, see below
```

What you should see: a per-epoch progress line, then a summary like

```
Keras model, validation: accuracy 99.3 %
Exporting float and int8 models (quantisation calibrated on 500 real images) ...
dig-class11_2610_s2.tflite (112 KiB), validation: accuracy 99.3 %
dig-class11_2610_s2_q.tflite (34 KiB), validation: accuracy 99.3 %
Quantisation loss: +0.00 percentage points of accuracy
3 wrong predictions (train + val) written to output/dig-class11_2610_s2/false_predictions.csv
Wrote output/dig-class11_2610_s2/: ... metrics.json, model-card.md
Deploy: python deploy.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite --device <ip> --activate
```

(numbers illustrative). Good = the `_q` accuracy within ~1 point of Keras and above the stock model's
score on your images (§6). A `FIRMWARE CONTRACT VIOLATED` line means the file must not go on the
device. If training stops after a handful of epochs with low accuracy, you have too few or badly
balanced images — check the class counts printed at the start. **Use the `_q.tflite` on the device.**

* Architectures, augmentation and export settings follow the upstream training notebooks
  (`dig-class11_…_s2`, `ana-cont_…_s2`, …), so results are directly comparable with the stock models.
* CPU time is small: 2,500 digit images × 15 epochs trained in about 10 s on a 6-core laptop, so
  even a few thousand images for 100+ epochs stay under a few minutes; analog models take longer.
  Early stopping is on by default; `--epochs` caps it.
* `--balance` up-weights rare classes — a water meter shows `0` far more often than `7`, and with no
  `N` images at all the model cannot learn that class; `--seed` makes runs repeatable.
* Output in `output/<type>_<name>_<size>/`:
  * `<type>_<name>_<size>.tflite` — float model
  * `<type>_<name>_<size>_q.tflite` — int8-quantised (**use this one on the device**; 3–4× smaller
    and faster, usually < 1 % accuracy loss — the script prints the exact difference)
  * `metrics.json`, `false_predictions.csv`, `model-card.md`
* The script **verifies** both files with the TensorFlow Lite interpreter: input float32 of the
  right shape, output size 11 / 100 / 2, and re-runs the validation set through the `.tflite`.

**Mixing in the community data.** The upstream image collections are the fastest way to cover
digits your meter hasn't shown yet:

* Digits: <https://github.com/jomjol/neural-network-digital-counter-readout> → `images/` (class100
  labels `x.y_…jpg`; the class11 trainer maps them to whole digits automatically).
* Analog: <https://github.com/jomjol/neural-network-analog-needle-readout> → `images/`.

Clone the repo and list its image folder(s) after `--data` together with your own. Your own images are
what teaches the model your font and lighting; the community set stops it from forgetting the rest.

---

## 6. Evaluate

Compare the stock model and yours on the same labelled images (ideally images collected *after*
you trained, so they are truly unseen):

```bash
python evaluate.py ../../sd-card/config/dig-class11_1910_s2_q.tflite data/labeled/dig-class11
python evaluate.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite data/labeled/dig-class11
```

Each run prints accuracy (or the within-±0.1 rate for analog) and writes `false_predictions.csv`.
Open the false predictions — if the same few images fail with both models they're probably
mislabelled.

---

## 7. Deploy

```bash
python deploy.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite \
                 --device 10.0.42.12 --section Digits --activate
```

What it does, in order (it prints each step):

1. Checks the model file against the firmware contract (same check as `train.py`).
2. Pauses the device's processing.
3. Uploads the model to `/config/<file>.tflite` and reads it back to verify it byte-for-byte.
4. With `--activate`: downloads `config.ini`, saves a timestamped copy under `backups/`, rewrites the
   `Model =` line of the chosen section, uploads it, reads it back and verifies it. If verification
   fails it puts the original back and stops — it never reboots on an unverified config.
5. Asks `Reboot the device now to load the new model? [y/N]` (`--yes` skips the question,
   `--no-reboot` never reboots). Resumes processing either way.

Without `--activate` only steps 1–3 happen: the file is on the device and you select it yourself under
**Configuration → Digits → Model** (expert mode), **Save**, then **Reboot**.

**Manual alternative** (no script): **System → File server → `config` → Upload** the `_q.tflite`, then
**Configuration → Digits → Model** → pick it → **Save** → **Reboot**.

**Confirm it worked** — after the reboot (about 30 s):

* **Recognition** page: every ROI shows the expected digit with a high confidence bar.
* **System → Log**: a line naming your model file at start-up and no `tflite does not fit the firmware`
  error (that means the output size / input type is wrong — see Troubleshooting).
* `http://<ip>/json`: `"error": "no error"` and a plausible `value` on the next round.

**Roll back** if the reading is worse: **Configuration → Digits → Model** → select the previous file
(the stock models are still in `/config/`) → Save → Reboot. Or re-upload the `config.ini` backup from
`backups/` with the file server and reboot.

---

## 8. Iterate: feed the low-confidence reads back into the model

With `ROIImages = true` and `ROIImagesMode = changed`, the device keeps writing crops to
`/log/digit/<day>/<hour>/` — but only when a ROI's read **changes** or the model was **unsure**
(confidence below 0.70, rejected, `N`, or overruled by the temporal vote). Each file name carries the
raw prediction and its confidence (`3_c54_main_main8_<ts>.jpg`), so the unsure ones are easy to spot.

### Track what the model is unsure about

```bash
cd tools/model-training && source .venv/bin/activate

# pull only what's new since the last run (incremental; pauses processing meanwhile)
python fetch_images.py --device 10.0.42.12 --pause

# list this week's low-confidence reads (c00–c69) by ROI
find data/device/10.0.42.12/log/digit -name '*_c[0-6][0-9]_*' -mtime -7 \
  | sed -E 's#.*/[0-9N.]+_c[0-9]+_([a-z]+_[a-z0-9]+)_.*#\1#' | sort | uniq -c
```

A ROI that keeps appearing there is either showing a digit the model has few examples of, or its box
has drifted — open a few of the files before blaming the model. On the device itself, the same
information is on the **Recognition** page (confidence bar per ROI) and in `/json` (`confidence`).

### Add them to the training set

`prelabel.py` only surfaces images you haven't dealt with yet: anything already in `data/labeled/`,
already in `data/review/`, or deleted in the label tool is skipped automatically, so the second pass
is just the new material. Use **your** model as the pre-labeller now — the stock model's mistakes are
exactly what you've fixed.

```bash
# 1. pre-label the new crops with the model that is on the device
python prelabel.py --model output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite --dedupe \
                   --input data/device/10.0.42.12/log/digit

# 2. label — unsure/ first (that's the point of this loop), then bulk-accept confident/
python label_tool.py --dir data/review/dig-class11
```

Label the unsure images with what they **really** show (`n` only for genuinely unreadable frames).
Those few images are worth more than hundreds of confident ones.

### Retrain and redeploy

```bash
# 3. retrain on everything labelled so far, with a new version tag
python train.py --type dig-class11 --name 2611 --data data/labeled/dig-class11 --balance

# 4. make sure it is at least as good as the model currently on the device
python evaluate.py output/dig-class11_2611_s2/dig-class11_2611_s2_q.tflite data/labeled/dig-class11
python evaluate.py output/dig-class11_2610_s2/dig-class11_2610_s2_q.tflite data/labeled/dig-class11

# 5. deploy (uploads + verifies, rewrites config.ini with a backup, asks before rebooting)
python deploy.py output/dig-class11_2611_s2/dig-class11_2611_s2_q.tflite \
                 --device 10.0.42.12 --section Digits --activate
```

Then watch `/log/digit` over the following days: fewer `_c0x_`–`_c6x_` files for the digits you just
added means the loop worked. Repeat whenever the find command above shows something new. Keep the
labelled images — they are the asset; models are cheap to regenerate. Old model files can stay in
`/config/` on the device as instant roll-back options (**Configuration → Digits → Model**).

---

## Contributing images upstream

The community models get better when unusual meters are added. Upstream accepts **pull requests
with images only** (no model files) into the `images/` folders of the two repositories above. Use
the hash-renamed labelled files from `data/labeled/` — they contain no timestamps or device data —
in a sub-folder named after your meter type.

---

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `tflite does not fit the firmware` in the device log | Output tensor size is not 11 / 100 / 2, or input is not float32. Use `train.py` (it verifies this) — don't hand-convert with `inference_input_type=int8`. |
| Device reboots / "op not supported" when the model loads | Model exported with a too-new Keras. The toolkit pins `keras<=3.10` / `tensorflow<=2.18` in `requirements.txt`; use that environment. |
| Accuracy 99 % in training, poor on the device | Duplicates leaked between train and validation (always run `prelabel.py --dedupe`), or the ROI boxes on the device changed since the images were collected. |
| One class never predicted | Class imbalance — count files per label in `data/labeled/`; add community images or collect longer. |
| Quantised model much worse than float | Rare with real representative data. Check `model-card.md` for the quantisation delta; if > 2 %, retrain with more varied images. |
| `fetch_images.py` is slow / times out | Use `--pause`; the single web task on the device is otherwise shared with the processing round. |
| ZIP download never starts (older firmware) | The old implementation builds the archive on the SD card first. Update the firmware or use `fetch_images.py` without `--zip`. |

---

## Appendix A — Firmware model contract

| | Digits | Analog |
| --- | --- | --- |
| Input tensor | `float32 [1, 32, 20, 3]`, RGB, **raw 0–255** (no scaling; first layer is `BatchNormalization`) | `float32 [1, 32, 32, 3]`, same |
| Output `11` | class11: `argmax`; class 10 = `N` | — |
| Output `100` | class100: `argmax / 10` | class100: `argmax / 10` |
| Output `2` | — | continuous: `value = fmod(atan2(out0, out1) / 2π + 2, 1) × 10` → `out0 = sin(2πv/10)`, `out1 = cos(2πv/10)` |
| Quantised (`_q`) | int8 weights/activations, **float I/O** (no `inference_input_type`) | same |
| ROI resize on device | 2-tap bilinear from the raw crop to the input size (`CImageBasis::Resize`); the toolkit's `--resize firmware` default is a bit-exact port | same |

## Appendix B — Files written by the device

| Setting | Path |
| --- | --- |
| `ROIImages` (Digits) | `/log/digit/<YYYYMMDD>/<HH>/<label>_c<NN>_<number>_<roi>_<ts>.jpg` |
| `ROIImages` (Analog) | `/log/analog/<YYYYMMDD>/<HH>/<label>_c<NN>_<roi>_<ts>.jpg` |
| `RawImages` | `/log/source/raw/<YYYYMMDD>/<HH>/raw_<ts>.jpg` (full 640 × 480 frame) |
| `SaveAllFiles` (debug) | `/img_tmp/<number>_<roi>.jpg` (raw crop) and `…_in.jpg` (resized model input), overwritten each round |
