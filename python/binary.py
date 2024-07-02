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
import subprocess
import sys
import time
from pathlib import Path

from dataset import Dataset
from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket
from visionsocket import VisionRecorder


def parser_binary(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument('--binary', default='bin/vision', help='Vision binary', type=Path)
    return parser


def run_ssl_vision(binary: Path, recorder: VisionRecorder, dataset: Dataset, image: Path):
    #TODO replace , with . in ssl vision config files
    config = dataset.read_ssl_config()
    config.find(".//Var[@name='camera index']").text = str(dataset.cam_id)
    config.find(".//Var[@name='Video']/Var[@name='file']").text = str(image.relative_to(dataset.config_dir, walk_up=True))
    for addr in config.findall(".//Var[@name='Multicast Address']"):
        addr.text = recorder.address[0]
    config.find(".//Var[@name='Multicast Port']").text = str(recorder.address[1])
    dataset.write_ssl_config(config)

    with recorder:
        with subprocess.Popen([str(binary.absolute()), '-s', '-c', '1'], cwd=str(dataset.config_dir), stdout=subprocess.PIPE, env={
            'QT_QPA_PLATFORM': 'offscreen'  # Don't render ssl-vision UI
        }) as vision:
            while (line := vision.stdout.readline().decode('utf-8')) != 'End of video stream reached\n':
                #print(line, end='')
                pass

            vision.terminate()
            vision.wait()

            if vision.returncode != 0:
                print(f'Nonzero return code: {vision.returncode}', file=sys.stderr)


def run_processor(binary: Path, recorder: VisionRecorder, dataset: Dataset, image: Path, geometry: SSL_WrapperPacket = None, ground_truth: Path = None, stdoutconsumer=lambda line: None):
    dataset.update_processor_config(
        opencv_path=str(image),
        benchmark={'wait_for_geometry': True, 'ground_truth': str(image.with_suffix('.vision.yml') if ground_truth is None else ground_truth)},
        network={'vision_ip': recorder.address[0], 'vision_port': recorder.address[1]}
    )

    if geometry is None:
        geometry = dataset.reference_geometry

    with recorder:
        with subprocess.Popen([str(binary), str(dataset.processor_config)], stdout=subprocess.PIPE) as vision:
            time.sleep(2.0)  # TODO better solution
            recorder.send(geometry)

            while vision.poll() is None:
                stdoutconsumer(vision.stdout.readline().decode('utf-8'))

            if vision.returncode != 0:
                print(f'Nonzero return code: {vision.returncode}', file=sys.stderr)


def run_binary(binary: Path, recorder: VisionRecorder, dataset: Dataset, image: Path, geometry: SSL_WrapperPacket = None, ground_truth: Path = None, stdoutconsumer=lambda line: None):
    if binary.name == 'vision':
        run_ssl_vision(binary, recorder, dataset, image)
    else:
        run_processor(binary, recorder, dataset, image, geometry=geometry, ground_truth=ground_truth, stdoutconsumer=stdoutconsumer)
