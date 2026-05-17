"""CLI entrypoint for the wrapper.

Run with:

    uv run python -m wrapper geometry.yml
"""

from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

from aiohttp import web

from wrapper_backend import snapshot, websocket
from wrapper_backend.bus import Bus
from wrapper_backend.geometry import Geometry
from wrapper_backend.multicast import Multicast

log = logging.getLogger("wrapper_backend")


async def _main() -> None:
    parser = argparse.ArgumentParser(prog="wrapper")
    parser.add_argument("geometry", type=Path, help="geometry.yml path")
    parser.add_argument("--vision-ip", default="224.5.23.2")
    parser.add_argument("--vision-port", type=int, default=10006)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    bus = Bus()
    multicast = Multicast(bus, args.vision_ip, args.vision_port)
    geometry = Geometry(bus, args.geometry)

    http_app = web.Application()
    websocket.register(http_app, bus)
    snapshot.register(http_app, Path("img"))

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


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )
    asyncio.run(_main())


if __name__ == "__main__":
    main()
