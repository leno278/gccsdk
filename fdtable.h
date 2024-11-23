 /* fdtable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FDTABLE_H
#define FDTABLE_H

#include "array.h"
#include "types.h"
#include <unistd.h>

typedef struct _fdtable_entry
{
  array_entry_base base;

  int opentable_idx;

} fdtable_entry;

fdtable_entry *get_fdtable_entry(app_object *app, int fd);
void fdtable_destroy(array_t *);
_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret);
_kernel_oserror *fdtable_close_file(int fd);
_kernel_oserror *fdtable_lseek(int fd, off_t offset, int whence, off_t *offset_ret);
_kernel_oserror *fdtable_read(int fd, void *buf, size_t count, int *bytes_read);
_kernel_oserror *fdtable_write(int fd, void *buf, size_t count, int *bytes_written);

_kernel_oserror *
fdtable_get_info(int fd,
		 int *type,
		 const char **name,
		 void **load,
		 void **exec,
		 off_t *length,
		 unsigned *attributes);

_kernel_oserror *
fdtable_get_lock(int fd, int *type, int *whence, off_t *start, off_t *len);
_kernel_oserror *
fdtable_set_lock(int fd, int *type, int *whence, off_t *start, off_t *len);
_kernel_oserror *fdtable_fsync(int fd);

void fdtable_print(void);

#endif
