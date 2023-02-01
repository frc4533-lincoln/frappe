#!/bin/bash


# Get helper functions and initial setup
my_dir="$(dirname "$0")"
source "$my_dir/setup.sh"

# Trap errors
set +e
trap on_error ERR SIGINT SIGTERM

echo "Downloading and expanding Raspbian image"
./scripts/build_image.sh
echo "Building cross compiler container"
./scripts/build_containers.sh
echo "Building native OpenCV"
./scripts/build_opencv.sh images/rpi0_test_2021_01_11.img
echo "Installing additional packages"
./scripts/install_packages.sh
