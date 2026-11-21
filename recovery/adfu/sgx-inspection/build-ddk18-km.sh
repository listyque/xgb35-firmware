#!/bin/sh
set -eu
if [ -d /opt/gfx_linux_ddk-master ]; then
    previous=$(mktemp -d /opt/sgx-ddk-previous.XXXXXX)
    mv /opt/gfx_linux_ddk-master "$previous/"
fi
cd /work
exec sh /work/reference-repro/build-reference.sh