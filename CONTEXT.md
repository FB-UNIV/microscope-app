# Microscope Capture App

Replacement for the legacy Matrox-based metallography imaging tool. Captures photos of metal samples from a Moticam, applies the levels/scale treatments teachers use to document them, and runs the two lab measurements the legacy app provided: Phase analysis and Dureté Vickers.

## Language

### Imaging

**Grossissement**:
The physical objective in use for a given photo, named by its magnification (e.g. x5, x10, x20, x50). Selected by the operator, not measured by the app.
_Avoid_: Magnification level, zoom

**Calibration**:
The pixels-per-µm ratio tied to one Grossissement and to the frame size it was measured at. Measuring one objective against the mire is enough: every other Grossissement gets a Calibration *derived* from it by the ratio of magnifications (x10 = 2 × x5), as the legacy app did with its single sensor-plane constant ("Résolution vidéo", 288.65 px/mm for its Basler, multiplied by the objective) (ADR 0020). An objective measured on the mire uses its own Calibration, which is more accurate; the others derive from the one measured most recently. Applying a Calibration to an image of another size is refused, never rescaled (ADR 0015): if the camera falls back to another mode, restore the mode or recalibrate. Each Vue keeps the Calibration it was captured with in its sidecar, derived or not; recalibrating changes only future Vues. The lab's mire has a 20 µm period (the gap between two neighbouring lines; another mire's period is set in the calibration dialog); only the distance across its lines counts, so the two clicks need not be level. Recalibrate until two readings agree within 0.25 %.
_Avoid_: Resolution (legacy TIFF-tag term from the old app, ambiguous with image resolution)

**Échelle**:
The visual scale-bar overlay rendered onto a photo, built from its Calibration. A Treatment: drawn onto the rendered photo, never into the stored original (ADR 0003). Added automatically to every Vue captured with a Calibration; there is no per-TP setting (ADR 0017).
_Avoid_: Resolution

**Measurement**:
An interactive tool: operator selects two points on a photo, app converts the pixel distance to a real-world length using the photo's Calibration. Read-only — does not itself mutate the photo.
_Avoid_: Échelle (that's the overlay; this is the point-to-point reading)

**Phase analysis**:
Area percentage of the dark vs light phase on a Vue (legacy "Pourcentage des constituants" / CALCUL button) — e.g. pearlite vs ferrite in a steel, from which students estimate carbon content. Computed by the legacy algorithm, ported bit-exact (ADR 0012): an automatic Seuil, then pixels within ± the tolerance band count half to each phase, and that band's share is reported as the error. Runs on the whole Vue or on a Zone. A photo wider than the legacy camera's 1042 px (e.g. the A5's 4K frames) is analysed on a copy scaled down to that width, because the legacy edge detector finds no edges on soft 4K frames; the phase map is still drawn on the full photo (ADR 0021).
_Avoid_: Segmentation, Classification (no per-grain labelling happens), Denombrement (legacy function name)

**Seuil**:
The grey level separating the two phases in a Phase analysis, plus its tolerance band. Found automatically from the grey levels of edge pixels; shown on the Vue as a two-colour phase overlay (a Treatment). When the image has no edges sharp enough (blurred, flat or empty), no Seuil is found: the app says *Seuil introuvable* and gives no percentage, where the legacy app silently fell back to 127 ± 42 (ADR 0021).
_Avoid_: Threshold in UI text (French UI), Binarisation

**Zone**:
The rectangle of a Vue a Phase analysis is restricted to, drawn by the operator to leave dust, inclusions or scratches out. Any size of at least 5×5 pixels at the legacy scale (19×19 on a 4K frame, the size of the legacy smoothing disk) is accepted; whether a zone needs a minimum size, and in what unit, is the teacher's call (#64). Saved with the phase map Treatment in the sidecar, so reopening the Vue analyses the same zone; outlined in cyan on the map. No zone means the whole Vue.
_Avoid_: Région, ROI, Sélection

**Dureté Vickers**:
Hardness from the two diagonals of a pyramid indent, measured on a Vue with the Measurement tool: HV = 1.85409 × load / mean diagonal² (legacy coefficient, ADR 0012). The legacy app fixed the load at 0.5 kgf (HV0.5); the app defaults to it and lets the operator pick another load (which loads the TP uses: #64).
_Avoid_: Hardness alone (several scales exist), Brinell/Rockwell (different tests)

**White reference**:
R/G/B gains measured once per microscope from a blank field (lamp on, no specimen) that make it neutral, baked into every capture taken on that microscope (ADR 0014). Corrects the green/magenta tint a camera's colour-temperature control can't reach. The legacy app kept the same thing as per-workstation gains.
_Avoid_: White balance (the camera's own colour-temperature control, locked separately), Calibration (reserved for px/µm)

**Camera profile**:
A workstation's camera settings — capture API, frame size, pixel format, exposure, colour temperature, White reference — re-applied every time the camera opens, because the Moticam A5 keeps whatever the last program set. For the A5: DirectShow, 3840×2160 MJPG, exposure fixed at 7.8 ms, 5000 K; brightness is set with the lamp knob (`docs/hardware/camera-probe.md`).
_Avoid_: Camera settings dialog, Driver settings

### Session organisation

**Department**:
Top-level grouping a TP belongs to (e.g. GMP, Chemistry) — mirrors a class or subject area. Legacy app exposed this as a login-like "utilisateur" field; unclear yet whether real per-user auth is needed or this is just a picker. Recovered from the legacy binary, it was only a picker: typed codes `gmp11`/`gmp1r` → GMP1, `gmp2m` → GMP2, `chimie` → FAC_CHIMIE, `isman` → ISMANS, `mphys` → MES_PHYS, `lpcnd` → LPCND, `lpmci` → LPMCI, `cttm` → CTTM, `autre` → AUTRE (plus a hidden developer code). Each Department's TPs lived in a `<DEPARTMENT>.txt` catalog: 7 TP slots × 8 Échantillon slots. Not migrated: Departments, TPs and Échantillons are entered fresh in the new app (ADR 0018).
_Avoid_: Utilisateur, Classe

**TP** (Travaux Pratique):
A practical-work exercise within a Department (e.g. "ACIERS"). Defines the set of Échantillons expected to be photographed during that session.
_Avoid_: Session (ambiguous with login session), Exercise

**Échantillon**:
A named sample slot within a TP (e.g. "Ac1: XC10"), pre-defined by the teacher before any photo exists.
_Avoid_: Sample

**Vue**:
One saved photo (+ its Treatments) attached to an Échantillon. Legacy app capped this at 3 per Échantillon with one flagged for printing; the cap is dropped (ADR 0010), and the flag survives as the Vue principale.
_Avoid_: Photo (a Vue is the photo in its TP/Échantillon context, not just an image file)

**Unsorted**:
A fallback bucket for Vues captured before a workstation has ever synced real Department/TP/Échantillon structure (first launch, offline). Deliberately breaks the normal Vue→Échantillon invariant — filed properly once the machine syncs. In Phase 1, Vues go there while no `catalog.json` exists, and are filed by hand once it does (ADR 0008).
_Avoid_: none — this is intentionally the escape hatch, not a normal state

**Corbeille**:
Where a deleted Vue goes, with its Treatments: a `Corbeille` folder in the Vue folder, under the same Department/TP/Échantillon folders it was filed in. Nothing is erased (ADR 0004), so a Vue deleted by mistake is recovered by moving its files back; the app lists nothing from it. A deleted Vue principale is unflagged.
_Avoid_: Trash, Bin (English UI words), Supprimées

**Fiche**:
A generated document for one TP: for each of its Échantillons, the name, the Vue principale with its Treatments, its Grossissement and the Échelle scale bar (ADR 0016), three per A4 page, one per row (ADR 0019). Produced on demand for printing/export. Not stored as source of truth itself — Vue + metadata remain canonical, Fiche is a rendered output (mirrors ADR 0003's flatten-on-export pattern).
_Avoid_: Report (less specific), Print (that's the action, not the artifact)

**Vue principale**:
The single Vue per Échantillon flagged to appear on its TP's Fiche. Replaces the legacy "one of 3 views gets printed" behavior — the 3-Vue cap is dropped, this flag concept survives it.
_Avoid_: Primary Vue (English mix)
