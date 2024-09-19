 /* fdtable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FDTABLE_H
#define FDTABLE_H

#include "array.h"
#include <unistd.h>

typedef struct _fdtable_entry
{
  array_entry_base base;

  int filetable_idx;
  int flags;
  int mode;

  off_t file_offset;

} fdtable_entry;

_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret);
_kernel_oserror *fdtable_close_file(int fd);
_kernel_oserror *fdtable_lseek(int fd, off_t offset, int whence, off_t *offset_ret);
_kernel_oserror *fdtable_read(int fd, void *buf, size_t count, int *bytes_read);
_kernel_oserror *fdtable_write(int fd, void *buf, size_t count, int *bytes_written);

#endif
