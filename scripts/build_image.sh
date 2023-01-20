#!/bin/bash

# Stop on error
set -e


# Script will not work on OSX
if [ ! `uname -s` = "Linux" ]; then
    echo "This script only works on Linux native or Linux under Parallels"
    exit 1
fi

srcimg="2021-01-11-raspios-buster-armhf-lite.img"
dstimg="rpi0_test_2021_01_11.img"


dir=`pwd`

# Get original image and uncompress
if [ ! -f images/$srcimg ]; then
    echo "fetching Raspian buster (legacy).."
    mkdir -p images
    cd images
    wget https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2021-01-12/2021-01-11-raspios-buster-armhf-lite.zip
    echo "Uncompressing image.."
    unzip 2021-01-11-raspios-buster-armhf-lite.zip
    cd $dir
fi

# If there isn't an image, copy the original
if [ ! -f images/$dstimg ]; then
    echo "Missing image file, copying from source image"
    cp images/$srcimg images/$dstimg
fi


# Grow the image to make space for installs
srcimg_size=`stat -c %s images/$srcimg`
dstimg_size=`stat -c %s images/$dstimg`

echo Source is: $srcimg_size 
echo Dest is: $dstimg_size
if [ "$srcimg_size" = "$dstimg_size" ]; then
    # Add zeros to the end of the imageuu
    dd if=/dev/zero bs=2G count=1 >> images/$dstimg
    # Resize the second partition to fill the added space
    echo "- +" |sfdisk -N 2 images/$dstimg
    # Make loopback devices of the image
    parts=`sudo kpartx -v -a images/$dstimg`
    echo "$parts"
    # Get name of loopback device for second (root) partition
    proot=$(echo "$parts"| awk '{getline; print $3;exit}' )
    echo "root partition" /dev/mapper/$proot
    # Fixup and resize the filesystem on the partition
    sudo e2fsck -f /dev/mapper/$proot
    sudo resize2fs /dev/mapper/$proot
    # Tear down the loopback devices
    sudo kpartx -d images/$dstimg
fi


echo "Finished"
