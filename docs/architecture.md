# Architecture

```mermaid
flowchart TD
  Browser[Browser user] --> Flask[Flask web interface]
  Browser -->|Direct API testing| API[FastAPI]
  Codex[Codex / MCP client] --> MCP[MCP server]
  Script[REST client / script] --> API
  API --> Jobs[(Jobs + decisions\nSQLite / Postgres)]
  API -->|JSON job spec| Engine[Headless JSON adapter]
  Engine --> Core[C++ blur-boundary algorithm]
  Desktop[Original VideoTrimmer desktop workflow] --> Core
  Engine -->|JSON analysis| API
  API -->|validated C++ cuts| Jobs
  API -->|JSON render spec| Engine
  Engine --> Export[MP4 export]
  Flask -->|REST| API
  MCP -->|REST| API
  S3[S3 media storage] -. production .-> API
```

## Boundaries

- **C++ core (`src/core/Clip.cpp`):** the original sequential blur detector and main-shot selection.
  The headless engine and original desktop workflow both use this class.
- **C++ engine:** JSON access to that selection and rendering. It never knows
  about HTTP, SQL, LLMs, or fixed filesystem locations.
- **FastAPI:** upload validation, job lifecycle, SQL persistence, and the API
  contract shared with Flask and MCP. It validates, but never replaces, the
  C++-selected cut.
- **Flask:** a deliberately small local/demo dashboard that calls FastAPI.
- **MCP:** a thin client-facing adapter; it invokes REST rather than duplicating
  business logic.
