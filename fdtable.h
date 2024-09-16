 /* fdtable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FDTABLE_H
#define FDTABLE_H

#include "array.h"

typedef struct _fdtable_entry
{
  array_entry_base base;

  int filetable_idx;
  int flags;
  int mode;

} fdtable_entry;

_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret);
_kernel_oserror *fdtable_close_file(int fd);

#endif
