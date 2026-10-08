#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
for source in "$ROOT/grub/"*.c "$ROOT/grub/"*.h; do
  name=${source##*/}
  [[ $name != bootiso.c ]] || name=hello.c
  target=$BUILD/grub-2.12/grub-core/hello/$name
  if ! cmp -s "$source" "$target"; then cp "$source" "$target"; fi
done
make AWK=gawk -C "$BUILD/grub-build/grub-core" -j"${JOBS:-4}" hello.mod command.lst > "$BUILD/module-build.log" 2>&1
