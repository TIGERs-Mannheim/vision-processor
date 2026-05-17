# wrapper-frontend

Browser UI for the vision-processor wrapper. Svelte 5 + TypeScript + Vite.

Currently a skeleton: connects to `wrapper_backend`'s WebSocket at
`ws://<host>:8765/ws`, asks the backend (via `GET /snapshots`) which
debug images currently exist on disk, and renders an `<img>` for each
one, refreshed once per second. A separate dev panel lets you subscribe
to `wrapper_packet.out` and see the raw JSON.

## Run

Two terminals from the repo root:

```
# terminal 1 (the wrapper backend, with its WebSocket on :8765)
./start_wrapper.sh

# terminal 2 (the Vite dev server, on :5173 with HMR)
cd wrapper-frontend
npm install
npm run dev
```

Open <http://localhost:5173>. Whatever debug images the C++ side has
written to `img/` will appear in the grid; click "Subscribe to
wrapper_packet.out" to also see the raw JSON frames.

## Architecture

- `src/lib/wrapper-bus.ts` — single `WebSocket` client. Exposes
  `connectionState` (Svelte store) and `topic<T>(name)` (returns a
  store of the latest message). Subscribes to a topic lazily on first
  reader, unsubscribes when the last reader goes away. Reconnects on
  close with exponential backoff (1s → 30s).
- `src/App.svelte` — placeholder UI: connection badge + a grid of
  `<img>` tags, one per entry returned by `GET /snapshots`. The list is
  refreshed every 5 s; each `<img>` is refreshed once per second via a
  cache-busting `?t=<ms>` query. Plus a dev panel with a subscribe
  toggle + JSON dump for `wrapper_packet.out`.
- `src/main.ts` — mounts `App` into `#app`.

The WS wire format mirrors `wrapper_backend/websocket.py`'s envelope:

```jsonc
// client -> server
{ "action": "subscribe",   "topic": "wrapper_packet.out" }
{ "action": "unsubscribe", "topic": "wrapper_packet.out" }
// server -> client
{ "topic": "wrapper_packet.out", "data": { ... } }
{ "error": "unknown topic", "topic": "..." }
```

Snapshot endpoints are plain HTTP: `GET /snapshots` returns the list of
available `{cam_id, view}` entries as JSON; `GET /snapshot/<cam_id>/<view>`
returns the actual `image/jpeg` or `image/png` (or 404 if missing).
`<img>` tags don't trigger CORS for display, so cross-origin `:5173` ->
`:8765` works in dev without a Vite proxy.

## Scripts

```
npm run dev           # Vite dev server with HMR
npm run build         # production build to dist/
npm run preview       # serve the production build locally
npm run check         # svelte-check + tsc (type-check everything)
npm run lint          # eslint over src/
npm run format        # prettier --write .
npm run format:check  # prettier --check . (CI-style)
```

`npm run check` is the rough analogue of `mypy` on the Python side.
`npm run lint` + `npm run format` together cover what `ruff` does for
Python. TypeScript is configured strict (`strict`,
`noUncheckedIndexedAccess`, `noImplicitOverride`,
`noPropertyAccessFromIndexSignature`, `noImplicitReturns`,
`noFallthroughCasesInSwitch`).

## Production serving

Currently not wired. `npm run build` produces `dist/`; the wrapper
backend does not yet serve it. For now run `npm run dev` (or
`npm run preview`) on a separate port.
