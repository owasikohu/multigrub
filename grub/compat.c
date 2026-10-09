/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GRUB dirhook omits file type. Inspect ISO9660 SUSP/SL before following links.
 * This reads metadata only; extraction still uses GRUB's filesystem API. */
#include <grub/disk.h>
static const char *failure_code;
static grub_err_t classified (const char *code,grub_err_t err,const char *message) {
  failure_code=code;
  grub_printf("[bootiso] %s: %s\n",code,message);
  return grub_error(err,"%s: %s",code,message);
}
static grub_uint32_t le32 (const grub_uint8_t *p) {
  return (grub_uint32_t)p[0]|((grub_uint32_t)p[1]<<8)|((grub_uint32_t)p[2]<<16)|((grub_uint32_t)p[3]<<24);
}
static grub_err_t rr_area (grub_disk_t disk,const grub_uint8_t *p,unsigned size,unsigned hops,const char *path) {
  unsigned pos=0;
  while(pos+4<=size && p[pos]) {
    const grub_uint8_t *e=p+pos;unsigned n=e[2];
    if(n<4 || n>size-pos) return grub_error(GRUB_ERR_BAD_FS,"invalid SUSP entry: %s",path);
    if(e[0]=='S' && e[1]=='T') break;
    if(e[0]=='S' && e[1]=='L') {
      grub_printf("[bootiso] Unsupported symbolic link (ISO record path): %s\n",path);
      return classified("SYMLINK_UNSUPPORTED",GRUB_ERR_BAD_FILE_TYPE,"Rock Ridge symbolic links cannot be preserved safely on FAT32");
    }
    if(e[0]=='C' && e[1]=='E') {
      grub_uint32_t len,offset;grub_uint8_t *area;grub_err_t err;
      if(n<28 || hops>=8) return grub_error(GRUB_ERR_BAD_FS,"invalid or cyclic SUSP continuation");
      len=le32(e+20);offset=le32(e+12);
      if(len<4 || len>65536 || offset>=2048) return grub_error(GRUB_ERR_BAD_FS,"SUSP continuation bounds");
      area=grub_malloc(len);if(!area) return grub_errno;
      err=grub_disk_read(disk,(grub_disk_addr_t)le32(e+4)*4,offset,len,area);
      if(!err) err=rr_area(disk,area,len,hops+1,path);
      grub_free(area);if(err) return err;
    }
    pos+=n;
  }
  return GRUB_ERR_NONE;
}
static grub_err_t rr_dir (grub_disk_t disk,grub_uint32_t block,grub_uint32_t size,
                          const char *parent,unsigned depth,unsigned *count,unsigned *skip) {
  grub_uint64_t off=0;grub_err_t err;
  if(depth>64) return grub_error(GRUB_ERR_BAD_FS,"ISO metadata directory depth limit");
  while(off<size) {
    grub_uint8_t record[255],n;unsigned start;char *path;
    err=grub_disk_read(disk,(grub_disk_addr_t)block*4,off,1,&n);if(err) return err;
    if(!n) {off=(off/2048+1)*2048;continue;}
    if(n<34 || n>size-off || n>2048-off%2048) return grub_error(GRUB_ERR_BAD_FS,"invalid ISO directory record");
    err=grub_disk_read(disk,(grub_disk_addr_t)block*4,off,n,record);if(err) return err;
    off+=n;
    if(record[32]>n-33) return grub_error(GRUB_ERR_BAD_FS,"invalid ISO record name");
    if(++*count>100000) return grub_error(GRUB_ERR_BAD_FS,"ISO metadata entry limit");
    start=33+record[32]+(!(record[32]&1));
    /* SP in root '.' declares bytes preceding SUSP entries. */
    if(depth==0 && record[32]==1 && record[33]==0 && start+7<=n &&
       record[start]=='S' && record[start+1]=='P' && record[start+2]==7 &&
       record[start+4]==0xbe && record[start+5]==0xef) *skip=record[start+6];
    if(record[32]==1 && record[33]<=1) continue;
    path=grub_xasprintf("%s/%.*s",parent,(int)record[32],record+33);if(!path) return grub_errno;
    if(*skip!=~0U && start+*skip<n) err=rr_area(disk,record+start+*skip,n-start-*skip,0,path);
    else err=GRUB_ERR_NONE;
    if(!err && (record[25]&2)) err=rr_dir(disk,le32(record+2)+record[1],le32(record+10),path,depth+1,count,skip);
    grub_free(path);if(err) return err;
  }
  return GRUB_ERR_NONE;
}
static grub_err_t check_symlinks (struct iso *iso) {
  grub_uint8_t pvd[2048];unsigned count=0,skip=~0U;grub_err_t err;
  if(grub_strcmp(iso->fs->name,"iso9660"))
    return classified("SYMLINK_UNSUPPORTED",GRUB_ERR_BAD_FILE_TYPE,"UDF symbolic-link compatibility is not verified");
  err=grub_disk_read(iso->dev->disk,16*4,0,sizeof(pvd),pvd);if(err) return err;
  if(pvd[0]!=1 || grub_memcmp(pvd+1,"CD001",5) || pvd[128]!=0 || pvd[129]!=8)
    return grub_error(GRUB_ERR_BAD_FS,"unsupported ISO primary descriptor");
  return rr_dir(iso->dev->disk,le32(pvd+158)+pvd[157],le32(pvd+166),"",0,&count,&skip);
}
struct loader_name { const char *wanted; char *actual; unsigned matches; int directory; };
static int loader_hook (const char *name,const struct grub_dirhook_info *info,void *data) {
  struct loader_name *match=data;
  if(!grub_strcasecmp(name,match->wanted) && !!info->dir==match->directory) {
    match->matches++;
    if(!match->actual) match->actual=grub_strdup(name);
  }
  return 0;
}
static grub_err_t check_loader (struct iso *iso) {
  const char *components[]={"EFI","BOOT","BOOTX64.EFI"};
  char *path=grub_strdup("/"),*full;unsigned i;
  grub_file_t file;
  if(!path) return grub_errno;
  /* ISO Rock Ridge names are case sensitive; destination FAT is not. */
  for(i=0;i<3;i++) {
    struct loader_name match={components[i],0,0,i<2};
    grub_err_t err=iso->fs->fs_dir(iso->dev,path,loader_hook,&match);
    char *next;
    if(err || !match.actual || match.matches!=1) {
      grub_free(match.actual);grub_free(path);
      if(match.matches>1) return grub_error(GRUB_ERR_BAD_FILENAME,"FAT name collision in EFI loader path");
      grub_errno=GRUB_ERR_NONE;
      return classified("NO_EFI_LOADER",GRUB_ERR_FILE_NOT_FOUND,"Unsupported generic UEFI boot: /EFI/BOOT/BOOTX64.EFI is missing or unreadable");
    }
    next=grub_xasprintf("%s%s%s",path,i?"/":"",match.actual);
    grub_free(path);grub_free(match.actual);path=next;
    if(!path) return grub_errno;
  }
  full=grub_xasprintf("(bootiso_loop)%s",path);grub_free(path);
  if(!full) return grub_errno;
  file=grub_file_open(full,GRUB_FILE_TYPE_NONE|GRUB_FILE_TYPE_NO_DECOMPRESS);grub_free(full);
  if(!file) {grub_errno=GRUB_ERR_NONE;return classified("NO_EFI_LOADER",GRUB_ERR_FILE_NOT_FOUND,"/EFI/BOOT/BOOTX64.EFI cannot be read");}
  if(!file->size) {grub_file_close(file);return classified("NO_EFI_LOADER",GRUB_ERR_BAD_FILE_TYPE,"/EFI/BOOT/BOOTX64.EFI is empty");}
  grub_file_close(file);return GRUB_ERR_NONE;
}
static grub_err_t too_large (const char *path,grub_uint64_t size) {
  grub_printf("[bootiso] Unsupported:\nISO contains a file larger than FAT32 maximum file size.\n%s\n%llu\n",path,(unsigned long long)size);
  return classified("FILE_TOO_LARGE",GRUB_ERR_OUT_OF_RANGE,"file exceeds 0xffffffff bytes");
}
