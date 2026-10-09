# Architecture investigation (GRUB 2.12, x86_64 EFI)

The target is an actual GPT disk, not an OS-visible emulated ISO. GRUB's
`loopback` is used only before handoff as a read-only file-backed disk.

- `grub-core/Makefile.core.def` defines module targets;
  `grub-core/hello/hello.c` demonstrates registration with
  `GRUB_MOD_INIT`, `grub_register_command`, and matching cleanup.
- `include/grub/efi/efi.h` exports `grub_efi_locate_handle` and
  `grub_efi_open_protocol`. The Simple File System GUID is in `efi/api.h`,
  but the file protocol structures are absent in this version. Our module
  declares the UEFI ABI locally using GRUB's `__grub_efi_api` calling convention.
- `grub_efidisk_get_device_handle` and `grub_efidisk_get_device_name` in
  `efi/disk.h` connect firmware handles to GRUB disk names.
- `grub-core/disk/loopback.c` registers file-backed disks; `grub_device_open`,
  `grub_fs_probe`, `fs_dir`, and `grub_file_open/read` provide ISO traversal.
  `grub-core/fs/iso9660.c` supports Rock Ridge/Joliet and `fs_label`.
- `grub-core/loader/efi/chainloader.c` creates a file device path, sets
  the loaded image's device handle, and delegates to firmware `StartImage`.
  Reuse this command rather than implementing a second EFI loader.
- OVMF discovers FAT on GPT partitions. Tests use writable raw images,
  separate pflash code/variables, TCG, serial output, and host-side mtools.
  No host mounts, privileged loop devices, Secure Boot, or GUI are needed.

## Feasibility and limits

Firmware supplies FAT writes, directory creation, deletion, flush, and
filesystem label metadata. A persistent scratch marker identifies the target
independently of its mutable label. Never choose the first FAT volume.
The scratch area is disposable; cleaning requires explicit ownership marker.
Writing finishes before chainloading, while boot services remain available.
GRUB's disk read cache must be invalidated before it reads firmware-written FAT.

Extraction preserves names, hierarchy and bytes subject to FAT's restrictions:
4 GiB maximum file size, case insensitivity, reserved/invalid names, and lack
of POSIX metadata. GRUB's public directory hook does not expose symlink type;
The compatibility preflight inspects Rock Ridge SL/CE metadata and rejects
all ISO-level symlinks explicitly; it does not silently dereference them.
A recursive directory cycle must be bounded and must not yield a success cache.

This does not guarantee bootability of every Linux ISO. A distro may search for
an ISO9660 label, require an optical device, or expect a filesystem unsupported
by its initramfs. FAT labels allow only 11 compatible characters, versus 32 for
ISO9660 volume identifiers. Do not truncate a label and imply equivalence.
Investigate such incompatibilities rather than altering distro kernel arguments.

## Minimal source integration

Build an authenticated, checksum-pinned GRUB 2.12 release. For the first prototype,
overlay only the existing `hello` module source in the generated build tree with
our module; its internal module name remains `hello`. This reuses upstream build
rules and changes no GRUB core code. The public commands have project-specific
names. Keep the source overlay in `grub/` and reapply it on builds.
