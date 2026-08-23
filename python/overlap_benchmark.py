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
import itertools
import math
from collections import defaultdict
from pathlib import Path

import json

from binary import parser_binary, run_binary
from dataset import parser_test_data, iterate_field, Dataset
from geom_publisher import load_geometry
from proto.vision.ssl_vision_detection_pb2 import SSL_DetectionRobot, SSL_DetectionBall
from blob_benchmark import AvgValue
from visionsocket import parser_vision_network, VisionRecorder


def double_files(a: Path, b: Path, glob: str) -> set[str]:
    a_names = {path.name for path in a.glob(glob)}
    return {path.name for path in b.glob(glob) if path.name in a_names}


def is_video(path: Path) -> bool:
    with path.open('r') as file:
        return len(json.load(file)) > 1


def reproject(args: argparse.Namespace, recorder: VisionRecorder, dataset: Dataset, geometryname: str, detectionsname: str) -> tuple[list[SSL_DetectionBall], list[SSL_DetectionRobot], list[SSL_DetectionRobot], float]:
    modelscore = [1.0]
    def score(line: str):
        if line.startswith('[Model score]'):
            modelscore[0] = float(line.split(' ')[2][:-1])
            print(dataset.folder.name, modelscore, line, end='')
    run_binary(args.binary, recorder, dataset, dataset.field, geometry=load_geometry(dataset.folder / geometryname), ground_truth=dataset.folder / detectionsname, stdoutconsumer=score)
    detection = recorder.subfield('detection')[0]
    return list(detection.balls), list(detection.robots_yellow), list(detection.robots_blue), modelscore[0]


def merge_bots(a: list[SSL_DetectionRobot], b: list[SSL_DetectionRobot]) -> list[tuple[SSL_DetectionRobot, SSL_DetectionRobot]]:
    b = {bot.robot_id: bot for bot in b}
    return [(bot, b[bot.robot_id]) for bot in a if bot.robot_id in b]


if __name__ == '__main__':
    parser = parser_test_data(parser_vision_network(parser_binary(argparse.ArgumentParser(prog='Vision recorder'))))
    parser.add_argument('--suffix', default='vision', help='Dataset suffix')
    args = parser.parse_args()

    recorder = VisionRecorder(args=args)
    total_score = defaultdict(lambda: defaultdict(lambda: 0.0))
    total_elements = defaultdict(lambda: defaultdict(lambda: 0))
    total_offset = defaultdict(lambda: defaultdict(lambda: [0.0, 0.0]))
    total_bot_error = defaultdict(lambda: defaultdict(lambda: 0.0))
    total_bots = defaultdict(lambda: defaultdict(lambda: 0))
    total_ball_error = defaultdict(lambda: defaultdict(lambda: 0.0))
    total_balls = defaultdict(lambda: defaultdict(lambda: 0))

    for field in args.data_folder.iterdir():
        if not field.is_dir():
            continue

        datasets = list(iterate_field(field))
        if len(datasets) < 2:
            continue

        print(f"Processing {field}")
        for a, b in itertools.combinations(datasets, 2):
            geometries = double_files(a.folder, b.folder, 'geometry*.yml')
            for detectionsname in double_files(a.folder, b.folder, f'*.{args.suffix}.json') - {f'geometry.{args.suffix}.json'}:
                if is_video(a.folder / detectionsname) or is_video(b.folder / detectionsname):
                    continue

                for geometryname in geometries:
                    print(f"Overlapping {detectionsname}: {geometryname}")
                    a_detection = reproject(args, recorder, a, geometryname, detectionsname)
                    b_detection = reproject(args, recorder, b, geometryname, detectionsname)
                    total_score[geometryname][field] += a_detection[3] + b_detection[3]
                    total_elements[geometryname][field] += 2

                    ball_pair_candidates = []
                    for a_ball in a_detection[0]:
                        min_dist = 1000.0
                        min_ball = None
                        for b_ball in b_detection[0]:
                            diff = (b_ball.x - a_ball.x, b_ball.y - a_ball.y)
                            dist = math.sqrt(diff[0]**2 + diff[1]**2)
                            if dist < min_dist:
                                min_dist = dist
                                min_ball = b_ball

                        if min_ball:
                            ball_pair_candidates.append((a_ball, min_ball))

                    balls_error = 0.0
                    balls_offset = [0.0, 0.0]
                    balls = 0
                    for a_ball, b_ball in ball_pair_candidates:
                        min_dist = 1000.0
                        min_ball = None
                        for a_ball2 in a_detection[0]:
                            diff = (b_ball.x - a_ball2.x, b_ball.y - a_ball2.y)
                            dist = math.sqrt(diff[0] ** 2 + diff[1] ** 2)
                            if dist < min_dist:
                                min_dist = dist
                                min_ball = a_ball2

                        if min_ball and min_ball == a_ball:
                            diff = (a_ball.x - b_ball.x, a_ball.y - b_ball.y)
                            balls_error += math.sqrt(diff[0] ** 2 + diff[1] ** 2)
                            balls_offset[0] += diff[0]
                            balls_offset[1] += diff[1]
                            balls += 1

                    bots_error = 0.0
                    bots_offset = [0.0, 0.0]
                    bots = 0
                    for a_bot, b_bot in merge_bots(a_detection[1], b_detection[1]) + merge_bots(a_detection[2], b_detection[2]):
                        diff = (a_bot.x - b_bot.x, a_bot.y - b_bot.y)
                        bots_error += math.sqrt(diff[0] ** 2 + diff[1] ** 2)
                        bots_offset[0] += diff[0]
                        bots_offset[1] += diff[1]
                        bots += 1

                    print(f"  {balls_error / balls if balls > 0 else math.nan: .2f} mm for {balls} balls")
                    if balls > 0:
                        total_ball_error[geometryname][field] += balls_error
                        total_offset[geometryname][field][0] += balls_offset[0]
                        total_offset[geometryname][field][1] += balls_offset[1]
                        total_balls[geometryname][field] += balls

                    print(f"  {bots_error / bots if bots > 0 else math.nan: .2f} mm for {bots} bots")
                    if bots > 0:
                        total_bot_error[geometryname][field] += bots_error
                        total_offset[geometryname][field][0] += bots_offset[0]
                        total_offset[geometryname][field][1] += bots_offset[1]
                        total_bots[geometryname][field] += bots

    for geometryname in total_offset:
        print(f"\n{geometryname}")
        global_bot_error = AvgValue()
        global_balls_error = AvgValue()
        global_score = AvgValue(True)
        for field in total_bot_error[geometryname]:
            bot_error = total_bot_error[geometryname][field] / total_bots[geometryname][field]
            ball_error = total_ball_error[geometryname][field] / total_balls[geometryname][field]
            offset = math.sqrt((total_offset[geometryname][field][0] / (total_bots[geometryname][field] + total_balls[geometryname][field]))**2 + (total_offset[geometryname][field][1] / (total_bots[geometryname][field] + total_balls[geometryname][field]))**2)
            score = 1 - total_score[geometryname][field] / total_elements[geometryname][field]
            print(f"  {field.name: >20}: {bot_error: .2f} mm for {total_bots[geometryname][field]: >3} bots {ball_error: .2f} mm for {total_balls[geometryname][field]: >3} balls, offset: {offset: .2f} mm, score {score: .4f}")
            global_bot_error += bot_error
            global_balls_error += ball_error
            global_score += score

        print(f"Total: {global_bot_error} mm bots {global_balls_error} mm balls {global_score} score")
