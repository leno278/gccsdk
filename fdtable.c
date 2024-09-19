/* fdtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdtable.h"
#include "filetable.h"
#include "types.h"
#include "main.h"
#include <kernel.h>
#include <stdlib.h>
#include <swis.h>

#define FDTABLE_BASE_FD 10000

static inline fdtable_entry *get_fdtable_entry(int fd)
{
  app_object *app;
  if ((app_find(&app)) != NULL)
    return NULL;
  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  return &entry[fd - FDTABLE_BASE_FD];
}

static char *canonicalise_filename(const char *filename)
{
  _kernel_oserror *err;
  int buffer_size;
  err = _swix(OS_FSControl,
	      _INR(0,5) | _OUT(5),
	      0x25,		/*  in: r0=reason code - Canonicalise path */
	      filename,		/*  in: r1=filename */
	      0,		/*  in: r2=ptr to buffer or 0 */
	      0,		/*  in: r3=ptr to path variable or 0 */
	      0,		/*  in: r4=ptr to string of paths or 0 */
	      0,		/*  in: r5=buffer size */
	      &buffer_size);	/* out: r5=-ve buffer size */
  if (err)
    return NULL;
  char *buffer = malloc(1 - buffer_size);
  if (!buffer)
    return NULL;

  err = _swix(OS_FSControl,
	      _INR(0,5),	/*  in: register range r0-r5 */
	      0x25,		/*  in: r0=reason code - Canonicalise path */
	      filename,		/*  in: r1=filename */
	      buffer,		/*  in: r2=ptr to buffer or 0 */
	      0,		/*  in: r3=ptr to path variable or 0 */
	      0,		/*  in: r4=ptr to string of paths or 0 */
	      1 - buffer_size);	/*  in: r5=buffer size */

  return err ? NULL : buffer;
}

_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret)
{
  _kernel_oserror *err = NULL;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  char *canon_name = canonicalise_filename(file);
  if (!canon_name)
    return armeabisupport_ENOMEM;

  int filetable_idx;
  err = filetable_open_file(canon_name, flags, mode, &filetable_idx);
  if (err)
    goto error_return;

  int i;
  for (i = 0; i < fdtable->num_entries; i++)
    if (entry[i].base.in_use == 0)
      break;

  if (i == fdtable->num_entries)
    {
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(fdtable);
      if (err)
	goto error_return;
      i = fdtable->num_entries / 2;
    }

  free(canon_name);

  *fd_ret = i + FDTABLE_BASE_FD;
  entry[i].base.in_use = 1;
  entry[i].filetable_idx = filetable_idx;
  entry[i].flags = flags;
  entry[i].mode = mode;
  entry[i].file_offset = 0;

  return NULL;

error_return:
  free(canon_name);
  *fd_ret = -1;
  return err;
}

_kernel_oserror *fdtable_close_file(int fd)
{
  fdtable_entry *entry = get_fdtable_entry(fd);
  if (!entry)
    return armeabisupport_EBADF;

  filetable_close_file(entry->filetable_idx);
  entry->base.in_use = 0;

  return NULL;
}

_kernel_oserror *fdtable_lseek(int fd, off_t offset, int whence, off_t *offset_ret)
{
  fdtable_entry *entry = get_fdtable_entry(fd);
  if (!entry)
    return armeabisupport_EBADF;

  switch (whence)
    {
    case SEEK_SET:
      entry->file_offset = offset;
      break;
    case SEEK_CUR:
      entry->file_offset += offset;
      break;
    case SEEK_END: {
      unsigned file_length;
      _kernel_oserror *err = filetable_get_file_length(entry->filetable_idx, &file_length);
      if (err)
	return err;
      entry->file_offset = file_length + offset;
      break;
    }
    default:
      return armeabisupport_EINVAL;
    }

  *offset_ret = entry->file_offset;

  return NULL;
}

_kernel_oserror *fdtable_read(int fd, void *buf, size_t count, int *bytes_read)
{
  fdtable_entry *entry = get_fdtable_entry(fd);
  if (!entry)
    return armeabisupport_EBADF;

  if (count == 0)
    {
      *bytes_read = 0;
      return NULL;
    }

  int bytes_not_read;
  _kernel_oserror *err;
  err = filetable_read_bytes(entry->filetable_idx,
			     buf,
			     count,
			     &entry->file_offset,
			     &bytes_not_read);
  if (err)
    return err;

  *bytes_read = count - bytes_not_read;

  return NULL;
}

_kernel_oserror *fdtable_write(int fd, void *buf, size_t count, int *bytes_written)
{
  fdtable_entry *entry = get_fdtable_entry(fd);
  if (!entry)
    return armeabisupport_EBADF;

  int bytes_not_written;
  _kernel_oserror *err;
  if (entry->flags & O_APPEND)
    {
      err = filetable_get_file_length(entry->filetable_idx,
				      &entry->file_offset);
      if (err)
	return err;
    }

  err = filetable_write_bytes(entry->filetable_idx,
			      buf,
			      count,
			      &entry->file_offset,
			      &bytes_not_written);
  if (err)
    return err;

  *bytes_written = count - bytes_not_written;

  return NULL;
}
