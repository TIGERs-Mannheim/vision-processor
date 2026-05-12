"""Tail a wrapper websocket channel. Usage:

uv run python tools/ws_tail.py [--url ws://127.0.0.1:8765] [--topic wrapper_packet.out]
"""

from __future__ import annotations

import argparse
import asyncio
import json

from websockets.asyncio.client import connect


async def _main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="ws://127.0.0.1:8765")
    parser.add_argument("--topic", default="wrapper_packet.out")
    args = parser.parse_args()

    async with connect(args.url) as ws:
        await ws.send(json.dumps({"action": "subscribe", "topic": args.topic}))
        async for raw in ws:
            frame = json.loads(raw)
            print(json.dumps(frame, indent=2))


if __name__ == "__main__":
    asyncio.run(_main())
