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


## Build

Make containers and image, this is a docker container with cross compile tools
for both the arm cpu and for the qpu and vpu to allow full cross compile of the
complete app. The image is a standard raspbian install image with a bunch
of necessities installed, used for a true source of headers, and for building
and installing OpenCV 4.5.2.

To build the docker containers and RPI image, do:
```
./scripts/build_all.sh
```

To compile the Frappe library and basic apps, do:
```
./scripts/compile.sh
```


## To run:

On Pi Zero
```
sudo ./stream
```

On other computer (eg with ip address of rpi0)

```
gst-launch-1.0 -v tcpclientsrc host=192.168.0.78 port=2222 ! jpegdec ! videoflip method="rotate-180" ! videoconvert ! autovideosink
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


