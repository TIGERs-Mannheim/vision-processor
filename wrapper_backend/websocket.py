"""WebSocket bridge from the bus to browser clients.

Each connected client owns a size-1 outbound queue (slow clients drop
intermediate frames, matching the bus's watch-channel semantics).

A "channel" represents one bus topic. The bus is read lazily: when the
first client joins a channel its bus-reader task starts; when the last
client leaves it stops. Topics nobody is listening to cost nothing.

Wire protocol (JSON, both directions):

    client -> server   {"action": "subscribe",   "topic": "..."}
    client -> server   {"action": "unsubscribe", "topic": "..."}
    server -> client   {"topic": "...", "data": {...}}
    server -> client   {"error": "...", "topic": "..."}
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Callable

from aiohttp import WSMsgType, web
from google.protobuf.json_format import MessageToDict

from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket
from wrapper_backend.bus import Bus

log = logging.getLogger("wrapper_backend.websocket")


def _encode_wrapper_packet(payload: bytes) -> dict[str, Any]:
    packet = SSL_WrapperPacket()
    packet.ParseFromString(payload)
    return MessageToDict(packet, preserving_proto_field_name=True)


# Encoders convert the raw bus payload for a topic into a JSON-serialisable
# dict. Topics not present here cannot be exposed to clients.
_TOPIC_ENCODERS: dict[str, Callable[[Any], dict[str, Any]]] = {
    "wrapper_packet.out": _encode_wrapper_packet,
}


class _Client:
    def __init__(self, websocket: web.WebSocketResponse) -> None:
        self._websocket = websocket
        self._outbox: asyncio.Queue[str] = asyncio.Queue(maxsize=1)
        self.topics: set[str] = set()

    def post(self, frame: str) -> None:
        try:
            self._outbox.get_nowait()
        except asyncio.QueueEmpty:
            pass
        self._outbox.put_nowait(frame)

    async def send_error(self, message: str, topic: str | None = None) -> None:
        payload: dict[str, str] = {"error": message}
        if topic is not None:
            payload["topic"] = topic
        await self._websocket.send_str(json.dumps(payload))

    async def deliver_forever(self) -> None:
        while True:
            frame = await self._outbox.get()
            if self._websocket.closed:
                return
            await self._websocket.send_str(frame)


class _Channel:
    def __init__(self, task: asyncio.Task[None]) -> None:
        self.clients: set[_Client] = set()
        self.task = task


class _TopicBridge:
    def __init__(self, bus: Bus) -> None:
        self._bus = bus
        self._channels: dict[str, _Channel] = {}

    async def handle(self, request: web.Request) -> web.WebSocketResponse:
        websocket = web.WebSocketResponse()
        await websocket.prepare(request)
        client = _Client(websocket)
        peer = request.remote
        log.info("client connected: %s", peer)
        delivery = asyncio.create_task(client.deliver_forever(), name="ws-deliver")
        try:
            async for message in websocket:
                if message.type == WSMsgType.TEXT:
                    await self._on_client_message(client, message.data)
        finally:
            for topic in list(client.topics):
                self._leave(client, topic)
            delivery.cancel()
            try:
                await delivery
            except asyncio.CancelledError:
                pass
            log.info("client disconnected: %s", peer)
        return websocket

    async def shutdown(self, _: web.Application) -> None:
        for topic, channel in list(self._channels.items()):
            channel.task.cancel()
            try:
                await channel.task
            except asyncio.CancelledError:
                pass
            self._channels.pop(topic, None)

    async def _on_client_message(self, client: _Client, raw: str) -> None:
        try:
            request = json.loads(raw)
            action = request["action"]
            topic = request["topic"]
        except (json.JSONDecodeError, KeyError, TypeError):
            await client.send_error("malformed request")
            return

        if action == "subscribe":
            if topic not in _TOPIC_ENCODERS:
                await client.send_error("unknown topic", topic)
                return
            self._join(client, topic)
        elif action == "unsubscribe":
            self._leave(client, topic)
        else:
            await client.send_error("unknown action", topic)

    def _join(self, client: _Client, topic: str) -> None:
        channel = self._channels.get(topic)
        if channel is None:
            task = asyncio.create_task(
                self._forward_topic(topic), name=f"ws-forward-{topic}"
            )
            channel = self._channels[topic] = _Channel(task)
            log.info("opened channel %s", topic)
        channel.clients.add(client)
        client.topics.add(topic)

    def _leave(self, client: _Client, topic: str) -> None:
        channel = self._channels.get(topic)
        if channel is None or client not in channel.clients:
            return
        channel.clients.discard(client)
        client.topics.discard(topic)
        if not channel.clients:
            channel.task.cancel()
            del self._channels[topic]
            log.info("closed channel %s", topic)

    async def _forward_topic(self, topic: str) -> None:
        encode = _TOPIC_ENCODERS[topic]
        queue = self._bus.subscribe(topic)
        try:
            while True:
                payload = await queue.get()
                try:
                    frame = json.dumps({"topic": topic, "data": encode(payload)})
                except Exception:
                    log.exception("dropping unencodable payload on %s", topic)
                    continue
                for client in self._channels[topic].clients:
                    client.post(frame)
        finally:
            self._bus.unsubscribe(topic, queue)


def register(http_app: web.Application, bus: Bus) -> None:
    bridge = _TopicBridge(bus)
    http_app.router.add_get("/ws", bridge.handle)
    http_app.on_cleanup.append(bridge.shutdown)
