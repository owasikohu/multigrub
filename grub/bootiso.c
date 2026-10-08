/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/dl.h>
#include <grub/command.h>
#include <grub/misc.h>
GRUB_MOD_LICENSE ("GPLv3+");
#include "efi_io.c"
#include "cache.c"
#include "extract.c"
#include "chain.c"
#include "menu.c"
static grub_command_t hello, write_cmd, file_cmd, iso_cmd, chain_cmd, menu_cmd, boot_cmd;
static grub_err_t write_test (grub_command_t cmd __attribute__((unused)), int argc __attribute__((unused)), char **argv __attribute__((unused))) {
  struct scratch s; grub_err_t err;
  if(scratch_open(&s)) return grub_errno;
  grub_printf("[bootiso] writing /test.txt\n");
  err=invalidate_cache(s.root);
  if(!err) err=put_text(s.root,"/test.txt","written by UEFI file protocol\n");
  scratch_close(&s);
  if(!err) grub_printf("[bootiso] write success\n");
  return err;
}
static grub_err_t hello_test (grub_command_t cmd __attribute__((unused)),
                            int argc __attribute__((unused)),
                            char **argv __attribute__((unused)))
{
  grub_printf ("hello from custom grub module\n");
  return GRUB_ERR_NONE;
}
GRUB_MOD_INIT(hello)
{
  menu_cmd = grub_register_command("bootiso_menu",bootiso_menu_cmd,"DIRECTORY","Create ISO menu entries.");
  boot_cmd = grub_register_command("bootiso_boot",bootiso_boot_cmd,"ISO","Extract and chainload ISO.");
  chain_cmd = grub_register_command("chain_scratch",chain_scratch_cmd,0,"Chainload scratch EFI bootloader.");
  file_cmd = grub_register_command("extract_file",extract_file_cmd,"ISO SOURCE DESTINATION","Copy an ISO file to scratch.");
  iso_cmd = grub_register_command("extract_iso",extract_iso_cmd,"ISO","Extract ISO onto disposable scratch.");
  write_cmd = grub_register_command("write_test",write_test,0,"Write scratch test file.");
  hello = grub_register_command ("hello_test", hello_test, 0, "Test bootiso module.");
}
GRUB_MOD_FINI(hello) { grub_unregister_command (hello); grub_unregister_command(write_cmd); grub_unregister_command(file_cmd); grub_unregister_command(iso_cmd); grub_unregister_command(chain_cmd); grub_unregister_command(menu_cmd); grub_unregister_command(boot_cmd); }
