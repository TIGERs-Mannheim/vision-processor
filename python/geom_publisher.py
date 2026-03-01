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
import time
from pathlib import Path

import yaml
from google.protobuf.json_format import ParseDict

from visionsocket import parser_vision_network, VisionSocket  # Importing visionsocket generates protobuf files
from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket, SSL_SOURCE_VISION_PROCESSOR
from proto.ssl_vision_geometry_pb2 import SSL_FieldShapeType, SSL_GeometryData


def yaml_load(path: Path, default = None):
    if path.exists():
        with path.open('r') as file:
            return yaml.safe_load(file)
    elif default:
        return default()
    else:
        raise FileNotFoundError


def _generate_default_lines(wrapper, config):
    def config_bool(config, key):
        return key not in config or config[key]

    lines = wrapper.geometry.field.field_lines

    default_lines = config['default_lines'] if 'default_lines' in config else {}
    field = config['field']
    thickness = field['line_thickness']
    half_length = field['field_length'] / 2
    half_width = field['field_width'] / 2

    def add_line(name, x1, y1, x2, y2, type=None):
        lines.add()
        line = lines[len(lines) - 1]
        line.name = name
        line.p1.x = x1
        line.p1.y = y1
        line.p2.x = x2
        line.p2.y = y2
        line.thickness = thickness
        line.type = SSL_FieldShapeType.Value(type if type else line.name)

    add_line(   'TopTouchLine', -half_length,  half_width,  half_length,  half_width)
    add_line('BottomTouchLine', -half_length, -half_width,  half_length, -half_width)
    add_line(   'LeftGoalLine', -half_length, -half_width, -half_length,  half_width)
    add_line(  'RightGoalLine',  half_length, -half_width,  half_length,  half_width)

    if config_bool(default_lines, 'halfway'):
        add_line(    'HalfwayLine',            0, -half_width,            0,  half_width)

    if config_bool(default_lines, 'goal2goal'):
        add_line('CenterLine', -half_length, 0, half_length, 0)

    penalty_length = half_length - field['penalty_area_depth']
    half_penalty = field['penalty_area_width'] / 2

    if config_bool(default_lines, 'penalty'):
        add_line(           'LeftPenaltyStretch', -penalty_length, -half_penalty, -penalty_length,  half_penalty)
        add_line(          'RightPenaltyStretch',  penalty_length, -half_penalty,  penalty_length,  half_penalty)
        add_line(  'LeftFieldLeftPenaltyStretch',    -half_length, -half_penalty, -penalty_length, -half_penalty)
        add_line( 'LeftFieldRightPenaltyStretch',    -half_length,  half_penalty, -penalty_length,  half_penalty)
        add_line( 'RightFieldLeftPenaltyStretch',  penalty_length,  half_penalty,     half_length,  half_penalty)
        add_line('RightFieldRightPenaltyStretch',  penalty_length, -half_penalty,     half_length, -half_penalty)

    if config_bool(default_lines, 'centercircle'):
        arcs = wrapper.geometry.field.field_arcs
        arcs.add()
        arc = arcs[len(arcs)-1]
        arc.name = 'CenterCircle'
        arc.type = SSL_FieldShapeType.Value(arc.name)
        arc.center.x = arc.center.y = 0.0
        arc.radius = field['center_circle_radius']
        arc.a1 = 0
        arc.a2 = 6.283185
        arc.thickness = thickness


def load_geometry(path: Path) -> SSL_WrapperPacket:
    config = yaml_load(path)
    wrapper = SSL_WrapperPacket()
    ParseDict(config, wrapper.geometry, ignore_unknown_fields=True)
    _generate_default_lines(wrapper, config)
    return wrapper


if __name__ == '__main__':
    parser = parser_vision_network(argparse.ArgumentParser(prog='Geometry publisher'))
    parser.add_argument('config', default='geometry.yml', help='Geometry configuration file')
    args = parser.parse_args()

    wrapper = load_geometry(Path(args.config))
    wrapper.source = SSL_SOURCE_VISION_PROCESSOR
    geometry: SSL_GeometryData = wrapper.geometry
    calib = geometry.calib

    receiver = VisionSocket(args=args)
    def update_cameras(received):
        if received.HasField('geometry'):
            for camera in received.geometry.calib:
                updated = False
                handled = False
                for c in calib:
                    if c.camera_id == camera.camera_id:
                        handled = True
                        if c.SerializeToString(deterministic=True) == camera.SerializeToString(deterministic=True):
                            continue
                        else:
                            calib[camera.camera_id].CopyFrom(camera)
                            updated = True

                if not handled:
                    calib.append(camera)
                    updated = True

                if updated:
                    print(f"Updated camera {camera.camera_id} calibration: {camera}")
    receiver.consume = update_cameras

    with receiver:
        while True:
            receiver.send(wrapper)
            time.sleep(1.0)
