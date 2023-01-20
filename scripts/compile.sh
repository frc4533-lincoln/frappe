#!/bin/bash

# Get helper functions and initial setup
my_dir="$(dirname "$0")"
source "$my_dir/setup.sh"

# Trap errors
set +e
trap on_error ERR SIGINT SIGTERM


mount_loop images/rpi0_test_2021_01_11.img

do_on_image sh -c 'mkdir -p build_rpi0 && \
cd build_rpi0 && \
export RASPBIAN_ROOTFS=/data/mnt && \
export PATH=/opt/cross-pi-gcc/bin:$PATH && \
export RASPBERRY_VERSION=1 && \
cmake -DCMAKE_TOOLCHAIN_FILE=../scripts/toolchain_rpi.cmake \
-DCMAKE_BUILD_TYPE=Release .. && \
make VERBOSE=1 -j4'

unmount_loop images/rpi0_test_2021_01_11.img

