#!/bin/bash
#
#     Copyright 2025 Felix Weinmann
#
#     Licensed under the Apache License, Version 2.0 (the "License");
#     you may not use this file except in compliance with the License.
#     You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#

# Stop script execution with any error
set -e


if [[ -v SKIP_DRIVERS ]]; then
    echo -e "\e[92m »  Skipping OpenCL drivers\e[39m"
    opencl_arch=
    opencl_debian=
else
    # Default fallback for a CPU and GPU vendor independent OpenCL runtime
    opencl_arch=pocl
    opencl_debian=pocl-opencl-icd

    # Determine graphics card vendor
    for dir in /sys/bus/pci/devices/*/; do
        # Search for graphics card devices
        if [[ $(< $dir/class) == 0x030000 ]]; then
            vendor=$(< $dir/vendor)
            if [[ $vendor == 0x10de ]]; then
                echo -e "\e[92m »  NVIDIA graphics card selected\e[39m"
                opencl_arch='opencl-nvidia nvidia-utils'
                opencl_debian=nvidia-opencl-icd
            elif [[ $vendor == 0x1002 ]]; then
                echo -e "\e[92m »  AMD graphics card selected\e[39m"
                opencl_arch='rocm-opencl-runtime mesa'
                opencl_debian=mesa-opencl-icd
            elif [[ $vendor == 0x8086 ]]; then
                echo -e "\e[92m »  Intel graphics card selected\e[39m"
                opencl_arch='intel-compute-runtime intel-media-driver'
                opencl_debian='intel-opencl-icd intel-media-va-driver-non-free'
                # Add user to render group to allow screenless gpu access (https://github.com/intel/compute-runtime/issues/701)
                sudo usermod -a -G render $(whoami)
            else
                echo -e "\e[91m »  Could not determine graphics card vendor, skipping OpenCL driver check and installation\e[39m" >&2
                opencl_arch=
                opencl_debian=
            fi
        fi
    done
fi

# Determine distribution/package manager
if [[ -f /etc/arch-release ]]; then
    echo -e "\e[92m »  Arch based distribution\e[39m"
    packages="gcc make cmake pkgconf eigen opencv yaml-cpp opencl-clhpp python-protobuf python-yaml $opencl_arch"
    check_cmd="pacman -Qi $packages"
    install_cmd="sudo pacman -S $packages"
elif [[ -f /etc/debian_version ]]; then
    echo -e "\e[92m »  Debian based distribution\e[39m"
    packages="build-essential cmake pkg-config libyaml-cpp-dev ocl-icd-opencl-dev libeigen3-dev libopencv-dev protobuf-compiler libprotobuf-dev ffmpeg libavcodec-dev libavformat-dev libavutil-dev python3-protobuf python3-yaml $opencl_debian"
    check_cmd="dpkg -s $packages"
    install_cmd="sudo apt install --no-install-recommends $packages"
else
    echo -e "\e[91m »  Could not determine current operating system, skipping dependency check and installation\e[39m" >&2
    check_cmd=true
fi

# Check and install necessary packages
if $($check_cmd &>/dev/null); then
    echo -e "\e[92m »  Dependencies already installed\e[39m"
else
    echo " »  Installing dependencies and OpenCL drivers"
    $install_cmd
fi


# Compile vision_processor
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd "$SCRIPT_DIR"
cmake -B build .
make -j -C build vision_processor


# Install systemd service(s)
function install_service {
    echo -e "\e[92m »  Enabling and (re-)starting $1 service\e[39m"

    systemd_folder="$HOME/.local/share/systemd/user"
    mkdir -p "$systemd_folder"

    cat > "$systemd_folder/$1.service" <<EOL
[Unit]
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
Restart=always
ExecStart=$SCRIPT_DIR/$2
WorkingDirectory=$SCRIPT_DIR

[Install]
WantedBy=default.target
EOL

    systemctl --user enable "$1.service"
    systemctl --user daemon-reload
    systemctl --user restart "$1.service"
}

read -p " »  Install systemd service(s)? [yn]» " enable_systemd
if [ "$enable_systemd" = "y" ]; then
    install_service vision_processor build/vision_processor

    read -p " »  Enable geom_publisher service? [yN]» " enable_geom
    if [ "$enable_geom" = "y" ]; then
        install_service geom_publisher "python/geom_publisher.py geometry.yml"
    else
        echo " »  Skipping geom_publisher service installationa"
    fi
else
    echo " »  Skipping systemd service installation"
fi

echo -e "\e[92m »  Done\e[39m"
