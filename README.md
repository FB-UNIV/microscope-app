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

## Current status: Phase 1 core and the capture app (per ADR 0009)

This is the first slice: the `Treatment` pipeline (ADR 0002), `Levels` and
`Échelle` treatments, and `Vue` persistence (ADR 0003 — plain original +
JSON sidecar, non-destructive). It started as pure C++ + OpenCV with no
Qt and no camera — deliberately, so the imaging math and the storage model
could be built and verified without needing a Windows machine, Qt, or a
Moticam attached. Camera capture came next (below), then the Qt 6 capture
app in `app/` (see *Using the app*).

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
folder, so two Vues are never flagged at once. `fileVue()` files a Vue into
an Échantillon — an Unsorted one once its TP exists (ADR 0008), or one
captured under the wrong Échantillon: the image moves with its sidecar and
any set-aside sidecar, keeping its capture-time name unless that name is
taken there, and never over another file (ADR 0004). The sidecar moves
first and the image last; a failed move puts back what had moved. A Vue
principale filed away is unflagged in its old Échantillon.

The `Catalog` of Departments → TPs (with the instructions students see) →
Échantillons is `catalog.json`, beside the workstation config. In Phase 1 a
teacher writes it by hand (UTF-8): copy
[`docs/catalog-example.json`](docs/catalog-example.json); an Échantillon may be
a bare string. Names that would share a folder (`Ac1: XC10` and `Ac1/ XC10`,
or `GMP` and `gmp`) are refused with both names. No file means no
catalog, and captures go to Unsorted (ADR 0008). In Phase 2 the same file
becomes the cache of what the API serves (ADR 0007).

**Not yet built:** a catalog editor inside the app, and everything in
ADRs 0004–0008 (the k3s-hosted sync/API/backend) — that's Phase 2,
deliberately deferred. No Motic SDK backend is planned: the A5 doesn't need
one.

## Using the app

`microscope_app` (French UI) on a lab PC, MotiConnect closed:

- **Capture.** Pick *Département › TP › Échantillon* (from `catalog.json`;
  the TP's instructions show on the right), the *Grossissement*, then
  *Capturer* (Space). The live preview runs on a worker thread through a
  `CaptureStation`, so the camera profile is re-applied and the White
  reference baked in exactly as the capture will be. The Vue is filed by
  `VueStore` with the Grossissement's Calibration and its Échelle — or
  without either when that objective isn't calibrated, or was calibrated at
  another frame size than the camera delivers (ADR 0015); the banner says
  which. It also names every camera setting not granted (exposure, colour
  temperature, frame size, pixel format); a camera held by MotiConnect gets
  a message and *Reconnecter la caméra* (F5).
- **Vues.** The right panel lists the Échantillon's Vues, newest first;
  *★ Choisir comme Vue principale* flags the one for the Fiche.
  Double-click opens a Vue: Levels with a live preview, the Échelle,
  removing a Treatment (all saved in the sidecar at once), *Mesurer une
  longueur*, *Dureté Vickers* (two diagonals, load selector, HV0.5 by
  default), and *Analyse de phases* (CALCUL, percentages, histogram with
  the Seuil, the phase map as a Treatment). Measuring is refused on an
  uncalibrated Vue. *Exporter une copie* writes the Vue with its
  Treatments as PNG, TIFF or JPEG; the photo and sidecar are untouched.
- **Filing Vues.** *Classer la Vue…* moves the selected Vue, with its
  Treatments, into the Échantillon picked in a small *Département › TP ›
  Échantillon* dialog. Once a catalog exists, Vues captured before it
  (Unsorted, ADR 0008) are listed with *Vues non classées (N)* above the
  list, until every one is filed. A Vue open in its own window must be
  closed first.
- **Deleting Vues.** *Supprimer la Vue…* (button, right-click, or Suppr in
  the list; also in the Vue's own window) asks first, then moves the Vue
  with its Treatments to `Corbeille/` in the Vue folder, under the same
  *Département / TP / Échantillon* folders (`VueStore::deleteVue`). Nothing
  is erased: move its files back to recover it. A deleted Vue principale
  is unflagged, and a window open on it closes. Who may delete is not
  restricted yet.
- **Fiche menu.** *Générer la Fiche du TP* writes the picked TP's Fiche as
  an A4 PDF (ADR 0016): each Échantillon's Vue principale with its
  Treatments and Échelle, its name and Grossissement, two per row, as few
  pages as fit (`layoutFiche()`: eight 16:9 photos per page). An
  Échantillon without a Vue principale gets an empty frame, and the app
  lists them before writing. Then open the PDF to print it (ADR 0010).
- **Poste menu (setup).** *Mesurer la référence blanche* (blank field, lamp
  on), *Calibrer un grossissement sur la mire* (click a line of the 20 µm
  mire and the line N periods further, until two readings agree within
  0.25 %), *Modifier / Recharger le catalogue de TP*, *Ouvrir le dossier des
  Vues*. Settings are saved to `workstation.json`.
- **Demo PC.** These options override the config for one run without
  changing it: `--device N` (a laptop's own webcam is often 0), `--api
  dshow|msmf|auto`, `--size WxH` (`0x0` = device default), and `--replay
  IMAGE` (repeatable) to work on image files with no camera at all.

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
2. Try the capture app on that PC with a real session: write `catalog.json`,
   measure the White reference, calibrate each objective on the mire,
   capture, then check a Measurement against the mire and Phase analysis
   against an archived result. Confirm the phase-map colours with a
   teacher (#25: the legacy plane order was inferred).
3. Print a Fiche on the lab printer and check the Échelle bar against the
   mire on paper.

Phase-analysis parity always runs (CI included) on three 256×256 crops of
legacy views in `tests/fixtures/legacy/`. The full-size checks run against
real legacy view TIFFs when given their folder:
`./test_phase_analysis <folder>`, or
`cmake -DMSAPP_LEGACY_DATA_DIR=<folder> ..` then `ctest`. Without it those
print `[SKIP]`. `tools/legacy_phase_reference.py` is the independent
numpy implementation the golden numbers come from.
