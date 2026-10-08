/* SPDX-License-Identifier: GPL-3.0-or-later
 * UEFI 2.x synchronous file protocol prefix, using GRUB's EFI ABI.
 * No gnu-efi runtime or private FAT implementation is linked.
 */
#ifndef BOOTISO_UEFI_FILE_H
#define BOOTISO_UEFI_FILE_H
#include <grub/efi/api.h>
typedef struct bi_file bi_file;
typedef grub_efi_status_t bi_status;
typedef grub_efi_uintn_t bi_uintn;
typedef grub_efi_char16_t bi_char16;
struct bi_file {
  grub_uint64_t revision;
  bi_status (__grub_efi_api *open)(bi_file *, bi_file **, bi_char16 *, grub_uint64_t, grub_uint64_t);
  bi_status (__grub_efi_api *close)(bi_file *);
  bi_status (__grub_efi_api *delete_file)(bi_file *);
  bi_status (__grub_efi_api *read)(bi_file *, bi_uintn *, void *);
  bi_status (__grub_efi_api *write)(bi_file *, bi_uintn *, void *);
  bi_status (__grub_efi_api *get_position)(bi_file *, grub_uint64_t *);
  bi_status (__grub_efi_api *set_position)(bi_file *, grub_uint64_t);
  bi_status (__grub_efi_api *get_info)(bi_file *, grub_guid_t *, bi_uintn *, void *);
  bi_status (__grub_efi_api *set_info)(bi_file *, grub_guid_t *, bi_uintn, void *);
  bi_status (__grub_efi_api *flush)(bi_file *);
};
struct bi_sfs {
  grub_uint64_t revision;
  bi_status (__grub_efi_api *open_volume)(struct bi_sfs *, bi_file **);
};
struct bi_info {
  grub_uint64_t size, file_size, physical_size;
  grub_efi_time_t create_time, access_time, modification_time;
  grub_uint64_t attribute;
  bi_char16 name[1];
};
#define BI_READ 1ULL
#define BI_WRITE 2ULL
#define BI_CREATE (1ULL << 63)
#define BI_DIRECTORY 0x10ULL
#define BI_MARKER "/.multigrub-scratch"
#define BI_MARKER_CONTENT "multigrub scratch v1\n"
#define BI_CACHE "/.bootiso-cache"
#endif
