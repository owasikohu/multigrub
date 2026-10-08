/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/script_sh.h>
static grub_err_t bootiso_boot_cmd (grub_command_t cmd,int argc,char **args) {
  grub_err_t err=extract_iso_cmd(cmd,argc,args);
  if(!err) err=chain_scratch_cmd(cmd,0,0);
  return err;
}
/* GRUB shell single-quoted arguments: reject the two characters that escape
 * this representation. Other ISO filenames, including spaces and UTF-8, work.
 */
static int quotable (const char *name) {
  const unsigned char *p=(const unsigned char*)name;
  for(;*p;p++) if(*p<32 || *p=='\'' || *p=='\\') return 0;
  return 1;
}
struct menu_ctx { const char *dir; grub_err_t err; unsigned count; };
static int menu_hook (const char *name,const struct grub_dirhook_info *info,void *data) {
  struct menu_ctx *ctx=data;
  grub_size_t len=grub_strlen(name);
  char *source,*path;
  if(info->dir || len<4 || grub_strcasecmp(name+len-4,".iso")) return 0;
  if(!quotable(name)) {ctx->err=grub_error(GRUB_ERR_BAD_FILENAME,"ISO menu filename contains unsupported quoting characters");return 1;}
  path=grub_xasprintf("%s/%s",ctx->dir,name);
  source=path?grub_xasprintf("menuentry '%s' --id '%s' { bootiso_boot '%s'; }",name,name,path):0;
  grub_free(path);
  if(!source) {ctx->err=grub_errno;return 1;}
  grub_printf("[bootiso] ISO menu entry: %s\n",name);
  ctx->err=grub_script_execute_sourcecode(source);
  grub_free(source);ctx->count++;
  return ctx->err!=0;
}
static grub_err_t bootiso_menu_cmd (grub_command_t cmd __attribute__((unused)),int argc,char **args) {
  grub_device_t dev;
  grub_fs_t fs;
  grub_err_t err;
  struct menu_ctx ctx={argc?args[0]:"/iso",0,0};
  if(argc>1 || ctx.dir[0]!='/' || !quotable(ctx.dir)) return grub_error(GRUB_ERR_BAD_ARGUMENT,"bootiso_menu /iso");
  dev=grub_device_open(0);if(!dev) return grub_errno;
  fs=grub_fs_probe(dev);
  if(!fs) {grub_device_close(dev);return grub_errno;}
  err=fs->fs_dir(dev,ctx.dir,menu_hook,&ctx);
  grub_device_close(dev);
  if(ctx.err) return ctx.err;
  if(err) return err;
  if(!ctx.count) return grub_error(GRUB_ERR_FILE_NOT_FOUND,"no ISO files in %s",ctx.dir);
  grub_printf("[bootiso] listed %u ISO files\n",ctx.count);
  return GRUB_ERR_NONE;
}
