/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/disk.h>
/* Same operation as pinned GRUB 2.12 kern/disk.c's non-exported helper.
 * The cache table is exported. Firmware writes bypass GRUB's cache; invalidate
 * unlocked entries before probing FAT, after all input files are closed.
 */
static void flush_grub_cache (void) {
  unsigned i;
  for(i=0;i<GRUB_DISK_CACHE_NUM;i++) {
    struct grub_disk_cache *c=grub_disk_cache_table+i;
    if(c->data && !c->lock) {grub_free(c->data);c->data=0;}
  }
}
static grub_err_t chain_scratch_inner (grub_command_t cmd __attribute__((unused)),
                                    int argc __attribute__((unused)),
                                    char **args __attribute__((unused))) {
  struct scratch s;
  bi_file *efi=0;
  bi_status status;
  char *path,*argv[1],*saved_root;
  grub_err_t err;
  if(scratch_open(&s)) return grub_errno;
  status=open_file(s.root,"/EFI/BOOT/BOOTX64.EFI",BI_READ,0,&efi);
  if(status!=GRUB_EFI_SUCCESS) {scratch_close(&s);return classified("NO_EFI_LOADER",GRUB_ERR_FILE_NOT_FOUND,"scratch /EFI/BOOT/BOOTX64.EFI is missing or unreadable");}
  status=efi->close(efi);
  if(status!=GRUB_EFI_SUCCESS) {scratch_close(&s);return efi_error("close EFI",status);}
  saved_root=grub_strdup(grub_env_get("root")?grub_env_get("root"):"");
  if(!saved_root) {scratch_close(&s);return grub_errno;}
  /* GRUB 2.12 chainloader derives DeviceHandle from root, not the file path. */
  grub_env_set("root",s.disk);
  path=grub_xasprintf("(%s)/EFI/BOOT/BOOTX64.EFI",s.disk);
  scratch_close(&s);
  if(!path) {grub_env_set("root",saved_root);grub_free(saved_root);return grub_errno;}
  flush_grub_cache();
  grub_printf("[bootiso] chainloading %s\n",path);
  argv[0]=path;
  err=grub_command_execute("chainloader",1,argv);
  grub_free(path);
  if(err) {failure_code="CHAINLOAD_FAILED";grub_printf("[bootiso] CHAINLOAD_FAILED: %s\n",grub_errmsg);}
  else {
    err=grub_command_execute("boot",0,0);
    failure_code="BOOTLOADER_RETURNED";
    grub_printf("[bootiso] BOOTLOADER_RETURNED: %s\n",err?grub_errmsg:"EFI bootloader returned to GRUB");
    if(!err) err=grub_error(GRUB_ERR_BAD_OS,"BOOTLOADER_RETURNED");
  }
  grub_env_set("root",saved_root);grub_free(saved_root);
  return err;
}

static grub_err_t chain_scratch_cmd (grub_command_t cmd,int argc,char **args) {
  grub_err_t err;
  failure_code=0;
  err=chain_scratch_inner(cmd,argc,args);
  if(err && !failure_code) grub_printf("[bootiso] CHAINLOAD_FAILED: %s\n",grub_errmsg);
  return err;
}
