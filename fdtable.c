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
#include <fcntl.h>
#include <swis.h>
#include <limits.h>

#define ENABLE_TRACING 1
#include "trace.h"

#define ENABLE_LOGGING 1
#include "log.h"

#define FDTABLE_BASE_FD 10000

static inline fdtable_entry *get_fdtable_entry(int fd)
{
  app_object *app;
  if ((app_find(&app)) != NULL)
    return NULL;
  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  int fd_idx = fd - FDTABLE_BASE_FD;
  if (fd_idx < 0 || fd_idx >= fdtable->num_entries)
    return NULL;

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
  TRACE("%s: file=%s", __func__, file);
  TRACE("%s:   flags=%X, mode=%X", __func__, flags, mode);

  char *canon_name = NULL;
  _kernel_oserror *err = NULL;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    goto error_return;

  array_t *fdtable = &app->fd_table;

  canon_name = canonicalise_filename(file);
  if (!canon_name)
    goto error_return;

  int sysfiletbl_idx;
  err = systemfiletable_open_file(canon_name, flags, mode, &sysfiletbl_idx);
  if (err)
    goto error_return;

  int openfiletbl_idx;
  err = openfiletable_add_entry(sysfiletbl_idx, flags, mode, &openfiletbl_idx);
  if (err)
    goto error_return;

#if ENABLE_LOGGING
  systemfiletable_print();
  openfiletable_print();
#endif

  int i;
  for (i = 0; i < fdtable->num_entries; i++)
    if (fdtable->entries[i].in_use == 0)
      {
	LOG("%s:   Found free entry at index %d", __func__, i);
	break;
      }

  if (i == fdtable->num_entries)
    {
      LOG("%s:   No free entries (i=%d), extending array", __func__, i);
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(fdtable);
      if (err)
	goto error_return;
      i = (fdtable->num_entries == ARRAY_INIT_NUM_ENTRIES) ? 0 : fdtable->num_entries / 2;
    }

  free(canon_name);

  LOG("%s:   Inserting entry at index %d", __func__, i);
  LOG("%s:     opentable index=%d", __func__, openfiletbl_idx);

  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  entry[i].base.in_use = 1;
  entry[i].opentable_idx = openfiletbl_idx;

  *fd_ret = i + FDTABLE_BASE_FD;

  LOG("%s:   returned fd=%d", __func__, *fd_ret);

#if ENABLE_LOGGING
  fdtable_print();
#endif

  return NULL;

error_return:
  /* FIXME: tidy up any table entries allocated above that are no longer valid. */
  LOG("%s: Error=%s", __func__, err->errmess);
  if (canon_name)
    free(canon_name);
  *fd_ret = -1;
  return err;
}

_kernel_oserror *fdtable_close_file(int fd)
{
  TRACE("%s: fd=%d", __func__, fd);

  fdtable_entry *fd_entry = get_fdtable_entry(fd);
  if (!fd_entry || fd_entry->base.in_use == 0)
    {
      LOG("%s:   Error=armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openfiletable_deref_entry(fd_entry->opentable_idx);

  fd_entry->base.in_use = 0;

  return NULL;
}

_kernel_oserror *fdtable_lseek(int fd, off_t offset, int whence, off_t *offset_ret)
{
  TRACE("%s: fd=%d, offset=%d, whence=%d", __func__, fd, offset, whence);

  _kernel_oserror *err = NULL;
  fdtable_entry *fd_entry = get_fdtable_entry(fd);
  if (!fd_entry)
    {
      *offset_ret = (off_t)-1;
      return armeabisupport_EBADF;
    }

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  off_t new_offset = (off_t)-1;

  switch (whence)
    {
    case SEEK_SET:
      new_offset = offset;
      LOG("%s: SEEK_SET: new file offset = %d", __func__, openfile_entry->file_offset);
      break;
    case SEEK_CUR:
      if ((LONG_MAX - openfile_entry->file_offset) < offset)
	err = armeabisupport_EOVERFLOW;
      else {
	new_offset = openfile_entry->file_offset + offset;
	LOG("%s: SEEK_CUR: new file offset = %d", __func__, openfile_entry->file_offset);
      }
      break;
    case SEEK_END: {
      off_t file_length;
      err = systemfiletable_get_file_length(openfile_entry->systemtable_idx, &file_length);
      if (!err)
	{
	  LOG("%s: file_length=%d", __func__, file_length);
	  if (LONG_MAX - file_length < offset)
	    {
	      err = armeabisupport_EOVERFLOW;
	      LOG("%s: SEEK_END: EOVERFLOW: LONG_MAX=%d, file_length=%d, (LONG_MAX - file_length)=%d",
		  __func__, LONG_MAX, file_length, LONG_MAX - file_length);
	    }
	  else
	    {
	      new_offset = file_length + offset;
	      LOG("%s: SEEK_END: new file offset = %d", __func__, openfile_entry->file_offset);
	    }
	}
      break;
    }
    default:
      err = armeabisupport_EINVAL;
      break;
    }

  *offset_ret = openfile_entry->file_offset = new_offset;

  LOG("%s: new_offset = %d", __func__, new_offset);

  if (new_offset < 0)
    err = armeabisupport_EINVAL;

  if (err)
    LOG("%s: Error: %s", __func__, err->errmess);

  return err;
}

_kernel_oserror *fdtable_read(int fd, void *buf, size_t count, int *bytes_read)
{
  TRACE("%s: fd=%d, buf=0x%X, count=%d", __func__, fd, buf, count);

  _kernel_oserror *err;
  fdtable_entry *fd_entry = get_fdtable_entry(fd);
  if (!fd_entry)
    {
      err = armeabisupport_EBADF;
      goto error_return;
    }

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_WRONLY)
    {
      err = armeabisupport_EBADF;
      goto error_return;
    }

  if (count == 0)
    {
      *bytes_read = 0;
      return NULL;
    }

  int bytes_not_read;
  err = systemfiletable_read_bytes(openfile_entry->systemtable_idx,
				   buf,
				   count,
				   &openfile_entry->file_offset,
				   &bytes_not_read);
  LOG("%s: file offset after osgbpb=%d", __func__, openfile_entry->file_offset);
  LOG("%s: bytes_not_read=%d", __func__, bytes_not_read);

  if (err)
    goto error_return;

  *bytes_read = count - bytes_not_read;

  return NULL;

error_return:
  LOG("%s: Error=%s",__func__, err->errmess);

  *bytes_read = 0;

  return err;
}

_kernel_oserror *fdtable_write(int fd, void *buf, size_t count, int *bytes_written)
{
  TRACE("%s: fd=%d, buf=0x%X, count=%d", __func__, fd, buf, count);

  _kernel_oserror *err;
  fdtable_entry *fd_entry = get_fdtable_entry(fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_RDONLY)
    {
      err = armeabisupport_EBADF;
      goto error_return;
    }

  int bytes_not_written;
  if (openfile_entry->flags & O_APPEND)
    {
      err = systemfiletable_get_file_length(openfile_entry->systemtable_idx,
					    &openfile_entry->file_offset);
      if (err)
	goto error_return;
    }

  err = systemfiletable_write_bytes(openfile_entry->systemtable_idx,
			      buf,
			      count,
			      &openfile_entry->file_offset,
			      &bytes_not_written);
  if (err)
    goto error_return;

  *bytes_written = count - bytes_not_written;

  return NULL;

error_return:
  LOG("%s:   Error=%s", __func__, err->errmess);

  *bytes_written = 0;

  return err;
}

void fdtable_destroy(array_t *table)
{
  fdtable_entry *fd_entry = (fdtable_entry *)table->entries;
  for (int i = 0; i < table->num_entries; i++)
    if (fd_entry[i].base.in_use)
      openfiletable_deref_entry(fd_entry[i].opentable_idx);
}

void fdtable_print(void)
{
  app_object *app;
  if ((app_find(&app)) != NULL)
    return;
  array_t *table = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)table->entries;

  LOG("fd table:");
  LOG("%-8s %s", "fd", "OpenTableIdx");
  for (int i = 0; i < table->num_entries; i++)
    if (entry[i].base.in_use)
      {
	LOG("%-8d %d", i + FDTABLE_BASE_FD, entry[i].opentable_idx);
      }
}
