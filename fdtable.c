/* fdtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdtable.h"
#include "filetable.h"
#include "filedesc.h"
#include "types.h"
#include "main.h"
#include <kernel.h>
#include <stdlib.h>
#include <fcntl.h>
#include <swis.h>
#include <limits.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define FDTABLE_BASE_FD 10000

fdtable_entry *get_fdtable_entry(app_object *app, int fd)
{
  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  int fd_idx = fd - FDTABLE_BASE_FD;
  if (fd_idx < 0 || fd_idx >= fdtable->num_entries)
    return NULL;

  return &entry[fd - FDTABLE_BASE_FD];
}

static _kernel_oserror *canonicalise_filename(const char *filename, char **result)
{
  _kernel_oserror *err;
  int buffer_size;

  *result = NULL;

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
    return err;
  char *buffer = malloc(1 - buffer_size);
  if (!buffer)
    return armeabisupport_ENOMEM;

  err = _swix(OS_FSControl,
	      _INR(0,5),	/*  in: register range r0-r5 */
	      0x25,		/*  in: r0=reason code - Canonicalise path */
	      filename,		/*  in: r1=filename */
	      buffer,		/*  in: r2=ptr to buffer or 0 */
	      0,		/*  in: r3=ptr to path variable or 0 */
	      0,		/*  in: r4=ptr to string of paths or 0 */
	      1 - buffer_size);	/*  in: r5=buffer size */

  if (err)
    free(buffer);
  else
    *result = buffer;

  return err;
}

_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret)
{
  TRACE("%s: file=%s", __func__, file);
  TRACE("%s:   flags=%X, mode=%X", __func__, flags, mode);

  int sysfiletbl_idx = ARRAY_INVALID_INDEX;
  int openfiletbl_idx = ARRAY_INVALID_INDEX;
  char *canon_name = NULL;
  _kernel_oserror *err;

  app_object *app;
  if ((err = app_find(&app)) != NULL)
    goto error_return;

  array_t *fdtable = &app->fd_table;

  err = canonicalise_filename(file, &canon_name);
  if (err)
    goto error_return;

  err = systemfiletable_open_file(canon_name, flags, mode, &sysfiletbl_idx);
  free(canon_name);
  if (err)
    goto error_return;

  err = openfiletable_add_entry(sysfiletbl_idx, flags, mode, &openfiletbl_idx);
  if (err)
    goto error_return;

#if ENABLE_LOGGING
  systemfiletable_print();
  openfiletable_print();
#endif

  LOG("%s:     opentable index=%d", __func__, openfiletbl_idx);

  if ((err = array_alloc_entry(fdtable, fd_ret)) == NULL)
    {
      fdtable_entry *entry = (fdtable_entry *)array_get_entry_ptr(fdtable, *fd_ret);

      entry->opentable_idx = openfiletbl_idx;

      (*fd_ret) += FDTABLE_BASE_FD;

      LOG("%s:   returned fd=%d", __func__, *fd_ret);

#if ENABLE_LOGGING
      fdtable_print();
#endif

      return NULL;
    }

  /* Fall through on error.  */

error_return:
  *fd_ret = -1;

  if (sysfiletbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.system_filetable, sysfiletbl_idx));
  if (openfiletbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_filetable, openfiletbl_idx));

  if (err)
    {
      LOG("%s: Error=%s", __func__, err->errmess);
    }

  return err;
}

_kernel_oserror *fdtable_close_file(int fd)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry || fd_entry->base.in_use == 0)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d", __func__, fd);

  /* IEEE Std 1003.1-1988 (“POSIX.1”) requires that all locks associated
   * with a file for a given process are removed when any file descriptor for
   * that file is closed by that process.
   */
  filelocklist_clear_locks(app, fd_entry->opentable_idx);
  openfiletable_deref_entry(fd_entry->opentable_idx);

  fd_entry->base.in_use = 0;

  return NULL;
}

_kernel_oserror *fdtable_lseek(int fd, off_t offset, int whence, off_t *offset_ret)
{
  _kernel_oserror *err = NULL;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    {
      *offset_ret = (off_t)-1;
      return armeabisupport_EBADF;
    }

  TRACE("%s: fd=%d, offset=%ld, whence=%d", __func__, fd, offset, whence);

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  off_t new_offset = (off_t)-1;

  switch (whence)
    {
    case SEEK_SET:
      new_offset = offset;
      LOG("%s: SEEK_SET: new file offset = %ld", __func__, openfile_entry->file_offset);
      break;
    case SEEK_CUR:
      if ((LONG_MAX - openfile_entry->file_offset) < offset)
	err = armeabisupport_EOVERFLOW;
      else {
	new_offset = openfile_entry->file_offset + offset;
	LOG("%s: SEEK_CUR: new file offset = %ld", __func__, openfile_entry->file_offset);
      }
      break;
    case SEEK_END: {
      off_t file_length;
      err = systemfiletable_get_file_length(openfile_entry->systemtable_idx, &file_length);
      if (!err)
	{
	  LOG("%s: file_length=%ld", __func__, file_length);
	  if (LONG_MAX - file_length < offset)
	    {
	      err = armeabisupport_EOVERFLOW;
	      LOG("%s: SEEK_END: EOVERFLOW: LONG_MAX=%ld, file_length=%ld, (LONG_MAX - file_length)=%ld",
		  __func__, LONG_MAX, file_length, LONG_MAX - file_length);
	    }
	  else
	    {
	      new_offset = file_length + offset;
	      LOG("%s: SEEK_END: new file offset = %ld", __func__, openfile_entry->file_offset);
	    }
	}
      break;
    }
    default:
      err = armeabisupport_EINVAL;
      break;
    }

  *offset_ret = openfile_entry->file_offset = new_offset;

  if (new_offset < 0)
    err = armeabisupport_EINVAL;

  if (err)
    LOG("%s: Error: %s", __func__, err->errmess);

  return err;
}

_kernel_oserror *fdtable_read(int fd, void *buf, size_t count, int *bytes_read)
{
  *bytes_read = 0;

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d, buf=%p, count=%ld", __func__, fd, buf, count);

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_WRONLY)
      return armeabisupport_EBADF;

  off_t file_length;
  if ((err = systemfiletable_get_file_length(openfile_entry->systemtable_idx, &file_length)) != NULL)
    return armeabisupport_EINVAL;

  if (count == 0 || openfile_entry->file_offset >= file_length)
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
  if (!err)
    *bytes_read = count - bytes_not_read;
  else
    LOG("%s: Error=%s",__func__, err->errmess);

  return err;
}

_kernel_oserror *fdtable_write(int fd, void *buf, size_t count, int *bytes_written)
{
  *bytes_written = 0;

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d, buf=%p, count=%ld", __func__, fd, buf, count);

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_RDONLY)
    {
      LOG("%s: Error: read only", __func__);
      return armeabisupport_EBADF;
    }

  int bytes_not_written;
  if (openfile_entry->flags & O_APPEND)
    {
      err = systemfiletable_get_file_length(openfile_entry->systemtable_idx,
					    &openfile_entry->file_offset);
      if (err)
	{
	  LOG("%s: Error=%s",__func__, err->errmess);
	  return err;
	}
    }

  err = systemfiletable_write_bytes(openfile_entry->systemtable_idx,
			      buf,
			      count,
			      &openfile_entry->file_offset,
			      &bytes_not_written);
  if (!err)
    *bytes_written = count - bytes_not_written;
  else
    LOG("%s: Error=%s",__func__, err->errmess);

  return err;
}

_kernel_oserror *
fdtable_get_info(int fd,
		 int *type,
		 const char **name,
		 void **load,
		 void **exec,
		 off_t *length,
		 unsigned *attributes)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  return systemfiletable_get_info(openfile_entry->systemtable_idx,
				  type,
				  name,
				  load,
				  exec,
				  length,
				  attributes);
}

/* If the returned end == NULL, then end should be interpreted as end of file.  */
static _kernel_oserror *
get_start_end_offsets(openfiletable_entry *openfile_entry,
		      int whence,
		      off_t offset,
		      off_t len,
		      off_t *start,
		      off_t *end)
{
  _kernel_oserror *err;
  off_t file_length;
  if ((err = systemfiletable_get_file_length(openfile_entry->systemtable_idx,
					     &file_length)) == NULL)
    {
      switch (whence)
	{
	case SEEK_SET:
	  if (len < 0)
	    {
	      *start = offset + len;
	      *end = offset;
	    }
	  else
	    *start = offset;
	  break;
	case SEEK_CUR:
	  if (len < 0)
	    {
	      *start = openfile_entry->file_offset + offset + len;
	      *end = openfile_entry->file_offset + offset;
	    }
	  else
	    *start = openfile_entry->file_offset + offset;
	  break;
	case SEEK_END:
	  if (len < 0)
	    {
	      *start = file_length + offset + len;
	      *end = file_length + offset;
	    }
	  else
	    *start = file_length + offset;
	  break;
	default:
	  return armeabisupport_EINVAL;
	}

      if (len >= 0)
	*end = (len == 0) ? 0 : *start + len;
    }

  return err;
}

_kernel_oserror *
fdtable_get_lock(int fd, int *type, int *whence, off_t *offset, off_t *len)
{
  app_object *app;
  if (app_find(&app) != NULL)
    return armeabisupport_EINVAL;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d, type=%d, whence=%d, offset=%ld, len=%ld", __func__, fd, *type, *whence, *offset, *len);

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(openfile_entry, *whence, *offset, *len, &start, &end)) != NULL)
    return err;

  if (filelocklist_get_lock(&openfile_entry->locks,
			    fd_entry->opentable_idx,
			    &start,
			    &end,
			    type))
    {
      *whence = SEEK_SET;
      *offset = start;
      *len = (end == 0) ? 0 : end - start;
    }
  else
    *type = F_UNLCK;

  return NULL;
}

_kernel_oserror *
fdtable_set_lock(int fd, int *type, int *whence, off_t *offset, off_t *len)
{
  app_object *app;
  if (app_find(&app) != NULL)
    return armeabisupport_EINVAL;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d, type=%d, whence=%d, offset=%ld, len=%ld", __func__, fd, *type, *whence, *offset, *len);

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(openfile_entry, *whence, *offset, *len, &start, &end)) != NULL)
    return err;

  return *type == F_UNLCK
	? filelocklist_remove_lock(app,
				   fd_entry->opentable_idx,
				   start,
				   end)
	: filelocklist_add_lock(app,
				fd_entry->opentable_idx,
				start,
				end,
				*type);
}

_kernel_oserror *fdtable_fsync(int fd)
{
  app_object *app;
  if (app_find(&app) != NULL)
    return armeabisupport_EINVAL;

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d", __func__, fd);

  return systemfiletable_fsync(openfile_entry->systemtable_idx);
}

_kernel_oserror *fdtable_ftruncate(int fd, off_t new_extent)
{
  app_object *app;
  if (app_find(&app) != NULL)
    return armeabisupport_EINVAL;

  if (filedesc_find_fd(fd) != NULL)
    {
      /* This is a shared memory object which is currently a no-op for ftruncate.  */
      return NULL;
    }

  fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
  if (!fd_entry)
    return armeabisupport_EBADF;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd_entry->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  TRACE("%s: fd=%d, new_extent=%ld, path=%s",
	__func__, fd, new_extent, systemfiletable_get_filename(openfile_entry->systemtable_idx));

  return systemfiletable_ftruncate(openfile_entry->systemtable_idx, new_extent);
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
  fdtable_entry *fdentry = (fdtable_entry *)table->entries;

  printf("%-8s %s\n", "FD", "PATH");
  for (int i = 0; i < table->num_entries; i++)
    if (fdentry[i].base.in_use)
      {
	openfiletable_entry *open_entry = openfiletable_get_entry(fdentry[i].opentable_idx);
	printf("%-8d %s\n", i + FDTABLE_BASE_FD, systemfiletable_get_filename(open_entry->systemtable_idx));
      }
}
