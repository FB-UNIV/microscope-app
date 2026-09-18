# First-launch-offline fallback: capture into an Unsorted bucket, never refuse to run

A workstation with zero cached config (first launch, never yet reached the network) still opens and allows capture, filing photos into a generic Unsorted bucket instead of refusing to run. This deliberately breaks the otherwise-clean invariant that every Vue belongs to an Échantillon within a TP within a Department. Considered refusing to run until first sync (keeps the invariant clean) but rejected — the entire session's design goal has been "never lose a capture to infrastructure," and this is the one edge case where that principle and a clean data model conflict. Principle wins.

**Consequences:** Unsorted Vues need manual filing into a real Échantillon once the machine syncs. The domain model has an explicit escape hatch, not just an implicit gap.
