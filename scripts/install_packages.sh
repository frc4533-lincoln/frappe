#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Usage: install_packages.sh <image file>"
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
apt-get -y install \
libzmq3-dev'


unmount_loop $dstimg

echo "Finished"
