# Microscope Capture App

Replacement for the legacy Matrox-based metallography imaging tool. Captures photos of metal samples from a Moticam, applies the levels/scale treatments teachers use to document them, and runs the two lab measurements the legacy app provided: Phase analysis and Dureté Vickers.

## Language

### Imaging

**Grossissement**:
The physical objective in use for a given photo (e.g. 4x/10x/40x/100x). Selected by the operator, not measured by the app.
_Avoid_: Magnification level, zoom

**Calibration**:
The pixels-per-µm ratio tied to one Grossissement and to the frame size it was measured at. Set once per objective (against a mire reticle), reused for every future photo taken at that Grossissement and frame size. Applying it to an image of another size is refused, never rescaled (ADR 0015): if the camera falls back to another mode, restore the mode or recalibrate. Each Vue keeps the Calibration it was captured with in its sidecar; recalibrating changes only future Vues. The legacy app instead kept one sensor-plane constant ("Résolution vidéo", 288.65 px/mm for its Basler) and multiplied it by the objective; per-objective Calibration is more accurate. The legacy mire has a 20 µm pitch; recalibrate until two readings agree within 0.25 %.
_Avoid_: Resolution (legacy TIFF-tag term from the old app, ambiguous with image resolution)

**Échelle**:
The visual scale-bar overlay rendered onto a photo, built from its Calibration. A Treatment: drawn onto the rendered photo, never into the stored original (ADR 0003). Added automatically to every Vue captured with a Calibration; there is no per-TP setting (ADR 0017).
_Avoid_: Resolution

**Measurement**:
An interactive tool: operator selects two points on a photo, app converts the pixel distance to a real-world length using the photo's Calibration. Read-only — does not itself mutate the photo.
_Avoid_: Échelle (that's the overlay; this is the point-to-point reading)

**Phase analysis**:
Area percentage of the dark vs light phase on a Vue (legacy "Pourcentage des constituants" / CALCUL button) — e.g. pearlite vs ferrite in a steel, from which students estimate carbon content. Computed by the legacy algorithm, ported bit-exact (ADR 0012): an automatic Seuil, then pixels within ± the tolerance band count half to each phase, and that band's share is reported as the error.
_Avoid_: Segmentation, Classification (no per-grain labelling happens), Denombrement (legacy function name)

**Seuil**:
The grey level separating the two phases in a Phase analysis, plus its tolerance band. Found automatically from the grey levels of edge pixels; shown on the Vue as a two-colour phase overlay (a Treatment).
_Avoid_: Threshold in UI text (French UI), Binarisation

**Dureté Vickers**:
Hardness from the two diagonals of a pyramid indent, measured on a Vue with the Measurement tool: HV = 1.85409 × load / mean diagonal² (legacy coefficient, ADR 0012). The legacy app fixed the load at 0.5 kgf (HV0.5).
_Avoid_: Hardness alone (several scales exist), Brinell/Rockwell (different tests)

**White reference**:
R/G/B gains measured once per microscope from a blank field (lamp on, no specimen) that make it neutral, baked into every capture taken on that microscope (ADR 0014). Corrects the green/magenta tint a camera's colour-temperature control can't reach. The legacy app kept the same thing as per-workstation gains.
_Avoid_: White balance (the camera's own colour-temperature control, locked separately), Calibration (reserved for px/µm)

**Camera profile**:
A workstation's camera settings — capture API, frame size, pixel format, exposure, colour temperature, White reference — re-applied every time the camera opens, because the Moticam A5 keeps whatever the last program set.
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
A fallback bucket for Vues captured before a workstation has ever synced real Department/TP/Échantillon structure (first launch, offline). Deliberately breaks the normal Vue→Échantillon invariant — filed properly once the machine syncs.
_Avoid_: none — this is intentionally the escape hatch, not a normal state

**Corbeille**:
Where a deleted Vue goes, with its Treatments: a `Corbeille` folder in the Vue folder, under the same Department/TP/Échantillon folders it was filed in. Nothing is erased (ADR 0004), so a Vue deleted by mistake is recovered by moving its files back; the app lists nothing from it. A deleted Vue principale is unflagged.
_Avoid_: Trash, Bin (English UI words), Supprimées

**Fiche**:
A generated document for one TP: for each of its Échantillons, the name, the Vue principale with its Treatments, its Grossissement and the Échelle scale bar, laid out on as few A4 pages as fit (ADR 0016). Produced on demand for printing/export. Not stored as source of truth itself — Vue + metadata remain canonical, Fiche is a rendered output (mirrors ADR 0003's flatten-on-export pattern).
_Avoid_: Report (less specific), Print (that's the action, not the artifact)

**Vue principale**:
The single Vue per Échantillon flagged to appear on its TP's Fiche. Replaces the legacy "one of 3 views gets printed" behavior — the 3-Vue cap is dropped, this flag concept survives it.
_Avoid_: Primary Vue (English mix)
