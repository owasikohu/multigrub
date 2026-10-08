#!/usr/bin/env bash
# Fetch a stock Linux ISO without altering its kernel, initramfs or boot config.
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$BUILD/linux"
cd "$BUILD/linux"
iso=alpine-standard-3.22.3-x86_64.iso
expected=4e05fdcf5d0cc8e7bd404d4512884bcce5f40f046f4adecbc84b06b83477cd1d
if [[ -f $iso ]] && echo "$expected  $iso" | sha256sum -c -; then
  echo "$PWD/$iso"
  exit 0
fi
base=https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/x86_64
curl --fail --location "$base/$iso.sha256" -o "$iso.sha256"
curl --fail --location "$base/$iso" -o "$iso"
sha256sum -c "$iso.sha256"
echo "$expected  $iso" | sha256sum -c -
echo "$PWD/$iso"
