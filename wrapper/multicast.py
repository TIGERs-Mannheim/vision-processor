"""Async multicast UDP I/O.

Inbound: parse SSL_WrapperPacket and demux to geometry.in / detection.in.
Outbound: subscribe to wrapper_packet.out, sendto raw bytes.

Socket-level details preserved from python/visionsocket.py:

- IP_MULTICAST_TTL = 32 (default 1 doesn't traverse routers)
- SO_REUSEADDR = 1 (co-host with vision_processor)
- bind to the multicast group address (filters inbound to the joined group)
- IP_ADD_MEMBERSHIP with INADDR_ANY (kernel default interface)
"""

from __future__ import annotations

import asyncio
import logging
import socket
import struct

from google.protobuf.message import DecodeError

from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket
from wrapper.bus import Bus

log = logging.getLogger("wrapper.multicast")


class _RxProtocol(asyncio.DatagramProtocol):
    def __init__(self, bus: Bus) -> None:
        self._bus = bus

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        packet = SSL_WrapperPacket()
        try:
            packet.ParseFromString(data)
        except DecodeError as exc:
            log.warning("dropping malformed datagram from %s: %s", addr, exc)
            return

        if packet.HasField("geometry"):
            self._bus.publish("geometry.in", packet.geometry)
        if packet.HasField("detection"):
            self._bus.publish("detection.in", packet.detection)


class Multicast:
    def __init__(self, bus: Bus, group: str, port: int) -> None:
        self._bus = bus
        self._group = group
        self._port = port
        self._transport: asyncio.DatagramTransport | None = None
        self._tx_task: asyncio.Task[None] | None = None

    async def start(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self._group, self._port))
        sock.setsockopt(
            socket.IPPROTO_IP,
            socket.IP_ADD_MEMBERSHIP,
            struct.pack("4sl", socket.inet_aton(self._group), socket.INADDR_ANY),
        )

        loop = asyncio.get_running_loop()
        transport, _protocol = await loop.create_datagram_endpoint(
            lambda: _RxProtocol(self._bus),
            sock=sock,
        )
        self._transport = transport
        self._tx_task = asyncio.create_task(self._tx_loop(), name="multicast-tx")
        log.info("multicast bound to %s:%d", self._group, self._port)

    async def _tx_loop(self) -> None:
        queue = self._bus.subscribe("wrapper_packet.out")
        assert self._transport is not None
        while True:
            payload: bytes = await queue.get()
            self._transport.sendto(payload, (self._group, self._port))

    async def close(self) -> None:
        if self._tx_task is not None:
            self._tx_task.cancel()
            try:
                await self._tx_task
            except asyncio.CancelledError:
                pass
            self._tx_task = None
        if self._transport is not None:
            self._transport.close()
            self._transport = None
