/* SPDX-License-Identifier: GPL-3.0-or-later
 * Verify chainloader's device/path context, not just execution of an EFI image.
 */
#include <efi.h>
#include <efilib.h>
static unsigned char inb(unsigned short port) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v; }
static void outb(unsigned short port, unsigned char v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(port)); }
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_LOADED_IMAGE *loaded = 0;
  EFI_FILE_IO_INTERFACE *fs = 0;
  EFI_FILE_HANDLE root = 0, file = 0;
  EFI_GUID li = LOADED_IMAGE_PROTOCOL, sf = SIMPLE_FILE_SYSTEM_PROTOCOL;
  EFI_STATUS status;
  UINTN size = 64;
  char buffer[64] = {0};
  InitializeLib(image, st);
  status = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image, &li, (void**)&loaded);
  if (!EFI_ERROR(status))
    status = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, loaded->DeviceHandle, &sf, (void**)&fs);
  if (!EFI_ERROR(status)) status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
  if (!EFI_ERROR(status)) status = uefi_call_wrapper(root->Open, 5, root, &file,
      L"\\nested\\payload.txt", EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR(status)) status = uefi_call_wrapper(file->Read, 3, file, &size, buffer);
  if (file) uefi_call_wrapper(file->Close, 1, file);
  if (root) uefi_call_wrapper(root->Close, 1, root);
  const char *message = !EFI_ERROR(status) && size == 22 &&
      CompareMem(buffer, "payload from the ISO\n\0", 22) == 0
      ? "EFI payload read from scratch successfully\r\n"
      : "EFI payload FAILED to read scratch\r\n";
  for (const char *p = message; *p; ++p) {
    while (!(inb(0x3fd) & 0x20)) {}
    outb(0x3f8, *p);
  }
  uefi_call_wrapper(st->RuntimeServices->ResetSystem, 4, EfiResetShutdown, EFI_SUCCESS, 0, 0);
  return EFI_SUCCESS;
}
