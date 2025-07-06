#!/usr/bin/env python3

#    Copyright 2024 Felix Weinmann
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.

import argparse
import json
import time
from pathlib import Path

from google.protobuf.json_format import ParseDict
from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket
from proto.ssl_vision_detection_pb2 import SSL_DetectionFrame
from visionsocket import parser_vision_network, VisionSocket
from geom_publisher import load_geometry


if __name__ == '__main__':
    parser = parser_vision_network(argparse.ArgumentParser(prog='Vision replay'))
    parser.add_argument('geometry', default='geometry.yml', help='Geometry configuration file')
    parser.add_argument('speed', type=float, default=1.0, help='Replay speed factor')
    parser.add_argument('detections', help='Vision detections file')
    args = parser.parse_args()

    with open(args.detections, 'r') as file:
        detections = json.load(file)

    wrapper = load_geometry(Path(args.geometry))
    socket = VisionSocket(args=args)
    socket.send(wrapper)

    timestamp = 0.0
    for detection in detections:
        wrapper = SSL_WrapperPacket()
        wrapper.detection.CopyFrom(ParseDict(detection, SSL_DetectionFrame()))
        socket.send(wrapper)
        next_time = detection['t_capture']
        time.sleep((next_time - timestamp) * args.speed)
        timestamp = next_time
