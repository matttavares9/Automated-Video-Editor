# Starter backlog

Create these as GitHub Issues and add them to a GitHub Project called **FastAPI Blur Trimmer**.

## Sprint 1 — reliable editing core

1. **[P0] Add integration fixture video to CI** — generate a tiny legal test
   video during CI and assert the engine returns JSON cuts.
2. **[P0] Move job execution off the request thread** — introduce a worker so
   long analyses do not hold an HTTP request open.
3. **[P1] Preserve audio during export** — use FFmpeg once the cut list has
   passed validation.

## Sprint 2 — assisted editing

4. **[P1] Add scene-change and speech signals** — use more than sharpness when
   building candidate cuts.
5. **[P1] Add MCP integration test** — run the MCP server against a test API.
6. **[P2] Add media object storage** — use an S3-compatible service before
   accepting production uploads on Render.
