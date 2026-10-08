/* SPDX-License-Identifier: GPL-3.0-or-later
 * Included by bootiso.c: synchronous firmware file operations only.
 */
#include "uefi_file.h"
#include <grub/efi/efi.h>
#include <grub/efi/disk.h>
#include <grub/charset.h>
#include <grub/mm.h>
#include <grub/env.h>
static grub_guid_t sfs_guid = GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static grub_guid_t info_guid = {0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static grub_guid_t label_guid = {0xdb47d7d3,0xfe81,0x11d3,{0x9a,0x35,0,0x90,0x27,0x3f,0xc1,0x4d}};
struct scratch { bi_file *root; char *disk; };
static grub_err_t efi_error (const char *op, bi_status status) {
  return grub_error (GRUB_ERR_IO, "[bootiso] %s: EFI status 0x%llx", op, (unsigned long long) status);
}
static bi_char16 *efi_path (const char *name) {
  grub_size_t len = grub_strlen(name), i;
  bi_char16 *p = grub_malloc((len+1)*sizeof(*p));
  grub_ssize_t n;
  if (!p) return 0;
  n = grub_utf8_to_utf16(p, len, (const grub_uint8_t *)name, len, 0);
  if (n < 0) { grub_free(p); grub_error(GRUB_ERR_BAD_FILENAME,"invalid UTF-8 path"); return 0; }
  p[n] = 0;
  for (i=0;i<(grub_size_t)n;i++) if(p[i]=='/') p[i]='\\';
  return p;
}
static bi_status open_file (bi_file *root, const char *name, grub_uint64_t mode,
                            grub_uint64_t attr, bi_file **out) {
  bi_char16 *p=efi_path(name);
  bi_status status;
  if (!p) return GRUB_EFI_OUT_OF_RESOURCES;
  status=root->open(root,out,p,mode,attr); grub_free(p); return status;
}
static void scratch_close (struct scratch *s) {
  if (s->root) s->root->close(s->root);
  grub_free(s->disk); s->root=0; s->disk=0;
}
static grub_err_t scratch_open (struct scratch *s) {
  grub_efi_handle_t *handles;
  bi_uintn count=0,i;
  grub_memset(s,0,sizeof(*s));
  handles=grub_efi_locate_handle(GRUB_EFI_BY_PROTOCOL,&sfs_guid,0,&count);
  if (!handles) return grub_error(GRUB_ERR_UNKNOWN_DEVICE,"no EFI filesystems");
  for(i=0;i<count;i++) {
    struct bi_sfs *fs=grub_efi_open_protocol(handles[i],&sfs_guid,GRUB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    bi_file *root=0,*marker=0;
    char contents[sizeof(BI_MARKER_CONTENT)];
    bi_uintn size=sizeof(contents);
    if (!fs || fs->open_volume(fs,&root)!=GRUB_EFI_SUCCESS) continue;
    if (open_file(root,BI_MARKER,BI_READ,0,&marker)==GRUB_EFI_SUCCESS) {
      bi_status status=marker->read(marker,&size,contents);
      marker->close(marker);
      if(status==GRUB_EFI_SUCCESS && size==sizeof(BI_MARKER_CONTENT)-1 &&
         grub_memcmp(contents,BI_MARKER_CONTENT,size)==0) {
        if(s->root) { root->close(root); grub_free(handles); scratch_close(s);
          return grub_error(GRUB_ERR_BAD_DEVICE,"multiple scratch markers; refusing writes"); }
        s->root=root;
        /* Upstream's parameter is declared handle*, but consumes the handle itself. */
        s->disk=grub_efidisk_get_device_name((grub_efi_handle_t *)handles[i]);
        if(!s->disk) { grub_free(handles); scratch_close(s); return grub_errno; }
        continue;
      }
    }
    root->close(root);
  }
  grub_free(handles);
  if(!s->root) return grub_error(GRUB_ERR_UNKNOWN_DEVICE,"owned scratch filesystem not found");
  grub_env_set("bootiso_scratch",s->disk);
  grub_printf("[bootiso] scratch filesystem found (%s)\n",s->disk);
  return GRUB_ERR_NONE;
}
/* Replace files, never append to old contents. Partial files remain uncached on failure. */
static grub_err_t create_file (bi_file *root,const char *path,bi_file **out) {
  bi_file *old=0;
  bi_status status=open_file(root,path,BI_READ|BI_WRITE,0,&old);
  if(status==GRUB_EFI_SUCCESS) {
    status=old->delete_file(old); /* Always closes old, including on failure. */
    if(status!=GRUB_EFI_SUCCESS) return efi_error("delete old file",status);
  } else if(status!=GRUB_EFI_NOT_FOUND) return efi_error("open old file",status);
  status=open_file(root,path,BI_READ|BI_WRITE|BI_CREATE,0,out);
  return status==GRUB_EFI_SUCCESS ? GRUB_ERR_NONE : efi_error("create file",status);
}
static grub_err_t write_bytes (bi_file *file,void *data,grub_size_t length) {
  bi_uintn size=length;
  bi_status status=file->write(file,&size,data);
  if(status!=GRUB_EFI_SUCCESS) return efi_error("write",status);
  if(size!=length) return grub_error(GRUB_ERR_IO,"short EFI write");
  return GRUB_ERR_NONE;
}
static grub_err_t finish_file (bi_file *file) {
  bi_status status=file->flush(file), closed=file->close(file);
  if(status!=GRUB_EFI_SUCCESS) return efi_error("flush",status);
  if(closed!=GRUB_EFI_SUCCESS) return efi_error("close",closed);
  return GRUB_ERR_NONE;
}
static grub_err_t put_text (bi_file *root,const char *path,const char *text) {
  bi_file *file=0;
  if(create_file(root,path,&file)) return grub_errno;
  if(write_bytes(file,(void*)text,grub_strlen(text))) { file->close(file); return grub_errno; }
  return finish_file(file);
}

/* Query firmware capacity rather than writing or parsing FAT metadata. */
struct bi_fs_info {
  grub_uint64_t size;
  grub_uint8_t read_only;
  grub_uint64_t volume_size,free_space;
  grub_uint32_t block_size;
  bi_char16 label[1];
};
static grub_err_t check_capacity (bi_file *root,grub_uint64_t bytes,unsigned count,int empty) {
  char buffer[4096] __attribute__((aligned(8)));
  struct bi_fs_info *info=(struct bi_fs_info*)buffer;
  grub_guid_t guid={0x09576e93,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
  bi_uintn size=sizeof(buffer);
  bi_status status=root->get_info(root,&guid,&size,buffer);
  grub_uint64_t required=bytes+(grub_uint64_t)count*8192;
  if(status!=GRUB_EFI_SUCCESS) return efi_error("query scratch capacity",status);
  if(size<38) return grub_error(GRUB_ERR_BAD_FS,"short filesystem info");
  if(info->read_only) return grub_error(GRUB_ERR_ACCESS_DENIED,"scratch is read-only");
  if(required>(empty?info->free_space:info->volume_size))
    return grub_error(GRUB_ERR_OUT_OF_RANGE,"scratch capacity insufficient");
  return GRUB_ERR_NONE;
}
static int bootfile_exists (bi_file *root) {
  bi_file *file=0;
  char buffer[1024] __attribute__((aligned(8)));
  struct bi_info *info=(struct bi_info*)buffer;
  bi_uintn size=sizeof(buffer);
  int valid=0;
  if(open_file(root,"/EFI/BOOT/BOOTX64.EFI",BI_READ,0,&file)!=GRUB_EFI_SUCCESS) return 0;
  if(file->get_info(file,&info_guid,&size,buffer)==GRUB_EFI_SUCCESS && size>=82)
    valid=!(info->attribute & BI_DIRECTORY) && info->file_size>0;
  file->close(file);return valid;
}
