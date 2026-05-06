from proto import ssl_vision_detection_tracked_pb2 as _ssl_vision_detection_tracked_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TrackerWrapperPacket(_message.Message):
    __slots__ = ("uuid", "source_name", "tracked_frame")
    UUID_FIELD_NUMBER: _ClassVar[int]
    SOURCE_NAME_FIELD_NUMBER: _ClassVar[int]
    TRACKED_FRAME_FIELD_NUMBER: _ClassVar[int]
    uuid: str
    source_name: str
    tracked_frame: _ssl_vision_detection_tracked_pb2.TrackedFrame
    def __init__(self, uuid: _Optional[str] = ..., source_name: _Optional[str] = ..., tracked_frame: _Optional[_Union[_ssl_vision_detection_tracked_pb2.TrackedFrame, _Mapping]] = ...) -> None: ...
