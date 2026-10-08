/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/fs.h>
#include <grub/file.h>
#include <grub/device.h>
struct iso { grub_device_t dev; grub_fs_t fs; };
#include "label.c"
static grub_err_t iso_open (const char *path,struct iso *iso) {
  char *args[2]={(char*)"bootiso_loop",(char*)path};
  grub_memset(iso,0,sizeof(*iso));
  grub_printf("[bootiso] opening %s\n",path);
  if(grub_command_execute("loopback",2,args)) return grub_errno;
  iso->dev=grub_device_open("bootiso_loop");
  if(!iso->dev) return grub_errno;
  iso->fs=grub_fs_probe(iso->dev);
  if(!iso->fs) return grub_errno;
  if(grub_strcmp(iso->fs->name,"iso9660") && grub_strcmp(iso->fs->name,"udf"))
    return grub_error(GRUB_ERR_BAD_FS,"expected ISO9660 or UDF");
  grub_printf("[bootiso] %s mounted\n",iso->fs->name);
  return GRUB_ERR_NONE;
}
static void iso_close (struct iso *iso) {
  char *args[2]={(char*)"-d",(char*)"bootiso_loop"};
  grub_err_t saved=grub_errno;
  if(iso->dev) grub_device_close(iso->dev);
  grub_command_execute("loopback",2,args);
  grub_errno=saved;
}
static int valid_component (const char *name) {
  const char *p;
  grub_size_t n=grub_strlen(name);
  if(!n || n>255 || name[n-1]=='.' || name[n-1]==' ') return 0;
  for(p=name;*p;p++)
    if((unsigned char)*p<32 || grub_strchr("/\\:*?\"<>|",*p)) return 0;
  if(!grub_strcasecmp(name,".multigrub-scratch") || !grub_strcasecmp(name,".bootiso-cache")) return 0;
  return 1;
}
static int valid_path (const char *path) {
  char *copy,*p,*end;
  int valid=1;
  if(!path || path[0]!='/' || grub_strlen(path)>4096) return 0;
  copy=grub_strdup(path+1); if(!copy) return 0;
  p=copy;
  while(*p) {
    end=grub_strchr(p,'/'); if(end) *end=0;
    if(!valid_component(p)) {valid=0;break;}
    if(!end) break;
    p=end+1;
  }
  grub_free(copy); return valid;
}
static grub_err_t mkdir_path (bi_file *root,const char *path,int exclusive) {
  bi_file *dir=0;
  bi_status status=open_file(root,path,BI_READ,0,&dir);
  if(status==GRUB_EFI_SUCCESS) {
    dir->close(dir);
    if(exclusive) return grub_error(GRUB_ERR_BAD_FILENAME,"FAT name collision: %s",path);
    return GRUB_ERR_NONE;
  }
  if(status!=GRUB_EFI_NOT_FOUND) return efi_error("check directory",status);
  status=open_file(root,path,BI_READ|BI_WRITE|BI_CREATE,BI_DIRECTORY,&dir);
  if(status!=GRUB_EFI_SUCCESS) return efi_error("mkdir",status);
  return finish_file(dir);
}
static grub_err_t ensure_parents (bi_file *root,const char *path) {
  char *copy=grub_strdup(path),*p;
  if(!copy) return grub_errno;
  for(p=copy+1;*p;p++) if(*p=='/') {
    *p=0;
    if(mkdir_path(root,copy,0)) {grub_free(copy);return grub_errno;}
    *p='/';
  }
  grub_free(copy); return GRUB_ERR_NONE;
}
static grub_err_t copy_iso_file (bi_file *root,const char *src,const char *dest,int exclusive) {
  char *full=grub_xasprintf("(bootiso_loop)%s",src);
  grub_file_t input;
  bi_file *output=0;
  char *buffer;
  grub_ssize_t got;
  grub_err_t err=GRUB_ERR_NONE;
  if(!full) return grub_errno;
  input=grub_file_open(full,GRUB_FILE_TYPE_NONE|GRUB_FILE_TYPE_NO_DECOMPRESS);
  grub_free(full);
  if(!input) return grub_errno;
  if(input->size>0xffffffffULL) {grub_file_close(input);return grub_error(GRUB_ERR_OUT_OF_RANGE,"file exceeds FAT32 limit: %s",src);}
  if(exclusive) {
    bi_file *exists=0;
    bi_status status=open_file(root,dest,BI_READ,0,&exists);
    if(status==GRUB_EFI_SUCCESS) {exists->close(exists);grub_file_close(input);return grub_error(GRUB_ERR_BAD_FILENAME,"FAT name collision: %s",dest);}
    if(status!=GRUB_EFI_NOT_FOUND) {grub_file_close(input);return efi_error("check destination",status);}
  }
  buffer=grub_malloc(128*1024);
  if(!buffer) {grub_file_close(input);return grub_errno;}
  if(create_file(root,dest,&output)) {err=grub_errno;goto done;}
  grub_printf("[bootiso] extracting %s\n",src);
  while((got=grub_file_read(input,buffer,128*1024))>0)
    if(write_bytes(output,buffer,got)) {err=grub_errno;break;}
  if(got<0) err=grub_errno;
  if(err) output->close(output); else err=finish_file(output);
 done:
  grub_free(buffer);grub_file_close(input);return err;
}
/* Firmware enumeration is restarted after each removal to avoid skipped entries. */
static grub_err_t clean_directory (bi_file *dir,int top,unsigned depth);
static grub_err_t clean_directory_inner (bi_file *dir,int top,unsigned depth,char *buffer) {
  if(depth>64) return grub_error(GRUB_ERR_OUT_OF_RANGE,"scratch directory depth limit");
  for(;;) {
    struct bi_info *info=(struct bi_info*)buffer;
    bi_uintn size;
    bi_status status=dir->set_position(dir,0);
    bi_file *child=0;
    int found=0;
    if(status!=GRUB_EFI_SUCCESS) return efi_error("rewind directory",status);
    for(;;) {
      size=4096;
      status=dir->read(dir,&size,buffer);
      if(status!=GRUB_EFI_SUCCESS) return efi_error("read directory",status);
      if(!size) break;
      if(size<82 || info->size>size) return grub_error(GRUB_ERR_BAD_FS,"invalid EFI file info");
      if((info->name[0]=='.' && !info->name[1]) ||
         (info->name[0]=='.' && info->name[1]=='.' && !info->name[2])) continue;
      {
        grub_size_t j=0, max=(size-80)/2;
        while(j<max && info->name[j]) j++;
        if(j==max || j>255) return grub_error(GRUB_ERR_BAD_FS,"invalid EFI filename length");
      }
      if(top) {
        const char *marker=BI_MARKER+1;
        grub_size_t j=0;
        while(marker[j] && (info->name[j] == (unsigned char)marker[j] ||
              (info->name[j]>='A' && info->name[j]<='Z' &&
               info->name[j]+('a'-'A') == (unsigned char)marker[j]))) j++;
        if(!marker[j] && !info->name[j]) continue;
      }
      status=dir->open(dir,&child,info->name,BI_READ|BI_WRITE,0);
      if(status!=GRUB_EFI_SUCCESS) return efi_error("open for cleanup",status);
      if(info->attribute & BI_DIRECTORY) {
        if(clean_directory(child,0,depth+1)) {child->close(child);return grub_errno;}
      }
      status=child->delete_file(child);
      if(status!=GRUB_EFI_SUCCESS) return efi_error("delete scratch entry",status);
      found=1;break;
    }
    if(!found) return GRUB_ERR_NONE;
  }
}
static grub_err_t clean_directory (bi_file *dir,int top,unsigned depth) {
  char *buffer;
  grub_err_t err;
  if(depth>64) return grub_error(GRUB_ERR_OUT_OF_RANGE,"scratch directory depth limit");
  buffer=grub_malloc(4096);
  if(!buffer) return grub_errno;
  err=clean_directory_inner(dir,top,depth,buffer);
  grub_free(buffer);return err;
}
struct walk {
  struct iso *iso;
  bi_file *root;
  const char *path;
  unsigned depth;
  grub_uint64_t *bytes;
  unsigned *count;
  grub_err_t err;
};
static grub_err_t walk_dir (struct walk *ctx);
static int walk_hook (const char *name,const struct grub_dirhook_info *info,void *data) {
  struct walk *ctx=data,child=*ctx;
  char *path;
  if(!grub_strcmp(name,".") || !grub_strcmp(name,"..")) return 0;
  if(!valid_component(name)) {ctx->err=grub_error(GRUB_ERR_BAD_FILENAME,"unsupported FAT filename: %s",name);return 1;}
  if(++*ctx->count>100000) {ctx->err=grub_error(GRUB_ERR_OUT_OF_RANGE,"ISO entry limit");return 1;}
  path=grub_xasprintf("%s%s%s",ctx->path,grub_strcmp(ctx->path,"/")?"/":"",name);
  if(!path) {ctx->err=grub_errno;return 1;}
  if(grub_strlen(path)>4096) {ctx->err=grub_error(GRUB_ERR_OUT_OF_RANGE,"ISO path limit");goto done;}
  if(info->dir) {
    if(ctx->root && mkdir_path(ctx->root,path,1)) {ctx->err=grub_errno;goto done;}
    child.path=path;child.depth++;child.err=0;
    ctx->err=walk_dir(&child);
  } else if(ctx->root) ctx->err=copy_iso_file(ctx->root,path,path,1);
  else {
    char *full=grub_xasprintf("(bootiso_loop)%s",path);
    grub_file_t file=full?grub_file_open(full,GRUB_FILE_TYPE_NONE|GRUB_FILE_TYPE_NO_DECOMPRESS):0;
    grub_free(full);
    if(!file) ctx->err=grub_errno;
    else {
      if(file->size>0xffffffffULL) ctx->err=grub_error(GRUB_ERR_OUT_OF_RANGE,"file exceeds FAT32: %s",path);
      else *ctx->bytes+=file->size;
      grub_file_close(file);
    }
  }
 done:
  grub_free(path);return ctx->err!=0;
}
static grub_err_t walk_dir (struct walk *ctx) {
  grub_err_t err;
  if(ctx->depth>64) return grub_error(GRUB_ERR_OUT_OF_RANGE,"ISO depth limit (possible symlink cycle)");
  err=ctx->iso->fs->fs_dir(ctx->iso->dev,ctx->path,walk_hook,ctx);
  return ctx->err?ctx->err:err;
}
static grub_err_t extract_file_cmd (grub_command_t cmd __attribute__((unused)),int argc,char **args) {
  struct iso iso;
  struct scratch s;
  grub_err_t err;
  if(argc!=3) return grub_error(GRUB_ERR_BAD_ARGUMENT,"extract_file ISO SOURCE DESTINATION");
  if(!valid_path(args[1]) || !valid_path(args[2])) return grub_error(GRUB_ERR_BAD_FILENAME,"invalid absolute path");
  if(scratch_open(&s)) return grub_errno;
  err=iso_open(args[0],&iso);
  if(!err) err=invalidate_cache(s.root);
  if(!err) err=ensure_parents(s.root,args[2]);
  if(!err) err=copy_iso_file(s.root,args[1],args[2],0);
  iso_close(&iso);scratch_close(&s);
  if(!err) grub_printf("[bootiso] file copy success\n");
  return err;
}
static grub_err_t extract_iso_cmd (grub_command_t cmd __attribute__((unused)),int argc,char **args) {
  struct iso iso;
  struct scratch s;
  grub_err_t err;
  char *key;
  unsigned count=0;
  grub_uint64_t bytes=0;
  struct walk ctx={&iso,0,"/",0,&bytes,&count,0};
  if(argc!=1) return grub_error(GRUB_ERR_BAD_ARGUMENT,"extract_iso ISO");
  key=cache_key(args[0]);if(!key) return grub_errno;
  if(scratch_open(&s)) {grub_free(key);return grub_errno;}
  if(cache_matches(s.root,key)) {
    grub_printf("[bootiso] cache hit; reusing scratch\n");
    scratch_close(&s);grub_free(key);return GRUB_ERR_NONE;
  }
  if(grub_errno) {scratch_close(&s);grub_free(key);return grub_errno;}
  grub_printf("[bootiso] cache miss\n");
  err=iso_open(args[0],&iso);
  if(!err) err=walk_dir(&ctx);
  if(!err) err=check_capacity(s.root,bytes,count,0);
  if(!err) err=invalidate_cache(s.root);
  if(!err) err=clean_directory(s.root,1,0);
  if(!err) err=check_capacity(s.root,bytes,count,1);
  if(!err) {count=0;ctx.root=s.root;err=walk_dir(&ctx);}
  if(!err) err=copy_volume_label(&iso,s.root);
  if(!err && bootfile_exists(s.root)) {
    err=put_text(s.root,BI_CACHE,key);
    if(!err) grub_printf("[bootiso] cache committed\n");
  }
  if(err) grub_printf("[bootiso] extraction failed: %s\n",grub_errmsg);
  iso_close(&iso);scratch_close(&s);grub_free(key);
  if(!err) grub_printf("[bootiso] extracted %u entries\n",count);
  return err;
}
