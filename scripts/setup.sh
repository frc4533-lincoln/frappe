#!/bin/bash

# Script will not work on OSX
if [ ! `uname -s` = "Linux" ]; then
    echo "This script only works on Linux native or Linux under Parallels"
    exit 1
fi



# Install all the libraries and headers we need, and build OpenCV 4.5.2
do_on_image_chroot ()
{
    docker run -it --rm --privileged -v $PWD:/data shell chroot /data/mnt "$@"
}

do_on_image ()
{
    docker run -it --rm --privileged -v $PWD:/data shell "$@"
}


mount_loop ()
{
    # Create the loop device and mount it
    parts=`sudo kpartx -v -a $1`
    echo "$parts"
    proot=$(echo "$parts"| awk '{getline; print $3;exit}' )
    echo "root partition" $proot
    echo "Mounting partition"
    mkdir -p mnt
    sudo mount /dev/mapper/$proot mnt
}

unmount_loop ()
{
    sudo umount mnt
    sudo kpartx -d $1
}

# Create error handler to unmount and delete loop device
on_error ()
{
    trap -p
    echo "Unmounting and deleting loop device"
    unmount_loop $dstimg
    exit 1
}