#!/usr/bin/env python3
"""Builds a self-contained demo of the whole workflow with the `pnam` CLI, for screenshots,
manual testing and CI smoke tests. Standard library only.

    python3 Build/make_demo_content.py PNAM_CLI DATA_DIR [--steps N] [--arch feather]

What it does, honestly labelled:
  * "Source captures": plain NAM files made by snapshotting the PANAMA demo parametric model
    (Resources/fixtures, MIT) at a grid of two of its knobs (K0, gain-like; K2, tone-like).
    They stand in for real captures of one amplifier and give a known ground truth.
  * A Capture Studio project assigning each capture its control positions, with 3 hold-outs.
  * Trains one parametric model from those captures with the real trainer (pnam train).
  * Registers the trained model and the original PANAMA demo in the model library, adds a
    synthetic test IR and a preset.
DATA_DIR is the app data folder (e.g. $XDG_DATA_HOME/ParametricNAMStudio on Linux).
"""
import argparse, json, math, os, pathlib, random, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIX = ROOT / "Resources" / "fixtures"


def write_wav(path, samples, sr):
    data = b"".join(struct.pack("<f", s) for s in samples)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, sr, sr * 4, 4, 32))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def synthetic_ir(sr=48000, n=2048, seed=4):
    """Decaying noise through a low-pass (~4.5 kHz) and high-pass (~90 Hz): a generic test IR, not a real cabinet."""
    rnd = random.Random(seed)
    x = [rnd.gauss(0, 1) * math.exp(-i / (0.004 * sr)) for i in range(n)]

    def biquad(sig, b, a):
        y, x1, x2, y1, y2 = [], 0, 0, 0, 0
        for v in sig:
            o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
            x2, x1, y2, y1 = x1, v, y1, o
            y.append(o)
        return y

    def lp(f0, q):
        w = 2 * math.pi * f0 / sr; al = math.sin(w) / (2 * q); c = math.cos(w)
        a0 = 1 + al
        return [(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0], [1, -2 * c / a0, (1 - al) / a0]

    def hp(f0, q):
        w = 2 * math.pi * f0 / sr; al = math.sin(w) / (2 * q); c = math.cos(w)
        a0 = 1 + al
        return [(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0], [1, -2 * c / a0, (1 - al) / a0]

    for f in (lp(4500, 0.9), lp(5500, 0.7), hp(90, 0.7)):
        x = biquad(x, *f)
    peak = max(abs(v) for v in x)
    return [v / peak * 0.9 for v in x]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cli")
    ap.add_argument("data_dir")
    ap.add_argument("--steps", type=int, default=3000)
    ap.add_argument("--arch", default="feather")
    a = ap.parse_args()
    data = pathlib.Path(a.data_dir)
    caps_dir = data / "Demo source captures"
    models_dir = data / "Models"
    projects = data / "Projects"
    presets = data / "Presets"
    for d in (caps_dir, models_dir, projects, presets):
        d.mkdir(parents=True, exist_ok=True)

    ref = FIX / "panama_demo.nam"
    grid = [(g, t, False) for g in (3.0, 6.5, 10.0) for t in (0.0, 5.0, 10.0)]
    grid += [(4.5, 2.5, True), (8.5, 7.5, True), (5.0, 6.0, True)]
    captures = []
    for g, t, hold in grid:
        name = f"Demo Amp - Gain {g:g} Tone {t:g}"
        path = caps_dir / f"{name}.nam"
        knobs = [g / 10, 0.5, t / 10, 0.5, 0.5, 0.5]
        subprocess.run([a.cli, "snapshot", str(ref), str(path), "--knobs", ",".join(f"{v:.4f}" for v in knobs)], check=True)
        m = json.loads(path.read_text())
        m["metadata"].update({"name": name, "gear_make": "Demo", "gear_model": "Demo Amp", "gear_type": "amp",
                              "modeled_by": "snapshot of the PANAMA demo model"})
        path.write_text(json.dumps(m))
        captures.append({"path": str(path), "label": f"Gain {g:g} Tone {t:g}", "knobs": [g / 10, t / 10], "holdout": hold, "assigned": True})

    (data / "captures.json").write_text(json.dumps({"version": 1, "captures": [
        {"path": c["path"], "amp": "Demo Amp", "amp_from_metadata": True, "cab": "no", "cab_reason": "metadata gear_type \"amp\""}
        for c in captures]}, indent=2))

    project = {"format": "pnam-studio-project", "version": 1, "amp": "Demo Amp", "manufacturer": "Demo", "channel": "",
               "description": "Two controls of the PANAMA demo model (K0 as Gain, K2 as Tone), captured as 12 snapshots.",
               "creator": "", "category": "Crunch",
               "knobs": [{"name": "Gain", "default": 0.5, "labels": [], "range": [0, 10]},
                         {"name": "Tone", "default": 0.5, "labels": [], "range": [0, 10]}],
               "captures": captures, "architecture": a.arch, "steps": a.steps, "knob_scale": True,
               "signal_seconds": 30.0, "di_files": [], "versions": []}
    proj_file = projects / "Demo Amp.pnamproj"
    proj_file.write_text(json.dumps(project, indent=2))

    model_out = projects / "Demo Amp models" / "Demo Amp v1.nam"
    model_out.parent.mkdir(exist_ok=True)
    subprocess.run([a.cli, "train", str(proj_file), "--out", str(model_out), "--arch", a.arch, "--steps", str(a.steps)], check=True)
    rep = json.loads(model_out.read_text())["metadata"]["training"]["report"]
    verdict = ("unseen settings " + ("excellent" if rep["mean_holdout_esr"] <= 0.01 else "good" if rep["mean_holdout_esr"] <= 0.03
                                     else "fair" if rep["mean_holdout_esr"] <= 0.08 else "poor" if rep["mean_holdout_esr"] <= 0.2 else "failed"))
    project["versions"].append({"number": 1, "file": str(model_out), "date": "demo", "architecture": a.arch, "verdict": verdict,
                                "trained_esr": rep["mean_trained_esr"], "holdout_esr": rep["mean_holdout_esr"],
                                "baseline_esr": rep["mean_nearest_baseline_esr"]})
    proj_file.write_text(json.dumps(project, indent=2))

    shutil.copy(model_out, models_dir / "Demo Amp v1.nam")
    shutil.copy(ref, models_dir / "PANAMA demo.nam")
    (data / "library.json").write_text(json.dumps({"version": 1, "models": [
        {"path": str(models_dir / "PANAMA demo.nam"), "name": "PANAMA demo (6 knobs)", "manufacturer": "ETH Zurich / Murphy Research",
         "amp": "PANAMA demo", "category": "Crunch", "tags": ["demo", "reference"], "collections": ["Demos"], "favorite": False,
         "description": "The PANAMA demo checkpoint converted to ParametricWaveNet (parametric-nam fixture, MIT).", "creator": "", "cabinet": "",
         "channel": "", "validation": ""},
        {"path": str(models_dir / "Demo Amp v1.nam"), "name": "Demo Amp v1", "manufacturer": "Demo", "amp": "Demo Amp",
         "category": "Crunch", "tags": ["demo", "trained here"], "collections": ["Demos"], "favorite": True,
         "description": "Trained in the Capture Studio from 9 captures; validated on 3 held-out settings.", "creator": "", "cabinet": "none (use an IR)",
         "channel": "", "validation": verdict}]}, indent=2))

    ir = data / "Synthetic test IR.wav"
    write_wav(ir, synthetic_ir(), 48000)
    preset = {"format": "pnam-preset", "version": 1, "name": "Demo crunch",
              "model": {"path": str(models_dir / "Demo Amp v1.nam"), "hash": "", "name": "Demo Amp v1"},
              "controls": {"names": ["Gain", "Tone"], "values": [0.62, 0.45]},
              "input_gain_db": 0.0, "output_gain_db": -3.0, "gate": {"on": True, "threshold_db": -72.0},
              "normalize_loudness": True, "stereo": True,
              "cab": {"enabled": True, "a": {"path": str(ir), "enabled": True, "level_db": 0.0, "pan": -0.3, "invert": False},
                      "b": {"path": "", "enabled": False, "level_db": -6.0, "pan": 0.3, "invert": False}}}
    (presets / "Demo crunch.pnampreset").write_text(json.dumps(preset, indent=2))
    print(f"demo content in {data}: held-out ESR {rep['mean_holdout_esr']:.4f}, nearest-capture baseline {rep['mean_nearest_baseline_esr']:.4f}")


if __name__ == "__main__":
    sys.exit(main())
