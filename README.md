# Frappe: Fiducial recognition accelerated with parallel processing elements

A fiducial recognition library designed to run on the Raspberry Pi Zero up to Raspberry 3B+. Currently handles a fixed resolution of 640x480 pixels, and the dictionary ArUco 36h12.

## Acknowledgements
Thanks to the many people who have worked on reverse engineering the undocumented parts of the Broadcom SoC and providing tools and clues. Apologies if I've forgotten anyone.

* Herman Hermitage et. al. [VideoCore IV Programmers Manual](https://github.com/hermanhermitage/videocoreiv/wiki/VideoCore-IV-Programmers-Manual)
* Marcel Müller [vc4asm - Macro assembler for Broadcom VideoCore IV](https://github.com/maazl/vc4asm)
* Julian Brown [VC4 GCC Toolchain](https://github.com/itszor/vc4-toolchain)
* Seneral [VC4CV computer vision framework](https://github.com/Seneral/VC4CV)
* Daniel Stadelmann [OpenCL for VideoCore IV GPU](https://github.com/doe300/VC4CL)
* Kristina Brooks [Minimal Raspberry Pi VPU firmware](https://github.com/christinaa/rpi-open-firmware)
* Broadcom [VideoCore IV 3D Architecture Reference Manual](https://docs.broadcom.com/doc/12358545)
* https://github.com/Pro/raspi-toolchain
* https://github.com/dbhi/qus

## Introduction
Building apps natively on the RPi Zero that utilise the QPUs and VPU is extremely tedious. We need not just a native toolchain, but also the VC4 toolchain and the QPU assembler. For Frappe, we also need full source build of OpenCV.

The approach we have taken is:

* Use a standard RPi disk image for the golden source of headers and libraries
* Use kpartx to create loopback mounts to the image so we can work on it
* Enable Docker support for QEMU so we can chroot into an RPi disk image
* Build all the tools for crosscompilation in a Docker container
* Within the Docker container, chroot into the RPi image and apt install OpenCV requirements
* Within the Docker container, download, crosscompile and install OpenCV onto the RPi image
* Compile Frappe, using VC4 gcc and QPU assembler, and the system and OpenCV libraries on the RPi image

The resultant binaries and libraries are left in the build_rpi0 directory.

The compilation framework is standard cmake, with the addition of a `CMAKE_TOOLCHAIN_FILE` from 
https://github.com/Pro/raspi-toolchain with modifications, which makes sure the correct cross compilers are used.




## Build
To build the docker containers and RPI image, do:
```
./scripts/build_all.sh
```

To compile the Frappe library and basic apps, do:
```
./scripts/compile.sh
```


## Working on the RPi0
The updated image can be burned onto an SD card and used directly on the RPi0. For development ease, we use a USB-Ethernet adaptor and a USB-MicroB to USB-A adaptor to give the Zero a wired network connection. We set up an NFS share and mount that on the RPi0, ssh'ing into it.

Development then follows the pattern:

On the development PC, within the share
```
./scripts/compile.sh
```
On Zero, within the share, e.g.
```
sudo ./build_rpi0/stream
```

Getting things wrong with QPU or VPU code will frequently cause a complete lockup and usually need a power cycle.



## Dependencies
The library relies on the /dev/vcsm kernel interface for providing cached zero-copy shared buffers between CPU and GPU (VC4). This was removed from RPi kernels from 5.9 onwards, with future similar functionality to be provided by DMA-BUF, however this is currently much slower since it doesn't seem to support cached access to the shared buffer.

The latest Raspian OS image that ships with the required funtionality is raspios_lite_armhf-2021-01-12, with kernel 5.4.83-v7+




## Applications:
All applications need to be run with root permissions.

#### `min_detect`
Runs a single image repeatedly, collecting stats.

#### `small`
Runs a full directory of images, collecting stats.

#### `stream`
Connects to camera, runs detector on camera image, streams fiducial information over ZMQ. Also compresses images to MJPEG and streams them over TCP.


On Pi Zero
```
sudo ./build_rpi0/stream
```

On other computer (replace <> with RPi0 IP address)

```
gst-launch-1.0 -v tcpclientsrc host=<RPi0 IP address> port=2222 ! jpegdec ! videoflip method="rotate-180" ! videoconvert ! autovideosink
```

Access to fiducials is via ZMQ. Port 2223 is a publisher sending a continuous stream of
detected fiducials. The format each string of the stream is:
```

%lld %lld %d %d [%d %f %f %f %f %f %f %f %f]*\n
# with the bracketed part repeated by number of detections (including zero).

Fields are
<camera capture timestamp (us)>     #
<delta to send time (us)>           # This is the total processing time
<number of fiducials>
<fid processing time (us)>          # This is just frappe time

# Fiducials
<id> <p0.x> <p0.y> <p1.y> <p1.y> <p2.y> <p2.y> <p3.y> <p3.y>
<id> <p0.x> <p0.y> <p1.y> <p1.y> <p2.y> <p2.y> <p3.y> <p3.y>
<id> <p0.x> <p0.y> <p1.y> <p1.y> <p2.y> <p2.y> <p3.y> <p3.y>
...
<id> <p0.x> <p0.y> <p1.y> <p1.y> <p2.y> <p2.y> <p3.y> <p3.y>
```


