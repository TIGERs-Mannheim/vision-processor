# Wrapper snapshot serving — design

**Date:** 2026-05-16
**Branch:** `feature/python-wrapper-tier-3-ui-skeleton`
**Status:** approved, pending implementation

## Goal

Get the C++ vision_processor's debug quad (`img/sample.<camId>.jpg`, already written by the new `SnapshotWriter`) onto the screen in the Svelte UI, at ~1 Hz, hardcoded to `cam_id = 0`.

## Decisions (the short version)

| Decision | Choice |
|---|---|
| Transport | HTTP `GET /snapshot/<cam_id>` |
| Same port as WS? | Yes — one aiohttp listener serves both |
| HTTP stack | Migrate end-to-end from `websockets` lib to `aiohttp` |
| File layout | Module-per-feature; each module exposes `register(app, ...)` |
| Refresh strategy | UI does blind 1 Hz polling with cache-busting `?t=<ms>` query |
| cam_id selection | Hardcoded `0` in the UI (multi-cam deferred) |
| Validation / auth / tests | Out of scope for this iteration |

## Architecture

One `aiohttp.web.Application` on a single port (default `8765`, unchanged from current WS-only). Routes:

| Route | Method | Handler | Purpose |
|---|---|---|---|
| `/ws` | GET (upgrades) | `websocket.ws_handler` | Existing bus-over-WS bridge, semantics unchanged |
| `/snapshot/{cam_id}` | GET | `snapshot.snapshot_handler` | Stream `img/sample.<cam_id>.jpg` |
| `/` + assets | GET (future) | `web.static(...)` | Production Svelte build, deferred |

File layout:

```
wrapper_backend/
  __main__.py     ← builds the aiohttp app, calls each module's register()
  websocket.py    ← exports register(http_app, bus); owns ws_handler internally
  snapshot.py     ← new; exports register(http_app, img_dir); owns snapshot_handler
  bus.py          ← unchanged
  geometry.py     ← unchanged
  multicast.py    ← unchanged
```

## websocket.py migration

The internal logic survives unchanged — per-client size-1 outbox, lazy per-topic channel ref-counting, JSON envelope (`{action, topic}` in, `{topic, data}` / `{error, topic}` out), `_TOPIC_ENCODERS`. Only the I/O substrate swaps.

**API:**
```python
def register(http_app: web.Application, bus: Bus) -> None:
    bridge = _TopicBridge(bus)
    http_app.router.add_get("/ws", bridge.handle)
    http_app.on_cleanup.append(bridge.shutdown)
```

`_TopicBridge` owns the dict of `_Channel`s. `bridge.handle(request)` is the per-connection coroutine.

**Substrate swap:**

| Was | Now |
|---|---|
| `from websockets.asyncio.server import ServerConnection, serve` | `from aiohttp import WSMsgType, web` |
| `ServerConnection.send(text)` | `web.WebSocketResponse.send_str(text)` |
| `async for raw in connection:` | `async for message in ws: if message.type == WSMsgType.TEXT: ...` |
| `except ConnectionClosed` | The async-for loop just exits on close |
| `class WebSocketServer` with `start()` / `close()` running its own `serve()` task | `class _TopicBridge` + module-level `register()`; lifecycle owned by the aiohttp `Application` |
| `connection.remote_address` | `request.remote` |

**What stays identical** (load-bearing — explicit so a reader doesn't refactor it away):
- `_Client._outbox = asyncio.Queue(maxsize=1)` + drain-then-put `post()`
- `_join` / `_leave` channel ref-counting
- `_forward_topic` reading from `bus.subscribe(topic)` then fan-out via `client.post(frame)`
- `_TOPIC_ENCODERS` table
- `deliver_forever` separate from the receive loop (decouples slow send from fast receive)
- Wire format — clients see the exact same JSON

**Lifecycle.** aiohttp manages the listener via `AppRunner`. When the runner shuts down, active WS sessions get closed → each `handle()` coroutine's `async for` ends → finally-block leaves all subscribed topics → `app.on_cleanup` then cancels any still-running `_forward_topic` tasks as a backstop.

**WSMessage gotcha.** aiohttp's `WSMessage` types include `TEXT`, `BINARY`, `CLOSE`, `CLOSING`, `CLOSED`, `ERROR`, `PING`, `PONG`. Only handle `TEXT` explicitly; let the iterator stop at `CLOSE` for everything else.

## snapshot.py (new)

```python
from pathlib import Path
from aiohttp import web


def register(http_app: web.Application, img_dir: Path) -> None:
    async def handler(request: web.Request) -> web.FileResponse:
        return web.FileResponse(img_dir / f"sample.{request.match_info['cam_id']}.jpg")
    http_app.router.add_get("/snapshot/{cam_id}", handler)
```

That's the whole module. Notes:

- `FileResponse` returns 404 automatically when the path is missing → no explicit existence check.
- `FileResponse` uses `sendfile()` where available → zero-copy from kernel; slow clients don't stall the event loop.
- UI cache-busts with `?t=<ms>` → no `Cache-Control` header needed.
- No cam_id validation — worst case is a 404, which is fine.
- C++ side writes via `tmp → rename` (POSIX-atomic) → GET sees either the previous or the new complete file, never a torn one.

## __main__.py rewire

```python
async def _main() -> None:
    args = _parse_args()
    bus = Bus()
    multicast = Multicast(bus, args.vision_ip, args.vision_port)
    geometry = Geometry(bus, args.geometry)

    http_app = web.Application()
    websocket.register(http_app, bus)
    snapshot.register(http_app, args.img_dir)

    http_runner = web.AppRunner(http_app)
    await http_runner.setup()
    http_site = web.TCPSite(http_runner, args.host, args.port)
    await http_site.start()
    log.info("http+ws listening on %s:%d", args.host, args.port)

    try:
        await multicast.start()
        await geometry.run()
    finally:
        await http_runner.cleanup()
        await multicast.close()
```

**CLI flag changes** (also touch `wrapper_backend/CLAUDE.md`):
- `--ws-host` → `--host`
- `--ws-port` → `--port`
- New `--img-dir` (default: `Path("img")`, interpreted relative to the wrapper's working directory — `./start_wrapper.sh` `cd`s into the repo root, matching where `vision_processor` writes)

**Dependencies (`pyproject.toml`):**
- Drop `websockets>=16.0`
- Add `aiohttp>=3.10`

## Frontend changes

`wrapper-frontend/src/lib/wrapper-bus.ts` — single URL change:

```ts
const DEFAULT_URL = `ws://${location.hostname || "localhost"}:8765/ws`;
//                                                            ^^^ new path
```

`wrapper-frontend/src/App.svelte` — add ~15 lines:

```svelte
<script lang="ts">
  import { onMount } from "svelte";
  // ...existing imports...

  const snapshotBase = `http://${location.hostname}:8765/snapshot/0`;
  let cacheBuster = $state(0);

  onMount(() => {
    const intervalId = setInterval(() => { cacheBuster = Date.now(); }, 1000);
    return () => { clearInterval(intervalId); };
  });
</script>

<section>
  <h2>Camera 0</h2>
  <img src={`${snapshotBase}?t=${cacheBuster}`} alt="camera 0 quad" />
</section>

<style>
  img { max-width: 100%; height: auto; display: block; }
</style>
```

Why hardcode the URL instead of using a Vite proxy: `<img src=...>` doesn't trigger CORS for display (only for canvas readback), so cross-origin works in dev. Matches the existing `wrapper-bus.ts` style. A Vite proxy + same-origin everywhere can come together with the prod static-serving work later.

## Error handling — explicit non-goals

- Snapshot missing → aiohttp's default 404. UI shows the browser's broken-image icon. No placeholder yet.
- WS disconnect → loop exits, channels reap via existing `_leave` calls.
- Out-of-range cam_id → 404 (no file matches).
- Malformed HTTP / oversize bodies → aiohttp's defaults.

## Testing — explicit non-goal

No tests exist for the wrapper today; we are not adding the first ones in this iteration. Smoke test against the live C++ binary, same as we did for `SnapshotWriter`:

1. Start `wrapper_backend` with a known `--img-dir` containing `sample.0.jpg`.
2. `curl -o /tmp/x.jpg http://localhost:8765/snapshot/0` → file with valid JPEG bytes.
3. With the C++ binary running and writing periodically, `npm run dev` and confirm the `<img>` refreshes.

## Migration order — three commits

1. **WS to aiohttp.** Drop `websockets`, add `aiohttp`, rewrite `websocket.py` (`register(app, bus)` shape), restructure `__main__.py` around `AppRunner`, rename CLI flags. Update `wrapper-bus.ts` URL path. Verify the existing Svelte skeleton still subscribes / receives. *No new feature — pure substrate change.*

2. **Snapshot endpoint.** Add `wrapper_backend/snapshot.py`, register it in `__main__.py`, add `--img-dir` flag. Verify `curl http://localhost:8765/snapshot/0` returns the JPEG written by the C++ binary.

3. **UI `<img>`.** Add `App.svelte` changes (1 Hz `setInterval`, cache-busting `?t=`). Verify the quad image refreshes in the browser end-to-end.

## Docs touched alongside the commits

- `wrapper_backend/CLAUDE.md` — update CLI flags section and the architecture paragraph (now mentions snapshot module + HTTP).
- `wrapper-frontend/README.md` — note the new `/snapshot/<id>` endpoint and the `<img>` polling.
