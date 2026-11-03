#!/bin/sh
# Keep the Ubuntu 22.04 runtime baseline without Python-based PPA helpers.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends ca-certificates curl gnupg git file make
key=C8EC952E2A0E1FBDC5090F6A2C277A0A352154E5
key_file=/usr/share/keyrings/parallel-xbox360-toolchain.asc
curl -fsSL "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x$key" -o "$key_file"
actual=$(gpg --batch --show-keys --with-colons "$key_file" | awk -F: '$1 == "fpr" { print $10; exit }')
[ "$actual" = "$key" ] || { echo 'Unexpected toolchain signing key' >&2; exit 1; }
printf '%s\n' "deb [signed-by=$key_file] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu jammy main" > /etc/apt/sources.list.d/parallel-xbox360-toolchain.list
apt-get update -qq
apt-get install -y --no-install-recommends gcc-14 g++-14 libfontconfig1-dev
