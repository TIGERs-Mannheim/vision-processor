# wrapper_backend

Python application that owns the field geometry and (eventually) the browser-based calibration UI for `vision_processor`. Replaces `python/geom_publisher.py` with a modular asyncio base that other modules — web server, WebSocket bridge, `vision_processor` supervisor — can plug into without touching existing code.

## Run

```
./start_wrapper.sh                          # uses geometry-divB.yml
./start_wrapper.sh geometry-divA.yml        # different config
./start_wrapper.sh geometry-divB.yml --vision-port 10100
./start_wrapper.sh geometry-divB.yml --port 9000
```

The wrapper loads the given `geometry-*.yml`, broadcasts an `SSL_WrapperPacket` at 1 Hz on the multicast bus (default `224.5.23.2:10006`), and absorbs incoming per-camera calibrations into its in-memory state. Logs go to stderr.

## Architecture

```
                  ┌─────────┐
                  │  Bus    │   in-process pub/sub
                  └─────────┘
                     ▲ ▼
        ┌────────────┴─┴──────────────┐
        │                              │
        ▼                              ▼
   multicast.py  ── geometry.in ──► geometry.py
   (UDP I/O)    ── detection.in ─►   (owns geometry.yml +
                                      in-memory wrapper)
                ◄ wrapper_packet ──
                       .out             ▲
                                        │
                              websocket.py   snapshot.py
                              (GET /ws)      (GET /snapshot/<id>)
                                        │           │
                                        └─── aiohttp Application :8765
```

- `bus.py` — watch-channel pub/sub. Each subscriber gets its own size-1 `asyncio.Queue`. On publish, every subscriber's queue is drained-then-filled, so slow subscribers see only the latest message and never block publishers.
- `multicast.py` — async-native UDP via `asyncio.DatagramProtocol`. Inbound parses `SSL_WrapperPacket` and demuxes into typed topics. Outbound subscribes to `wrapper_packet.out` and sends bytes.
- `geometry.py` — owns the in-memory `SSL_WrapperPacket`. Two concurrent tasks: one absorbs incoming calibrations into it (replace-or-append, never remove); the other emits a serialised snapshot at 1 Hz.
- `websocket.py` — aiohttp WS at `/ws`. Lazy per-topic bus subscription; per-client size-1 outbox; JSON envelope. See `wrapper-frontend/src/lib/wrapper-bus.ts` for the client side.
- `snapshot.py` — aiohttp `GET /snapshot/{cam_id}/{view}`. Globs `img/{cam_id}.{view}.*` and streams the most-recently-modified match (written by the C++ `SnapshotWriter`). Returns 404 when nothing matches. `img/` is hardcoded on both sides — the C++ side writes there too, relative to its cwd.
- `__main__.py` — CLI, logging, builds the aiohttp Application and wires every module onto it and the bus.

Topics:

| Topic | Payload |
|---|---|
| `geometry.in` | `SSL_GeometryData` (demuxed inbound) |
| `detection.in` | `SSL_DetectionFrame` (demuxed inbound) |
| `wrapper_packet.out` | serialised `SSL_WrapperPacket` bytes |

For the design rationale (why watch-channel semantics, why strict YAML parsing) see `docs/superpowers/specs/2026-05-06-python-wrapper-mvp-design.md`.

## Development

Project is managed by [uv](https://astral.sh/uv). On a fresh clone:

```
uv sync                       # install deps + dev deps
uv run pre-commit install     # enable git hooks
```

Day-to-day:

```
uv run mypy wrapper_backend/
uv run ruff check wrapper_backend/
uv run ruff format wrapper_backend/
```

The pre-commit hook runs ruff (`--fix` + format) scoped to `wrapper_backend/` and mypy on every commit. Existing `python/` scripts are not subject to the new tooling and keep running against the system Python.

Type stubs for `protobuf` and `pyyaml` are dev deps. The two `# type: ignore[assignment]` on `SSL_FieldShapeType.Value(...)` calls in `geometry.py` work around an upstream `types-protobuf` stub mismatch (`Value()` is typed as returning `int` while proto enum fields are typed as the enum).

## Behavioural deltas vs `python/geom_publisher.py`

- **Strict YAML parsing.** `ParseDict` runs without `ignore_unknown_fields`, so a typo in `geometry.yml` raises at startup instead of being silently dropped.
- **`default_lines:` renamed to `optional_field_lines:`** with all four toggles required. See `wrapper_backend/CLAUDE.md` for details.
- **Dash-form CLI flags only** (`--vision-ip`, `--vision-port`, `--host`, `--port`). The underscore form is dropped.
