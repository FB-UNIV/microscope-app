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

Also built since: the `Measurement` tool; the legacy lab measurements,
ported bit-exact from the decompiled legacy exe (ADR 0012) — Phase
analysis (`analyzePhases`, plus the `phase_overlay` Treatment) and Dureté
Vickers (`vickersHardness`); the `ICamera` capture abstraction with file
replay and OpenCV backends (ADR 0011); and importers for legacy Department
catalogs (`GMP1.txt`, …) and legacy view TIFFs with their calibration.

**Not yet built:** Qt UI, the Motic SDK camera backend (waiting on the
SDK — Motic USB cameras are WinUSB, not UVC, see ADR 0011), Fiche (PDF)
generation, and everything in ADRs 0004–0008 (the k3s-hosted
sync/API/backend) — that's Phase 2, deliberately deferred.

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
`tests/output/`.

## Layout

```
include/            Public headers for the core lib
src/                 Implementation
tests/               Test harness (no test framework dependency — plain asserts + PASS/FAIL output)
CONTEXT.md           Domain glossary — read this first
docs/adr/            Architecture Decision Records — the "why" behind every structural choice
docs/legacy/         Reverse-engineering report of the legacy Metal exe + legacy→new gap analysis
tools/               legacy_phase_reference.py — independent reference for the phase-analysis port
tests/fixtures/      Real legacy Department catalogs (GMP1.txt, CTTM.txt) used by the importer tests
```

## Next steps

1. Qt `MainWindow`: live preview + capture button, wired to `ICamera` +
   `Vue::capture` (file-replay backend until the Motic one exists).
2. Motic SDK backend: one `ICamera` subclass once the developer SDK is in
   hand; check first whether the purchased model shows up as a UVC
   "Camera" in Device Manager (then the OpenCV backend already works).
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
