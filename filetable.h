/* filetable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FILETABLE_H
#define FILETABLE_H

#include "array.h"
#include "types.h"
#include <unistd.h>

typedef struct _openfiletable_entry
{
  array_entry_base base;

  int systemtable_idx;
  int flags;
  int mode;
  off_t file_offset;

  int ref_count;

} openfiletable_entry;

static inline openfiletable_entry *openfiletable_get_entry(int idx)
{
  array_t *open_filetable = &global.open_filetable;
  openfiletable_entry *openfile_entry = (openfiletable_entry *)open_filetable->entries;
  return &openfile_entry[idx];
}

_kernel_oserror *openfiletable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret);
_kernel_oserror *openfiletable_deref_entry(int openfiletable_idx);

typedef struct _systemfiletable_entry
{
  array_entry_base base;

  const char *filename;
  int ref_count;

  int file_handle;

} systemfiletable_entry;

_kernel_oserror *systemfiletable_open_file(const char *file, int flags, int mode, int *);
void openfiletable_print(void);

/* Return 0 for success, -1 for error.  */
_kernel_oserror *systemfiletable_close_file(int filetable_idx);

_kernel_oserror *systemfiletable_get_file_length(int filetable_idx, unsigned *length);
_kernel_oserror *systemfiletable_read_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_read);
_kernel_oserror *systemfiletable_write_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_written);
void systemfiletable_print(void);

#endif
