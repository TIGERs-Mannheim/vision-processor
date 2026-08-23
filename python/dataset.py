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
import builtins
import concurrent.futures
import multiprocessing
import sys
from pathlib import Path
from typing import Iterable
from xml.etree import ElementTree

import yaml

from geom_publisher import load_geometry, yaml_load
from proto.vision.ssl_vision_wrapper_pb2 import SSL_WrapperPacket


def parser_test_data(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument('--data_folder', default='test-data', help='Data folder', type=Path)
    parser.add_argument('--field', default='*', help='Field filter')
    return parser


def _merge_dict(d1, d2):
    for key, value in d2.items():
        if key in d1 and type(value) is dict and type(d1[key]) is dict:
            _merge_dict(d1[key], value)
        else:
            d1[key] = value


class Dataset:

    def __init__(self, folder: Path):
        self.folder = folder

    @property
    def cam_id(self) -> int:
        try:
            return int(self.folder.name[3:])
        except:
            print("[Dataset] Failed to extract cam_id from dataset folder name, defaulting to 0", file=sys.stderr)
            return 0

    @property
    def field(self) -> Path:
        return self.folder / 'field.png'

    @property
    def reference_geometry(self) -> SSL_WrapperPacket:
        return load_geometry(self.folder / 'geometry.yml')

    @property
    def config_dir(self) -> Path:
        return self.folder / 'ssl-vision-config'

    @property
    def ssl_config(self) -> Path:
        return self.config_dir / 'robocup-ssl.xml'

    @property
    def processor_config(self) -> Path:
        return self.config_dir / 'config.yml'

    def read_ssl_config(self) -> ElementTree:
        return ElementTree.parse(str(self.ssl_config))

    def write_ssl_config(self, config: ElementTree):
        config.write(str(self.ssl_config))

    def update_processor_config(self, **options):
        config = yaml_load(self.processor_config, default={})

        _merge_dict(config, options)

        if 'camera' not in config:
            config['camera'] = {}

        config['cam_id'] = self.cam_id
        config['camera']['driver'] = 'OPENCV'

        with self.processor_config.open('w') as file:
            yaml.dump(config, file)

    def images(self) -> Iterable[Path]:
        for video in sorted(self.folder.glob('*.mp4')):
            yield video
        for image in sorted(self.folder.glob('*.png')):
            if image != self.field:
                yield image

    def is_image_dataset(self) -> bool:
        return len(list(self.folder.glob('*.mp4'))) == 0

    def __str__(self):
        return str(self.folder)


def iterate_field(field: Path) -> Iterable[Dataset]:
    for dataset in field.iterdir():
        if dataset.is_dir():
            yield Dataset(dataset)


def iterate_fields(fields: Path, field_filter='*') -> Iterable[Dataset]:
    for field in fields.glob(field_filter):
        if field.is_dir():
            for dataset in iterate_field(field):
                yield dataset


def threaded_field_iter(fields: Path, consumer, workers=None, field_filter='*'):
    if workers == 1:
        for field in iterate_fields(fields, field_filter):
            consumer(field)

        return
    else:
        if workers is None:
            workers = multiprocessing.cpu_count()

        pool = concurrent.futures.ThreadPoolExecutor(max_workers=workers)

    try:
        concurrent.futures.wait(pool.map(consumer, iterate_fields(fields, field_filter)))
    except AttributeError:
        pass # Thrown if consumer returns None, not an error in our case
