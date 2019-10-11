#!/bin/sh -ex
[ -d bots ]  || git clone --depth=1 https://github.com/cockpit-project/bots/
[ -e configure ] || ./autogen.sh
make -j$(nproc)
test/image-prepare -v rhel-8-0
