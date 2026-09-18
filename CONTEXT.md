# Microscope Capture App

Replacement for the legacy Matrox-based metallography imaging tool. Captures photos of metal samples from a Moticam and applies the levels/scale treatments teachers use to document them.

## Language

### Imaging

**Grossissement**:
The physical objective in use for a given photo (e.g. 4x/10x/40x/100x). Selected by the operator, not measured by the app.
_Avoid_: Magnification level, zoom

**Calibration**:
The pixels-per-µm ratio tied to one Grossissement. Set once per objective (against a mire reticle), reused for every future photo taken at that Grossissement.
_Avoid_: Resolution (legacy TIFF-tag term from the old app, ambiguous with image resolution)

**Échelle**:
The visual scale-bar overlay rendered onto a photo, built from its Calibration. A Treatment — it mutates the saved photo.
_Avoid_: Resolution

**Measurement**:
An interactive tool: operator selects two points on a photo, app converts the pixel distance to a real-world length using the photo's Calibration. Read-only — does not itself mutate the photo.
_Avoid_: Échelle (that's the overlay; this is the point-to-point reading)

### Session organisation

**Department**:
Top-level grouping a TP belongs to (e.g. GMP, Chemistry) — mirrors a class or subject area. Legacy app exposed this as a login-like "utilisateur" field; unclear yet whether real per-user auth is needed or this is just a picker.
_Avoid_: Utilisateur, Classe

**TP** (Travaux Pratique):
A practical-work exercise within a Department (e.g. "ACIERS"). Defines the set of Échantillons expected to be photographed during that session.
_Avoid_: Session (ambiguous with login session), Exercise

**Échantillon**:
A named sample slot within a TP (e.g. "Ac1: XC10"), pre-defined by the teacher before any photo exists.
_Avoid_: Sample

**Vue**:
One saved photo (+ its Treatments) attached to an Échantillon. Legacy app capped this at 3 per Échantillon with one flagged for printing — open question whether that cap still applies (not yet resolved).
_Avoid_: Photo (a Vue is the photo in its TP/Échantillon context, not just an image file)

**Unsorted**:
A fallback bucket for Vues captured before a workstation has ever synced real Department/TP/Échantillon structure (first launch, offline). Deliberately breaks the normal Vue→Échantillon invariant — filed properly once the machine syncs.
_Avoid_: none — this is intentionally the escape hatch, not a normal state

**Fiche**:
A generated one-page document combining an Échantillon's name, its Vue principale, Grossissement, and the Échelle scale bar, produced on demand for printing/export. Not stored as source of truth itself — Vue + metadata remain canonical, Fiche is a rendered output (mirrors ADR 0003's flatten-on-export pattern).
_Avoid_: Report (less specific), Print (that's the action, not the artifact)

**Vue principale**:
The single Vue per Échantillon flagged to appear on that Échantillon's Fiche. Replaces the legacy "one of 3 views gets printed" behavior — the 3-Vue cap is dropped, this flag concept survives it.
_Avoid_: Primary Vue (English mix)
