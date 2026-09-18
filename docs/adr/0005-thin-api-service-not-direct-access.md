# Backend: thin API service on k3s, not direct client access to DB/storage

Workstations never touch Postgres or the object store (MinIO/S3) directly. A single stateless API service, deployed on the existing k3s cluster, owns both and is the only thing clients talk to over HTTPS. Considered giving each workstation direct DB + bucket credentials instead — less code to write since Postgres/MinIO are off-the-shelf — but that spreads credentials across every lab machine and means every future business-rule change (starting with the conflict-resolution logic from ADR 0004) requires redeploying every client instead of updating one service. The cluster already exists; running one more small service on it costs effectively nothing.

**Consequences:** The API service becomes a real dependency — if it's down, sync (not capture, per ADR 0004) stalls until it's back.
