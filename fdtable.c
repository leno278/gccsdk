/* fdtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdtable.h"
#include "filetable.h"
#include "shmtable.h"
#include "types.h"
#include "main.h"
#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <swis.h>
#include <limits.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define FDTABLE_BASE_FD 10000

/* Return true if the FDs point to the same file/shm.  */
static inline bool fdtable_entries_equal(fdtable_entry *fd1, fdtable_entry *fd2)
{
  if (fd1->type != fd2->type)
    return false;

  switch (fd1->type)
  {
    case fd_type_shared_file: {
      openfiletable_entry *openfile_table = (openfiletable_entry *)&global.open_filetable;

      return (openfile_table[fd1->opentable_idx].systemtable_idx ==
	      openfile_table[fd2->opentable_idx].systemtable_idx);
    }
    case fd_type_shm: {
      openshmtable_entry *openshm_table = (openshmtable_entry *)&global.open_shmtable;

      return (openshm_table[fd1->opentable_idx].systemtable_idx ==
	      openshm_table[fd2->opentable_idx].systemtable_idx);
    }
    default:
      break;;
  }

  return false;
}

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

      entry->type = fd_type_shared_file;
      entry->opentable_idx = openfiletbl_idx;

      (*fd_ret) += FDTABLE_BASE_FD;

      LOG("%s:   returned fd=%d", __func__, *fd_ret);

#if ENABLE_LOGGING
      fdtable_print(app);
#endif

      return NULL;
    }

  /* Fall through on error.  */

error_return:
  *fd_ret = ARRAY_INVALID_INDEX;

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

_kernel_oserror *fdtable_close(app_object *app, fdtable_entry *fd)
{
  _kernel_oserror *err;

  TRACE("%s: fd=%d", __func__, fd);

  switch (fd->type)
    {
    case fd_type_shared_file:
      /* IEEE Std 1003.1-1988 (“POSIX.1”) requires that all locks associated
       * with a file for a given process are removed when any file descriptor for
       * that file is closed by that process.
       */
      filelocklist_clear_locks(app, fd->opentable_idx);
      err = openfiletable_deref_entry(fd->opentable_idx);
      break;
    case fd_type_shm:
      err = openshmtable_deref_entry(fd->opentable_idx);
      break;
    default:
      err = armeabisupport_EBADF;
      break;
    }

  array_entry_mark_unused(&fd->base);

  return err;
}

_kernel_oserror *fdtable_lseek(fdtable_entry *fd,
			       off_t offset,
			       int whence,
			       off_t *offset_ret)
{
  _kernel_oserror *err = NULL;


  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
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

  if (new_offset < 0)
    err = armeabisupport_EINVAL;

  if (err)
    {
      *offset_ret = (off_t)-1;
      LOG("%s: Error: %s", __func__, err->errmess);
    }
  else
    *offset_ret = openfile_entry->file_offset = new_offset;

  return err;
}

_kernel_oserror *fdtable_read(fdtable_entry *fd,
			      void *buf,
			      size_t count,
			      int *bytes_read)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_WRONLY)
      return armeabisupport_EBADF;

  _kernel_oserror *err;
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

_kernel_oserror *fdtable_write(fdtable_entry *fd,
			       void *buf,
			       size_t count,
			       int *bytes_written)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);

  if ((openfile_entry->flags & O_ACCMODE) == O_RDONLY)
    {
      LOG("%s: Error: read only", __func__);
      return armeabisupport_EBADF;
    }

  _kernel_oserror *err;
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
fdtable_get_info(fdtable_entry *fd,
		 int *type,
		 const char **name,
		 void **load,
		 void **exec,
		 off_t *length,
		 unsigned *attributes)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
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
fdtable_get_lock(fdtable_entry *fd,
		 int *type,
		 int *whence,
		 off_t *offset,
		 off_t *len)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(openfile_entry, *whence, *offset, *len, &start, &end)) != NULL)
    return err;

  if (filelocklist_get_lock(&openfile_entry->locks,
			    fd->opentable_idx,
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
fdtable_set_lock(app_object *app,
		 fdtable_entry *fd,
		 int *type,
		 int *whence,
		 off_t *offset,
		 off_t *len)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(openfile_entry, *whence, *offset, *len, &start, &end)) != NULL)
    return err;

  return *type == F_UNLCK
	? filelocklist_remove_lock(app,
				   fd->opentable_idx,
				   start,
				   end)
	: filelocklist_add_lock(app,
				fd->opentable_idx,
				start,
				end,
				*type);
}

_kernel_oserror *fdtable_fsync(fdtable_entry *fd)
{
  if (fd->type != fd_type_shared_file)
    return armeabisupport_EINVAL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
  if (!openfile_entry)
    return armeabisupport_EBADF;

  return systemfiletable_fsync(openfile_entry->systemtable_idx);
}

_kernel_oserror *fdtable_ftruncate(fdtable_entry *fd, off_t new_extent)
{
  _kernel_oserror *err;
  switch (fd->type)
  {
    case fd_type_shared_file: {
      openfiletable_entry *openfile_entry = openfiletable_get_entry(fd->opentable_idx);
      err = openfile_entry
	    ? systemfiletable_ftruncate(openfile_entry->systemtable_idx, new_extent)
	    : armeabisupport_EBADF;
      break;
    }
    case fd_type_shm:
      /* This is a shared memory object which is currently a no-op for ftruncate.  */
      err = NULL;
      break;
    default:
      err = armeabisupport_EBADF;
      break;
  }

  return err;
}

_kernel_oserror *fdtable_open_shm(const char *name, int oflag, int mode, int *fd_ret)
{
  _kernel_oserror *err;
  int sysshmtbl_idx = ARRAY_INVALID_INDEX;
  int openshmtbl_idx = ARRAY_INVALID_INDEX;

  if (strlen(name) > PATH_MAX)
    {
      LOG("shm_open: \"%s\" name too long", name);
      return armeabisupport_ENAMETOOLONG;
    }

  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  bool exists = systemshmtable_shm_exists(name) == ARRAY_INVALID_INDEX;

  /* Fail if O_CREAT is not set and the named shared memory object does not exist.  */
  if (!exists && !(oflag & O_CREAT))
    {
      LOG("shm_open: \"%s\" does not exist and O_CREATE not set", name);
      return armeabisupport_ENOENT;
    }

  /* Fail if O_CREAT and O_EXCL are set and the named shared memory object already exists.  */
  if (exists && ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)))
    {
      LOG("shm_open: \"%s\" already exists, but O_CREAT and O_EXCL are set", name);
      return armeabisupport_EEXIST;
    }

  err = systemshmtable_open_shm(name, oflag, mode, &sysshmtbl_idx);
  if (err)
    return err;

  err = openshmtable_add_entry(sysshmtbl_idx, oflag, mode, &openshmtbl_idx);
  if (err)
    {
      array_entry_mark_unused(array_get_entry_ptr(&global.system_shmtable, sysshmtbl_idx));
      return err;
    }

  LOG("%s:     openshmtable index=%d", __func__, openshmtbl_idx);

  array_t *fd_table = &app->fd_table;

  if ((err = array_alloc_entry(fd_table, fd_ret)) == NULL)
    {
      fdtable_entry *entry = (fdtable_entry *)array_get_entry_ptr(fd_table, *fd_ret);

      entry->type = fd_type_shm;
      entry->opentable_idx = openshmtbl_idx;

      (*fd_ret) += FDTABLE_BASE_FD;

      LOG("%s:   returned fd=%d", __func__, *fd_ret);

#if ENABLE_LOGGING
      fdtable_print(app);
#endif

      return NULL;
    }

  *fd_ret = ARRAY_INVALID_INDEX;

  if (sysshmtbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.system_shmtable, sysshmtbl_idx));
  if (openshmtbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_shmtable, openshmtbl_idx));

  if (err)
    {
      LOG("%s: Error=%s", __func__, err->errmess);
    }

  return err;
}

_kernel_oserror *fdtable_dup(fdtable_entry *fd, int *new_fd_ret)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  /* dup() is the only time that the opentable ref is ever incremented, open() creates
   * a new entry rather than increasing the ref.  */
  switch (fd->type)
  {
    case fd_type_shared_file: {
      openfiletable_entry *openfile_entry =
		(openfiletable_entry *)array_get_entry_ptr(&global.open_filetable,
							   fd->opentable_idx);
      openfile_entry->ref_count++;
      break;
    }
    case fd_type_shm: {
      openshmtable_entry *openshm_entry =
		(openshmtable_entry *)array_get_entry_ptr(&global.open_shmtable,
							  fd->opentable_idx);
      openshm_entry->ref_count++;
      break;
    }
    default:
      return armeabisupport_EBADF;
  }

  if ((err = array_alloc_entry(&app->fd_table, new_fd_ret)) != NULL)
    return err;

  fdtable_entry *new_entry = (fdtable_entry *)array_get_entry_ptr(&app->fd_table,
								  *new_fd_ret);
  new_entry->type = fd->type;
  new_entry->opentable_idx = fd->opentable_idx;

  (*new_fd_ret) += FDTABLE_BASE_FD;

  LOG("%s: fd %d duplicated as fd %d",
      __func__,
      array_get_entry_idx(&app->fd_table, fd),
      *new_fd_ret);

  return NULL;
}

_kernel_oserror *fdtable_dup2(fdtable_entry *old_fd, int fd)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *new_fd;
  /* Does the FD exist in the array?  */
  if ((new_fd = get_fdtable_entry(app, fd)) != NULL)
    {
      /* The FD does exist in the array, is it in use?  */
      if (array_entry_is_in_use(&new_fd->base))
	{
	  /* The intended FD is in use, is it the same as the old one?
	   * If old_fd is a valid file descriptor, and new_fd has the
	   * same value as old_fd, then dup2() does nothing, and returns
	   * new_fd.  */
	  if (fdtable_entries_equal(old_fd, new_fd))
	    return NULL;
	  /* Doesn't point to the same open object, close it first.  */
	  fdtable_close(app, new_fd);
	}
    }
  else
    {
      if ((err = array_alloc(&app->fd_table, fd - FDTABLE_BASE_FD)) != NULL)
	return err;
      new_fd = (fdtable_entry *)array_get_entry_ptr(&app->fd_table, fd - FDTABLE_BASE_FD);
    }

  array_entry_mark_used(&new_fd->base);
  new_fd->type = old_fd->type;
  new_fd->opentable_idx = old_fd->opentable_idx;
  // TODO: Copy closeOnExec flag.

  return NULL;
}

void fdtable_destroy(array_t *table)
{
  fdtable_entry *fd_entry = (fdtable_entry *)table->entries;
  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd_entry[i].base))
      {
	switch (fd_entry[i].type)
	{
	  case fd_type_shared_file:
	    openfiletable_deref_entry(fd_entry[i].opentable_idx);
	    break;
	  case fd_type_shm:
	    openshmtable_deref_entry(fd_entry[i].opentable_idx);
	    break;
	  default:
	    break;
	}
      }
}

void fdtable_print(app_object *app)
{
  array_t *table = &app->fd_table;
  fdtable_entry *fd = (fdtable_entry *)table->entries;

  printf("%-8s %-10s %s\n", "FD", "TYPE", "PATH");
  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd[i].base))
      {
	switch (fd->type)
	{
	  case fd_type_shared_file: {
	    openfiletable_entry *open_entry =
			(openfiletable_entry *)array_get_entry_ptr(&global.open_filetable,
								   fd[i].opentable_idx);
	    printf("%-8d %-10s %s\n",
		   i + FDTABLE_BASE_FD,
		   "FILE",
		   systemfiletable_get_filename(open_entry->systemtable_idx));
	    break;
	  }
	  case fd_type_shm: {
	    openshmtable_entry *open_entry =
			(openshmtable_entry *)array_get_entry_ptr(&global.open_shmtable,
								  fd[i].opentable_idx);
	    printf("%-8d %-10s %s\n",
		   i + FDTABLE_BASE_FD,
		   "SHM",
		   systemshmtable_get_name(open_entry->systemtable_idx));
	    break;
	  }
	  case fd_type_invalid:
	    printf("%-8d %s\n",
		   i + FDTABLE_BASE_FD,
		   "INVALID");
	    break;
	}
      }
}
