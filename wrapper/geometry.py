"""Owns geometry.yml + the in-memory SSL_WrapperPacket.

Two concurrent tasks:
- _absorb_loop: subscribe to geometry.in, replace-or-append per-camera
  calibrations into the SSL_WrapperPacket.
- _publish_loop: every second, the SSL_WrapperPacket on wrapper_packet.out.
"""

from __future__ import annotations

import asyncio
import logging
import math
from pathlib import Path

import yaml
from google.protobuf.json_format import ParseDict

from proto.ssl_vision_geometry_pb2 import (
    SSL_FieldShapeType,
    SSL_GeometryData,
)
from proto.ssl_vision_wrapper_pb2 import (
    SSL_SOURCE_VISION_PROCESSOR,
    SSL_WrapperPacket,
)
from wrapper.bus import Bus

log = logging.getLogger("wrapper.geometry")

PUBLISH_INTERVAL_S = 1.0


def _generate_field_markings(wrapper: SSL_WrapperPacket, field: dict) -> None:
    lines = wrapper.geometry.field.field_lines

    thickness = field["line_thickness"]
    half_length = field["field_length"] / 2
    half_width = field["field_width"] / 2
    penalty_length = half_length - field["penalty_area_depth"]
    half_penalty = field["penalty_area_width"] / 2

    def add_line(name: str, x1: float, y1: float, x2: float, y2: float) -> None:
        line = lines.add()
        line.name = name
        line.p1.x = x1
        line.p1.y = y1
        line.p2.x = x2
        line.p2.y = y2
        line.thickness = thickness
        line.type = SSL_FieldShapeType.Value(name)  # type: ignore[assignment]

    add_line("TopTouchLine", -half_length, half_width, half_length, half_width)
    add_line("BottomTouchLine", -half_length, -half_width, half_length, -half_width)
    add_line("LeftGoalLine", -half_length, -half_width, -half_length, half_width)
    add_line("RightGoalLine", half_length, -half_width, half_length, half_width)
    add_line("HalfwayLine", 0, -half_width, 0, half_width)
    add_line("CenterLine", -half_length, 0, half_length, 0)
    add_line(
        "LeftPenaltyStretch",
        -penalty_length,
        -half_penalty,
        -penalty_length,
        half_penalty,
    )
    add_line(
        "RightPenaltyStretch",
        penalty_length,
        -half_penalty,
        penalty_length,
        half_penalty,
    )
    add_line(
        "LeftFieldLeftPenaltyStretch",
        -half_length,
        -half_penalty,
        -penalty_length,
        -half_penalty,
    )
    add_line(
        "LeftFieldRightPenaltyStretch",
        -half_length,
        half_penalty,
        -penalty_length,
        half_penalty,
    )
    add_line(
        "RightFieldLeftPenaltyStretch",
        penalty_length,
        half_penalty,
        half_length,
        half_penalty,
    )
    add_line(
        "RightFieldRightPenaltyStretch",
        penalty_length,
        -half_penalty,
        half_length,
        -half_penalty,
    )

    arc = wrapper.geometry.field.field_arcs.add()
    arc.name = "CenterCircle"
    arc.type = SSL_FieldShapeType.Value(arc.name)  # type: ignore[assignment]
    arc.center.x = 0.0
    arc.center.y = 0.0
    arc.radius = field["center_circle_radius"]
    arc.a1 = 0
    arc.a2 = math.tau
    arc.thickness = thickness


def load_geometry(path: Path) -> SSL_WrapperPacket:
    with path.open("r") as f:
        config = yaml.safe_load(f)

    wrapper = SSL_WrapperPacket()
    ParseDict(config, wrapper.geometry)
    _generate_field_markings(wrapper, config["field"])
    wrapper.source = SSL_SOURCE_VISION_PROCESSOR
    return wrapper


class Geometry:
    def __init__(self, bus: Bus, geometry_yml_path: Path) -> None:
        self._bus = bus
        self._wrapper = load_geometry(geometry_yml_path)
        log.info(
            "loaded %s with %d initial calib(s)",
            geometry_yml_path,
            len(self._wrapper.geometry.calib),
        )

    async def run(self) -> None:
        await asyncio.gather(
            self._absorb_loop(),
            self._publish_loop(),
        )

    async def _absorb_loop(self) -> None:
        queue = self._bus.subscribe("geometry.in")
        while True:
            incoming: SSL_GeometryData = await queue.get()
            self._merge_calibs(incoming)

    async def _publish_loop(self) -> None:
        while True:
            self._bus.publish("wrapper_packet.out", self._wrapper.SerializeToString())
            await asyncio.sleep(PUBLISH_INTERVAL_S)

    def _merge_calibs(self, incoming: SSL_GeometryData) -> None:
        calib = self._wrapper.geometry.calib
        by_id = {c.camera_id: c for c in calib}
        for camera in incoming.calib:
            existing = by_id.get(camera.camera_id)

            # Initial calibration received for camera. Store it
            if existing is None:
                calib.append(camera)
                log.info("Added camera %d calibration", camera.camera_id)
                continue
            incoming_bytes = camera.SerializeToString(deterministic=True)

            # Don't update if nothing changed
            if existing.SerializeToString(deterministic=True) == incoming_bytes:
                continue
            existing.CopyFrom(camera)
            log.info("Updated camera %d calibration", camera.camera_id)
