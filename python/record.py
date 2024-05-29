#!/usr/bin/env python3
import argparse
import sys
import threading

import cv2
import yaml

from binary import parser_binary, run_binary
from dataset import threaded_field_iter, parser_test_data
from visionsocket import VisionRecorder


_thread_counter = 1
_thread_ip = threading.local()
_thread_lock = threading.RLock()

def thread_local_ip():
    if not hasattr(_thread_ip, 'ip'):
        global _thread_counter

        with _thread_lock:
            _thread_ip.ip = str(_thread_counter)
            _thread_counter += 1

    return '224.83.83.' + _thread_ip.ip


if __name__ == '__main__':
    args = parser_test_data(parser_binary(argparse.ArgumentParser(prog='Vision recorder'))).parse_args()

    def consumer(dataset):
        recorder = VisionRecorder(vision_ip=thread_local_ip())

        for video in dataset.images():
            print(f"Recording {video}")

            if video.suffix == '.mp4':
                # https://stackoverflow.com/questions/25359288/how-to-know-total-number-of-frame-in-a-file-with-cv2-in-python
                capture = cv2.VideoCapture(str(video))
                frames = int(capture.get(cv2.CAP_PROP_FRAME_COUNT))
            else:
                frames = 1

            detections = []

            while len(detections) != frames:
                run_binary(args.binary, recorder, dataset, video)

                detections = recorder.dict_subfield('detection')

                if len(detections) != frames:
                    print(f"{video}: Detection size mismatch: Expected {frames} Vision {len(detections)}, repeating", file=sys.stderr)

            with video.with_suffix('.' + args.binary.name + '.yml').open('w') as file:
                yaml.dump(detections, file, Dumper=yaml.CDumper)

    threaded_field_iter(args.data_folder, consumer)
