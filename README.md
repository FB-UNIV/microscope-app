# Microscope Capture App

[![CI](https://github.com/FB-UNIV/microscope-app/actions/workflows/ci.yml/badge.svg)](https://github.com/FB-UNIV/microscope-app/actions/workflows/ci.yml)

Replacement for the legacy Matrox-based metallography imaging tool. See
[`CONTEXT.md`](./CONTEXT.md) for the domain glossary (Grossissement,
Calibration, Échelle, Vue, TP, Échantillon, Fiche, etc.) and
[`docs/adr/`](./docs/adr/) for every architecture decision and *why* it was
made — read those before changing anything structural here, especially
anything that looks like it could be simplified. Several things in this
codebase look more complex than they need to be on purpose (see ADR 0004
and ADR 0007 in particular); the ADR explains the failure mode it's there
to prevent.

## Current status: Phase 1 core (per ADR 0009), pre-UI

This is the first slice: the `Treatment` pipeline (ADR 0002), `Levels` and
`Échelle` treatments, and `Vue` persistence (ADR 0003 — plain original +
JSON sidecar, non-destructive). It started as pure C++ + OpenCV with no
Qt and no camera — deliberately, so the imaging math and the storage model
could be built and verified without needing a Windows machine, Qt, or a
Moticam attached. Camera capture came next (below); the Qt 6 app in
`app/` now shows a live camera preview (no capture button yet).

Also built since: `CaptureStation` with the measured Moticam A5 camera
profile and the per-microscope White reference (ADR 0014); the
`Measurement` tool; the legacy lab measurements,
ported bit-exact from the decompiled legacy exe (ADR 0012) — Phase
analysis (`analyzePhases`, plus the `phase_overlay` Treatment) and Dureté
Vickers (`vickersHardness`); the `ICamera` capture abstraction with file
replay and OpenCV backends — the OpenCV one is the production path, since
the lab's Moticam A5 cameras are UVC (ADR 0013); `tools/camera_probe` for
the first hardware check. Each Vue's sidecar keeps the Calibration it was
captured with, tied to the frame size it was measured at (ADR 0015), and
every calibrated capture gets its Échelle automatically (ADR 0017). Nothing
is migrated from the legacy app (ADR 0018).

Each workstation keeps its `WorkstationConfig` — the camera profile (capture
API, frame size, pixel format, exposure, colour temperature), the White
reference, and one Calibration per Grossissement with the frame size it was
measured at — in `workstation.json` under the per-user *local* app data
folder (`defaultWorkstationConfigPath()`: `%LOCALAPPDATA%\microscope-app\`
on Windows, so it never roams to another lab PC; `$XDG_CONFIG_HOME` or
`~/.config` elsewhere). First launch loads the Moticam A5 profile with no
Calibrations. Saves are atomic, and a file that can't be read loads as
defaults with an error instead of blocking the app; the next save keeps it
aside as `workstation.json.unreadable` (ADR 0004).
`calibrationForFrame(grossissement, width, height)` refuses a Calibration
measured at another frame size than the camera delivers (ADR 0015).

`VueStore` files every capture under a new, never-reused path:
`<root>/<Department>/<TP>/<Échantillon>/<UTC time>_<random>.tiff`, or
`<root>/Unsorted/` before any TP exists (ADR 0008). Names are made safe as
Windows folder names (`TP 1: Aciers` → `TP 1_ Aciers`). `listVues()` lists
an Échantillon's Vues in capture order. The root is the workstation
config's `vue_root`, by default `vues` beside the config file. Each
Échantillon's Vue principale — the one its TP's Fiche shows (ADR 0016) —
is flagged with `setPrincipale()`, one atomic `vue-principale.json` in its
folder, so two Vues are never flagged at once.

The `Catalog` of Departments → TPs (with the instructions students see) →
Échantillons is `catalog.json`, beside the workstation config. In Phase 1 a
teacher writes it by hand (UTF-8): copy
[`docs/catalog-example.json`](docs/catalog-example.json); an Échantillon may be
a bare string. Names that would share a folder (`Ac1: XC10` and `Ac1/ XC10`,
or `GMP` and `gmp`) are refused with both names. No file means no
catalog, and captures go to Unsorted (ADR 0008). In Phase 2 the same file
becomes the cache of what the API serves (ADR 0007).

`microscope_app` previews the workstation's camera through a
`CaptureStation` on a worker thread, so the profile is re-applied and the
White reference baked in exactly as a capture would see it. A banner names
every setting the camera did not grant (exposure, colour temperature, frame
size, pixel format), and a camera held by MotiConnect gets a message plus
a *Reconnecter la caméra* button (F5). For a demo PC, these options
override the config for one run without changing it: `--device N` (e.g. a
laptop whose own webcam is 0), `--api dshow|msmf|auto`, `--size WxH`
(`0x0` = device default), and `--replay IMAGE` (repeatable) to preview image
files with no camera at all.

**Not yet built:** the rest of the Qt UI (capture, Calibration wizard, TP/Échantillon picker), Fiche (PDF) generation, and everything in ADRs
0004–0008 (the k3s-hosted sync/API/backend) — that's Phase 2, deliberately
deferred. No Motic SDK backend is planned: the A5 doesn't need one.

## Building

Requires CMake, a C++17 compiler, OpenCV, nlohmann-json, and Qt 6.4+
(Widgets) for the app. Without Qt, configure with `-DMSAPP_BUILD_APP=OFF`
to build only the core, its tests and `camera_probe`: the core never links
Qt.

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./test_pipeline
```

`ctest` also runs `microscope_app --smoke-test` on Qt's offscreen platform:
it shows the `MainWindow` and quits, so it needs no display. CI publishes
a ready-to-run Windows bundle of the app (`microscope_app-windows`: exe,
Qt, OpenCV and MSVC runtime DLLs), built with the Qt version pinned in
`.github/workflows/ci.yml`.

`test_pipeline` synthesizes a low-contrast test image (standing in for a
real capture until real hardware is available), runs it through Levels +
Échelle, saves it as a Vue, reloads the Vue from disk, and checks the
reload round-trips pixel-identically. Output images land in
`msapp_pipeline_test/` under the system temp directory (the test prints
their full paths), never in the source tree.

## Layout

```
include/            Public headers for the core lib
src/                 Implementation
app/                 Qt 6 desktop app (MainWindow) — the only code that links Qt
tests/               Test harness (no test framework dependency — plain asserts + PASS/FAIL output)
CONTEXT.md           Domain glossary — read this first
docs/adr/            Architecture Decision Records — the "why" behind every structural choice
docs/legacy/         Reverse-engineering report of the legacy Metal exe + legacy→new gap analysis
tools/               camera_probe.cpp — lab-PC camera check; legacy_phase_reference.py — independent reference for the phase-analysis port
tests/fixtures/      256×256 crops of legacy views for the phase-analysis parity test
```

## Next steps

1. On a lab PC with a Moticam A5 plugged in (MotiConnect closed), run
   `camera_probe` and `camera_probe --api dshow`, then retry with
   `--size WIDTHxHEIGHT` / `--mjpg` until the full sensor resolution comes
   back; record the working capture API, size and pixel format (ADR 0013).
   Step-by-step guide, including where to download the ready-to-run
   Windows bundle CI builds: [docs/hardware/camera-probe.md](docs/hardware/camera-probe.md).
2. Finish the Qt `MainWindow` (`app/`; the live preview is done): a capture
   button once Vues have unique paths (#18), passing
   `calibrationForFrame()` of the selected Grossissement to `capture()`; a
   setup action to calibrate the White reference from a blank field and save
   it with `saveWorkstationConfig()`; Measurements with
   `Measurement(p1, p2, vue)`.
3. Calibration wizard UI against the 20 µm mire. `MireCalibration` already
   does the maths: each pair of clicks N periods apart is one px/µm
   reading, readings repeat until the newest two agree within 0.25 % (the
   legacy procedure), and `result()` is their mean with the frame size it
   was measured at (ADR 0015), to store with
   `WorkstationConfig::setCalibration()`.
4. Fiche generation: one PDF sheet per TP with each Échantillon's name,
   Vue principale, Grossissement and Échelle (ADR 0016).

Phase-analysis parity always runs (CI included) on three 256×256 crops of
legacy views in `tests/fixtures/legacy/`. The full-size checks run against
real legacy view TIFFs when given their folder:
`./test_phase_analysis <folder>`, or
`cmake -DMSAPP_LEGACY_DATA_DIR=<folder> ..` then `ctest`. Without it those
print `[SKIP]`. `tools/legacy_phase_reference.py` is the independent
numpy implementation the golden numbers come from.
