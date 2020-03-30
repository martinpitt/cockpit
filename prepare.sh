#!/bin/sh -ex
[ -d bots ]  || git clone --depth=1 https://github.com/cockpit-project/bots/
[ -e configure ] || ./autogen.sh
make -j$(nproc)
test/image-prepare -vq rhel-8-2
bots/image-customize -v -i httpd -r 'dnf install -y --enablerepo=epel ansible' rhel-8-2
