"""CLI entrypoint for the wrapper.

Run with:

    uv run python -m wrapper geometry.yml
"""

from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path

from wrapper.bus import Bus
from wrapper.geometry import Geometry
from wrapper.multicast import Multicast


async def _main() -> None:
    parser = argparse.ArgumentParser(prog="wrapper")
    parser.add_argument("geometry", type=Path, help="geometry.yml path")
    parser.add_argument("--vision-ip", default="224.5.23.2")
    parser.add_argument("--vision-port", type=int, default=10006)
    args = parser.parse_args()

    bus = Bus()
    multicast = Multicast(bus, args.vision_ip, args.vision_port)
    geometry = Geometry(bus, args.geometry)

    await multicast.start()
    try:
        await geometry.run()
    finally:
        await multicast.close()


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s",
    )
    asyncio.run(_main())


if __name__ == "__main__":
    main()
