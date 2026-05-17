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

The wrapper is a single asyncio process listening on `:8765`. Inside,
modules don't call each other directly; they talk through an in-process
pub/sub bus. From the outside, this is what it does:

- Listens for SSL vision multicast traffic on UDP and parses it.
- Folds incoming per-camera calibrations into one merged geometry state.
- Re-broadcasts that merged state back onto the multicast group at 1 Hz.
- Exposes the same internal topics to the browser frontend over WebSocket.
- Serves the debug images that the C++ side writes to disk.

### Files

Each file is one module. In rough "outside-in" order:

- **`multicast.py`** — the UDP I/O layer. Receives `SSL_WrapperPacket`s
  from the multicast group, splits them into geometry / detection topics,
  and sends our own merged packets back out.
- **`geometry.py`** — the brains. Holds one in-memory `SSL_WrapperPacket`,
  replaces or appends per-camera calibrations as they arrive, and emits
  the current state once a second.
- **`websocket.py`** — the WebSocket endpoint at `/ws`. Lets browser
  clients subscribe to any bus topic and receive frames as JSON. Client
  side lives in `wrapper-frontend/src/lib/wrapper-bus.ts`.
- **`snapshot.py`** — the debug-image endpoints. `GET /snapshots`
  returns the list of `{cam_id, view}` entries currently on disk;
  `GET /snapshot/<cam_id>/<view>` serves the JPEG/PNG itself. Files are
  written by the C++ `SnapshotWriter` into `img/`, which is hardcoded on
  both sides (relative to each process's cwd).
- **`bus.py`** — the pub/sub bus everything else talks through. Each
  subscriber gets its own size-1 queue, so slow readers see only the
  latest message and never block publishers.
- **`__main__.py`** — entry point. Parses CLI args, sets up logging, and
  wires all the modules above onto one aiohttp app.

### Topics on the bus

| Topic | Payload | Written by | Read by |
|---|---|---|---|
| `geometry.in` | `SSL_GeometryData` | `multicast.py` (inbound) | `geometry.py` |
| `detection.in` | `SSL_DetectionFrame` | `multicast.py` (inbound) | (none yet) |
| `wrapper_packet.out` | serialised `SSL_WrapperPacket` bytes | `geometry.py` | `multicast.py` (outbound), `websocket.py` |

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
