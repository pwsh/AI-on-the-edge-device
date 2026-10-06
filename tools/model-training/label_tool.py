#!/usr/bin/env python3
"""Fast keyboard labelling of crops in your browser (no extra packages needed).

Start it on a review folder created by prelabel.py and open http://127.0.0.1:8765

    python label_tool.py --dir data/review/dig-class11

Every accepted image is MOVED to data/labeled/<type>/<label>_<hash>.jpg (the format
train.py reads and the upstream image collections use); deleted images go to a
_trash/ folder inside the review folder (undo-able). Whatever you do not touch stays
where it is, so you can stop at any time and continue later.

Fixing wrong labels (relabel mode): point --dir at a labelled folder (files named
<label>_<hash>.jpg) and the tool works in place: a different label renames the file in
the same folder (3_<hash>.jpg -> 5_<hash>.jpg), the current label leaves it alone, d
moves it to <folder>/_trash/. --from-csv limits the queue to the files listed in a
false_predictions.csv from train.py / evaluate.py (most confident disagreements first)
and shows what the model said; --model runs a .tflite over the queue as a hint.

    python label_tool.py --dir data/labeled/dig-class11 \
        --from-csv output/dig-class11_2610_s2/false_predictions.csv

Keys (digit class11 models):
    0-9, n          label as that digit / "N" (not a number, between two digits)
    space / enter   accept the predicted label (relabel mode: keep the current label)
Keys (class100 and analog models, labels 0.0 .. 9.9):
    two digits      e.g. 3 then 7 = 3.7 (Esc cancels the first digit)
    + / -           change the current value by 0.1 (or use the slider)
    space / enter   accept the current value (starts at the prediction / current label)
Always:
    d               delete (bad crop, unreadable, not a digit ...)
    left / right    previous / next image
    u               undo the last action
"""

from __future__ import annotations

import argparse
import csv
import html
import json
import mimetypes
import os
import shutil
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from common import (ALL_TYPES, file_sha1, format_label, hash_from_name, iter_images,
                    label_to_class, parse_confidence, parse_label)


def is_labeled_name(name: str, kind: str) -> bool:
    """True for a labelled-folder name (`<label>_<...>`), False for a review crop (`_cNN_`)."""
    return parse_confidence(name) is None and parse_label(name, kind) is not None


def same_label(a, b, kind: str) -> bool:
    if a is None or b is None:
        return False
    if kind in ("class11", "class100"):
        return label_to_class(a, kind) == label_to_class(b, kind)
    return format_label(a, kind) == format_label(b, kind)


class Store:
    """In-memory list of review items plus the file moves (with an undo stack).

    Normal mode: accepted images are MOVED to <out>/<type>/<label>_<hash>.jpg.
    Relabel mode (relabel=True): the images already live in a labelled folder; a new
    label renames the file in place, the current label leaves it untouched.

    files:  the queue (default: every image under review_dir)
    hints:  {path: [(prediction, confidence or None, source), ...]}, shown as
            "<source> said <prediction> (<confidence>)"
    order:  "confidence" | "name" | "hint" (most confident hint first) | "given"
    """

    def __init__(self, review_dir: Path, out_dir: Path, kind: str, type_name: str, order: str,
                 files=None, hints=None, relabel: bool = False):
        self.review_dir = review_dir
        self.out_dir = out_dir / type_name
        self.trash = review_dir / "_trash"
        self.kind = kind
        self.relabel = relabel
        self.lock = threading.Lock()
        # Each undo entry is a list of (item id, previous path, previous status, previous label).
        self.undo_stack: list[list[tuple[int, Path, str, object]]] = []
        self.items = []
        hints = hints or {}
        for f in (iter_images([review_dir]) if files is None else files):
            f = Path(f)
            h = hints.get(f, [])
            it = {"path": f, "name": f.name, "status": "pending", "label": None,
                  "hints": [html.escape(f"{src} said {t}{_fmt_conf(c)}") for t, c, src in h],
                  "hconf": next((c for _, c, _ in h if c is not None), None),
                  "hdiff": False}
            if relabel:
                # Current label from the name; no prediction token in labelled names.
                it.update(pred=None, conf=None, orig=parse_label(f.name, kind))
            else:
                it.update(pred=parse_label(f.name, kind), conf=parse_confidence(f.name),
                          orig=None)
            it["hdiff"] = bool(h) and it["orig"] is not None and not any(
                same_label(parse_label(f"{t}_x.jpg", kind), it["orig"], kind) for t, _, _ in h)
            self.items.append(it)
        if order == "confidence":
            # Least confident first: those need a human most. Unknown confidence first too.
            self.items.sort(key=lambda it: (it["conf"] if it["conf"] is not None else -1,
                                            it["name"]))
        elif order == "hint":
            # Disagreements with the model first, the most confident ones first: a model that
            # is very sure and disagrees with the file name usually means a wrong label.
            self.items.sort(key=lambda it: (not it["hdiff"] and bool(it["hints"]),
                                            -(it["hconf"] if it["hconf"] is not None else -1),
                                            it["name"]))
        elif order == "name":
            self.items.sort(key=lambda it: it["name"])
        for i, it in enumerate(self.items):
            it["id"] = i

    # -- helpers --------------------------------------------------------------
    def public(self, it) -> dict:
        def fmt(v):
            return None if v is None else format_label(v, self.kind)
        return {"id": it["id"], "name": it["path"].name, "pred": fmt(it["pred"]),
                "conf": it["conf"], "status": it["status"], "label": fmt(it["label"]),
                "orig": fmt(it["orig"]), "hints": it["hints"]}

    def counts(self) -> dict:
        keys = (("pending", "changed", "unchanged", "deleted") if self.relabel
                else ("pending", "labeled", "deleted"))
        c = dict.fromkeys(keys, 0)
        for it in self.items:
            c[it["status"]] += 1
        return c

    def _move(self, src: Path, dst: Path) -> Path:
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            # Same content hash already labelled the same way: keep both, don't clobber.
            stem, n = dst.stem, 1
            while dst.exists():
                dst = dst.with_name(f"{stem}-{n}{dst.suffix}")
                n += 1
        shutil.move(str(src), str(dst))
        return dst

    def _label_one(self, it, value, record: list) -> None:
        old = (it["id"], it["path"], it["status"], it["label"])
        if self.relabel:
            p = it["path"]
            if not same_label(value, parse_label(p.name, self.kind), self.kind):
                # Rename in place, keeping the hash (or whatever follows the label).
                h = hash_from_name(p.name)
                rest = f"{h}{p.suffix}" if h else p.name.split("_", 1)[1]
                it["path"] = self._move(p, p.with_name(f"{format_label(value, self.kind)}_{rest}"))
            status = "unchanged" if same_label(value, it["orig"], self.kind) else "changed"
        else:
            h = hash_from_name(it["name"]) or file_sha1(it["path"])
            dst = self.out_dir / f"{format_label(value, self.kind)}_{h}{it['path'].suffix.lower()}"
            if it["path"] != dst:
                it["path"] = self._move(it["path"], dst)
            status = "labeled"
        it["status"], it["label"] = status, value
        record.append(old)

    # -- actions --------------------------------------------------------------
    def label(self, idx: int, text: str) -> dict:
        value = parse_label(f"{text}_x.jpg", self.kind)
        if value is None:
            raise ValueError(f"invalid label {text!r}")
        with self.lock:
            it = self.items[idx]
            if it["status"] == "deleted":
                raise ValueError("this image was deleted (press u to undo)")
            rec: list = []
            self._label_one(it, value, rec)
            self.undo_stack.append(rec)
            return self.public(it)

    def delete(self, idx: int) -> dict:
        with self.lock:
            it = self.items[idx]
            if it["status"] == "deleted":
                return self.public(it)
            old = (it["id"], it["path"], it["status"], it["label"])
            if self.relabel:    # into a _trash/ next to the file (train.py skips _trash*)
                dst = it["path"].parent / "_trash" / it["path"].name
            else:
                dst = self.trash / it["name"]
            it["path"] = self._move(it["path"], dst)
            it["status"], it["label"] = "deleted", None
            self.undo_stack.append([old])
            return self.public(it)

    def accept_all(self, threshold: float) -> int:
        if self.relabel:
            raise ValueError("accept all is not available in relabel mode")
        with self.lock:
            rec: list = []
            for it in self.items:
                if (it["status"] == "pending" and it["pred"] is not None
                        and it["conf"] is not None and it["conf"] >= threshold):
                    self._label_one(it, it["pred"], rec)
            if rec:
                self.undo_stack.append(rec)
            return len(rec)

    def undo(self):
        with self.lock:
            if not self.undo_stack:
                return None
            rec = self.undo_stack.pop()
            for idx, path, status, label in reversed(rec):
                it = self.items[idx]
                if it["path"] != path:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    shutil.move(str(it["path"]), str(path))
                it["path"], it["status"], it["label"] = path, status, label
            return rec[0][0]


# ---------------------------------------------------------------------------
# Queue sources: false_predictions.csv and an optional model hint
# ---------------------------------------------------------------------------


def _fmt_conf(c):
    return "" if c is None else f" ({c:.3f})"


def read_false_predictions(csv_path: Path, search_dir: Path | None):
    """Return [(path, prediction text, confidence or None)] from a false_predictions.csv.

    Relative paths are tried against the CSV's folder first, then the current folder.
    A file that has been renamed since (relabelled, or `-1` suffix) is found again by
    its content hash in search_dir or in the folder the CSV path points to. Missing
    files (and deleted ones, in _trash/) are reported and skipped.
    """
    indexes: dict = {}

    def find_by_hash(folder: Path, h: str):
        if folder not in indexes:
            idx: dict = {}
            if folder.is_dir():
                for f in iter_images([folder]):
                    fh = hash_from_name(f.name)
                    if fh:
                        idx.setdefault(fh, f)
            indexes[folder] = idx
        return indexes[folder].get(h)

    rows, missing, seen = [], [], set()
    with open(csv_path, newline="") as fh:
        reader = csv.DictReader(fh)
        if not reader.fieldnames or "file" not in reader.fieldnames:
            raise SystemExit(f"ERROR: {csv_path} has no 'file' column "
                             "(expected a false_predictions.csv)")
        for row in reader:
            name = (row.get("file") or "").strip()
            if not name:
                continue
            p = Path(name)
            cands = [p] if p.is_absolute() else [csv_path.parent / p, Path.cwd() / p]
            found = next((c for c in cands if c.is_file()), None)
            h = hash_from_name(p.name)
            if found is None and h:
                for folder in ([search_dir] if search_dir is not None else []) + \
                        [c.parent for c in cands]:
                    found = find_by_hash(folder.resolve(), h)
                    if found is not None:
                        break
            if found is None:
                missing.append(name)
                continue
            found = found.resolve()
            if found in seen:
                continue
            seen.add(found)
            try:
                conf = float(row.get("confidence") or "")
            except ValueError:
                conf = None
            rows.append((found, (row.get("prediction") or "").strip(), conf))
    if missing:
        print(f"WARNING: {len(missing)} file(s) from {csv_path.name} not found, skipped "
              f"(e.g. {missing[0]})")
    return rows


def model_hints(model_path: Path, files, folder_kind: str) -> dict:
    """Run a .tflite once over the queued files like the firmware does; {path: (text, conf)}.

    Needs numpy / Pillow / TensorFlow (requirements.txt); only imported when --model is used.
    """
    try:
        from common import TFLiteModel, load_images
        model = TFLiteModel(model_path)
    except ImportError as e:
        raise SystemExit(f"ERROR: --model needs the training packages "
                         f"(pip install -r requirements.txt): {e}")
    files = list(files)
    if model.type.kind != folder_kind:
        print(f"NOTE: {model_path.name} is a {model.type.name} model, the folder holds "
              f"{folder_kind} labels; hints may not be comparable.")
    vals, confs = model.predict(load_images(files, model.height, model.width,
                                            progress=f"Running {model_path.name}"))
    mk = model.type.kind
    out = {}
    for f, v, c in zip(files, vals, confs):
        text = format_label(v, mk) if mk in ("class11", "class100") else f"{float(v):.1f}"
        out[f] = (text, float(c))
    return out


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Label tool</title>
<style>
:root{--bg:#f6f7f9;--fg:#1d2330;--muted:#6b7280;--card:#fff;--line:#d9dde3;--accent:#2563eb;
--ok:#16a34a;--bad:#dc2626;--warn:#d97706}
@media (prefers-color-scheme:dark){:root{--bg:#14171c;--fg:#e6e8eb;--muted:#9aa3ae;--card:#1d2128;
--line:#323843;--accent:#60a5fa;--ok:#4ade80;--bad:#f87171;--warn:#fbbf24}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:10px 16px;
border-bottom:1px solid var(--line);background:var(--card)}
header b{font-size:16px}.sp{flex:1}
.pill{padding:2px 8px;border-radius:10px;border:1px solid var(--line);font-size:13px}
main{display:flex;flex-direction:column;align-items:center;padding:12px 16px;gap:10px}
#stage{min-height:260px;display:flex;align-items:center;justify-content:center}
#big{image-rendering:pixelated;image-rendering:crisp-edges;border:1px solid var(--line);
background:#000}
#info{font-size:20px;text-align:center}
#info .val{font-size:44px;font-weight:700;font-variant-numeric:tabular-nums;color:var(--accent)}
#info .st{font-size:14px;color:var(--muted)}
.labeled,.changed{color:var(--ok)!important}.deleted{color:var(--bad)!important}
.unchanged{color:var(--muted)!important}
#mode{padding:6px 16px;font-size:14px;border-bottom:1px solid var(--line);color:var(--muted)}
#mode b{color:var(--fg)}
#info .cap{font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}
#info .hint{font-size:16px;color:var(--warn)}
#slider{width:min(520px,90vw)}
#strip{display:flex;gap:6px;overflow-x:auto;max-width:100%;padding:4px}
.th{display:flex;flex-direction:column;align-items:center;cursor:pointer;
border:2px solid transparent;border-radius:4px;padding:2px;font-size:12px}
.th img{height:64px;image-rendering:pixelated}
.th.cur{border-color:var(--accent)}.th.labeled,.th.changed{background:rgba(22,163,74,.15)}
.th.deleted{opacity:.35}
.keys{color:var(--muted);font-size:13px;text-align:center;max-width:760px}
kbd{border:1px solid var(--line);border-bottom-width:2px;border-radius:4px;padding:0 4px;
font-size:12px;background:var(--card)}
button,input[type=number]{font:inherit;padding:3px 8px;border-radius:6px;
border:1px solid var(--line);background:var(--card);color:var(--fg)}
button{cursor:pointer}button:hover{border-color:var(--accent)}
#msg{min-height:1.4em;color:var(--warn)}
</style></head><body>
<header><b>Label tool</b><span class="pill" id="type"></span>
<span id="counts"></span><span class="sp"></span>
<span id="accbox"><label>Accept all remaining with confidence &ge;
<input type="number" id="thr" min="0" max="1" step="0.01" value="0.98" style="width:5.5em">
</label> <button id="acc">Accept</button></span><button id="undo">Undo (u)</button></header>
<div id="mode"></div>
<main>
<div id="stage"><img id="big" alt=""></div>
<div id="info"></div>
<input type="range" id="slider" min="0" max="99" step="1" hidden>
<div id="msg"></div>
<div id="strip"></div>
<div class="keys" id="keys"></div>
</main>
<script>
"use strict";
const KIND = "__KIND__", TYPE = "__TYPE__", MODE = __MODE__, RELABEL = MODE.relabel;
let items = [], idx = 0, cur = null, pend = null;
const $ = id => document.getElementById(id);
$("type").textContent = TYPE;
$("mode").innerHTML = MODE.header;
$("accbox").hidden = RELABEL;
$("keys").innerHTML = (KIND === "class11"
  ? "<kbd>0</kbd>-<kbd>9</kbd> digit &nbsp; <kbd>n</kbd> N (not a number) &nbsp; " +
    "<kbd>space</kbd>/<kbd>enter</kbd> " + (RELABEL ? "keep current label" : "accept prediction")
  : "two digits e.g. <kbd>3</kbd><kbd>7</kbd> = 3.7 (<kbd>Esc</kbd> cancels) &nbsp; " +
    "<kbd>+</kbd>/<kbd>-</kbd> &plusmn;0.1 &nbsp; <kbd>space</kbd>/<kbd>enter</kbd> accept value") +
  " &nbsp; <kbd>d</kbd> delete &nbsp; <kbd>&larr;</kbd>/<kbd>&rarr;</kbd> navigate &nbsp; " +
  "<kbd>u</kbd> undo";
const fmt = v => (Math.floor(v / 10)) + "." + (v % 10);
const toTenths = s => s == null ? null : Math.round(parseFloat(s) * 10) % 100;

async function api(path, body) {
  const r = await fetch(path, {method: body ? "POST" : "GET",
    headers: {"Content-Type": "application/json"}, body: body ? JSON.stringify(body) : undefined});
  const j = await r.json();
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}
function counts() {
  const c = {pending: 0, labeled: 0, changed: 0, unchanged: 0, deleted: 0};
  items.forEach(it => c[it.status]++);
  $("counts").textContent = (RELABEL
    ? `${c.pending} to do · ${c.changed} changed · ${c.unchanged} unchanged · ${c.deleted} deleted`
    : `${c.pending} to do · ${c.labeled} labelled · ${c.deleted} deleted`) +
    ` · ${items.length} total`;
}
const shown = it => it.label ?? (RELABEL ? it.orig : it.pred);
function nextPending(from) {
  for (let k = 1; k <= items.length; k++) {
    const j = (from + k) % items.length;
    if (items[j].status === "pending") return j;
  }
  return Math.min(from + 1, items.length - 1);
}
function show() {
  counts();
  if (!items.length) { $("info").textContent = "No images in this folder."; return; }
  idx = Math.max(0, Math.min(idx, items.length - 1));
  const it = items[idx];
  pend = null;
  cur = KIND === "class11" ? null : toTenths(shown(it)) ?? 0;
  const img = $("big");
  img.onload = () => {
    const w = img.naturalWidth, h = img.naturalHeight;
    const s = Math.max(1, Math.min(8, Math.floor(Math.min(innerHeight * 0.5 / h,
      (innerWidth - 32) / w))));
    img.style.width = (w * s) + "px"; img.style.height = (h * s) + "px";
  };
  img.src = `/img/${it.id}?s=${it.status}${it.label ?? ""}`;
  renderInfo();
  const sl = $("slider");
  sl.hidden = KIND === "class11";
  if (!sl.hidden) sl.value = cur;
  const strip = $("strip"); strip.innerHTML = "";
  for (let j = Math.max(0, idx - 5); j <= Math.min(items.length - 1, idx + 6); j++) {
    const t = items[j], d = document.createElement("div");
    d.className = "th " + t.status + (j === idx ? " cur" : "");
    d.innerHTML = `<img src="/img/${t.id}?s=${t.status}${t.label ?? ""}" alt="">` +
      `<span>${shown(t) ?? "?"}</span>`;
    d.onclick = () => { idx = j; show(); };
    strip.appendChild(d);
  }
}
function renderInfo() {
  const it = items[idx];
  const conf = it.conf == null ? "" : ` (${Math.round(it.conf * 100)}%)`;
  let val = shown(it) ?? "?";
  if (KIND !== "class11") val = pend != null ? pend + "._" : fmt(cur);
  const hints = (it.hints || []).map(h => `<div class="hint">${h}</div>`).join("");
  if (RELABEL) {
    const st = it.status === "pending" ? "" : `<div class="st ${it.status}">${it.status}` +
      (it.status === "changed" ? ` ${it.orig} &rarr; ${it.label}` : "") + "</div>";
    $("info").innerHTML = `<div class="cap">current label</div>` +
      `<div class="val ${it.status}">${val}</div>${hints}` +
      `<div class="st">${idx + 1} / ${items.length} · ${it.name}</div>${st}`;
    return;
  }
  const st = it.status === "pending" ? "" :
    `<div class="st ${it.status}">${it.status}${it.label ? " as " + it.label : ""}</div>`;
  $("info").innerHTML = `<div class="val ${it.status}">${val}</div>${hints}` +
    `<div class="st">${idx + 1} / ${items.length} · predicted ${it.pred ?? "-"}${conf}` +
    ` · ${it.name}</div>${st}`;
}
async function doLabel(text) {
  const it = items[idx];
  try {
    Object.assign(it, await api("/api/label", {id: it.id, label: text}));
    $("msg").textContent = "";
    idx = nextPending(idx); show();
  } catch (e) { $("msg").textContent = e.message; }
}
async function doDelete() {
  const it = items[idx];
  try { Object.assign(it, await api("/api/delete", {id: it.id})); idx = nextPending(idx); show(); }
  catch (e) { $("msg").textContent = e.message; }
}
async function reload(focus) {
  items = (await api("/api/items")).items;
  if (focus != null) idx = items.findIndex(t => t.id === focus);
  show();
}
async function doUndo() {
  const r = await api("/api/undo", {});
  if (r.id == null) { $("msg").textContent = "Nothing to undo."; return; }
  $("msg").textContent = ""; await reload(r.id);
}
$("undo").onclick = doUndo;
$("acc").onclick = async () => {
  const thr = parseFloat($("thr").value);
  if (!confirm(`Label every remaining image with confidence >= ${thr} as predicted?`)) return;
  const r = await api("/api/accept_all", {threshold: thr});
  $("msg").textContent = `${r.count} images accepted (undo reverts all of them).`;
  await reload(); idx = nextPending(-1); show();
};
$("slider").oninput = e => { cur = +e.target.value; pend = null; renderInfo(); };
document.addEventListener("keydown", e => {
  if (e.target.tagName === "INPUT" && e.target.type !== "range") return;
  if (e.ctrlKey || e.metaKey || e.altKey || !items.length) return;
  const k = e.key, it = items[idx];
  if (k === "ArrowLeft") { idx = Math.max(0, idx - 1); show(); }
  else if (k === "ArrowRight") { idx = Math.min(items.length - 1, idx + 1); show(); }
  else if (k === "d" || k === "D" || k === "Delete") doDelete();
  else if (k === "u" || k === "U") doUndo();
  else if (KIND === "class11") {
    if (/^[0-9]$/.test(k)) doLabel(k);
    else if (k === "n" || k === "N") doLabel("N");
    else if (k === " " || k === "Enter") { const v = RELABEL ? shown(it) : it.pred; if (v != null) doLabel(v); }
    else return;
  } else {
    if (/^[0-9]$/.test(k)) {
      if (pend == null) { pend = +k; renderInfo(); }
      else { const v = pend * 10 + (+k); pend = null; doLabel(fmt(v)); }
    }
    else if (k === "+" || k === "=") { cur = (cur + 1) % 100; pend = null; $("slider").value = cur; renderInfo(); }
    else if (k === "-" || k === "_") { cur = (cur + 99) % 100; pend = null; $("slider").value = cur; renderInfo(); }
    else if (k === "Escape") { pend = null; renderInfo(); }
    else if (k === " " || k === "Enter") doLabel(pend != null ? fmt(pend * 10) : fmt(cur));
    else return;
  }
  e.preventDefault();
});
reload().then(() => { idx = nextPending(-1); show(); });
</script></body></html>
"""


def make_handler(store: Store, page: bytes):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):   # keep the console quiet
            pass

        def _send(self, code: int, body: bytes, ctype: str = "application/json"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, obj, code: int = 200):
            self._send(code, json.dumps(obj).encode())

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html"):
                return self._send(200, page, "text/html; charset=utf-8")
            if path == "/api/items":
                with store.lock:
                    return self._json({"items": [store.public(it) for it in store.items]})
            if path == "/api/counts":   # cheap status poll (used by pipeline.py)
                with store.lock:
                    return self._json({**store.counts(), "total": len(store.items),
                                       "relabel": store.relabel})
            if path.startswith("/img/"):
                try:
                    it = store.items[int(path[5:])]
                    with store.lock:
                        p = it["path"]
                    data = p.read_bytes()
                except (ValueError, IndexError, OSError):
                    return self._json({"error": "not found"}, 404)
                return self._send(200, data, mimetypes.guess_type(p.name)[0] or "image/jpeg")
            self._json({"error": "not found"}, 404)

        def do_POST(self):
            try:
                n = int(self.headers.get("Content-Length") or 0)
                body = json.loads(self.rfile.read(n) or b"{}")
                if self.path == "/api/label":
                    return self._json(store.label(int(body["id"]), str(body["label"])))
                if self.path == "/api/delete":
                    return self._json(store.delete(int(body["id"])))
                if self.path == "/api/undo":
                    return self._json({"id": store.undo()})
                if self.path == "/api/accept_all":
                    return self._json({"count": store.accept_all(float(body["threshold"]))})
            except (ValueError, KeyError, IndexError, OSError) as e:
                return self._json({"error": str(e)}, 400)
            self._json({"error": "not found"}, 404)

    return Handler


def _disp(p: Path) -> str:
    try:
        return str(p.resolve().relative_to(Path.cwd().resolve()))
    except ValueError:
        return str(p)


def _type_from_paths(paths) -> str | None:
    for p in paths:
        for part in reversed(Path(p).resolve().parts):
            if part in ALL_TYPES:
                return part
    return None


def build(args):
    """Set up the Store and the page from the command line; returns (store, page, header)."""
    if args.dir is None and args.from_csv is None:
        raise SystemExit("ERROR: give --dir (and/or --from-csv)")
    review = Path(args.dir) if args.dir else None
    if review is not None and not review.is_dir():
        raise SystemExit(f"ERROR: {review} is not a folder")

    hints: dict = {}
    if args.from_csv:
        csv_path = Path(args.from_csv)
        if not csv_path.is_file():
            raise SystemExit(f"ERROR: {csv_path} not found")
        rows = read_false_predictions(csv_path, review)
        if review is not None:
            root = review.resolve()
            inside = [r for r in rows if r[0].is_relative_to(root)]
            if len(inside) < len(rows):
                print(f"NOTE: {len(rows) - len(inside)} file(s) from the CSV are not in "
                      f"{review}, skipped")
            rows = inside
        # Most confident disagreements first: those are the likely label errors.
        rows.sort(key=lambda r: (r[2] is None, -(r[2] or 0.0), r[0].name))
        files = [r[0] for r in rows]
        for f, pred, conf in rows:
            if pred:
                hints.setdefault(f, []).append((pred, conf, "model"))
        if review is None:
            if not files:
                raise SystemExit(f"ERROR: none of the files in {csv_path} were found")
            review = Path(os.path.commonpath([str(f.parent) for f in files]))
        source = f" from {csv_path.name}"
    else:
        files = [f.resolve() for f in iter_images([review])]
        source = ""

    type_name = args.type or _type_from_paths([review] + files[:1])
    if not type_name:
        raise SystemExit("ERROR: cannot tell the model type from the folder name; use --type")
    kind = ALL_TYPES[type_name].kind

    relabel = args.relabel or (bool(files) and all(is_labeled_name(f.name, kind) for f in files)
                               and (all(hash_from_name(f.name) for f in files)
                                    or "labeled" in review.resolve().parts))

    if args.model:
        for f, h in model_hints(Path(args.model), files, kind).items():
            hints.setdefault(f, []).append((h[0], h[1], Path(args.model).name))

    order = args.order
    if order is None:
        if args.from_csv:
            order = "given"
        elif relabel:
            order = "hint" if args.model else "name"
        else:
            order = "confidence"

    store = Store(review, Path(args.out), kind, type_name, order, files=files, hints=hints,
                  relabel=relabel)
    n = f"{len(files)} file{'s' if len(files) != 1 else ''}"
    if relabel:
        header = f"Relabel in place: {_disp(review)} \u2014 {n}{source}"
    else:
        header = (f"Label: {_disp(review)} \u2014 {n}{source} \u2192 "
                  f"{_disp(store.out_dir)}")
    mode = json.dumps({"relabel": relabel, "header": html.escape(header)}).replace("</", "<\\/")
    page = (PAGE.replace("__KIND__", kind).replace("__TYPE__", type_name)
            .replace("__MODE__", mode).encode())
    return store, page, header


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Local web UI for fast keyboard labelling of meter crops, and for "
                    "fixing wrong labels in a labelled folder (relabel mode).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--dir",
                    help="folder with images to label (e.g. data/review/dig-class11), or a "
                         "labelled folder to fix in place (e.g. data/labeled/dig-class11)")
    ap.add_argument("--relabel", action="store_true",
                    help="work in place: a new label renames the file in its folder. "
                         "Automatic when --dir holds only <label>_<hash> files")
    ap.add_argument("--from-csv", metavar="CSV",
                    help="only show the files listed in a false_predictions.csv from train.py "
                         "/ evaluate.py, most confident first, with the model's prediction")
    ap.add_argument("--model", metavar="TFLITE",
                    help="run this .tflite once over the queue and show its reading as a hint "
                         "(needs the packages from requirements.txt)")
    ap.add_argument("--type", choices=list(ALL_TYPES),
                    help="model type (default: taken from the folder name)")
    ap.add_argument("--out", default="data/labeled",
                    help="labelled images go to <out>/<type>/<label>_<hash>.jpg "
                         "(not used in relabel mode)")
    ap.add_argument("--order", choices=["confidence", "name", "hint"], default=None,
                    help="'confidence' (default for review folders) shows the least confident "
                         "images first; 'hint' shows the most confident model disagreements "
                         "first (default in relabel mode with --model); --from-csv defaults to "
                         "the CSV's confidence, highest first")
    ap.add_argument("--host", default="127.0.0.1", help="address to listen on")
    ap.add_argument("--port", type=int, default=8765, help="port to listen on")
    args = ap.parse_args()

    store, page, header = build(args)
    srv = ThreadingHTTPServer((args.host, args.port), make_handler(store, page))
    c = store.counts()
    print(header)
    if store.relabel:
        print("Relabel mode: a new label renames the file in place; deleted images go to "
              "_trash/ next to them.")
    else:
        print(f"{c['pending']} to label; labelled images go to {store.out_dir}/")
    print(f"Open http://{args.host}:{args.port}/ in your browser - Ctrl-C to stop.", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
        c = store.counts()
        print("\nStopped: " + ", ".join(f"{v} {k}" for k, v in c.items() if k != "pending")
              + f", {c['pending']} left to do.")
        os._exit(0)


if __name__ == "__main__":
    main()
