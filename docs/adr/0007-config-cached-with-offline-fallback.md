# Config (Department/TP/Échantillon) is cached locally with offline fallback

The student capture app fetches current Department/TP/Échantillon config from the API on startup, but always caches the last successful response locally. If the API is unreachable at startup, the app starts anyway using the cached copy rather than failing to launch. This is the same resilience principle as ADR 0004 (never let a network hiccup block the person in the lab room) applied to the read path instead of the write path — directly replacing the legacy behavior (pull fresh from a network share on every start, no fallback) that was named as a real pain point.

**Consequences:** A workstation can run on stale config if it hasn't synced since a teacher last edited a TP. Needs a lightweight "config as of [time]" indicator so students/teachers aren't misled by an unseen edit — defaulting to this unless you'd rather it stay silent.
