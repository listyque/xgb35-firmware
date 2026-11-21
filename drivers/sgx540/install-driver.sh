#!/bin/sh
set -eu
archive=$(readlink -f "${1:?usage: install-driver.sh DRIVER.tar.gz}")
dest=/opt/sgx540
id=$(sha256sum "$archive" | cut -c1-16)
mkdir -p "$dest" /usr/local/bin
exec 8>"$dest/.install-lock"
flock -x 8
stage=$(mktemp -d "$dest/.install.XXXXXX")
trap 'rm -rf "$stage"' EXIT
if [ ! -d "$dest/$id" ]; then
    tar -xzf "$archive" -C "$stage"
    (cd "$stage" && sha256sum -c SHA256SUMS)
    chmod 755 "$stage"
    mv -T "$stage" "$dest/$id"
    stage=$(mktemp -d "$dest/.install.XXXXXX")
fi
if [ -x "$dest/current/sgx-run" ] && [ "$(readlink "$dest/current")" != "$id" ] &&
   [ -d /sys/module/pvrsrvkm_sgx540_130 ]; then
    "$dest/current/sgx-run" --stop
fi
ln -s "$id" "$stage/current"
mv -Tf "$stage/current" "$dest/current"
ln -sfn "$dest/current/sgx-run" /usr/local/bin/sgx-run
echo "Installed XGB35 SGX540 driver $id"
