#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$BUILD"
archive=$BUILD/grub-2.12.tar.xz
if [[ ! -f $archive ]]; then
  curl --fail --location https://deb.debian.org/debian/pool/main/g/grub2/grub2_2.12.orig.tar.xz -o "$archive"
fi
echo "f3c97391f7c4eaa677a78e090c7e97e6dc47b16f655f04683ebd37bef7fe0faa  $archive" | sha256sum -c -
[[ -d $BUILD/grub-2.12 ]] || tar -xf "$archive" -C "$BUILD"
# GRUB 2.12 release omits this initially empty dependency file.
# Upstream ChangeLog documents it as empty until explicit overrides are needed.
[[ -f $BUILD/grub-2.12/grub-core/extra_deps.lst ]] || touch "$BUILD/grub-2.12/grub-core/extra_deps.lst"
mkdir -p "$BUILD/grub-build"
cd "$BUILD/grub-build"
if [[ ! -f Makefile ]]; then
  "$BUILD/grub-2.12/configure" --with-platform=efi --target=x86_64 \
    --disable-werror --disable-grub-mkfont --disable-nls --disable-device-mapper \
    --disable-efiemu > "$BUILD/configure.log" 2>&1
fi
if [[ ! -f grub-core/normal.mod || ! -f grub-mkstandalone ]]; then
  make AWK=gawk -j"${JOBS:-4}" > "$BUILD/build.log" 2>&1
fi
"$ROOT/scripts/module.sh"
printf 'GRUB source build complete\n'
