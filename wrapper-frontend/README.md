# wrapper-frontend

Browser UI for the vision-processor wrapper. Svelte 5 + TypeScript + Vite.

Currently a skeleton: connects to `wrapper_backend`'s WebSocket on
`ws://<host>:8765`, subscribes to `wrapper_packet.out`, and dumps each
frame as JSON. Proves the end-to-end pipeline before any visualization
work begins.

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

Open <http://localhost:5173>. Click "Subscribe to wrapper_packet.out"
to start receiving frames.

## Architecture

- `src/lib/wrapper-bus.ts` — single `WebSocket` client. Exposes
  `connectionState` (Svelte store) and `topic<T>(name)` (returns a
  store of the latest message). Subscribes to a topic lazily on first
  reader, unsubscribes when the last reader goes away. Reconnects on
  close with exponential backoff (1s → 30s).
- `src/App.svelte` — placeholder UI: connection badge + subscribe
  toggle + JSON dump.
- `src/main.ts` — mounts `App` into `#app`.

The wire format mirrors `wrapper_backend/websocket.py`'s envelope:

```jsonc
// client -> server
{ "action": "subscribe",   "topic": "wrapper_packet.out" }
{ "action": "unsubscribe", "topic": "wrapper_packet.out" }
// server -> client
{ "topic": "wrapper_packet.out", "data": { ... } }
{ "error": "unknown topic", "topic": "..." }
```

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
