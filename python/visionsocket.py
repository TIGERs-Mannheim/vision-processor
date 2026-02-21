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
import os
import pathlib
import socket
import struct
import threading

from google.protobuf.json_format import MessageToDict

if not os.path.exists('python/proto/ssl_vision_wrapper_pb2.py'):
    print("Compiling Protobuf files...")
    import subprocess
    try:
        subprocess.run([
            'protoc',
            '--python_out=python', '--pyi_out=python',
            *[str(path) for path in pathlib.Path().rglob('proto/*.proto')]
        ], check=True)
    except subprocess.CalledProcessError:
        # Ubuntu 22.04 protoc can't do pyi
        subprocess.run([
            'protoc',
            '--python_out=python',
            *[str(path) for path in pathlib.Path().rglob('proto/*.proto')]
        ], check=True)



from proto.ssl_vision_wrapper_pb2 import SSL_WrapperPacket


def parser_vision_network(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument('--vision_ip', default='224.5.23.2', help='Multicast IP address of the vision')
    parser.add_argument('--vision_port', type=int, default=10006, help='Multicast port of the vision')
    return parser


def open_multicast_socket(ip, port):
    # Adapted from https://stackoverflow.com/a/1794373 (CC BY-SA 4.0 by Gordon Wrigley)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

    TTL = 32
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, struct.pack('b', TTL))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((ip, port))
    sock.setsockopt(
        socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
        struct.pack("4sl", socket.inet_aton(ip), socket.INADDR_ANY)
    )
    return sock


class VisionSocket:

    def __init__(self, vision_ip='224.5.23.2', vision_port=10006, args: argparse.Namespace=None):
        if args:
            vision_ip = args.vision_ip
            vision_port = args.vision_port

        self.running = False
        self.address = (vision_ip, vision_port)
        self.socket = open_multicast_socket(vision_ip, vision_port)

    def consume(self, wrapper: SSL_WrapperPacket):
        pass

    def send(self, wrapper: SSL_WrapperPacket):
        self.socket.sendto(wrapper.SerializeToString(), self.address)

    def _receive_thread(self):
        while self.running:
            data = self.socket.recv(65536)
            if not self.running and len(data) == 0:
                return

            wrapper = SSL_WrapperPacket()
            wrapper.ParseFromString(data)
            self.consume(wrapper)

    def __enter__(self):
        self.running = True
        self.thread = threading.Thread(target=self._receive_thread, name="Vision receiver")
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.running = False
        self.socket.sendto(b'', self.address)
        self.thread.join()


class VisionRecorder(VisionSocket):

    def __init__(self, vision_ip='224.5.23.2', vision_port=10006, args: argparse.Namespace=None):
        self.packets = []
        super().__init__(vision_ip, vision_port, args)

    def subfield(self, field: str) -> list:
        return [getattr(packet, field) for packet in self.packets if packet.HasField(field)]

    def dict_subfield(self, field: str) -> list[dict]:
        return [MessageToDict(packet, preserving_proto_field_name=True) for packet in self.subfield(field)]

    def __enter__(self):
        self.packets.clear()
        return super().__enter__()

    def consume(self, wrapper: SSL_WrapperPacket):
        self.packets.append(wrapper)
