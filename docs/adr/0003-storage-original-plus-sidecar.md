# Storage: plain original photo + treatment sidecar, not baked-in or proprietary bundle

Each captured photo is saved as a plain, standard image file (TIFF/PNG), untouched — openable by any tool, no app lock-in. Applied treatments (levels, Échelle overlay, params) live in a same-name JSON sidecar file, not baked into the original. The app re-renders treatments on load from the sidecar; "export" flattens a treated copy on demand. Considered baking treatments directly into the saved file (closest to the legacy app's behavior) and a custom proprietary bundle format, but school users need to grab raw photos for slides/reports without the app in the loop, and non-destructive editing (ADR 0002) requires the original survive untouched regardless.

**Consequences:** Losing the sidecar loses the treatment history but never the photo itself. "Export" and "save" are different actions.
