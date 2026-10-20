# Architecture Decision Records

Each ADR records one decision and why it was made. An ADR is not rewritten
when a later one changes it: its **Status** line names the ADR that amends or
supersedes it, and the later ADR links back. Domain terms are in
[`CONTEXT.md`](../../CONTEXT.md).

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-target-platform-and-stack.md) | Windows 11+, with a stack that keeps Linux possible | |
| [0002](0002-treatment-pipeline-architecture.md) | Treatments are a generic pipeline, not fixed functions | |
| [0003](0003-storage-original-plus-sidecar.md) | A Vue is the plain original photo plus a JSON sidecar of Treatments | |
| [0004](0004-local-first-capture-async-sync.md) | Capture writes locally first; sync to the backend never blocks it | |
| [0005](0005-thin-api-service-not-direct-access.md) | The backend is a thin API service on k3s, not direct database access | Phase 2 |
| [0006](0006-per-install-token-auth.md) | API auth by a token per install, not per-user sign-in | Phase 2 |
| [0007](0007-config-cached-with-offline-fallback.md) | Department/TP/Échantillon config is cached locally, with an offline fallback | Phase 2 |
| [0008](0008-unsorted-bucket-offline-first-launch.md) | Before any TP exists, capture into Unsorted rather than refuse | |
| [0009](0009-phased-delivery.md) | Phase 1 is local only; the backend comes in Phase 2 | |
| [0010](0010-fiche-generated-artifact-cap-dropped.md) | A Fiche is a generated file, not a direct print; the 3-Vue cap is dropped | layout superseded by 0016 |
| [0011](0011-motic-capture-via-sdk-not-uvc.md) | Motic USB cameras need the Motic SDK | superseded in part by 0013 |
| [0012](0012-legacy-measurements-ported-bit-exact.md) | Phase analysis and Vickers hardness are ported bit-exact from the legacy app | amended by 0021 |
| [0013](0013-moticam-a5-is-uvc.md) | The Moticam A5 is UVC: capture through OpenCV | |
| [0014](0014-locked-colour-temperature-plus-white-reference.md) | Lock the colour temperature and bake a per-microscope White reference into captures | |
| [0015](0015-calibration-tied-to-frame-size.md) | A Calibration is tied to its frame size: a mismatch is refused, never rescaled | amended by 0018, 0020 |
| [0016](0016-fiche-one-sheet-per-tp.md) | A Fiche is one sheet per TP: each Vue principale with its Grossissement and Échelle | layout superseded by 0019; amended by 0022 |
| [0017](0017-echelle-on-every-calibrated-vue.md) | Every calibrated Vue gets an Échelle; no per-TP setting | |
| [0018](0018-no-legacy-migration.md) | Nothing is migrated from the legacy app | |
| [0019](0019-fiche-three-vues-per-page.md) | A Fiche prints three Vues per A4 page, one per row | |
| [0020](0020-calibrate-one-objective-derive-the-others.md) | Calibrate one objective on the mire; the others are derived from it | |
| [0021](0021-phase-analysis-at-legacy-scale.md) | Phase analysis runs at the legacy camera's scale; a missing Seuil is reported | |
| [0022](0022-fiche-prints-the-original-with-its-echelle.md) | A Fiche prints each Vue principale's original photo with only its Échelle | |
| [0023](0023-metallo-neutral-dark-theme.md) | The app is Métallo, in neutral greys with one copper accent | |
