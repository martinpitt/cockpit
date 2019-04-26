#!/bin/sh -ex
[ -e configure ] || ./autogen.sh
make -j$(nproc)
bots/image-prepare -v  rhel-8-0
