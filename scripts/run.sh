#!/usr/bin/env bash
# Interactive serial GRUB menu. Ctrl-a x quits QEMU.
set -euo pipefail
source "$(dirname "$0")/env.sh"
disk=${1:?Usage: scripts/run.sh DISK [--usb]}
disk=$(realpath "$disk")
share=$TOOLS/usr/share
[[ -f $share/OVMF/OVMF_CODE_4M.fd ]] || share=/usr/share
vars=${disk%/*}/OVMF_VARS-interactive.fd
[[ -f $vars ]] || cp "$share/OVMF/OVMF_VARS_4M.fd" "$vars"
args=(-machine q35 -accel tcg -m 1024 -L "$share/qemu" -display none -vga none
  -device "VGA,romfile=$share/seabios/vgabios-stdvga.bin"
  -serial mon:stdio -no-reboot -net none
  -drive "if=pflash,format=raw,readonly=on,file=$share/OVMF/OVMF_CODE_4M.fd"
  -drive "if=pflash,format=raw,file=$vars")
if [[ ${2:-} == --usb ]]; then
  args+=(-device qemu-xhci -drive "if=none,id=stick,format=raw,file=$disk"
    -device usb-storage,drive=stick,removable=on)
else
  args+=(-drive "if=ide,format=raw,file=$disk")
fi
exec qemu-system-x86_64 "${args[@]}"
