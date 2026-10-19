# Performance and code-quality review — 2026-10-19

Scope: the whole codebase at `3e8f45a` (`src/`, `include/`, `app/`, `tools/`, `tests/`), against the
C++ Core Guidelines, with a focus on the CPU spikes seen on the lab PCs. Method:

- Reading every file (core, Qt app, tools), with the ADRs as the reference for deliberate choices
  (ADR 0012's bit-exact float arithmetic is not flagged).
- A micro-benchmark of the app's own per-frame code on a 3840×2160 frame (§1).
- `g++ 12` strict warnings (`-Wpedantic -Wshadow -Wconversion -Wold-style-cast …`), every header
  compiled standalone, the core tests under ASan + UBSan + LeakSanitizer.
- Line coverage of the core tests with gcov (§5).
- clang-tidy and cppcheck are not installed on the dev machine (WSL Debian); the Qt app could not be
  built there (no Qt 6), so `app/` was reviewed by reading only.

**Verdict:** no CRITICAL issue (no memory-safety or security defect found); three HIGH robustness
issues and a clear, measured CPU story. The core is in very good shape (92.8 % line coverage, clean
under strict warnings and sanitizers); the Qt app carries most of the risk and almost no tests.

## TL;DR — what to do, in order

| # | Change | Why | Effort |
|---|--------|-----|--------|
| 1 | `cv::setNumThreads(1)` (or 2) at start-up | Removes the all-core bursts: OpenCV's pool burns 2.5–15× the CPU on these image sizes (§1.2) | 1 line |
| 2 | Preview: shrink to 1080p **before** white balance; skip the identity `clone()` | −40 % CPU per frame, same picture (§1.1) | small |
| 3 | Cache thumbnails, decode off the GUI thread | 125 ms per Vue on the GUI thread after every capture (§1.3) | small |
| 4 | Catch exceptions at every Qt slot boundary | Any throw in a slot terminates the app mid-session (H3) | small |
| 5 | Tolerate empty/dropped frames in the preview loop | One empty frame kills the live view (H2) | small |
| 6 | Read/write images through `imdecode`/`imencode` + wide-path streams | Non-ANSI Échantillon names (γ, α) break capture on Windows (H1) | medium |
| 7 | Try raw MJPG + `IMREAD_REDUCED_COLOR_2` for the preview | −60 % CPU per frame, needs a check on the A5 (§1.1) | medium, needs hardware |
| 8 | Merge the e2e pipeline and the coverage/sanitizer jobs (§6) | Regressions in the windows and the camera thread are caught | done, to merge |

**Status, same day:** rows 1–3 and §1.4 are implemented on the same branch as §6.
- `cv::setNumThreads(1)` is set in `main()`.
- The live view goes through `CaptureStation::grabLivePreview` and `whiteBalancedPreview`: it shrinks
  first, then applies a lookup table. While the window still holds the last frame, the next one is
  only grabbed (`dropFrame`).
- Thumbnails are cached by path and decoded one at a time on a `QThreadPool`.
- `FileReplayCamera` decodes each file once.

Measured per 4K preview frame:

| Case | CPU per frame |
|---|---:|
| Before (16 threads) | 77.5 ms |
| After (1 thread) | 39.4 ms (−49 %) |
| A dropped frame | 30.6 ms (the MJPG decode alone) |

At 13 fps that is about half a core instead of one. The Qt parts (`main.cpp`, `preview_worker.cpp`,
`main_window.cpp`) have not been compiled locally; the CI build is their first.

## 1. CPU usage

Measured with `bench_preview.cpp` (kept out of the repo; it calls the app's own functions) on the
lab mire resized to 3840×2160 and re-encoded as a quality-90 MJPG frame (1.6 MiB), on WSL Debian with
OpenCV 4.6 and 16 hardware threads. A lab PC is slower; the ratios hold.

### 1.1 Live preview: about one core busy all the time

`PreviewWorker::run` (`app/preview_worker.cpp:93`) → `CaptureStation::grabPreview` →
`toQImage(frame, 1920×1080)`, at the 13 fps camera_probe measured for 4K MJPG over DSHOW:

| Stage, per frame | CPU (1 thread) |
|---|---:|
| MJPG decode at 4K, inside `VideoCapture::read` (`src/opencv_camera.cpp:51`) | 30 ms |
| `applyWhiteBalanceGains` at 4K, `cv::multiply` by a `cv::Scalar` (`src/white_reference.cpp:59`) | 16 ms |
| … with identity gains: a 25 MB `clone()` instead (`src/white_reference.cpp:51`) | 1.3 ms |
| `toQImage`: INTER_AREA shrink 4K → 1080p + deep copy (`app/qt_image.cpp:14,21`) | 4 ms |
| **Whole frame today** | **60 ms ≈ 0.8 core at 13 fps** |

- Every frame is decoded and white-balanced at 4K **even when the window drops it**
  (`framePending_` already set, `app/preview_worker.cpp:95`): the work is thrown away.
- White balance runs on 8.3 M pixels, then the result is shrunk 4×. Per-channel gains commute with
  area averaging up to rounding (a level or two), so shrinking first and white-balancing 2 M pixels
  shows the same picture — except next to pixels a gain clips, where clipping after the average
  makes the preview slightly brighter. Captures keep the full-resolution path.
  *Design note:* `capture_station.hpp` promises the preview is "exactly what a capture would store";
  the live view becomes "the same up to rounding, a little brighter beside clipped pixels". Worth a
  line in ADR 0014.
- If DSHOW hands over the raw MJPG buffer (`CAP_PROP_CONVERT_RGB = false`; to be checked with
  camera_probe on an A5 — not verified), `cv::imdecode(buf, IMREAD_REDUCED_COLOR_2)` decodes straight
  to 1920×1080 using libjpeg's DCT scaling.

| Whole preview frame | CPU (1 thread) |
|---|---:|
| Today: decode 4K + white balance 4K + shrink + copy | 60 ms |
| A: decode 4K + shrink + white balance (LUT) on 1080p | 36 ms (−40 %) |
| B: raw MJPG decoded at half size + white balance on 1080p | 24 ms (−60 %) |

### 1.2 The spikes: OpenCV's thread pool wakes every core

OpenCV runs `cv::LUT`, `cv::resize`, `cv::multiply`… on all cores. At these sizes the pool costs far
more CPU than it saves wall time:

| Operation on a 4K image | 16 threads: wall / CPU | 1 thread: wall / CPU |
|---|---:|---:|
| `LevelsTreatment::apply` | 2.4 ms / **35 ms** (14.6 cores) | 9.1 ms / 9.0 ms |
| white balance via `cv::LUT` | 2.6 ms / **37 ms** (14.2 cores) | 14.5 ms / 14.5 ms |
| shrink 4K → 1080p | 1.0 ms / 12.6 ms (12 cores) | 3.4 ms / 3.4 ms |
| `analyzeVuePhases` (CALCUL) | 7.8 ms / 39 ms (4.9 cores) | 26 ms / 26 ms |
| whole preview frame today | 58 ms / 74 ms | 60 ms / 60 ms |

The preview takes the same wall time on one thread, with 20 % less CPU and no burst across every core
at each frame — the "100 %" in Task Manager. The Windows prebuilt OpenCV runs on the Concurrency
Runtime (`concrt140.dll` ships in the bundle), whose workers also spin, so the same pattern is
expected there. With `cv::setNumThreads(1)` every interactive operation above still finishes in
under 30 ms. (`cv::setNumThreads` is process-wide: set it once in `main()`.)

### 1.3 Thumbnails: 125 ms of GUI-thread decoding per Vue, after every capture

`MainWindow::refreshVues()` (`app/main_window.cpp:549`) rebuilds every list item, so
`loadNextThumbnail()` (`app/main_window.cpp:604-617`) re-reads **every** Vue of the Échantillon on the
GUI thread after each capture, filing, deletion, Vue principale flag or picker change. A 4K TIFF takes
**125 ms** to read, and `IMREAD_REDUCED_COLOR_4` saves nothing for TIFF (only JPEG decodes at a
reduced scale). Twenty Vues: 2.5 s of one core and a stuttering preview after every capture.

Fix: Vues are immutable by path (ADR 0004), so cache the `QIcon` by path; in `captured()`, add only
the new item; decode off the GUI thread (`QtConcurrent::run` + `QFutureWatcher`).

### 1.4 Replay mode decodes the file on every grab

`FileReplayCamera::grab()` (`src/file_replay_camera.cpp:33`) calls `cv::imread` on every frame. With a
4K PNG that is 100+ ms per frame: a full core in `--replay` mode. Decode each file once and return
clones.

### 1.5 GUI-thread painting and rendering (not measured: needs a Qt profiler)

- `PreviewWidget::paintEvent` (`app/preview_widget.cpp:31-32`) draws a `Format_BGR888` image, scaled
  with `SmoothPixmapTransform`, every frame. BGR888 is not one of the raster engine's fast formats;
  converting once in the worker to `Format_RGB32` (`cv::COLOR_BGR2BGRA` on 1080p, ~1 ms) should let
  Qt use its SIMD scaling path.
- `VueWindow::previewLevels` (`app/vue_window.cpp:357`) re-renders every Treatment on the 4K original
  and converts it twice (`toQImage` copy, then `QPixmap::fromImage`, ~33 MB) every 150 ms while the
  spin box moves. Render a 1080p proxy while editing.
- `PhaseOverlayTreatment::apply` is a scalar per-pixel loop (12 ms at 4K, `src/phase_overlay_treatment.cpp:14-39`);
  a 256-entry LUT per output channel would do it in a few ms. Must keep the exact output (ADR 0012).
- `mireLineNormal` runs a float Sobel on the full 4K frame (`src/mire_calibration.cpp`), once per new
  mire still: downscale 2–4× first.
- `VueWindow::handleMoved` deletes and recreates every mark and handle on each mouse move while a
  point is dragged. Fine with a few marks; moving the dragged items in place would scale.

## 2. Correctness and robustness

Verified by reading the code at the lines given. Line numbers are those of `3e8f45a`, the commit
reviewed; #67–#69 and this branch move some of them by a few lines.

### HIGH

**H1. Image I/O goes through `path::string()`, which fails for non-ANSI names on Windows.**
`src/vue.cpp:55,71,178`, `src/file_replay_camera.cpp:33`, `app/main_window.cpp:610`. VueStore builds
folders from UTF-8 correctly (`u8path`), but on MSVC `path::string()` converts to the ANSI code page
and throws `std::system_error` for characters it cannot represent, and OpenCV's `imread`/`imwrite`
open files through ANSI `fopen`. "Échantillon" survives on a French (cp1252) PC; "Fonte γ" or
"Acier α+β" — plausible names in metallography — do not: every capture, load and thumbnail of that
Échantillon fails, and the thumbnail one terminates the app (H3). The same `string()` calls in error
messages (`vue.cpp:46,56,73`, `measurement.cpp:12`) throw while building the message.
Fix: one `readImage(path)` / `writeImage(path)` pair — `std::ifstream`/`std::ofstream` on the
`path` (wide on Windows) plus `cv::imdecode`/`cv::imencode` — and `u8string()` in messages. The write
half also makes image writes atomic (M2). Add a test with a non-ASCII Échantillon. *(To confirm on a
lab PC: not reproducible on Linux.)*

**H2. The live preview dies on the first empty or dropped frame.**
`OpenCvCamera::grab` throws when `read()` returns no frame (`src/opencv_camera.cpp:51`); the
preview loop does not catch it (`app/preview_worker.cpp:93`), so it reaches the outer catch and
emits `failed`. `tools/camera_probe.cpp:179-181` already knows that "some UVC cameras return empty
frames while the stream starts up" and retries ten times — the app does not. A USB hiccup mid-TP
kills the live view the same way; the student must press F5.
Fix: skip warm-up frames after `open()`, tolerate N consecutive failures with a short pause, then
emit `failed`.

**H3. Exceptions can escape Qt slots, which ends in `std::terminate`.**
`main()` has no `try` around `app.exec()` and nothing overrides `QApplication::notify`. Uncaught paths
include `VueWindow::previewLevels` (`app/vue_window.cpp:357`), `VueWindow::edit` calling
`showSaved()` after its own catch (`app/vue_window.cpp:337`), `MainWindow::loadNextThumbnail`
(`app/main_window.cpp:610`), and `std::filesystem::exists` in `openCatalogFile`
(`app/main_window.cpp:910`). Example trigger: `Vue::load` reads with `IMREAD_UNCHANGED`
(`src/vue.cpp:71`) and the store accepts any `.tif`; a 16-bit TIFF dropped in an Échantillon folder
makes the 8-bit-only treatments throw while rendering — the app aborts with the camera open.
Fix: a small helper that runs a slot body and shows a message on exception; reject or convert
non-8-bit images in `Vue::load`.

### MEDIUM

- **M1. `Vue::addTreatment` has no rollback** (`src/vue.cpp:88-89`), unlike `insertTreatment`,
  `replaceTreatment` and `removeTreatment`, and unlike its header comment. After a failed save,
  `VueWindow::edit` warns "non enregistré" but shows and lists the unsaved Treatment, which the next
  successful save then persists.
- **M2. `Vue::capture` writes the image non-atomically** (`src/vue.cpp:55`), while the sidecar is
  atomic; an interrupted write leaves a truncated TIFF, and `exists()` then `imwrite` is a
  check-then-act. If the sidecar or Échelle write fails afterwards, the operator sees "Capture refusée"
  yet the Vue is listed without its Calibration.
- **M3. `writeFileAtomically` neither flushes to disk nor handles `rename` failures**
  (`src/safe_write.cpp:18-30`): after a power loss NTFS can leave an empty file; `rename` throws
  `std::filesystem::filesystem_error` (documented as `runtime_error`) and leaves the `.tmp`; the fixed
  `.tmp` name collides between two processes.
- **M4. Unbounded `wait()` on the GUI thread** (`app/main_window.cpp:493`, and the destructor): a camera
  stuck in `read()` or a slow 4K DSHOW `open()` freezes the window on F5 or on close.
- **M5. `LocationPicker::setCatalog` toggles every descendant widget** (`app/location_picker.cpp:82`),
  combo-box internals included — may show a stray popup container after "Recharger le catalogue"
  (unverified). Toggle an explicit list.
- **M6. The folder-collision check folds only ASCII case** (`src/catalog.cpp`, `src/vue_store.cpp`):
  NTFS folds Unicode case, so "Échantillon A" and "échantillon a" validate but share a folder.
- **M7. Long GUI-thread jobs behind a wait cursor only:** `writeFichePdf`, `exportRendered`,
  `Vue::load` of a 4K TIFF (125 ms each). The preview stalls and Windows may mark the window
  "not responding" for a big Fiche.
- **M8. `toQImage` documents 8-bit input but never checks it** (`app/qt_image.cpp:9-22`).
- **M9. Calibration validation is inconsistent:** `Calibration::fromJson` validates nothing;
  `Measurement` checks `<= 0.0`, which lets NaN through; `EchelleTreatment` uses `std::isfinite`.

### LOW

- Unchecked float→integer casts: `formatMicrons` casts to `long long` (UB for inf/huge,
  `src/format_microns.cpp:10`); Échelle bar length `int` cast; `zone.x + zone.width` can overflow from a
  hand-edited sidecar (`src/phase_overlay_treatment.cpp:74`).
- `Vue::exportRendered` can overwrite a user's `x.tmp.png` and leaves the temp file if `rename` throws.
- `tools/camera_probe.cpp`: `imwrite` result unchecked; `create_directories` outside the `try`.
- No total-path check against `MAX_PATH` (root + three 64-byte names can exceed 260 characters).
- `ImageView::wheelEvent` zoom is unbounded and ignores the wheel delta's magnitude.
- Empty catches: `app/vue_window.cpp:351`, `app/main_window.cpp` (`openVueFolder`).
- `lab_test/` (170 MB of camera frames and a bundle) is untracked but not in `.gitignore`.

## 3. Concurrency (verified by reading)

Sound. No `cv::Mat` crosses the camera thread: `frame` is local to `PreviewWorker::run`, `toQImage`
deep-copies, and the queued payloads are `QImage`/`QString`/`QSize`. `CaptureStation` lives on the
worker thread only; `CameraJobs` are captured by value; the generation counter plus the shared
`atomic<bool>` give correct stale-signal handling and back-pressure (no repaint storm); the
destructor joins the thread before `this` dies, so jobs capturing `this` are safe. Weak points are
M4 (unbounded join) and H2 (no frame-error tolerance). The worker has no frame-rate cap besides
`read()` blocking: a backend returning stale frames instantly would spin a core.

## 4. Coding standards (C++ Core Guidelines)

**Strong:** RAII throughout, `unique_ptr` for Treatments, Rule of Zero; `enum class`; no C casts, no
`reinterpret_cast`, no `using namespace` in headers, `#pragma once` everywhere; raw `new` only for
Qt parent-owned objects; every header self-contained; the core compiles clean under `-Wpedantic
-Wshadow -Wconversion -Wold-style-cast` apart from 18 deliberate `-Wsign-conversion` in the legacy
phase-analysis port; ASan/UBSan clean.

**Gaps, by pattern:**

| Guideline | Finding |
|---|---|
| F.6, E.12 `noexcept` | none anywhere; trivial accessors and move operations qualify |
| `[[nodiscard]]` | none; `computeWhiteReferenceGains`, `mireLineNormal`, `Vue::render`, `VueStore::newVuePath`… return values that must not be dropped |
| C.67 | `ICamera` and `Treatment` are polymorphic yet copyable: delete copy/move in the bases |
| E.14, E.17 | `runtime_error`, `invalid_argument`, `out_of_range` and leaked `filesystem_error` mixed; no app-level policy at slot boundaries (H3) |
| F.2, F.3 | `VueWindow::VueWindow` is ~210 lines, `MainWindow::MainWindow` ~150; `main_window.cpp` (972 lines) and `vue_window.cpp` (694) mix UI layout, tool state machines, rendering and persistence |
| C.129 / ADR 0002 | `describe()` in `vue_window.cpp` is a `dynamic_cast` ladder over Treatment types: a virtual `Treatment::describe()` would keep new Treatments self-contained |
| DRY | set-aside-then-save logic repeated in `workstation_config.cpp`, `catalog.cpp`, `vue.cpp`; magnification parsing duplicated in `main_window.cpp` and `calibration.hpp` |
| ES.45 | inline magic numbers: Échelle margins and fonts, zoom factors 1.25/0.8, ring sizes, the 1000 ms fps window |
| SL.con.1 | C arrays (`float weights[27]`, `uchar compressed[256]`) where `std::array` fits |
| SF.11 / build time | public headers pull `<nlohmann/json.hpp>` (`treatment.hpp`, `white_balance.hpp`, `calibration.hpp`, `camera_factory.hpp`); `<utility>`/`<memory>` often included transitively only |
| Con.2 | `VueStore` mutators (`setPrincipale`, `fileVue`, `deleteVue`) are `const` because the store holds only a path — legal, but misleading |

**Build and CI:** Linux builds with `-Wall -Wextra` only, MSVC with defaults (no `/W4`), no sanitizer
or coverage job, no clang-tidy. The first two are added in this change (§6); `/W4` and a
`.clang-tidy` (`bugprone-*`, `performance-*`, `modernize-*`, `cppcoreguidelines-*` minus the noisy
ones) are the natural next step.

## 5. Test coverage

Core (`src/`, `include/`), gcov line and function coverage of the 14 existing tests, before the
end-to-end test was added:

| | Lines | Functions |
|---|---:|---:|
| **Total** | **1439 / 1550 — 92.8 %** | **211 / 228 — 92.5 %** |
| `src/opencv_camera.cpp` | 16 / 70 — 22.9 % | needs a real camera |
| `src/capture_station.cpp` | 42 / 48 — 87.5 % | the `CameraConfig` constructor path (now covered by the e2e test) |
| `src/levels_treatment.cpp` | 21 / 24 — 87.5 % | the invalid-range throw |
| `src/phase_overlay_treatment.cpp` | 57 / 63 — 90.5 % | grey and BGRA input branches |
| every other core file | ≥ 95 % | |

The Qt app (`app/`, ~3 000 lines: windows, camera thread, ImageView gestures, Fiche PDF) had **no test
beyond the launch smoke test** — that is where H2, H3, M1's UI half and every §1 GUI cost live.

Test-suite notes: the home-grown `check()` does not record file:line; fixed temp folders
(`/tmp/msapp_*_test`) collide when two runs overlap; no test uses a non-ASCII path; `test_camera`
relies on device index 97 being absent.

Sanitizers: all 15 core tests (the e2e one included) pass under ASan + UBSan + LeakSanitizer with no
report in project code. Two environment traps showed up and are handled in the new CI job: GCC 12's
ASan loops on `AddressSanitizer:DEADLYSIGNAL` under kernels with 32 bits of mmap randomisation (WSL's
6.18 here — a sanitized test under `ctest` spun until `ctest` ran out of memory), fixed by
`vm.mmap_rnd_bits=28`; and the distro OpenCV leaks in libusb and libgphoto2 when it probes USB
cameras (`test_camera`), suppressed in `tests/lsan.supp`.

## 6. End-to-end pipeline (added in this change)

1. **`tests/test_e2e_lab_session.cpp`** — core e2e, no Qt, no camera, 42 checks, ~1 s. A whole
   lab session through the real files: first launch without `workstation.json` → White reference
   on a blank field → x10 calibrated on a mire, x40 derived (ADR 0020) → capture at x40 into the
   example catalog's Échantillon via `FileReplayCamera` → reopen the Vue in a "new session" (sidecar,
   Calibration, capture info, bit-exact original) → Measurement, Vickers, phase analysis, Levels and
   phase map persisted → export → Vue principale on the Fiche → file an Unsorted Vue, delete one to
   the Corbeille. Passes locally, also under ASan + UBSan.
2. **`app/tests/test_app_e2e.cpp`** — GUI e2e with Qt Test, offscreen. Drives the real `MainWindow`
   on the replay camera: waits for live frames, clicks Capturer, checks the Vue appears in the list
   and on disk with its x10 Calibration and Échelle; opens it in `VueWindow`, runs CALCUL, measures
   100 µm with two clicks on the image. Any modal message box is closed and fails the test. The app
   sources moved into a static library `microscope_app_ui` so the exe and the test link the same
   code; key widgets got object names. **Not compiled yet — no Qt on the dev machine; CI is its first
   build.**
3. **CI** (`.github/workflows/ci.yml`): Linux and Windows now run *Unit tests*
   (`ctest --label-exclude e2e`) then *End-to-end tests* (`ctest --label-regex e2e`); new
   **Coverage** job (gcovr HTML artifact + summary, the core gated at 90 % of lines) and
   **Sanitizers** job (ASan + UBSan; LeakSanitizer on the core tests with `tests/lsan.supp`, off for
   the Qt tests, whose libraries keep process-lifetime allocations).

Run locally (core only, as on the WSL machine):

```bash
cmake -S . -B build -DMSAPP_BUILD_APP=OFF && cmake --build build -j8 && ctest --test-dir build -L e2e --output-on-failure
```

## 7. Suggested order

1. Merge §6 and let CI build the GUI test; fix whatever its first run shows.
2. The cheap CPU wins: `cv::setNumThreads`, shrink-before-white-balance, no identity clone, thumbnail
   cache, replay decode cache (§1.1–1.4). Re-measure on a lab PC with the A5 (Task Manager + the fps
   label).
3. H3, H2, then H1 with a non-ASCII path test; M1–M3 together (they share the "atomic image + sidecar"
   design).
4. The raw-MJPG preview (§1.1 B) after a camera_probe experiment on the A5.
5. Standards clean-up as files are touched: `[[nodiscard]]`, C.67 in the two interfaces,
   `Treatment::describe()`, split `MainWindow`/`VueWindow`, `/W4` + clang-tidy in CI.
