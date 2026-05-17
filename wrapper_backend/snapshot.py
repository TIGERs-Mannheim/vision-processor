"""HTTP endpoints for the debug images the C++ vision_processor writes to disk.

Filenames follow ``<cam_id>.<view>.{jpg,png}``. ``view`` may contain dots
(e.g. ``pixels.corner``). Non-image files and anything that doesn't match
the scheme (legacy ``rtsp:__...`` dumps, ``.calib.json`` sidecars) are
ignored.
"""

from __future__ import annotations

import re
from pathlib import Path

from aiohttp import web

_FILENAME_RE = re.compile(r"^(?P<cam_id>\d+)\.(?P<view>.+)\.(?P<ext>jpg|jpeg|png)$")


def register(http_app: web.Application, img_dir: Path) -> None:
    async def list_handler(_: web.Request) -> web.Response:
        entries: list[dict[str, str]] = []
        if img_dir.is_dir():
            for path in img_dir.iterdir():
                if not path.is_file():
                    continue
                match = _FILENAME_RE.match(path.name)
                if match is None:
                    continue
                entries.append({"cam_id": match["cam_id"], "view": match["view"]})
        entries.sort(key=lambda e: (int(e["cam_id"]), e["view"]))
        return web.json_response(entries)

    async def file_handler(request: web.Request) -> web.FileResponse:
        cam_id = request.match_info["cam_id"]
        view = request.match_info["view"]
        matches = list(img_dir.glob(f"{cam_id}.{view}.*"))
        if not matches:
            raise web.HTTPNotFound
        return web.FileResponse(max(matches, key=lambda p: p.stat().st_mtime))

    http_app.router.add_get("/snapshots", list_handler)
    http_app.router.add_get("/snapshot/{cam_id}/{view}", file_handler)
