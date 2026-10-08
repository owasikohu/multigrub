/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/crypto.h>
struct time_match {const char *name; grub_int64_t mtime; int found;};
static int time_hook (const char *name,const struct grub_dirhook_info *info,void *data) {
  struct time_match *m=data;
  if(!grub_strcmp(m->name,name) && info->mtimeset) {m->mtime=info->mtime;m->found=1;return 1;}
  return 0;
}
static char *cache_key (const char *path) {
  const gcry_md_spec_t *md=grub_crypto_lookup_md_by_name("SHA256");
  grub_file_t file;
  void *ctx;
  char *buffer,*key=0,*parent,*slash;
  char hex[65];
  grub_uint64_t size;
  grub_ssize_t got;
  unsigned i;
  struct time_match t={0,0,0};
  if(!md) {grub_error(GRUB_ERR_BAD_ARGUMENT,"SHA256 unavailable (insmod gcry_sha256)");return 0;}
  file=grub_file_open(path,GRUB_FILE_TYPE_NONE|GRUB_FILE_TYPE_NO_DECOMPRESS);
  if(!file) return 0;
  size=file->size;
  /* Directory hooks expose source mtime where supported. Hash remains authoritative. */
  parent=grub_strdup(grub_strchr(path,')')?grub_strchr(path,')')+1:path);
  if(parent) {
    slash=grub_strrchr(parent,'/');
    if(slash) {
      t.name=slash+1;*slash=0;
      file->fs->fs_dir(file->device,*parent?parent:"/",time_hook,&t);
      grub_errno=GRUB_ERR_NONE;
    }
    grub_free(parent);
  }
  ctx=grub_malloc(md->contextsize);buffer=grub_malloc(128*1024);
  if(!ctx || !buffer) {grub_free(ctx);grub_free(buffer);grub_file_close(file);return 0;}
  md->init(ctx);
  while((got=grub_file_read(file,buffer,128*1024))>0) md->write(ctx,buffer,got);
  if(got>=0 && file->offset==size) {
    const unsigned char *digest;
    md->final(ctx);digest=md->read(ctx);
    for(i=0;i<32;i++) grub_snprintf(hex+2*i,3,"%02x",digest[i]);
    hex[64]=0;
    key=grub_xasprintf("bootiso-cache-v1\npath=%s\nsize=%llu\nmtime=%s%lld\nsha256=%s\n",path,
        (unsigned long long)size,t.found?"":"unknown:",(long long)t.mtime,hex);
  } else if(got>=0) grub_error(GRUB_ERR_IO,"short ISO read during hashing");
  grub_free(ctx);grub_free(buffer);grub_file_close(file);return key;
}
static int cache_matches (bi_file *root,const char *key) {
  bi_file *file=0;
  char *buffer;
  bi_uintn length=grub_strlen(key),size=length+1;
  bi_status status;
  int same=0;
  if(open_file(root,BI_CACHE,BI_READ,0,&file)!=GRUB_EFI_SUCCESS) return 0;
  buffer=grub_malloc(size);
  if(!buffer) {file->close(file);return 0;}
  status=file->read(file,&size,buffer);
  same=status==GRUB_EFI_SUCCESS && size==length && !grub_memcmp(buffer,key,length);
  file->close(file);grub_free(buffer);
  if(same) same=bootfile_exists(root);
  return same;
}
static grub_err_t invalidate_cache (bi_file *root) {
  bi_file *file=0;
  bi_status status=open_file(root,BI_CACHE,BI_READ|BI_WRITE,0,&file);
  if(status==GRUB_EFI_NOT_FOUND) return GRUB_ERR_NONE;
  if(status!=GRUB_EFI_SUCCESS) return efi_error("open cache",status);
  status=file->delete_file(file);
  return status==GRUB_EFI_SUCCESS?GRUB_ERR_NONE:efi_error("invalidate cache",status);
}
