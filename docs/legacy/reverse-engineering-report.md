# MetalNew-wx3 (05-03-2020) — Reverse-Engineering Report

Target: `MetalNew-wx3 05-03-2020.exe`
SHA-256: `79a8002337d3f6ba27d451bdb6a1bb0cf5eea07616e4edce3e9b5542648fba60`
Method: static analysis only (Ghidra 12.1.3 via REA, dumpbin, Sysinternals strings). The program was **not executed**.

Legend: **[O]** = observed in binary · **[I]** = inferred · **[?]** = unknown / not resolved

---

## 1. What the application is

A **metallography teaching / lab tool** ("Metal") used by the University of Le Mans ecosystem
(GMP, Fac Chimie, ISMANS, Licences CND/MCI, Mesures Physiques, CTTM transfer centre) to:

1. Grab live microscope images from a frame grabber / camera (Matrox MIL).
2. Let students pick a **TP** (practical work) → **Échantillon** (sample) → **Vue** (view).
3. Measure **phase percentages** ("Pourcentage des constituants") by automatic thresholding.
4. Measure **Vickers hardness** (HV) from two indentation diagonals.
5. Save views (TIFF/JPEG) to a shared network folder, archive them, and **print** up to 3 views/page.

## 2. Build / technology facts [O]

| Item | Value |
|---|---|
| Arch | PE32, x86, Windows GUI subsystem |
| Linked | 2020-03-05 14:55:03 (PE timestamp) |
| Toolchain | Visual Studio 2012 (linker 11.00), **Debug build** (`MSVCR110D.dll`, `MSVCP110D.dll`) |
| PDB path | `C:\Christian\metalPC3 - wx2 - wx3\Debug-wx3\MetalNew-WX3.pdb` (PDB not available) |
| GUI | **wxWidgets 3.x**, statically linked, MDI frame; legacy `.wxr` dialog resources embedded as strings |
| Imaging HW | **Matrox Imaging Library** `MIL.dll`, 33 functions imported **by ordinal only** |
| Other DLLs | KERNEL32, USER32, GDI32, COMDLG32, SHELL32, COMCTL32, ole32, OLEAUT32, RPCRT4, ADVAPI32 |
| Size | 6.7 MB, ~38 300 functions (vast majority = wxWidgets/CRT/libjpeg/libtiff/libpng) |
| Resources | French (LCID 1036); wx std icons/cursors; icon `SASHTEST_ICN` (built from wx sash sample) |
| Source files named in asserts | `mainMetal.cpp`, `metal.cpp`, `directPrint.cpp`, `imageMac.cpp`, `thread.h` |

Application C++ classes (RTTI) [O]: `MyApp`, `MyFrame` (MDI parent), `MyChild`, `MyCanvas`,
`FenetreImage`, `MySashLayoutWindow`, `MetalDialog`, `MetalPrintout`, `DirectPrint`, `MyThreadVideo`.

App code lives roughly in `0x401000–0x480000`; everything above is library code.

## 3. Runtime flow [O/I]

```
MyApp::OnInit
 ├─ LoadConfig()            reads binary file "ConfigurationMetal"           (0x42b240)
 ├─ network share probe      fopen(<workdir>\<test>, "wt")                    (0x450230)
 │    └─ fail → dialog "REESSAYER" / "LOCAL"  → fallback folder "LocalMetalData"
 │         ("Vérifiez que le serveur est en marche (PC LEICA)")
 ├─ user/profile selection   dialog "selection_Uilisateurs"                   (0x4261d0)
 ├─ load TP catalog          DATA_METAL.txt  + <PROFILE>.txt override          (0x450a90)
 ├─ AcquisitionVideo init    MIL system/digitizer/display alloc               (0x405680)
 └─ MDI frame: tableau_de_bord
      SAISIE VIDEO | FICHIERS VUES | Impression | CALCUL | Aide
```

### 3.1 User profiles (code typed by user) [O] — `0x4261d0`
Matched on characters 2..6/7 of the typed code:

| Code | Profile id | Folder/label |
|---|---|---|
| `gmp11` | 0 | GMP1 |
| `gmp1r` | 1 | GMP1 |
| `gmp2m` | 2 | GMP2 |
| `chimie` | 3 | FAC_CHIMIE |
| `isman` | 4 | ISMANS |
| `mphys` | 5 | MES_PHYS |
| `lpcnd` | 6 | LPCND (Licence CND) |
| `lpmci` | 7 | LPMCI (Licence MCI) |
| `cttm` | 11 | CTTM (centre de transfert) |
| `cricri` | 12 | PERSO ("Utilisateur Christian" — developer mode, not in the on-screen list) |
| `autre` | 13 | AUTRE |

These are **profile selectors, not authentication**. Selected id → global `DAT_00a1c700`, label → `DAT_00a1c708`.

### 3.2 Configuration file `ConfigurationMetal` [O] — `0x42b240`
Binary, little-endian, versioned by first `short`:
- `< 0x2743` legacy; `0x2743`, `0x2744`, `0x2745` successive formats.
- Contains: video standard/board shorts, 4–5 pairs of shorts, two floats (one used as
  `scale = K / (value/10)`), a **10-entry calibration table of 3 floats** (per objective /
  magnification — `[I]`), extra floats in v0x2745, and a **1024-byte working-directory path**
  (network share root).
- `SaveConfig()` writes the same structure.

### 3.3 TP catalog `DATA_METAL.txt` + `<PROFILE>.txt` [O] — `0x450a90` (verified against real `GMP1.txt`, `CTTM.txt`)
`DATA_METAL.txt`: header `%8x%hd` (version **2**, N comment lines), then **20 profiles × 7 TPs × 8 samples**.
`<PROFILE>.txt` (e.g. `GMP1.txt`): header `       4 8` = version **4**, 8 comment lines to skip, then
**7 TPs**, each = separator line, TP name, student instruction text, **print code** (short), **flags** (short, seen `16`),
comment line, **8 sample names** (blank = unused slot). Overrides the selected profile's block.
Print codes (from file comments): `-1` one print, `-2` unlimited, `-3` two views once, `-4` three views once,
`-5-N` several views N times, `0` controlled per sample. Flag `0x10` tested in `0x4521e0` — likely "show Échelle" `[I]`.

### 3.3b Real `ConfigurationMetal` decoded (1190 bytes, v`0x2745`) [O]
| Field | Value | Meaning (from `Calibration de la caméra.rtf`) |
|---|---|---|
| f@0x40 | 288.65 | **Résolution vidéo, px/mm at 1× objective** (sample px/mm = 288.65 × G) |
| f@0x44 | 338.54 | **screen width in mm** (display-scale calibration) |
| 10×3 floats | 4 valid rows ≈ (1.745, 1.0, 2.576) | likely R/G/B white-balance gains `[I]` |
| s@0xCE | 4 | number of valid rows / current index `[I]` |
| f@0xC4, f@0xC8 | 0.36, 1.6 | `[?]` (camera exposure/gain?) |
| path | `Z:\MetalData` | network share root |
Unused rows / 5th pair contain `0xCDCD…` = uninitialised Debug-heap memory written to disk (legacy bug).
Calibration procedure: reticle (mire) pitch 20 µm, `Rcs = Ri·Lm/Lr`, tolerance 0.25 %.

### 3.3c Real view TIFFs decoded [O]
2448×2048 RGB 8-bit, uncompressed (Basler acA2440-35uc). Private tag `0xF000` (84 bytes) = mini tag list:
`0x200` = **objective G** (20 / 50), `0x207` = **px/mm on sample** (5773.0 / 14432.5 = 288.65 × G),
`0x203`/`0x204` = 806.30 `[?]`, `0x205` = 2, `0x201` = 10, `0x7D00` = end. → legacy archives carry their own calibration.
`etat_vue_tp` (392 bytes) = 7 TPs × 8 samples × 7 view flags (non-zero = view saved) — shared state on `Z:\MetalData`.
Known TP names (from dialog `select_TP`): *TP1 ACIER, TP2 FONTES, TP3 ALLIAGES CUIVREUX, TP4 Essais m…*.
Sample buttons in `choixEchantVu`: *Ac1 XC10, Ac2 XC38, Ac3 XC80, Ac4 XC150, P2 XC38, INCONNU*.

### 3.4 Files written [O]
| Pattern | Purpose |
|---|---|
| `TP%dEch%dVue%d.tif` / `CTTM%dEch%dVue%d.tif` | working view images (custom TIFF reader/writer `lireImageTiff`/`ecritImageTiff`) |
| `%s\Archives\TP%dE%dV%d_G%4hd_%s_%s.jpg` | archived JPEG: TP, sample, view, magnification×10, sanitised sample name, date |
| `%s\Archives\Etat_TP_DU_%s.met`, `Etat_CTTM_DU_%s.met` | daily session state / print-queue status |
| `log.txt` | log |

## 4. Image acquisition (Matrox MIL) [O/I] — `0x405680`

Tries hardware in order, keeps the first that works:

| Mode | Hardware | Details |
|---|---|---|
| 1 | **Matrox Meteor** (analog) | format from config: `M_PAL[_RGB/_YC]`, `M_NTSC[...]`, `M_SECAM[...]`, `M_CCIR` |
| 3 | **Matrox Camera Link** board + **DALSA 2M30** | DCF `DCF/mcl2m30a_n2s8Mod.dcf` (8-bit) or `C:/Program Files/Matrox Imaging/gnl/dcf/Dalstar/mcl2m30(a)_n2s.dcf` (16-bit) |
| 4 | **MIL default system** (USB3/GenICam) | Basler **acA2440-35uc** or **acA2500-60uc** detected by model string; many feature controls (white balance "Balance des Blancs", exposure) |
| -1 | none | "no camera" mode, default 1042×780, file-only work |

MIL constants seen: `M_SIZE_X (0x600)`, `M_SIZE_Y (0x601)`, `M_SIZE_BAND (0x3ed)`.
Ordinal guesses `[I]`: 1766 ≈ `MsysAlloc`, 1660 ≈ `MdigAlloc`, 1655 ≈ `MdispAlloc`, 141 ≈ `MdigControl`.
Exact names require the matching `MIL.dll` export table `[?]`.
Continuous grab runs in a worker thread (`MyThreadVideo`, `AcquisitionVideo::GrabContinuousThread2m30`, `ArretThread2m30`).

## 5. Measurement algorithms (recovered exactly) [O]

Internal image struct (0x20 bytes) `[O/I]`:
`+4 short width, +6 short height, +8 u8 bytesPerPixel(=1), +9 u8 type (0=grey, 0x40=RGB planar), +0xC/+0x10/+0x14 plane pointers`.

### 5.1 `gere_calcul` — phase percentage pipeline (`0x4663f0`)
1. If RGB: convert to grey, auto-threshold, then a 5×5 disk-shaped morphological filter
   sequence (4 passes, `0x471f60` / `0x4721b0`, likely erode/dilate `[I]`).
2. If grey: auto-threshold directly.
3. Call `denombre` to compute percentages.

### 5.2 Auto-threshold `denombre_metal_ima_fast` (`0x4131b0`) — edge-weighted histogram
1. **`module_histo_grad_x_y_niv`** (`0x414100`): for each interior pixel
   `gx = I(x-1,y) − I(x+1,y)`, `gy = I(x,y-1) − I(x,y+1)`,
   `g = (gx² + gy²) >> 9` (0..255). Build 1-D histogram `Hg[g]` and 2-D histogram
   `H2[g][mean4]` where `mean4` = mean of the 4 neighbours.
2. **`0x4133f0`**: `gmax` = highest gradient bin whose count > 0.5 % of the histogram peak (min 2).
3. **`0x4143c0`**: `gmin = gmax·33/100` (clamped ≤ 254); grey-level histogram of edge pixels weighted by
   gradient: `H[v] = Σ_{g=gmin}^{gtop-1} (g − gmin)·H2[g][v]`, where `gtop` = **last non-empty gradient bin**
   (not `gmax`) — corrected after assembly review. All float→int conversions in this pipeline
   **truncate** (x87 control word `|0xC00`); Ghidra's `ROUND` is misleading. Morphology helpers resolved:
   `0x472400` = grey erosion, `0x473420` = grey dilation, 5×5 disk (square minus corners), applied
   erode→dilate→dilate→erode (open then close), 2-px border replicated from the interior.
   Ported in `microscope-app/src/phase_analysis.cpp`; cross-checked against `tools/legacy_phase_reference.py`.
4. **`0x4139d0`**: smooth `H` with Gaussian kernel `exp(−k²/21.125)`, k ∈ [−13, 13];
   find peak P; **threshold** = midpoint of first/last bins above `0.7072·P` (≈ P/√2);
   **half-width** = half the span of bins above `P/2`; caller divides by 3 → **tolerance band**.

### 5.3 `denombre` — phase fractions (`0x466cd0`)
With threshold `S`, tolerance `T`, N pixels:
- `nHigh` = pixels `> S+T`; `nBand` = pixels in `[S−T, S+T]`
- **% light phase** = `(nHigh + nBand/2)·100/N`
- **% dark phase** = `(N − nHigh − nBand/2)·100/N`
- **error %** = `nBand·100/(2N)`  → shown as `Erreur: %2.2f%%`

### 5.4 `creeImageSeuil` — threshold overlay (`0x466860`)
LUT `v → v·0.90196 + 25`. Pixel `< S` → `(L, L/2, L)`; pixel `≥ S` → `(L, L, 0)` on the
3 output planes (two-colour phase map). RGB input uses `(R+G+B)/3`.

### 5.5 Vickers hardness (`0x44aad0`)
Two diagonals clicked (pixels) → `d = ((d1+d2)/2) / (calib · magnification) · 1000` (µm),
`HV = 0.927045 / d² · 10⁶` ⇒ standard `HV = 1.8544·F/d²` with **F hard-coded to 0.5 kgf (HV0.5)**.

## 6. Printing [O strings / I behaviour]
`MetalPrintout` / `DirectPrint`: up to **3 views per page**; physical print only when 3 views are
queued ("La sortie papier aura lieu lorsque 3 impressions seront demandées"); per-sample/per-view
print limits enforced; queue persisted in `.met` state files.

## 7. Modernization notes

**Must preserve (business logic, fully recovered above):** auto-threshold (§5.2), phase counting (§5.3),
overlay colours (§5.4), HV formula + 0.5 kgf load (§5.5), file naming (§3.4), profile codes (§3.1),
config/catalog formats (§3.2–3.3) for data migration.

**Replace / drop:**
- Debug CRT build shipped to production → Release build or new stack.
- MIL ordinal coupling + 3 legacy grabber paths → single camera abstraction (e.g. Basler pylon / GenICam / OpenCV).
  Meteor analog + DALSA Camera Link are likely obsolete — confirm with lab.
- Hand-rolled TIFF/JPEG I/O → standard libraries.
- `.wxr` legacy dialogs, MDI → modern UI.
- Network-share-as-database (`fopen` write probe, `.met` files) → proper storage/service.
- Plain profile codes incl. hidden `cricri` dev profile → real auth if needed.

## 8. Open questions [?]
- Exact MIL ordinal → function names (need the shipped `MIL.dll`).
- Real contents of `ConfigurationMetal`, `DATA_METAL.txt`, profile `.txt` files (runtime data, not in the exe) — get samples from the lab PC.
- Meaning of calibration table entries and `DAT_00a1c418` scale factor (pixels/µm per objective).
- Exact semantics of the 5×5 morphology helpers `0x471f60` / `0x4721b0`.
- Print layout details (`DirectPrint`) not decompiled.
- Network share path is stored in config at runtime, not hard-coded.

## 9. Key addresses
| Address | Role |
|---|---|
| `0x405680` | AcquisitionVideo init (MIL) |
| `0x4131b0` | denombre_metal_ima_fast (auto threshold) |
| `0x414100` | module_histo_grad_x_y_niv |
| `0x4133f0` / `0x4143c0` / `0x4139d0` | threshold helpers |
| `0x4261d0` | profile code selection |
| `0x42b240` | LoadConfig |
| `0x44aad0` | Vickers HV display/compute |
| `0x450230` | network share probe / LOCAL fallback |
| `0x450a90` | load DATA_METAL.txt + profile file |
| `0x4521e0` | save view / JPEG archive naming |
| `0x4663f0` | gere_calcul |
| `0x466860` | creeImageSeuil |
| `0x466cd0` | denombre |

Evidence IDs (REA ledger): overview `ev_c21cc8a6…`, resources `ev_84d511fc…`, algorithms `ev_f9155da0…`, `ev_dd630a5c…`, `ev_172983b0…`,
acquisition `ev_3cbd5eed…`, startup/catalog `ev_388c4310…`, profiles/archive `ev_c85c960f…`, hardness `ev_14743692…`.
