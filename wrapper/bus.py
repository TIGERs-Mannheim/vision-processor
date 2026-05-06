"""In-process async pub/sub with watch-channel semantics.

Each subscriber gets its own size-1 asyncio.Queue. On publish, every
subscriber's queue is drained-then-filled with the new message. Slow
subscribers see only the latest value at the time they wake up;
intermediate values are dropped.
"""

from __future__ import annotations

import asyncio
from typing import Any


class Bus:
    def __init__(self) -> None:
        self._subs: dict[str, list[asyncio.Queue[Any]]] = {}

    def subscribe(self, topic: str) -> asyncio.Queue[Any]:
        queue: asyncio.Queue[Any] = asyncio.Queue(maxsize=1)
        self._subs.setdefault(topic, []).append(queue)
        return queue

    def publish(self, topic: str, msg: Any) -> None:
        for queue in self._subs.get(topic, ()):
            try:
                queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
            queue.put_nowait(msg)

    def unsubscribe(self, topic: str, queue: asyncio.Queue[Any]) -> None:
        subs = self._subs.get(topic)
        if subs is None:
            return
        try:
            subs.remove(queue)
        except ValueError:
            return
        if not subs:
            del self._subs[topic]
