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

  bool close_on_exec;

} fdtable_entry;

_kernel_oserror *fdtable_alloc_entry(app_object *app,
				     int opentable_idx,
				     bool close_on_exec,
				     int *fd_ret);

fdtable_entry *get_fdtable_entry(app_object *app, int fd);
void fdtable_destroy(array_t *);
_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret);
_kernel_oserror *fdtable_open_shm(const char *name, int oflag, int mode, int *fd_ret);
_kernel_oserror *fdtable_open_ipc(size_t size, int fd_ret[2]);
_kernel_oserror *fdtable_close(app_object *app, fdtable_entry *fd);
_kernel_oserror *fdtable_lseek(fdtable_entry *fd, off_t offset, int whence, off_t *offset_ret);
_kernel_oserror *fdtable_read(fdtable_entry *fd, void *buf, size_t count, int *bytes_read);
_kernel_oserror *fdtable_write(fdtable_entry *fd, void *buf, size_t count, int *bytes_written);

_kernel_oserror *
fdtable_get_info(fdtable_entry *fd,
		 int *type,
		 const char **name,
		 void **load,
		 void **exec,
		 off_t *length,
		 unsigned *attributes);

_kernel_oserror *
fdtable_get_lock(fdtable_entry *fd, int *type, int *whence, off_t *start, off_t *len);
_kernel_oserror *
fdtable_set_lock(app_object *app, fdtable_entry *fd, int *type, int *whence, off_t *start, off_t *len);
_kernel_oserror *fdtable_fsync(fdtable_entry *fd);
_kernel_oserror *fdtable_ftruncate(fdtable_entry *fd, off_t new_extent);
_kernel_oserror *fdtable_dup(fdtable_entry *fd, int *new_fd_ret);
_kernel_oserror *fdtable_dup2(fdtable_entry *fd, int new_fd);
void fdtable_copy(app_object *dst_app, app_object *src_app);
void fdtable_close_on_exec(app_object *app);

void fdtable_print(app_object *app);
void fdtable_log(app_object *app);

#endif
