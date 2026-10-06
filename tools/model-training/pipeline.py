#!/usr/bin/env python3
"""Run the whole "collect -> relabel -> retrain -> redeploy" loop from one config file.

    python pipeline.py training.toml
    python pipeline.py training.toml --steps fetch,prelabel,label_new
    python pipeline.py training.toml --dry-run

Copy training.example.toml to training.toml and adjust it. The steps run in a fixed
order (each can be switched off in [steps] or picked with --steps):

    1 fetch               fetch_images.py (incremental)
    2 prelabel            prelabel.py with the current model (skips handled images)
    3 label_new        *  label_tool.py on data/review/<type>
    4 evaluate_current    evaluate.py: current model over data/labeled/<type>
    5 relabel          *  label_tool.py --from-csv (in place) on its disagreements
    6 train               train.py
    7 relabel_after_train *  label_tool.py --from-csv on the new model's disagreements;
                          trains again (same name) if any label changed
    8 compare             evaluate.py: new vs current model on data/labeled/<type>
    9 deploy              deploy.py, only if new >= current + min_gain

  * manual: the label tool is started, the browser opened, and the pipeline continues
    when the queue is empty or when you press Enter.

The pipeline only drives the other scripts (same interpreter, same command-line flags);
every command it runs is printed so you can repeat any step by hand. Re-running after a
stopped step is safe: fetch is incremental, prelabel skips handled images and train
reuses (overwrites) the name of the unfinished run.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import os
import queue
import shlex
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from common import (MODEL_TYPES, config_value, hash_from_name,  # noqa: E402
                    iter_images, parse_config)

try:
    import tomllib
except ModuleNotFoundError:          # Python < 3.11
    tomllib = None

# (name, manual, description)
STEPS = [
    ("fetch", False, "download new crops from the device"),
    ("prelabel", False, "pre-label new crops with the current model"),
    ("label_new", True, "label the new crops in the browser"),
    ("evaluate_current", False, "check the labelled set with the current model"),
    ("relabel", True, "fix labels the current model disagrees with"),
    ("train", False, "train a new model"),
    ("relabel_after_train", True, "fix labels the new model disagrees with (retrain if changed)"),
    ("compare", False, "new vs. current model on the labelled set"),
    ("deploy", False, "upload the new model to the device"),
]
STEP_NAMES = [s[0] for s in STEPS]
MANUAL = {s[0] for s in STEPS if s[1]}
HAND = "✋"

DEFAULTS: dict = {
    "device": {"ip": "", "section": "", "pause_during_transfer": True},
    "model": {"type": "dig-class11", "size": "s2", "current": "auto", "name": "auto"},
    "paths": {"data": "data", "output": "output", "eval": "eval", "backups": "backups",
              "community": []},
    "prelabel": {"dedupe": True, "dedupe_distance": 2, "threshold": 0.9},
    "train": {"epochs": 200, "balance": True, "optimizer": "adam", "seed": 42, "extra_args": []},
    "deploy": {"activate": True, "auto_reboot": False, "min_gain": 0.0},
    "steps": {s: True for s in STEP_NAMES},
    "review": {"port": 8765, "open_browser": True},
}

# Python packages each step needs (the label tool runs on plain Python).
NEEDS = {
    "fetch": ["requests"],
    "prelabel": ["numpy", "PIL", "tensorflow"],
    "evaluate_current": ["numpy", "PIL", "tensorflow"],
    "train": ["numpy", "PIL", "tensorflow"],
    "relabel_after_train": ["numpy", "PIL", "tensorflow"],    # may train again
    "compare": ["numpy", "PIL", "tensorflow"],
    "deploy": ["numpy", "requests", "tensorflow"],
}


class PipelineError(Exception):
    """Expected failure: printed as a friendly message, no stack trace."""


class StepFailed(PipelineError):
    pass


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------


def load_config(path: Path) -> dict:
    if tomllib is None:
        raise PipelineError("pipeline.py needs Python 3.11 or newer (for tomllib); "
                            "use the toolkit's .venv (Python 3.12)")
    if not path.is_file():
        hint = ""
        if path.name == "training.toml":
            hint = "\n  Start from the example: cp training.example.toml training.toml"
        raise PipelineError(f"config file {path} not found{hint}")
    try:
        raw = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as e:
        raise PipelineError(f"{path}: {e}") from None

    cfg: dict = {}
    problems = []
    for sec, val in raw.items():
        if sec not in DEFAULTS:
            problems.append(f"unknown section [{sec}]")
        elif not isinstance(val, dict):
            problems.append(f"[{sec}] must be a table")
    for sec, defaults in DEFAULTS.items():
        given = raw.get(sec, {}) if isinstance(raw.get(sec, {}), dict) else {}
        out = dict(defaults)
        for key, val in given.items():
            if key not in defaults:
                problems.append(f"unknown key [{sec}] {key}"
                                + (f" (steps are: {', '.join(STEP_NAMES)})" if sec == "steps"
                                   else ""))
                continue
            want = type(defaults[key])
            ok = (isinstance(val, want) and not (want is int and isinstance(val, bool))
                  or (want is float and isinstance(val, int) and not isinstance(val, bool)))
            if not ok:
                problems.append(f"[{sec}] {key} must be {want.__name__}, got {val!r}")
                continue
            out[key] = float(val) if want is float else val
        cfg[sec] = out

    m = cfg["model"]
    if m["type"] not in MODEL_TYPES:
        problems.append(f"[model] type must be one of {', '.join(MODEL_TYPES)}")
    else:
        if not cfg["device"]["section"]:
            cfg["device"]["section"] = MODEL_TYPES[m["type"]].section
    if cfg["device"]["section"] and cfg["device"]["section"] not in ("Digits", "Analog"):
        problems.append("[device] section must be Digits or Analog")
    if cfg["train"]["optimizer"] not in ("adam", "adadelta"):
        problems.append("[train] optimizer must be adam or adadelta")
    if not all(isinstance(a, str) for a in cfg["train"]["extra_args"]):
        problems.append("[train] extra_args must be a list of strings")
    if not all(isinstance(a, str) for a in cfg["paths"]["community"]):
        problems.append("[paths] community must be a list of folder names")
    if problems:
        raise PipelineError(f"{path}: " + "; ".join(problems))
    return cfg


def tk_path(value: str) -> Path:
    """Config paths are relative to the toolkit folder unless absolute."""
    p = Path(value).expanduser()
    return p if p.is_absolute() else (HERE / p)


def rel(p: Path) -> str:
    """Path for display and for the child's command line (children run in HERE)."""
    p = Path(os.path.abspath(p))           # no resolve(): keep symlinks (venv python)
    try:
        return str(p.relative_to(HERE))
    except ValueError:
        return str(p)


def count_images(folder: Path) -> int:
    return sum(1 for _ in iter_images([folder])) if folder.is_dir() else 0


def fmt_secs(s: float) -> str:
    s = int(round(s))
    return f"{s // 60}m{s % 60:02d}s" if s >= 60 else f"{s}s"


# ---------------------------------------------------------------------------
# Pipeline
# ---------------------------------------------------------------------------


class Pipeline:
    def __init__(self, cfg: dict, config_path: Path, steps: list[str], dry_run: bool,
                 yes: bool):
        self.cfg = cfg
        self.config_path = config_path
        self.steps = steps
        self.dry = dry_run
        self.yes = yes
        p = cfg["paths"]
        self.data = tk_path(p["data"])
        self.output = tk_path(p["output"])
        self.eval_dir = tk_path(p["eval"])
        self.backups = tk_path(p["backups"])
        self.community = [tk_path(c) for c in p["community"]]
        m = cfg["model"]
        self.type = m["type"]
        self.mt = MODEL_TYPES[self.type]
        self.size = m["size"]
        self.section = cfg["device"]["section"]
        self.labeled = self.data / "labeled" / self.type
        self.review_dir = self.data / "review" / self.type
        ip = cfg["device"]["ip"].strip().rstrip("/")
        self.ip = ip
        self.base = ip if ip.startswith(("http://", "https://")) else f"http://{ip}"
        # Same folder name fetch_images.py uses (Device.name).
        self.devname = (self.base.split("://", 1)[1].replace(":", "_").replace("/", "_")
                        if ip else "")
        self.py = sys.executable
        self.state_path = self.data / "state.json"
        self.state = self._load_state()
        self.logf = None
        self._current: Path | None = None
        self._dev_config: str | None = None
        self.trained_name: str | None = None
        self.compare_result: dict | None = None
        self.compared = False
        self.timings: list[tuple[str, float, str]] = []
        self.stdin_q: queue.Queue = queue.Queue()
        self.stdin_eof = threading.Event()

    # -- output -------------------------------------------------------------
    def say(self, msg: str = "") -> None:
        print(msg, flush=True)
        if self.logf:
            self.logf.write((msg + "\n").encode("utf-8", "replace"))
            self.logf.flush()

    def _log_bytes(self, b: bytes) -> None:
        if self.logf:
            self.logf.write(b)
            self.logf.flush()

    def py_display(self) -> str:
        try:
            return str(Path(self.py).relative_to(HERE))
        except ValueError:
            return self.py

    def show_cmd(self, argv: list[str]) -> str:
        shown = [self.py_display()] + argv[1:]
        return shlex.join(shown) if os.name != "nt" else subprocess.list2cmdline(shown)

    # -- state ------------------------------------------------------------
    def _load_state(self) -> dict:
        try:
            return json.loads(self.state_path.read_text())
        except (OSError, ValueError):
            return {}

    def save_state(self, **kw) -> None:
        if self.dry:
            return
        self.state.update(kw)
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.state_path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(self.state, indent=1) + "\n")
        os.replace(tmp, self.state_path)

    # -- stdin (Enter to continue, reboot question) --------------------------
    def start_stdin_reader(self) -> None:
        def reader():
            while True:
                try:
                    line = sys.stdin.readline()
                except (OSError, ValueError):
                    line = ""
                if not line:
                    self.stdin_eof.set()
                    return
                self.stdin_q.put(line)
        threading.Thread(target=reader, daemon=True, name="stdin").start()

    def drain_stdin(self) -> None:
        while not self.stdin_q.empty():
            self.stdin_q.get_nowait()

    def ask_yes_no(self, question: str) -> bool:
        if self.yes:
            self.say(f"{question} [y/N] yes (--yes)")
            return True
        self.drain_stdin()
        print(f"{question} [y/N] ", end="", flush=True)
        while True:
            try:
                ans = self.stdin_q.get(timeout=0.5).strip().lower()
                break
            except queue.Empty:
                if self.stdin_eof.is_set():
                    ans = ""
                    print("(no terminal input: no)")
                    break
        self._log_bytes(f"{question} [y/N] {ans}\n".encode())
        return ans in ("y", "yes", "j", "ja")

    # -- device -------------------------------------------------------------
    def http_get(self, path: str, attempts: int = 3, timeout: float = 15) -> bytes:
        if not self.ip:
            raise PipelineError("[device] ip is not set in the config")
        url = self.base + path
        last = None
        for i in range(attempts):
            try:
                with urllib.request.urlopen(url, timeout=timeout) as r:
                    return r.read()
            except urllib.error.HTTPError as e:
                last = f"HTTP {e.code}"
                if e.code == 404:
                    break
            except (urllib.error.URLError, OSError) as e:
                last = getattr(e, "reason", None) or e
            if i + 1 < attempts:
                time.sleep(2)
        raise PipelineError(f"cannot read {url} ({last}). Is the device on and is "
                            f"[device] ip = \"{self.ip}\" right?")

    def device_config(self) -> str:
        if self._dev_config is None:
            self._dev_config = self.http_get("/fileserver/config/config.ini").decode("latin-1")
        return self._dev_config

    # -- models -------------------------------------------------------------
    def current_model(self) -> Path:
        """model.current: an explicit file, or 'auto' = the model the device uses now."""
        if self._current is not None:
            return self._current
        try:
            return self._resolve_current()
        except PipelineError as e:
            if not self.dry:
                raise
            self.say(f"  WARNING (dry run): {e}")
            self._current = Path("<current-model>")
            return self._current

    def _resolve_current(self) -> Path:
        v = self.cfg["model"]["current"]
        if v != "auto":
            p = tk_path(v)
            if not p.is_file():
                raise PipelineError(f"[model] current = \"{v}\" not found ({p})")
            self._current = p
            return p
        sections = parse_config(self.device_config())
        sec = sections.get(self.section)
        if sec is None:
            raise PipelineError(f"the device's config.ini has no [{self.section}] section; "
                                f"set [model] current to a .tflite file")
        if not sec["enabled"]:
            raise PipelineError(f"[{self.section}] is disabled in the device's config.ini "
                                f"(';[{self.section}]'); set [model] current to a .tflite file "
                                f"or [device] section to the one in use")
        model = config_value(sections, self.section, "Model")
        if not model:
            raise PipelineError(f"no 'Model =' line in [{self.section}] of the device's "
                                f"config.ini; set [model] current to a .tflite file")
        name = model.replace("\\", "/").rsplit("/", 1)[-1]
        cands = (sorted(self.output.glob(f"*/{name}")) +
                 [HERE.parent.parent / "sd-card" / "config" / name,
                  self.data / "models" / name])
        found = next((c for c in cands if c.is_file()), None)
        if found is None:
            dest = self.data / "models" / name
            dev_path = model if model.startswith("/") else f"/config/{name}"
            if self.dry:
                self.say(f"  current model: {model} on the device, not found locally; "
                         f"would download it to {rel(dest)}")
                self._current = dest
                return dest
            self.say(f"  current model {name} is not in {rel(self.output)}/ or sd-card/config/;"
                     f" downloading it from the device ...")
            data = self.http_get("/fileserver" + dev_path, timeout=60)
            if not data:
                raise PipelineError(f"{dev_path} on the device is empty")
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = dest.with_name(dest.name + ".part")
            tmp.write_bytes(data)
            os.replace(tmp, dest)
            found = dest
        self.say(f"  current model (device [{self.section}] Model = {model}): {rel(found)}")
        if not name.startswith(self.type + "_"):
            self.say(f"  NOTE: {name} does not look like a {self.type} model; prelabel sorts "
                     f"by the model's own type")
        self._current = found
        return found

    def run_name(self) -> str:
        """Version tag for train.py; stable within an unfinished run (resumable)."""
        if self.trained_name:
            return self.trained_name
        n = self.cfg["model"]["name"]
        if n != "auto":
            return n
        today = time.strftime("%y%m%d")
        open_name = self.state.get("open_run_name")
        if open_name and str(open_name).startswith(today):
            return open_name
        for cand in (today, time.strftime("%y%m%d-%H%M"), time.strftime("%y%m%d-%H%M%S")):
            if not (self.output / self.stem(cand)).exists():
                return cand
        return time.strftime("%y%m%d-%H%M%S")

    def stem(self, name: str) -> str:
        return f"{self.type}_{name}_{self.size}"

    def new_name(self) -> str:
        """Model the compare / deploy / relabel_after_train steps work on."""
        if self.trained_name:
            return self.trained_name
        if self.cfg["model"]["name"] != "auto":
            return self.cfg["model"]["name"]
        last = self.state.get("last_trained")
        if last and self.state.get("last_trained_type") == self.type:
            return last
        raise PipelineError("no trained model yet - enable the train step (or set [model] name "
                            "to an existing run)")

    def new_model(self) -> Path:
        stem = self.stem(self.new_name())
        p = self.output / stem / f"{stem}_q.tflite"
        if not p.is_file() and not self.dry:
            raise PipelineError(f"{rel(p)} not found - run the train step first")
        return p

    # -- running children -----------------------------------------------------
    def child_env(self) -> dict:
        env = dict(os.environ)
        env["PYTHONUNBUFFERED"] = "1"
        env.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
        env.setdefault("PYTHONIOENCODING", "utf-8")
        return env

    def run(self, step: str, argv: list[str]) -> None:
        self.say(f"$ {self.show_cmd(argv)}")
        if self.dry:
            return
        p = subprocess.Popen(argv, cwd=HERE, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=self.child_env())
        try:
            self._pump(p)
            code = p.wait()
        except KeyboardInterrupt:
            self._stop(p)
            raise
        if code != 0:
            raise StepFailed(f"step '{step}' failed ({Path(argv[1]).name} exit code {code})")

    def _pump(self, p: subprocess.Popen) -> None:
        fd = p.stdout.fileno()
        out = sys.stdout.buffer
        while True:
            chunk = os.read(fd, 8192)
            if not chunk:
                break
            out.write(chunk)
            out.flush()
            self._log_bytes(chunk)

    @staticmethod
    def _stop(p: subprocess.Popen) -> None:
        if p.poll() is not None:
            return
        try:
            if os.name == "nt":
                p.terminate()
            else:
                p.send_signal(signal.SIGINT)    # label_tool prints its summary on Ctrl-C
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()

    # -- manual review --------------------------------------------------------
    def review(self, step: str, argv: list[str], snapshot: Path | None = None) -> dict:
        """Run label_tool.py until its queue is empty or Enter is pressed.

        Returns the final counts plus "files_changed" (rename/trash count from comparing
        the folder listing before and after) when snapshot is given.
        """
        port = self.cfg["review"]["port"]
        url = f"http://127.0.0.1:{port}/"
        self.say(f"$ {self.show_cmd(argv)}")
        if self.dry:
            self.say(f"  {HAND} would open {url} and wait until the queue is empty or Enter")
            return {}
        with socket.socket() as s:
            if os.name != "nt":     # like http.server: ignore TIME_WAIT from the last review
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind(("127.0.0.1", port))
            except OSError:
                raise PipelineError(f"port {port} is in use (another label_tool.py running?); "
                                    f"stop it or change [review] port") from None
        before = self._listing(snapshot) if snapshot else None
        p = subprocess.Popen(argv, cwd=HERE, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=self.child_env())
        pump = threading.Thread(target=self._pump, args=(p,), daemon=True)
        pump.start()
        counts: dict = {}
        try:
            t0 = time.time()
            while True:              # wait for the server
                if p.poll() is not None:
                    pump.join(5)
                    raise StepFailed(f"step '{step}': label_tool.py exited (code {p.returncode})"
                                     f" before it was ready - see its output above")
                c = self._counts(port)
                if c is not None:
                    counts = c
                    break
                if time.time() - t0 > 120:
                    raise StepFailed(f"step '{step}': label_tool.py did not start within 2 min")
                time.sleep(0.3)
            self.say(f"\n  {HAND} Review in your browser: {url}   "
                     f"({counts.get('pending', '?')} to do)")
            if self.cfg["review"]["open_browser"]:
                try:
                    webbrowser.open(url)
                except Exception:    # noqa: BLE001 - a missing browser is not fatal
                    pass
            self.drain_stdin()
            if self.stdin_eof.is_set():
                self.say("  Waiting until the queue is empty (no terminal input available) ...")
            else:
                self.say("  Press Enter when you are done reviewing "
                         "(or wait: continues automatically when the queue is empty)")
            while True:
                if counts.get("pending", 1) == 0:
                    self.say("  Queue is empty - continuing.")
                    break
                try:
                    self.stdin_q.get(timeout=2)
                    self.say("  Enter pressed - continuing.")
                    break
                except queue.Empty:
                    pass
                if p.poll() is not None:
                    self.say("  label_tool.py was stopped - continuing.")
                    break
                c = self._counts(port)
                if c is not None:
                    counts = c
        finally:
            c = self._counts(port) if p.poll() is None else None
            if c is not None:
                counts = c
            self._stop(p)
            pump.join(5)
        if snapshot:
            after = self._listing(snapshot)
            counts["files_changed"] = len(before - after)
        done = ", ".join(f"{v} {k}" for k, v in counts.items()
                         if k not in ("total", "relabel", "files_changed"))
        self.say(f"  Review finished: {done}.")
        return counts

    @staticmethod
    def _counts(port: int) -> dict | None:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/counts", timeout=3) as r:
                return json.loads(r.read())
        except (OSError, ValueError):
            return None

    @staticmethod
    def _listing(folder: Path) -> set[str]:
        """All image paths below folder, _trash included (a trashed file 'disappears')."""
        if not folder.is_dir():
            return set()
        return {str(f.relative_to(folder)) for f in folder.rglob("*")
                if f.is_file() and not any(part.startswith("_trash")
                                           for part in f.relative_to(folder).parts)}

    def csv_rows_in(self, csv_path: Path, folder: Path) -> int:
        """How many rows of a false_predictions.csv point at an image in folder."""
        if not csv_path.is_file():
            return 0
        hashes = {hash_from_name(f.name) for f in iter_images([folder])} - {None}
        n = 0
        with open(csv_path, newline="") as fh:
            for row in csv.DictReader(fh):
                name = (row.get("file") or "").strip()
                if not name:
                    continue
                p = Path(name)
                p = p if p.is_absolute() else HERE / p
                h = hash_from_name(p.name)
                inside = p.resolve().is_relative_to(folder.resolve())
                if (inside and p.is_file()) or (h and h in hashes):
                    n += 1
        return n

    # -- steps --------------------------------------------------------------
    def step_fetch(self) -> None:
        if not self.dry:
            self.device_config()        # quick reachability check with a clear message
        folder = "/log/digit" if self.section == "Digits" else "/log/analog"
        argv = [self.py, "fetch_images.py", "--device", self.ip, "--folders", folder,
                "--out", rel(self.data / "device")]
        if self.cfg["device"]["pause_during_transfer"]:
            argv.append("--pause")
        self.run("fetch", argv)

    def crops_folder(self) -> Path:
        sub = "digit" if self.section == "Digits" else "analog"
        return self.data / "device" / self.devname / "log" / sub

    def step_prelabel(self) -> None:
        src = self.crops_folder()
        if not self.dry and count_images(src) == 0:
            self.say(f"  Nothing to pre-label: no crops in {rel(src)} (run the fetch step).")
            return
        c = self.cfg["prelabel"]
        argv = [self.py, "prelabel.py", "--model", rel(self.current_model()),
                "--input", rel(src), "--out", rel(self.data / "review"),
                "--labeled", rel(self.data / "labeled"), "--threshold", str(c["threshold"])]
        if c["dedupe"]:
            argv += ["--dedupe", "--dedupe-distance", str(c["dedupe_distance"])]
        self.run("prelabel", argv)

    def label_tool_argv(self, folder: Path, csv_path: Path | None = None) -> list[str]:
        argv = [self.py, "label_tool.py", "--dir", rel(folder), "--type", self.type,
                "--port", str(self.cfg["review"]["port"])]
        if csv_path is None:
            argv += ["--out", rel(self.data / "labeled")]
        else:
            argv += ["--relabel", "--from-csv", rel(csv_path)]
        return argv

    def step_label_new(self) -> None:
        n = count_images(self.review_dir)
        if n == 0 and not self.dry:
            self.say(f"  Nothing to review: no new images in {rel(self.review_dir)}.")
            return
        self.review("label_new", self.label_tool_argv(self.review_dir))

    def eval_out(self, model: Path) -> Path:
        return self.eval_dir / model.stem

    def step_evaluate_current(self) -> None:
        if count_images(self.labeled) == 0 and not self.dry:
            self.say(f"  Nothing labelled yet in {rel(self.labeled)} - nothing to check.")
            return
        cur = self.current_model()
        self.run("evaluate_current", [self.py, "evaluate.py", rel(cur), rel(self.labeled),
                                      "--out", rel(self.eval_out(cur))])

    def step_relabel(self) -> None:
        cur = self.current_model()
        csv_path = self.eval_out(cur) / "false_predictions.csv"
        if not csv_path.is_file() and not self.dry:
            self.say(f"  Nothing to review: {rel(csv_path)} does not exist "
                     f"(enable evaluate_current).")
            return
        if not self.dry and self.csv_rows_in(csv_path, self.labeled) == 0:
            self.say("  Nothing to review: the current model agrees with every label.")
            return
        self.review("relabel", self.label_tool_argv(self.labeled, csv_path),
                    snapshot=self.labeled)

    def train_argv(self, name: str) -> list[str]:
        t = self.cfg["train"]
        data = [self.labeled]
        for c in self.community:
            if c.is_dir():
                data.append(c)
            else:
                self.say(f"  WARNING: community folder {c} not found, ignored")
        argv = [self.py, "train.py", "--type", self.type, "--size", self.size,
                "--data", *[rel(d) for d in data], "--name", name,
                "--out", rel(self.output / self.stem(name)),
                "--epochs", str(t["epochs"]), "--optimizer", t["optimizer"],
                "--seed", str(t["seed"])]
        if t["balance"]:
            argv.append("--balance")
        return argv + list(t["extra_args"])

    def step_train(self) -> None:
        n = count_images(self.labeled)
        if n == 0 and not self.dry:
            raise StepFailed(f"nothing labelled yet in {rel(self.labeled)} - label some images "
                             f"first (steps fetch, prelabel, label_new)")
        name = self.run_name()
        self.save_state(open_run_name=name)
        if not self.dry:
            self.say(f"  {n} labelled images; run name {name}")
        self.run("train", self.train_argv(name))
        self.trained_name = name
        stem = self.stem(name)
        self.save_state(last_trained=name, last_trained_type=self.type,
                        last_trained_model=str(self.output / stem / f"{stem}_q.tflite"),
                        last_trained_at=time.strftime("%Y-%m-%d %H:%M:%S"))

    def step_relabel_after_train(self) -> None:
        name = self.new_name()
        stem = self.stem(name)
        csv_path = self.output / stem / "false_predictions.csv"
        if not self.dry:
            if not csv_path.is_file():
                raise StepFailed(f"{rel(csv_path)} not found - run the train step first")
            if self.csv_rows_in(csv_path, self.labeled) == 0:
                self.say("  Nothing to review: the new model agrees with every label.")
                return
        res = self.review("relabel_after_train", self.label_tool_argv(self.labeled, csv_path),
                          snapshot=self.labeled)
        changed = res.get("files_changed", 0)
        if self.dry:
            self.say("  (trains again with the same name if any label changes)")
            return
        if changed:
            self.say(f"\n  {changed} file(s) changed during the review -> training again "
                     f"(same name, overwriting {rel(self.output / stem)})")
            self.trained_name = name
            self.run("relabel_after_train", self.train_argv(name))
            self.save_state(last_trained=name, last_trained_type=self.type,
                            last_trained_at=time.strftime("%Y-%m-%d %H:%M:%S"))
        else:
            self.say("  No label changed - no second training needed.")

    def metric(self, metrics: dict) -> tuple[str, float]:
        key = "within_0.1" if self.type.startswith("ana-") else "accuracy"
        if key not in metrics:
            key = next((k for k in ("accuracy", "within_0.1") if k in metrics), None)
            if key is None:
                raise PipelineError("evaluate.py's metrics.json has no accuracy / within_0.1")
        return key, float(metrics[key]) * 100

    def step_compare(self) -> None:
        if count_images(self.labeled) == 0 and not self.dry:
            raise StepFailed(f"nothing labelled in {rel(self.labeled)} to compare on")
        cur, new = self.current_model(), self.new_model()
        self.compared = True
        results = {}
        for role, model in (("current", cur), ("new", new)):
            out = self.eval_out(model)
            if role == "new" and model.resolve() == cur.resolve():
                out = self.eval_dir / (model.stem + "-new")
            self.run("compare", [self.py, "evaluate.py", rel(model), rel(self.labeled),
                                 "--out", rel(out)])
            if not self.dry:
                try:
                    results[role] = json.loads((out / "metrics.json").read_text())
                except (OSError, ValueError) as e:
                    raise StepFailed(f"cannot read {rel(out / 'metrics.json')}: {e}") from None
        if self.dry:
            return
        key, c = self.metric(results["current"])
        _, n = self.metric(results["new"])
        label = "within +-0.1" if key == "within_0.1" else "accuracy"
        samples = results["new"].get("samples", 0)
        self.say(f"\n  Comparison on {rel(self.labeled)} ({samples} images, {label}):")
        self.say(f"    {'model':<44} {label:>12}  {'wrong':>6}")
        for role, model, val in (("current", cur, c), ("new", new, n)):
            s = results[role].get("samples", 0)
            wrong = round(s * (1 - val / 100))
            self.say(f"    {role + ' ' + model.name:<44} {val:>10.2f} %  {wrong:>6}")
        self.say(f"\n  current: {c:.2f} %  new: {n:.2f} %  ({n - c:+.2f})")
        self.compare_result = {"current": c, "new": n, "metric": key,
                               "current_model": str(cur), "new_model": str(new)}
        self.save_state(last_compare=self.compare_result)

    def step_deploy(self) -> None:
        d = self.cfg["deploy"]
        new = self.new_model()
        gain = d["min_gain"]
        if gain == -1:
            self.say("  min_gain = -1: deploying without comparison")
        else:
            if not self.compared:
                self.say("  (running the comparison for the min_gain gate)")
                self.step_compare()
            if not self.dry:
                r = self.compare_result
                need = r["current"] + gain
                if r["new"] < need - 1e-9:
                    self.say(f"  Deploy skipped: the new model ({r['new']:.2f} %) is not at least "
                             f"{gain:.2f} points better than the current one "
                             f"({r['current']:.2f} %, needs >= {need:.2f} %).")
                    self.say("  Set [deploy] min_gain = -1 to deploy anyway.")
                    self.save_state(last_deploy_skipped=time.strftime("%Y-%m-%d %H:%M:%S"))
                    return
                self.say(f"  Gate passed: {r['new']:.2f} % >= {r['current']:.2f} % + {gain:.2f}")
        if not self.dry:
            self.device_config()
        argv = [self.py, "deploy.py", rel(new), "--device", self.ip, "--section", self.section,
                "--backup-dir", rel(self.backups)]
        if d["activate"]:
            argv.append("--activate")
            if d["auto_reboot"] or self.yes:
                argv.append("--yes")
            elif self.dry:
                self.say("  (asks first: reboot after the upload? yes -> --yes, no -> --no-reboot)")
                argv.append("--no-reboot")
            else:
                argv.append("--yes" if self.ask_yes_no(
                    "Reboot the device after the upload to load the new model?")
                    else "--no-reboot")
        self.run("deploy", argv)
        self.save_state(last_deployed=new.name, last_deployed_at=time.strftime("%Y-%m-%d %H:%M:%S"),
                        last_deployed_activated=d["activate"])

    # -- driver ---------------------------------------------------------------
    def print_plan(self) -> None:
        dev = f"[{self.section}] on {self.ip}" if self.ip else f"[{self.section}]"
        self.say(f"Pipeline {self.config_path.name}: {self.type} {self.size}, {dev}"
                 + ("   (dry run)" if self.dry else ""))
        n = 0
        for name, manual, desc in STEPS:
            mark = HAND if manual else " "
            if name in self.steps:
                n += 1
                self.say(f"  {n:>2} {name:<20}{mark}  {desc}")
            else:
                self.say(f"   - {name:<20}{mark}  (off)")
        self.say(f"  {HAND} = manual review in the browser; commands run in {HERE}")

    def check_packages(self) -> None:
        need = sorted({m for s in self.steps for m in NEEDS.get(s, [])})
        missing = [m for m in need if importlib.util.find_spec(m) is None]
        if missing:
            names = {"PIL": "Pillow"}
            msg = (f"missing Python packages for these steps: "
                   f"{', '.join(names.get(m, m) for m in missing)} (running {self.py}).\n"
                   f"  Install them with:  pip install -r requirements.txt")
            venv = HERE / ".venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
            if venv.is_file() and Path(sys.prefix).resolve() != (HERE / ".venv").resolve():
                msg += f"\n  or use the toolkit's venv:  {rel(venv)} pipeline.py ..."
            if self.dry:
                self.say("WARNING: " + msg)
            else:
                raise PipelineError(msg)

    def main(self) -> int:
        if not self.dry:
            self.output.mkdir(parents=True, exist_ok=True)
            log_path = self.output / f"pipeline-{time.strftime('%Y%m%d-%H%M%S')}.log"
            self.logf = open(log_path, "wb")
        self.print_plan()
        if not self.steps:
            self.say("No steps enabled - nothing to do.")
            return 0
        if not self.ip and ({"fetch", "deploy"} & set(self.steps)
                            or self.cfg["model"]["current"] == "auto"):
            raise PipelineError("[device] ip is not set in the config")
        self.check_packages()
        if not self.dry:
            self.start_stdin_reader()
        t_all = time.time()
        total = len(self.steps)
        current = None
        try:
            for i, name in enumerate(self.steps, 1):
                current = name
                mark = f" {HAND}" if name in MANUAL else ""
                self.say(f"\n=== [{i}/{total}] {name}{mark} " + "=" * max(3, 50 - len(name)))
                t0 = time.time()
                getattr(self, f"step_{name}")()
                self.timings.append((name, time.time() - t0, "ok"))
        except KeyboardInterrupt:
            self.timings.append((current, time.time() - t0, "interrupted"))
            self.summary(t_all)
            self.say(f"\nInterrupted during '{current}'. Re-running is safe "
                     f"(finished steps are repeated cheaply).")
            self.save_state(last_run=time.strftime("%Y-%m-%d %H:%M:%S"),
                            last_run_result=f"interrupted at {current}")
            return 130
        except PipelineError as e:
            self.timings.append((current, time.time() - t0, "FAILED"))
            self.summary(t_all)
            self.say(f"\nERROR: {e}")
            self.say(f"Pipeline stopped at step '{current}'. Fix the problem and run it again "
                     f"(add --steps {current},... to start there).")
            self.save_state(last_run=time.strftime("%Y-%m-%d %H:%M:%S"),
                            last_run_result=f"failed at {current}")
            return 1
        self.summary(t_all)
        self.save_state(last_run=time.strftime("%Y-%m-%d %H:%M:%S"), last_run_result="ok",
                        last_run_steps=self.steps, open_run_name=None)
        return 0

    def summary(self, t_all: float) -> None:
        if self.dry:
            return
        self.say("\n=== Summary " + "=" * 47)
        for name, secs, res in self.timings:
            self.say(f"  {name:<22}{fmt_secs(secs):>8}  {res}")
        self.say(f"  {'total':<22}{fmt_secs(time.time() - t_all):>8}")
        if self.trained_name:
            self.say(f"  trained: {rel(self.output / self.stem(self.trained_name))}/")
        if self.logf:
            self.say(f"  log: {rel(Path(self.logf.name))}   state: {rel(self.state_path)}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Run the collect -> relabel -> retrain -> redeploy loop from a TOML config.",
        epilog="Steps (fixed order): " + ", ".join(STEP_NAMES))
    ap.add_argument("config", help="TOML config (start from training.example.toml)")
    ap.add_argument("--steps", help="comma-separated steps to run this time, overriding [steps] "
                                    "(still run in the fixed order)")
    ap.add_argument("--dry-run", action="store_true", help="print the commands, run nothing")
    ap.add_argument("--yes", action="store_true",
                    help="answer terminal questions (reboot after deploy) with yes")
    args = ap.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    try:
        cfg = load_config(Path(args.config))
        if args.steps:
            want = [s.strip() for s in args.steps.split(",") if s.strip()]
            bad = [s for s in want if s not in STEP_NAMES]
            if bad:
                raise PipelineError(f"unknown step(s) {', '.join(bad)}; "
                                    f"steps are: {', '.join(STEP_NAMES)}")
            steps = [s for s in STEP_NAMES if s in want]
        else:
            steps = [s for s in STEP_NAMES if cfg["steps"][s]]
        pl = Pipeline(cfg, Path(args.config), steps, args.dry_run, args.yes)
        code = pl.main()
    except PipelineError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        code = 2
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        code = 130
    sys.exit(code)


if __name__ == "__main__":
    main()
