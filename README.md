# Microscope Capture App

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

**Not yet built:** Qt UI, actual camera capture (OpenCV `VideoCapture`
first, Motic SDK as fallback — see conversation history), the
Measurement tool, Fiche (PDF) generation, and everything in ADRs 0004–0008
(the k3s-hosted sync/API/backend) — that's Phase 2, deliberately deferred.

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
```

## Next steps

1. Qt `MainWindow`: live preview + capture button, wired to `Vue::capture`.
2. Camera capture: try `cv::VideoCapture` against the real Moticam first;
   fall back to the official Motic SDK only if that proves unreliable
   (this was flagged as untested — first real test happens here).
3. Measurement tool (ephemeral, cursor-based — see `CONTEXT.md`).
4. Fiche generation (PDF: Échantillon name + Vue principale + Grossissement
   + Échelle — see ADR 0010).
