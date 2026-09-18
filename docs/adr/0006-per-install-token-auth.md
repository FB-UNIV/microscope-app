# API auth: per-install shared token, not per-user SSO

The API service authenticates the calling workstation install (a token baked into each app deployment), not individual people. No SSO/LDAP integration, no per-student or per-teacher login. Nothing in the current workflow requires knowing *which person* captured a Vue — only which Department/TP it belongs to — so full identity management would solve a problem that hasn't been stated. A token is the minimum boundary against a wide-open API on the school network.

**Consequences:** No per-user audit trail exists. If accountability for individual captures is ever needed, this will need revisiting — adding real auth later is additive (a login flow on top of the same API), not a rebuild.
