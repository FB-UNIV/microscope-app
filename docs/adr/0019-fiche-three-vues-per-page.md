# A Fiche prints three Vues per A4 page, one per row

**Status:** supersedes the "as few A4 pages as fit" layout of [ADR 0016](0016-fiche-one-sheet-per-tp.md); what a Fiche shows (name, Vue principale with its Treatments, Grossissement, Échelle) still stands.

ADR 0016 packed a TP's Vues principales two per row, as many rows as fit: eight 16:9 photos on one A4 sheet, each about 90 × 51 mm. At the first lab session (2026-10-16) the teacher found them too small to read on paper, and asked for two or three photos per page across the page width. Decided: three per A4 portrait page, one per row, each photo centred and as large as its third of the page allows, which is about 133 × 75 mm for the A5's 16:9 frames. Every TP gets the same number per page whatever the photos' shape, so a sheet looks the same from one TP to the next. Considered two per page at the full page width (186 × 105 mm): the biggest print, but a TP with 8 Échantillons would take 4 pages instead of 3, and the teacher chose three. The legacy app also put up to three views on a page (ADR 0016).

**Consequences:** `FichePage` gains `rows` (per page, default 3) and `columns` defaults to 1; `layoutFiche()` no longer fits as many rows as possible. A photo wider than its row (a panorama) is limited by the page width instead, and gets less height. A TP with 8 Échantillons now prints on 3 pages instead of 1.
