# Fiche is a generated artifact, not a direct-to-printer action; 3-Vue cap dropped

Print is reframed as: generate a Fiche (a document combining Échantillon name + Vue principale) as its own file first, then print that — not fire the raw photo straight at a printer like the legacy app did. This means a digital copy always exists even if the paper printout is lost, damaged, or never happens (printer down, no ink) — consistent with this session's whole "never lose output to infrastructure" theme, and reuses the same flatten-on-demand pattern already decided in ADR 0003. Separately, the legacy 3-Vue-per-Échantillon cap is dropped (an artificial 90s storage limit that actively fights the archivable goal); the "one Vue is the report photo" concept survives as the Vue principale flag.

**Consequences:** Exact Fiche contents beyond name + photo (Grossissement, Échelle, TP/Department, date?) still open — next question.
