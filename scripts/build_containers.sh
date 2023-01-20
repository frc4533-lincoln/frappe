#!/bin/bash

# Use QUS project containers to register up-to-date QEMU that supports
# aarch64 running armv7.
# https://github.com/dbhi/qus

# Reset
docker run --rm --privileged aptman/qus -- -r
# Install
docker run --rm --privileged aptman/qus -s -- -p arm

# Build cross compilers
docker buildx build -f docker/Dockerfile.cross -t cross .
docker buildx build -f docker/Dockerfile.shell -t shell .


