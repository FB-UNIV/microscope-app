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

## Current status: Phase 1 core (per ADR 0009), pre-UI, pre-camera

This is the first slice: the `Treatment` pipeline (ADR 0002), `Levels` and
`Échelle` treatments, and `Vue` persistence (ADR 0003 — plain original +
JSON sidecar, non-destructive). It's pure C++ + OpenCV, no Qt yet, no
camera capture yet — deliberately, so the imaging math and the storage
model could be built and verified without needing a Windows machine, Qt,
or a Moticam attached.

Also built since: `CaptureStation` with the measured Moticam A5 camera
profile and the per-microscope White reference (ADR 0014); the
`Measurement` tool; the legacy lab measurements,
ported bit-exact from the decompiled legacy exe (ADR 0012) — Phase
analysis (`analyzePhases`, plus the `phase_overlay` Treatment) and Dureté
Vickers (`vickersHardness`); the `ICamera` capture abstraction with file
replay and OpenCV backends — the OpenCV one is the production path, since
the lab's Moticam A5 cameras are UVC (ADR 0013); `tools/camera_probe` for
the first hardware check; and importers for legacy Department catalogs
(`GMP1.txt`, …) and legacy view TIFFs with their calibration.

**Not yet built:** Qt UI, Fiche (PDF) generation, and everything in ADRs
0004–0008 (the k3s-hosted sync/API/backend) — that's Phase 2, deliberately
deferred. No Motic SDK backend is planned: the A5 doesn't need one.

## Building

Requires CMake, a C++17 compiler, OpenCV, and nlohmann-json.

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./test_pipeline
```

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
tests/               Test harness (no test framework dependency — plain asserts + PASS/FAIL output)
CONTEXT.md           Domain glossary — read this first
docs/adr/            Architecture Decision Records — the "why" behind every structural choice
docs/legacy/         Reverse-engineering report of the legacy Metal exe + legacy→new gap analysis
tools/               camera_probe.cpp — lab-PC camera check; legacy_phase_reference.py — independent reference for the phase-analysis port
tests/fixtures/      Real legacy Department catalogs (GMP1.txt, CTTM.txt) used by the importer tests
```

## Next steps

1. On a lab PC with a Moticam A5 plugged in (MotiConnect closed), run
   `camera_probe` and `camera_probe --api dshow`, then retry with
   `--size WIDTHxHEIGHT` / `--mjpg` until the full sensor resolution comes
   back; record the working capture API, size and pixel format (ADR 0013).
   Step-by-step guide, including where to download the ready-to-run
   Windows bundle CI builds: [docs/hardware/camera-probe.md](docs/hardware/camera-probe.md).
2. Qt `MainWindow`: live preview + capture button, driving a
   `CaptureStation` built from `moticamA5Profile()` (re-applies the camera
   profile on every open, bakes in the White reference — ADR 0014), plus a
   setup action to calibrate the White reference from a blank field.
3. Calibration wizard against the 20 µm mire (legacy procedure: repeat
   until two readings agree within 0.25 %).
4. Fiche generation (PDF: Échantillon name + Vue principale + Grossissement
   + Échelle — see ADR 0010).

Phase-analysis parity always runs (CI included) on three 256×256 crops of
legacy views in `tests/fixtures/legacy/`. The full-size checks run against
real legacy view TIFFs when given their folder:
`./test_phase_analysis <folder>` / `./test_legacy_import <folder>`, or
`cmake -DMSAPP_LEGACY_DATA_DIR=<folder> ..` then `ctest`. Without it those
print `[SKIP]`. `tools/legacy_phase_reference.py` is the independent
numpy implementation the golden numbers come from.
