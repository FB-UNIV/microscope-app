# Treatments modeled as a generic pipeline, not fixed functions

Each image adjustment (levels, échelle overlay, and anything added later) is modeled as a `Treatment`: an ordered, non-destructive step applied on top of an always-preserved original photo, rather than as separate hardcoded functions called directly by the capture/save/UI code. Considered hardcoding the known treatments directly for simplicity, but a teaching tool's treatment list is expected to grow over its lifetime — a pipeline keeps "add treatment #4" a one-file change instead of surgery across the app.

**Consequences:** Every treatment (including the two we know about today) must be written against the `Treatment` interface from the start, even though only levels and échelle exist right now.
