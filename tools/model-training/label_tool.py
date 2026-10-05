#!/usr/bin/env python3
"""Fast keyboard labelling of crops in your browser (no extra packages needed).

Start it on a review folder created by prelabel.py and open http://127.0.0.1:8765

    python label_tool.py --dir data/review/dig-class11

Every accepted image is MOVED to data/labeled/<type>/<label>_<hash>.jpg (the format
train.py reads and the upstream image collections use); deleted images go to a
_trash/ folder inside the review folder (undo-able). Whatever you do not touch stays
where it is, so you can stop at any time and continue later.

Keys (digit class11 models):
    0-9, n          label as that digit / "N" (not a number, between two digits)
    space / enter   accept the predicted label
Keys (class100 and analog models, labels 0.0 .. 9.9):
    two digits      e.g. 3 then 7 = 3.7 (Esc cancels the first digit)
    + / -           change the current value by 0.1 (or use the slider)
    space / enter   accept the current value (starts at the prediction)
Always:
    d               delete (bad crop, unreadable, not a digit ...)
    left / right    previous / next image
    u               undo the last action
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import shutil
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from common import (ALL_TYPES, file_sha1, format_label, hash_from_name, iter_images,
                    parse_confidence, parse_label)


class Store:
    """In-memory list of review items plus the file moves (with an undo stack)."""

    def __init__(self, review_dir: Path, out_dir: Path, kind: str, type_name: str, order: str):
        self.review_dir = review_dir
        self.out_dir = out_dir / type_name
        self.trash = review_dir / "_trash"
        self.kind = kind
        self.lock = threading.Lock()
        # Each undo entry is a list of (item id, previous path, previous status, previous label).
        self.undo_stack: list[list[tuple[int, Path, str, object]]] = []
        self.items = []
        for f in iter_images([review_dir]):
            pred = parse_label(f.name, kind)
            conf = parse_confidence(f.name)
            self.items.append({"path": f, "name": f.name, "pred": pred, "conf": conf,
                               "status": "pending", "label": None})
        if order == "confidence":
            # Least confident first: those need a human most. Unknown confidence first too.
            self.items.sort(key=lambda it: (it["conf"] if it["conf"] is not None else -1,
                                            it["name"]))
        else:
            self.items.sort(key=lambda it: it["name"])
        for i, it in enumerate(self.items):
            it["id"] = i

    # -- helpers --------------------------------------------------------------
    def public(self, it) -> dict:
        def fmt(v):
            return None if v is None else format_label(v, self.kind)
        return {"id": it["id"], "name": it["name"], "pred": fmt(it["pred"]),
                "conf": it["conf"], "status": it["status"], "label": fmt(it["label"])}

    def counts(self) -> dict:
        c = {"pending": 0, "labeled": 0, "deleted": 0}
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
        h = hash_from_name(it["name"]) or file_sha1(it["path"])
        dst = self.out_dir / f"{format_label(value, self.kind)}_{h}{it['path'].suffix.lower()}"
        old = (it["id"], it["path"], it["status"], it["label"])
        if it["path"] != dst:
            it["path"] = self._move(it["path"], dst)
        it["status"], it["label"] = "labeled", value
        record.append(old)

    # -- actions --------------------------------------------------------------
    def label(self, idx: int, text: str) -> dict:
        value = parse_label(f"{text}_x.jpg", self.kind)
        if value is None:
            raise ValueError(f"invalid label {text!r}")
        with self.lock:
            rec: list = []
            self._label_one(self.items[idx], value, rec)
            self.undo_stack.append(rec)
            return self.public(self.items[idx])

    def delete(self, idx: int) -> dict:
        with self.lock:
            it = self.items[idx]
            old = (it["id"], it["path"], it["status"], it["label"])
            it["path"] = self._move(it["path"], self.trash / it["name"])
            it["status"], it["label"] = "deleted", None
            self.undo_stack.append([old])
            return self.public(it)

    def accept_all(self, threshold: float) -> int:
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
.labeled{color:var(--ok)!important}.deleted{color:var(--bad)!important}
#slider{width:min(520px,90vw)}
#strip{display:flex;gap:6px;overflow-x:auto;max-width:100%;padding:4px}
.th{display:flex;flex-direction:column;align-items:center;cursor:pointer;
border:2px solid transparent;border-radius:4px;padding:2px;font-size:12px}
.th img{height:64px;image-rendering:pixelated}
.th.cur{border-color:var(--accent)}.th.labeled{background:rgba(22,163,74,.15)}
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
<label>Accept all remaining with confidence &ge;
<input type="number" id="thr" min="0" max="1" step="0.01" value="0.98" style="width:5.5em">
</label><button id="acc">Accept</button><button id="undo">Undo (u)</button></header>
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
const KIND = "__KIND__", TYPE = "__TYPE__";
let items = [], idx = 0, cur = null, pend = null;
const $ = id => document.getElementById(id);
$("type").textContent = TYPE;
$("keys").innerHTML = (KIND === "class11"
  ? "<kbd>0</kbd>-<kbd>9</kbd> digit &nbsp; <kbd>n</kbd> N (not a number) &nbsp; " +
    "<kbd>space</kbd>/<kbd>enter</kbd> accept prediction"
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
  const c = {pending: 0, labeled: 0, deleted: 0};
  items.forEach(it => c[it.status]++);
  $("counts").textContent = `${c.pending} to do · ${c.labeled} labelled · ${c.deleted} deleted` +
    ` · ${items.length} total`;
}
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
  cur = KIND === "class11" ? null : toTenths(it.label ?? it.pred) ?? 0;
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
      `<span>${t.label ?? t.pred ?? "?"}</span>`;
    d.onclick = () => { idx = j; show(); };
    strip.appendChild(d);
  }
}
function renderInfo() {
  const it = items[idx];
  const conf = it.conf == null ? "" : ` (${Math.round(it.conf * 100)}%)`;
  let val = it.label ?? it.pred ?? "?";
  if (KIND !== "class11") val = pend != null ? pend + "._" : fmt(cur);
  const st = it.status === "pending" ? "" :
    `<div class="st ${it.status}">${it.status}${it.label ? " as " + it.label : ""}</div>`;
  $("info").innerHTML = `<div class="val ${it.status}">${val}</div>` +
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
    else if (k === " " || k === "Enter") { if (it.pred != null) doLabel(it.pred); }
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


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Local web UI for fast keyboard labelling of meter crops.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--dir", required=True,
                    help="folder with images to label (e.g. data/review/dig-class11)")
    ap.add_argument("--type", choices=list(ALL_TYPES),
                    help="model type (default: taken from the folder name)")
    ap.add_argument("--out", default="data/labeled",
                    help="labelled images go to <out>/<type>/<label>_<hash>.jpg")
    ap.add_argument("--order", choices=["confidence", "name"], default="confidence",
                    help="'confidence' shows the least confident images first")
    ap.add_argument("--host", default="127.0.0.1", help="address to listen on")
    ap.add_argument("--port", type=int, default=8765, help="port to listen on")
    args = ap.parse_args()

    review = Path(args.dir)
    if not review.is_dir():
        raise SystemExit(f"ERROR: {review} is not a folder")
    type_name = args.type
    if not type_name:
        for part in reversed(review.resolve().parts):
            if part in ALL_TYPES:
                type_name = part
                break
    if not type_name:
        raise SystemExit("ERROR: cannot tell the model type from the folder name; use --type")
    kind = ALL_TYPES[type_name].kind

    store = Store(review, Path(args.out), kind, type_name, args.order)
    page = PAGE.replace("__KIND__", kind).replace("__TYPE__", type_name).encode()
    srv = ThreadingHTTPServer((args.host, args.port), make_handler(store, page))
    c = store.counts()
    print(f"{len(store.items)} images ({c['pending']} to label) in {review}")
    print(f"Labelled images go to {store.out_dir}/")
    print(f"Open http://{args.host}:{args.port}/ in your browser - Ctrl-C to stop.", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
        c = store.counts()
        print(f"\nStopped: {c['labeled']} labelled, {c['deleted']} deleted, "
              f"{c['pending']} left to do.")
        os._exit(0)


if __name__ == "__main__":
    main()
