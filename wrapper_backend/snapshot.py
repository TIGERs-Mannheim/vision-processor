"""HTTP endpoint that serves the C++ vision_processor's debug views.

The C++ side writes ``<img_dir>/<cam_id>.<view>.{jpg,png}`` (gated by the
``debug_stream_interval_ms`` config). When several files match (e.g. the
calibration ``pixels`` family), the most recently modified one wins.
"""

from __future__ import annotations

from pathlib import Path

from aiohttp import web


def register(http_app: web.Application, img_dir: Path) -> None:
    async def handler(request: web.Request) -> web.FileResponse:
        cam_id = request.match_info["cam_id"]
        view = request.match_info["view"]
        matches = list(img_dir.glob(f"{cam_id}.{view}.*"))
        if not matches:
            raise web.HTTPNotFound
        return web.FileResponse(max(matches, key=lambda p: p.stat().st_mtime))

    http_app.router.add_get("/snapshot/{cam_id}/{view}", handler)
