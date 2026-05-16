"""HTTP endpoint that streams the C++ vision_processor's debug quad.

The C++ side writes ``<img_dir>/sample.<cam_id>.jpg`` periodically (gated
by the ``debug_stream_interval_ms`` config). aiohttp's ``FileResponse``
returns 404 automatically when the file is missing, so no explicit
existence check is required.
"""

from __future__ import annotations

from pathlib import Path

from aiohttp import web


def register(http_app: web.Application, img_dir: Path) -> None:
    async def handler(request: web.Request) -> web.FileResponse:
        return web.FileResponse(img_dir / f"sample.{request.match_info['cam_id']}.jpg")

    http_app.router.add_get("/snapshot/{cam_id}", handler)
