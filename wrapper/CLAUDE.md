# CLAUDE.md — wrapper

Async Python application replacing `python/geom_publisher.py`. Owned by uv; managed via `pyproject.toml` at repo root.

## Commands

All run from repo root.

- Run: `./start_wrapper.sh [geometry-X.yml]` (or `uv run python -m wrapper geometry.yml`)
- Type check: `uv run mypy wrapper/`
- Lint: `uv run ruff check wrapper/`
- Format: `uv run ruff format wrapper/`
- Pre-commit (manual run on wrapper files): `uv run pre-commit run --files wrapper/*.py`

## Architecture

Four modules glued by a watch-channel pub/sub bus:

- `bus.py` — every `subscribe(topic)` returns its own size-1 `asyncio.Queue`. `publish` drains-then-puts; slow subscribers see only the latest value.
- `multicast.py` — `asyncio.DatagramProtocol` UDP. Inbound parses `SSL_WrapperPacket` and demuxes into `geometry.in` / `detection.in`. Outbound subscribes to `wrapper_packet.out` (bytes) and `sendto`s.
- `geometry.py` — owns `geometry.yml` + in-memory `SSL_WrapperPacket`. Two tasks: `_absorb_loop` (replace-or-append calibs) and `_publish_loop` (1 Hz emit). Both share `self._wrapper`; no locks because asyncio doesn't preempt between non-`await` statements. Publisher serialises to bytes before publishing so the snapshot is locked-in before the multicast adapter awaits.
- `websocket.py` — `websockets`-based JSON bridge. Lazy per-topic bus subscription: a channel's bus-reader task starts when the first client joins and stops when the last leaves. Each connected client owns its own size-1 outbound queue (slow clients drop intermediate frames). Read-only for now; envelope (`{"topic": ..., "data": ...}` and `{"action": ..., "topic": ...}`) is symmetric so inbound commands can be added later without breaking the wire format. Topic-to-JSON encoders live in `_TOPIC_ENCODERS`.

Topics: `geometry.in`, `detection.in` (inbound demuxed), `wrapper_packet.out` (outbound bytes).

## Gotchas

- `ParseDict` runs strict (no `ignore_unknown_fields`). A typo in `geometry.yml` raises at startup. Don't add forgiveness.
- `optional_field_lines:` controls the SSL markings that may be absent on lab/exhibition carpets: `goal2goal` (CenterLine), `halfway` (HalfwayLine), `centercircle` (CenterCircle arc), `penalty` (the six penalty-area stretches). Touchlines and goal lines are always emitted. The block and all four keys are required — `load_geometry` pops the block before `ParseDict` so strict parse still rejects typos elsewhere, and missing keys raise `KeyError` rather than silently defaulting.
- Two `# type: ignore[assignment]` on `SSL_FieldShapeType.Value(...)` calls are unavoidable: `types-protobuf` types `Value()` as `int` while proto enum fields are typed as the enum.
- Generated proto bindings are NOT committed. `wrapper/__init__.py` runs `protoc` on first import if `wrapper/proto/ssl_vision_wrapper_pb2.py` is missing, then prepends `wrapper/` to `sys.path` so `from proto.* import ...` resolves to `wrapper/proto/`. mypy uses `mypy_path = "wrapper"` and excludes `wrapper/proto/` to mirror this.
- `python/` scripts (`geom_publisher.py`, `cam_viewer.py`, benchmarks) are NOT covered by the wrapper's tooling. They keep running on system Python; never modify them as part of wrapper work unless explicitly asked.
- Pre-commit hooks are scoped to `^wrapper/` for ruff. Don't widen the scope without reason — would reformat all the legacy `python/` files.

## CLI flags

Dash form only: `--vision-ip`, `--vision-port`, `--ws-host`, `--ws-port`. `geom_publisher.py`'s underscore form is dropped.

## Out-of-scope (planned but not yet built)

Web UI, `vision_processor` subprocess supervisor, calib persistence to `geometry.yml`. Each will be an additive module subscribing to the bus — don't restructure existing modules to anticipate them.
