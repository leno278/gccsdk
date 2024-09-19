/* filetable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FILETABLE_H
#define FILETABLE_H

#include "array.h"
#include <unistd.h>

typedef struct _filetable_entry
{
  array_entry_base base;

  const char *filename;
  int ref_count;

  int file_handle;

} filetable_entry;

_kernel_oserror *filetable_open_file(const char *file, int flags, int mode, int *);

/* Return 0 for success, -1 for error.  */
_kernel_oserror *filetable_close_file(int filetable_idx);

_kernel_oserror *filetable_get_file_length(int filetable_idx, unsigned *length);
_kernel_oserror *filetable_read_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_read);
_kernel_oserror *filetable_write_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_written);

#endif
