/* SPDX-License-Identifier: GPL-3.0-or-later */
static grub_err_t copy_volume_label (struct iso *iso,bi_file *root) {
  char *label=0;
  bi_char16 text[12];
  grub_size_t i,len;
  bi_status status;
  if(!iso->fs->fs_label) {grub_printf("[bootiso] no volume label API\n");return GRUB_ERR_NONE;}
  if(iso->fs->fs_label(iso->dev,&label)) return grub_errno;
  if(!label) return GRUB_ERR_NONE;
  grub_printf("[bootiso] ISO Volume ID: %s\n",label);
  len=grub_strlen(label);
  if(!len || len>11) {
    grub_printf("[bootiso] label not copied: FAT requires 1..11 compatible characters\n");
    grub_free(label);return GRUB_ERR_NONE;
  }
  for(i=0;i<len;i++) {
    unsigned char c=label[i];
    if(c<32 || c>126 || grub_strchr("\"*+,./:;<=>?[\\]|",c) || (c>='a' && c<='z')) {
      grub_printf("[bootiso] label not copied: exact label cannot be represented safely by FAT\n");
      grub_free(label);return GRUB_ERR_NONE;
    }
    text[i]=c;
  }
  if(label[len-1]==' ') {
    grub_printf("[bootiso] label not copied: trailing space\n");grub_free(label);return GRUB_ERR_NONE;
  }
  text[len]=0;
  status=root->set_info(root,&label_guid,(len+1)*sizeof(text[0]),text);
  grub_free(label);
  if(status!=GRUB_EFI_SUCCESS) return efi_error("set volume label",status);
  status=root->flush(root);
  if(status!=GRUB_EFI_SUCCESS) return efi_error("flush label",status);
  grub_printf("[bootiso] scratch volume label copied\n");return GRUB_ERR_NONE;
}
