"""Async multicast UDP I/O.

Inbound: parse SSL_WrapperPacket and demux to geometry.in / detection.in.
Outbound: subscribe to wrapper_packet.out, sendto raw bytes.
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


class _MulticastToBus(asyncio.DatagramProtocol):
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


class _BusToMulticast:
    def __init__(
        self,
        bus: Bus,
        transport: asyncio.DatagramTransport,
        target: tuple[str, int],
    ) -> None:
        self._bus = bus
        self._transport = transport
        self._target = target

    async def run(self) -> None:
        queue = self._bus.subscribe("wrapper_packet.out")
        while True:
            payload: bytes = await queue.get()
            self._transport.sendto(payload, self._target)


class Multicast:
    def __init__(self, bus: Bus, group: str, port: int) -> None:
        self._bus = bus
        self._group = group
        self._port = port
        self._transport: asyncio.DatagramTransport | None = None
        self._bus_to_multicast: asyncio.Task[None] | None = None

    async def start(self) -> None:
        # Join multicast
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

        # Start multicast -> bus task
        transport, _protocol = await loop.create_datagram_endpoint(
            lambda: _MulticastToBus(self._bus),
            sock=sock,
        )
        self._transport = transport

        # Start bus -> multicast task
        self._bus_to_multicast = asyncio.create_task(
            _BusToMulticast(self._bus, transport, (self._group, self._port)).run(),
            name="multicast-tx",
        )

        log.info("multicast bound to %s:%d", self._group, self._port)

    async def close(self) -> None:
        # Cancel bus -> multicast task
        if self._bus_to_multicast is not None:
            self._bus_to_multicast.cancel()
            try:
                await self._bus_to_multicast
            except asyncio.CancelledError:
                pass
            self._bus_to_multicast = None
        # Cancel mutlicast -> bus task
        if self._transport is not None:
            self._transport.close()
            self._transport = None
