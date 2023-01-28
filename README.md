# Frappe: Fiducial recognition accelerated with parallel processing elements

A fiducial recognition library designed to run on the Raspberry Pi Zero up to Raspberry 3B+. Currently handles a fixed resolution of 640x480 pixels, and the dictionary ArUco 36h12.

## Acknowledgements
https://github.com/Seneral/VC4CV



## Building

There are many dependancies to build properly, not least a source build of OpenCV, which takes many hours if run on an RPi0. We make this easier by providing a Docker build that runs a cross compilation and provides a set of libraries to copy to the target.



## Dependencies
The library relies on the /dev/vcsm kernel interface for providing cached zero-copy shared buffers between CPU and GPU (VC4). This was removed from RPi kernels from 5.9 onwards, with future similar functionality to be provided by DMA-BUF, however this is currently much slower since it doesn't seem to support cached access to the shared buffer.

The latest Raspian OS image that ships with the required funtionality is raspios_lite_armhf-2021-01-12, with kernel 5.4.83-v7+
```
wget https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2021-01-12/2021-01-11-raspios-buster-armhf-lite.zip
```

## QEMU issues
One way of working with images is to use a qemu-chroot, but this does not work properly
on and M1 machine, because of an assumption that all aarch64 will support arm32

The only way I've managed to get this to work is to build an x86_64 docker image, run this
with --platform linux/x86_64, which will use rosetta, then use a qemu chroot inside the 
docker to manipulate the file system image. This fails with ubuntu 20.04, possibly:
https://bugs.launchpad.net/ubuntu/+source/qemu/+bug/1906479
but seems to work on 22.04.. no, a fluke? Tried different versions, works with 18.04. This
bug seems to be persistantly showing up as a regression, havent seen deinite answers..


## Binfmt
List
```
update-binfmts --display
```

Register new
```
update-binfmts --install qemu-arm /usr/bin/qemu-arm-static
update-binfmts --install qemu-arm /usr/bin/qemu-arm-static --magic \x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00 --mask \xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff
```

the arm info is:
```
qemu-arm (enabled):
     package = qemu-user-static
        type = magic
      offset = 0
       magic = \x7f\x45\x4c\x46\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00
        mask = \xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff
 interpreter = /usr/bin/qemu-arm-static
    detector = 

```


## Build

Make containers and image, this is a docker container with cross compile tools
for both the arm cpu and for the qpu and vpu to allow full cross compile of the
complete app. The image is a standard raspbian install image with a bunch
of necessities installed, used for a true source of headers, and for building
and installing OpenCV 4.5.2.

Do:
```
./scripts/build_all.sh
```

To compile the Frappe library and basic apps, do:
```
./scripts/compile.sh
```

## Hacked userland
This is just the raspicam bits lifted and hacked to use the Frappe library.
This uses the Frappe library to recognise fiducials in the camera video stream and 
make them available over a tcp socket

To build:
```
./scripts/compile_userland.sh
```


To run, on a pi zero, either copy or use a network mount:
```
cd userland/raspicam/build_rpi0
./raspivid -t 0
```

## To run:

On Pi Zero
```
sudo ./stream
```

On other computer (eg with ip address or rpi0)

```
gst-launch-1.0 -v tcpclientsrc host=192.168.0.78 port=2222 ! jpegdec ! videoflip method="rotate-180" ! videoconvert ! autovideosink
```