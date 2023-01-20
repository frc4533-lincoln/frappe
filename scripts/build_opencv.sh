#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Usage: build_opencv.sh <image file>"
    exit 1
fi

# Get helper functions and initial setup
my_dir="$(dirname "$0")"
source "$my_dir/setup.sh"



dstimg=$1

# Connect to image
mount_loop $dstimg

# Trap errors
set +e
trap on_error ERR SIGINT SIGTERM

# Install dependencies and build
echo "Running docker on image $dstimg"


# # Opencv build rdependencies
do_on_image_chroot sh -c 'apt-get update --allow-releaseinfo-change && \
apt-get -y install libgles2-mesa-dev build-essential \
gfortran python3-dev python3-numpy libjpeg-dev libtiff-dev libgif-dev \
libgstreamer1.0-dev gstreamer1.0-gtk3 libgstreamer-plugins-base1.0-dev gstreamer1.0-gl \
libavcodec-dev libavformat-dev libswscale-dev libgtk2.0-dev libcanberra-gtk* \
libxvidcore-dev libx264-dev libgtk-3-dev libtbb2 libtbb-dev libdc1394-22-dev libv4l-dev \
libopenblas-dev libatlas-base-dev libblas-dev libjasper-dev liblapack-dev libhdf5-dev \
protobuf-compiler python-dev python-numpy python-pip libncurses-dev libdrm-dev'


#
do_on_image sh -c 'ls /data/mnt'
#

# https://github.com/opencv/opencv/issues/13328
#
do_on_image sh -c 'export RASPBIAN_ROOTFS=/data/mnt && \
export PATH=/opt/cross-pi-gcc/bin:$PATH && \
export RASPBERRY_VERSION=1 && \
cd /data/mnt/root && \
wget -O opencv.zip https://github.com/opencv/opencv/archive/4.5.2.zip && \
wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.5.2.zip && \
unzip opencv.zip && unzip opencv_contrib.zip && rm opencv.zip && rm opencv_contrib.zip && \
mv opencv-4.5.2 opencv && mv opencv_contrib-4.5.2 opencv_contrib && \
cd /data/mnt/root/opencv && mkdir -p build && cd build && \
cmake -D CMAKE_TOOLCHAIN_FILE=/data/scripts/toolchain_rpi.cmake \
-D CMAKE_BUILD_TYPE=RELEASE \
-D CMAKE_INSTALL_PREFIX=/data/mnt/usr/local \
-D OPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules \
-D ENABLE_NEON=OFF \
-D ENABLE_VFPV3=OFF \
-D WITH_OPENMP=ON \
-D WITH_OPENCL=OFF \
-D BUILD_ZLIB=ON \
-D BUILD_TIFF=OFF \
-D WITH_FFMPEG=ON \
-D WITH_TBB=OFF \
-D BUILD_TESTS=OFF \
-D WITH_EIGEN=OFF \
-D WITH_GSTREAMER=ON \
-D WITH_V4L=ON \
-D WITH_LIBV4L=ON \
-D WITH_VTK=OFF \
-D WITH_PROTOBUF=OFF \
-D OPENCV_ENABLE_NONFREE=ON \
-D INSTALL_C_EXAMPLES=OFF \
-D INSTALL_PYTHON_EXAMPLES=OFF \
-D OPENCV_GENERATE_PKGCONFIG=ON \
-D BUILD_EXAMPLES=OFF .. && \
make -j8 && \
make install'

unmount_loop $dstimg

echo "Finished"
