# Legacy Metal → microscope-app: gap analysis for the Moticam rewrite

Inputs: [reverse-engineering report](reverse-engineering-report.md), real legacy data files (config, Department
catalogs, view TIFFs — not in this repo except `tests/fixtures/legacy/`), this app's Phase 1 core, and the
MotiConnect 1.0.1.9 installer (Motic's end-user software, source of the driver INF quoted below).

## 1. Biggest risk: Moticam capture is not UVC

> **Update:** the purchased cameras are **Moticam A5, confirmed UVC** — this risk is retired for them; capture uses
> the OpenCV backend (ADR 0013). The analysis below still holds for other, WinUSB-only Motic models.

`MoticUSBCamera.inf` (MotiConnect 1.0.1.9, driver 3.0.0.1, 2015) binds every Motic USB camera to **WinUSB**
with private interface GUID `{9E234069-2033-4498-8318-79CC4BC58E66}`:

| VID:PID | Name in INF |
|---|---|
| 0634:1003/1023/1010/3111/3222/3022, 232F:0100/3521/4001–4005, 04B4:1807 | Motic USB2 Camera |
| 04B4:00F1, 232F:0101 | Motic USB3 Camera |
| 4255:0008 | Moticam580 |
| 232F:1000 | Motic HDMI Camera |
| 232F:3001/3002 | Motic Vision Camera |
| 0634:1004 | old version |

WinUSB devices are **not** visible to DirectShow / Media Foundation, so `cv::VideoCapture` will not open them.
README step 2 ("VideoCapture first, Motic SDK as fallback") should be inverted: **Motic SDK is the primary path**
for these models. Only if your exact Moticam model enumerates as a UVC "Camera" device (check Device Manager →
Cameras vs. "Image"/libusb) does OpenCV work directly.

Needed: exact model + VID:PID, and the Motic developer SDK (headers + DLLs + licence). MotiConnect is the end-user app, not the SDK.

ADR 0001's capture abstraction is the right call — put the SDK behind `ICamera` (open / start live / grab frame /
set exposure, gain, white balance / resolution list), with an OpenCV backend for UVC/test and a file-replay backend for CI.

## 2. Feature parity table

| Legacy feature | Evidence | New app status | Action |
|---|---|---|---|
| Live capture | MIL, 3 HW paths | `ICamera` + OpenCV backend | Moticam A5 is UVC → OpenCV backend (ADR 0013); confirm modes with `camera_probe` |
| White balance / exposure | config rows (1.745, 1.0, 2.576), "Balance des Blancs" | — | expose via `ICamera`; persist per workstation |
| Levels | "expansion a·x + b" | `LevelsTreatment` (black/white point) | equivalent: `a = 255/(w−b0)`, `b = −a·b0` |
| Échelle overlay | per-TP flag `0x10` | `EchelleTreatment` | done; add per-TP "show échelle" setting? |
| Length measurement | cursor tool | `Measurement` | done |
| Calibration | 1 constant 288.65 px/mm × G | per-objective `pixelsPerMicron` | new model better; build reticle wizard (20 µm mire, 0.25 % check) |
| **Phase % ("Pourcentage des constituants")** | `gere_calcul`, `denombre`, auto-threshold | **absent from CONTEXT.md** | decide: keep? (TP1 Aciers C10/C40/C75/C150 = pearlite fraction → %C) |
| **Vickers HV0.5** | `0x44aad0`, `HV=0.927045/d²·10⁶` | **absent** | decide: keep? (Mesures Physiques) |
| Threshold overlay image | `creeImageSeuil` | — | becomes a `Treatment` if phase % kept |
| Dept/TP/Échantillon | `DATA_METAL.txt`, `<PROFILE>.txt` | concepts in CONTEXT.md | importer for v2/v4 text formats |
| Profiles by code | 11 codes incl. hidden `cricri` | ADR 0006 (install token, no per-user) | Department picker; drop codes |
| Fixed-name views + shared state | `TP%dEch%dVue%d.tif`, `etat_vue_tp` on `Z:\MetalData` | ADR 0004 immutable unique files | **confirms ADR 0004 root cause** |
| Print queue, 3 views/page, print codes | `.met`, codes −1…−5-N | ADR 0010 Fiche, cap dropped | print codes become obsolete |
| Legacy archive | TIFF + tag `0xF000` (G, px/mm) | — | importer → Vue + sidecar, `pixelsPerMicron = tag0x207/1000` |

## 3. Exact algorithms to port if phase % / HV are kept

All recovered from the binary (addresses in RE report §5), ready to reimplement in OpenCV with golden tests against legacy TIFFs:

1. Gradient histogram: `g = ((I[x-1]-I[x+1])² + (I[y-1]-I[y+1])²) >> 9`, 2-D histo `H2[g][mean4]`.
2. `gmax` = top gradient bin > 0.5 % of peak; `gmin = 0.33·gmax`; `H[v] = Σ (g−gmin)·H2[g][v]`.
3. Gaussian smooth `exp(−k²/21.125)`, k∈[−13,13]; threshold = centre of span > 0.7072·peak; tolerance = (span > peak/2)/2/3.
4. Counts: light = `(nHigh + nBand/2)/N`, dark = `(N − nHigh − nBand/2)/N`, error = `nBand/(2N)`.
5. Colour input → grey `(R+G+B)/3`, threshold on that grey, then 5×5 disk erode→dilate→dilate→erode (open+close) before counting.

**Status:** all of §3 is now implemented in `microscope-app` (`phase_analysis`, `phase_overlay_treatment`, `vickers`), with legacy
importers (`legacy_catalog`, `legacy_view`) and the `ICamera` layer (file + OpenCV backends; Motic backend waits on the SDK).
101 test checks pass in WSL Debian (g++ / OpenCV 4.6), incl. exact parity on 5 legacy CTTM TIFFs.
6. HV0.5: `d_µm = ((d1+d2)/2) / pixelsPerMicron`; `HV = 0.927045·10⁶ / d_µm²`.

## 4. Migration assets already in hand
- `GMP1*.txt`, `GMP2.txt`, `CTTM.txt`, `LPCND.txt`, `LPMCI.txt`, `MES_PHYS.txt` → initial Department/TP/Échantillon seed.
- 8 `CTTM1Ech*Vue*.tif` (x20 and x50) → test fixtures for importer + algorithm golden tests.
- `Calibration de la caméra.rtf` + `.xls` → calibration wizard spec.

## 5. Open decisions
1. ~~Exact Moticam model(s) and whether the Motic SDK is in hand.~~ Resolved: Moticam A5, UVC — no SDK needed (ADR 0013).
2. Keep phase % and Vickers HV? (neither is in CONTEXT.md today)
3. Import legacy archives, or start fresh?
4. Per-TP "show échelle" flag — keep?
