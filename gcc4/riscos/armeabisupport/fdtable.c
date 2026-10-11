/* fdtable.c
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdtable.h"
#include "filetable.h"
#include "shmtable.h"
#include "ipctable.h"
#include "opentable.h"
#include "memory.h"
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

#define FDTABLE_BASE_FD 1000

/* Return true if the FDs point to the same file/shm.  */
static inline bool fdtable_entries_equal(fdtable_entry *fd1, fdtable_entry *fd2)
{
  opentable_entry *ot_entry1 = opentable_get_entry(fd1->opentable_idx);
  opentable_entry *ot_entry2 = opentable_get_entry(fd2->opentable_idx);

  if (!ot_entry1 || !ot_entry2 || ot_entry1->type != ot_entry2->type)
    return false;

  return (ot_entry1->systemtable_idx == ot_entry2->systemtable_idx);
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

_kernel_oserror *fdtable_alloc_entry(app_object *app,
				     int opentable_idx,
				     bool close_on_exec,
				     int *fd_ret)
{
  _kernel_oserror *err;

  if ((err = array_alloc_entry(&app->fd_table, fd_ret)) != NULL)
    return err;

  fdtable_entry *entry = (fdtable_entry *)array_get_entry_ptr(&app->fd_table, *fd_ret);

  entry->opentable_idx = opentable_idx;
  entry->close_on_exec = close_on_exec;

  (*fd_ret) += FDTABLE_BASE_FD;

  return NULL;
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

  err = canonicalise_filename(file, &canon_name);
  if (err)
    goto error_return;

  err = systemfiletable_open_file(canon_name, flags, mode, &sysfiletbl_idx);
  free(canon_name);
  if (err)
    goto error_return;

  err = opentable_add_file(sysfiletbl_idx, flags, mode, &openfiletbl_idx);
  if (err)
    goto error_return;

#if ENABLE_LOGGING
  opentable_log();
#endif

  LOG("%s:     open table index %d", __func__, openfiletbl_idx);

  if ((err = fdtable_alloc_entry(app,
				 openfiletbl_idx,
				 /*close_on_exec=*/(flags & O_CLOEXEC) != 0,
				 fd_ret)) == NULL)
    {
      LOG("%s:   returned fd=%d", __func__, *fd_ret);

#if ENABLE_LOGGING
      fdtable_log(app);
#endif

      return NULL;
    }

  /* Fall through on error.  */

error_return:
  *fd_ret = ARRAY_INVALID_INDEX;

  if (sysfiletbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE], sysfiletbl_idx));
  if (openfiletbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_table, openfiletbl_idx));

  if (err)
    {
      LOG("%s: Error=%s", __func__, err->errmess);
    }

  return err;
}

_kernel_oserror *fdtable_close(app_object *app, fdtable_entry *fd)
{
  TRACE("%s: fd=%d", __func__, array_get_entry_idx(&app->fd_table, &fd->base) + FDTABLE_BASE_FD);

  /* IEEE Std 1003.1-1988 (“POSIX.1”) requires that all locks associated
   * with a file for a given process are removed when any file descriptor for
   * that file is closed by that process.
   */
  filelocklist_clear_locks(app, fd->opentable_idx);

#if ENABLE_LOGGING
  fdtable_log(NULL);
#endif

  _kernel_oserror *err;

  err = opentable_deref_entry(fd->opentable_idx);

  fd->opentable_idx = ARRAY_INVALID_INDEX;
  array_entry_mark_unused(&fd->base);

  return err;
}

_kernel_oserror *fdtable_lseek(fdtable_entry *fd,
			       off_t offset,
			       int whence,
			       off_t *offset_ret)
{
  _kernel_oserror *err = NULL;
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  off_t new_offset = (off_t)-1;

  switch (whence)
    {
    case SEEK_SET:
      new_offset = offset;
      LOG("%s: SEEK_SET: new file offset = %ld", __func__, entry->file.file_offset);
      break;
    case SEEK_CUR:
      if ((LONG_MAX - entry->file.file_offset) < offset)
	{
	  err = armeabisupport_EOVERFLOW;
	  LOG("%s: SEEK_CUR: EOVERFLOW: LONG_MAX=%ld, file_offset=%ld, (LONG_MAX - file_offset)=%ld",
	      __func__, LONG_MAX, entry->file.file_offset, LONG_MAX - entry->file.file_offset);
	}
      else
	{
	  new_offset = entry->file.file_offset + offset;
	  LOG("%s: SEEK_CUR: new file offset = %ld", __func__, entry->file.file_offset);
	}
      break;
    case SEEK_END: {
      off_t file_length;
      err = systemfiletable_get_file_length(entry->systemtable_idx, &file_length);
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
	      LOG("%s: SEEK_END: new file offset = %ld", __func__, entry->file.file_offset);
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
    *offset_ret = entry->file.file_offset = new_offset;

  return err;
}

_kernel_oserror *fdtable_read(fdtable_entry *fd,
			      void *buf,
			      size_t count,
			      int *bytes_read)
{
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  if ((entry->file.flags & O_ACCMODE) == O_WRONLY)
      return armeabisupport_EBADF;

  _kernel_oserror *err;
  off_t file_length;
  if ((err = systemfiletable_get_file_length(entry->systemtable_idx, &file_length)) != NULL)
    return armeabisupport_EINVAL;

  if (count == 0 || entry->file.file_offset >= file_length)
    {
      *bytes_read = 0;
      return NULL;
    }

  int bytes_not_read;
  err = systemfiletable_read_bytes(entry->systemtable_idx,
				   buf,
				   count,
				   &entry->file.file_offset,
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
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  if ((entry->file.flags & O_ACCMODE) == O_RDONLY)
    {
      LOG("%s: Error: read only", __func__);
      return armeabisupport_EBADF;
    }

  _kernel_oserror *err;
  int bytes_not_written;
  if (entry->file.flags & O_APPEND)
    {
      err = systemfiletable_get_file_length(entry->systemtable_idx,
					    &entry->file.file_offset);
      if (err)
	{
	  LOG("%s: Error=%s",__func__, err->errmess);
	  return err;
	}
    }

  err = systemfiletable_write_bytes(entry->systemtable_idx,
			      buf,
			      count,
			      &entry->file.file_offset,
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
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (!entry)
    return armeabisupport_EBADF;

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;


  return systemfiletable_get_info(entry->systemtable_idx,
				  type,
				  name,
				  load,
				  exec,
				  length,
				  attributes);
}

/* If the returned end == NULL, then end should be interpreted as end of file.  */
static _kernel_oserror *
get_start_end_offsets(opentable_entry *entry,
		      int whence,
		      off_t offset,
		      off_t len,
		      off_t *start,
		      off_t *end)
{
  _kernel_oserror *err;
  off_t file_length;
  if ((err = systemfiletable_get_file_length(entry->systemtable_idx,
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
	      *start = entry->file.file_offset + offset + len;
	      *end = entry->file.file_offset + offset;
	    }
	  else
	    *start = entry->file.file_offset + offset;
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
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (!entry)
    return armeabisupport_EBADF;

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(entry, *whence, *offset, *len, &start, &end)) != NULL)
    return err;

  if (filelocklist_get_lock(&entry->file.locks,
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
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (!entry)
    return armeabisupport_EBADF;

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  _kernel_oserror *err;
  off_t start, end;
  if ((err = get_start_end_offsets(entry, *whence, *offset, *len, &start, &end)) != NULL)
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
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (!entry)
    return armeabisupport_EBADF;

  if (entry->type != opentable_type_FILE)
    return armeabisupport_EINVAL;

  return systemfiletable_fsync(entry->systemtable_idx);
}

_kernel_oserror *fdtable_ftruncate(fdtable_entry *fd, off_t new_extent)
{
  _kernel_oserror *err;
  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  switch (entry->type)
  {
    case opentable_type_FILE: {
#if ENABLE_LOGGING
      app_object *app;
      if ((err = app_find(&app)) != NULL)
	return err;
      LOG("%s: fd=%d, new_extent=%ld, path=%s",
	  __func__,
	  array_get_entry_idx(&app->fd_table, &fd->base) + FDTABLE_BASE_FD,
	  new_extent,
	  systemfiletable_get_filename(entry->systemtable_idx));
#endif
      err = entry
	    ? systemfiletable_ftruncate(entry->systemtable_idx, new_extent)
	    : armeabisupport_EBADF;
      break;
    }
    case opentable_type_SHM:
      /* This is a shared memory object which is currently a no-op for ftruncate.  */
      err = NULL;
      break;
    default:
      err = armeabisupport_EBADF;
      break;
  }

  return err;
}

_kernel_oserror *fdtable_open_ipc(size_t size, int fd_ret[2])
{
  TRACE("%s: size=%ld", __func__, size);

  _kernel_oserror *err;

  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  array_t *fd_table = &app->fd_table;
  int sysipctbl_idx = ARRAY_INVALID_INDEX;
  int openipctbl1_idx = ARRAY_INVALID_INDEX;
  int openipctbl2_idx = ARRAY_INVALID_INDEX;
  int fd1 = ARRAY_INVALID_INDEX;
  int fd2 = ARRAY_INVALID_INDEX;

  if ((err = array_alloc_entry(fd_table, &fd1)) != NULL)
    goto error_return;
  if ((err = array_alloc_entry(fd_table, &fd2)) != NULL)
    goto error_return;
  if ((err = systemipctable_open_ipc(size, &sysipctbl_idx)) != NULL)
    goto error_return;

  systemipctable_entry *sysipc_entry = (systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
										   sysipctbl_idx);
  opentable_entry *ot_shm_entry = opentable_get_entry(sysipc_entry->opentable_idx);
  systemshmtable_entry *sys_shm_entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
										    ot_shm_entry->systemtable_idx);
  mmap_allocator *allocator = mmap_allocator_get_entry(sys_shm_entry->allocator_idx);
  mmap_mapping *mapping = mmap_mapping_get_entry(allocator, sys_shm_entry->mapping_idx);

  eabi_PTR write_buffer = page_to_addr(&allocator->da, mapping->start_page);
  eabi_PTR read_buffer = write_buffer + size;

  LOG("%s:  fd1=%d, fd2=%d, write_buffer=%X, read_buffer=%X",
      __func__, fd1 + FDTABLE_BASE_FD, fd2 + FDTABLE_BASE_FD, write_buffer, read_buffer);

  if ((err = opentable_add_ipc(sysipctbl_idx,
			       write_buffer,
			       read_buffer,
			       size,
			       &openipctbl1_idx)) != NULL)
    goto error_return;
  if ((err = opentable_add_ipc(sysipctbl_idx,
			       read_buffer,
			       write_buffer,
			       size,
			       &openipctbl2_idx)) != NULL)
    goto error_return;

  sysipc_entry->table_base.ref_count = 2;

  opentable_entry *openipc1 = (opentable_entry *)array_get_entry_ptr(&global.open_table,
								     openipctbl1_idx);
  opentable_entry *openipc2 = (opentable_entry *)array_get_entry_ptr(&global.open_table,
								     openipctbl2_idx);
  openipc1->ipc.opentable_oppend_idx = openipctbl2_idx;
  openipc2->ipc.opentable_oppend_idx = openipctbl1_idx;

  fdtable_entry *entry = (fdtable_entry *)fd_table->entries;

  entry[fd1].close_on_exec = entry[fd2].close_on_exec = false;
  entry[fd1].opentable_idx = openipctbl1_idx;
  entry[fd2].opentable_idx = openipctbl2_idx;

  fd_ret[0] = fd1 + FDTABLE_BASE_FD;
  fd_ret[1] = fd2 + FDTABLE_BASE_FD;

#if ENABLE_LOGGING
  fdtable_log(app);
#endif

  return NULL;

error_return:
  if (fd1 != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(fd_table, fd1));
  if (fd2 != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(fd_table, fd2));
  if (openipctbl1_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_table, openipctbl1_idx));
  if (openipctbl2_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_table, openipctbl2_idx));
  if (sysipctbl_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.system_table[fd_type_IPC], sysipctbl_idx));

  fd_ret[0] = fd_ret[1] = -1;

  return err;
}

_kernel_oserror *fdtable_open_shm(const char *name, int oflag, int mode, int *fd_ret)
{
  _kernel_oserror *err;
  int opentable_idx = ARRAY_INVALID_INDEX;

  TRACE("%s: name=%s", __func__, name);
  TRACE("%s:   oflag=%X, mode=%X", __func__, oflag, mode);

  if (strlen(name) > PATH_MAX)
    {
      LOG("shm_open: \"%s\" name too long", name);
      return armeabisupport_ENAMETOOLONG;
    }

  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  if ((err = opentable_add_shm(name, oflag, mode, &opentable_idx)) != NULL)
    return err;

#if ENABLE_LOGGING
  opentable_entry *entry = opentable_get_entry(opentable_idx);
  LOG("%s: ref count now %d for open table index %d", __func__, entry->ref_count, opentable_idx);
#endif

  if ((err = fdtable_alloc_entry(app,
				 opentable_idx,
				 /*close_on_exec=*/false,
				 fd_ret)) == NULL)
    {
#if ENABLE_LOGGING
      LOG("%s: fd=%d", __func__, *fd_ret);
      fdtable_log(app);
#endif

      return NULL;
    }

  *fd_ret = ARRAY_INVALID_INDEX;

  if (opentable_idx != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(array_get_entry_ptr(&global.open_table, opentable_idx));

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

  TRACE("%s: fd=%d",
	__func__,
	array_get_entry_idx(&app->fd_table, &fd->base) + FDTABLE_BASE_FD);

  opentable_entry *entry = opentable_get_entry(fd->opentable_idx);
  if (!entry)
    return armeabisupport_EINVAL;

  if (entry->type <= opentable_type_INVALID || entry->type >= opentable_type_MAX)
    {
      LOG("%s: FD type %d not known", __func__, entry->type);
      return armeabisupport_EBADF;
    }

  /* dup() is the only time that the opentable ref is ever incremented, open() creates
   * a new entry rather than increasing the ref.  */
  entry->ref_count++;

  if ((err = fdtable_alloc_entry(app,
				 fd->opentable_idx,
				 /*close_on_exec=*/false,
				 new_fd_ret)) != NULL)
    return err;

  LOG("%s: fd %d duplicated as fd %d, open table index %d ref count now %d (+++)",
      __func__,
      array_get_entry_idx(&app->fd_table, &fd->base) + FDTABLE_BASE_FD,
      *new_fd_ret,
      fd->opentable_idx,
      entry->ref_count);

#if ENABLE_LOGGING
  fdtable_log(NULL);
#endif

  return NULL;
}

_kernel_oserror *fdtable_dup2(fdtable_entry *old_fd, int fd)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  TRACE("%s: old_fd=%d, new_fd=%d",
	__func__,
	array_get_entry_idx(&app->fd_table, &old_fd->base) + FDTABLE_BASE_FD,
	fd);

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
  new_fd->opentable_idx = old_fd->opentable_idx;
  new_fd->close_on_exec = false;

  return NULL;
}

void fdtable_destroy(array_t *table)
{
  fdtable_entry *fd_entry = (fdtable_entry *)table->entries;
  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd_entry[i].base))
      opentable_deref_entry(fd_entry[i].opentable_idx);
}

void fdtable_print(app_object *app)
{
  array_t *table = &app->fd_table;
  fdtable_entry *fd = (fdtable_entry *)table->entries;

  printf("FD Table:\n");
  printf("  %-8s %s\n", "FD", "TYPE");
  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd[i].base))
      {
	opentable_entry *open_entry = opentable_get_entry(fd[i].opentable_idx);
	switch (open_entry->type)
	{
	  case opentable_type_FILE: {
	    systemfiletable_entry *system_entry =
			(systemfiletable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE],
								   open_entry->systemtable_idx);
	    printf("  %-8d %-10s     OPEN IDX:%-3d REF:%d\n",
		   i + FDTABLE_BASE_FD,
		   "FILE",
		   fd[i].opentable_idx,
		   open_entry->ref_count);
	    printf("                       SYSTEM IDX:%-3d REF:%-3d PATH:%s\n",
		   open_entry->systemtable_idx,
		   system_entry->table_base.ref_count,
		   system_entry->filename);
	    break;
	  }
	  case opentable_type_SHM: {
	    systemshmtable_entry *system_entry =
			(systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
								   open_entry->systemtable_idx);
	    printf("  %-8d %-10s     OPEN IDX:%-3d REF:%d\n",
		   i + FDTABLE_BASE_FD,
		   "SHM",
		   fd[i].opentable_idx,
		   open_entry->ref_count);
	    printf("                       SYSTEM IDX:%-3d REF:%-3d NAME:%s\n",
		   open_entry->systemtable_idx,
		   system_entry->table_base.ref_count,
		   system_entry->name);
	    break;
	  }
	  case opentable_type_IPC: {
	    systemipctable_entry *system_entry =
			(systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
								   open_entry->systemtable_idx);
	    printf("  %-8d %-10s WB:%10X RB:%10X WP:%10d RP:%10d\n",
		   i + FDTABLE_BASE_FD,
		   "IPC",
		   (unsigned)open_entry->ipc.write_buffer,
		   (unsigned)open_entry->ipc.read_buffer,
		   open_entry->ipc.write_pos,
		   open_entry->ipc.read_pos);
	    printf("                    OPEN IDX:%-3d REF:%d\n",
		   fd[i].opentable_idx,
		   open_entry->ref_count);
	    printf("                          SYSTEM IDX:%-3d REF:%d\n",
		   open_entry->systemtable_idx,
		   system_entry->table_base.ref_count);
	    break;
	  }
	  case opentable_type_INVALID:
	  default:
	    printf("  %-8d %s\n",
		   i + FDTABLE_BASE_FD,
		   "INVALID");
	    break;
	}
      }
}

void fdtable_copy(app_object *dst_app, app_object *src_app)
{
  TRACE("%s", __func__);

  array_copy(&dst_app->fd_table, &src_app->fd_table);

  array_t *array = &dst_app->fd_table;
  fdtable_entry *fd = (fdtable_entry *)array->entries;

  for (int i = 0; i < array->num_entries; i++)
    if (array_entry_is_in_use(&fd[i].base))
      {
	opentable_entry *open_table = opentable_get_entry(fd[i].opentable_idx);
	if (open_table->type == opentable_type_IPC)
	  systemipctable_mmap(open_table, i + FDTABLE_BASE_FD);
	open_table->ref_count++;
	LOG("%s: Ref count now %d for open table index %d", __func__, open_table->ref_count, fd[i].opentable_idx);
      }
}

void fdtable_close_on_exec(app_object *app)
{
  array_t *table = &app->fd_table;
  fdtable_entry *fd = (fdtable_entry *)table->entries;

  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd[i].base) && fd[i].close_on_exec)
      {
	LOG("%s: FD %d close-on-exec flag set in app '%s'", __func__,
	    i + FDTABLE_BASE_FD,
	    app->name);
	fdtable_close(app, &fd[i]);
      }
}

#if ENABLE_LOGGING
void fdtable_log(app_object *app)
{
  if (!app)
    {
      _kernel_oserror *err;

      if ((err = app_find(&app)) != NULL)
	return;
    }

  array_t *table = &app->fd_table;
  fdtable_entry *fd = (fdtable_entry *)table->entries;

  LOG("%-8s %s", "FD", "TYPE");

  for (int i = 0; i < table->num_entries; i++)
    if (array_entry_is_in_use(&fd[i].base))
      {
	opentable_entry *open_entry = opentable_get_entry(fd[i].opentable_idx);
	switch (open_entry->type)
	{
	  case opentable_type_FILE: {
	    systemfiletable_entry *system_entry =
			(systemfiletable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE],
								   open_entry->systemtable_idx);
	    LOG("%-8d %-10s  OPEN IDX:%-3d REF:%d",
		i + FDTABLE_BASE_FD,
		"FILE",
		fd[i].opentable_idx,
		open_entry->ref_count);
	    LOG("                   SYSTEM IDX:%-3d REF:%-3d HANDLE:%d PATH:%s",
		open_entry->systemtable_idx,
		system_entry->table_base.ref_count,
		system_entry->file_handle,
		system_entry->filename);
	    break;
	  }
	  case opentable_type_SHM: {
	    systemshmtable_entry *system_entry =
			(systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
								   open_entry->systemtable_idx);
	    LOG("%-8d %-10s  OPEN IDX:%-3d REF:%d",
		i + FDTABLE_BASE_FD,
		"SHM",
		fd[i].opentable_idx,
		open_entry->ref_count);
	    mmap_allocator *allocator = mmap_allocator_get_entry(system_entry->allocator_idx);
	    mmap_mapping *block = mmap_mapping_get_entry(allocator, system_entry->mapping_idx);
	    LOG("                   SYSTEM IDX:%-3d REF:%-3d ADDR:%10X NAME:%s",
		open_entry->systemtable_idx,
		system_entry->table_base.ref_count,
		allocator ? page_to_addr(&allocator->da, block->start_page) : NULL,
		system_entry->name);
	    break;
	  }
	  case opentable_type_IPC: {
	    systemipctable_entry *system_entry =
			(systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
								   open_entry->systemtable_idx);
	    LOG("%-8d %-10s  OPEN IDX:%-3d REF:%d",
		i + FDTABLE_BASE_FD,
		"IPC",
		fd[i].opentable_idx,
		open_entry->ref_count);
	    LOG("                   SYSTEM IDX:%-3d REF:%d",
		open_entry->systemtable_idx,
		system_entry->table_base.ref_count);
	    LOG("                           WB:%10X RB:%10X WP:%10d RP:%10d",
		(unsigned)open_entry->ipc.write_buffer,
		(unsigned)open_entry->ipc.read_buffer,
		open_entry->ipc.write_pos,
		open_entry->ipc.read_pos);
	    break;
	  }
	  case opentable_type_INVALID:
	  default:
	    LOG("%-8d %s",
		i + FDTABLE_BASE_FD,
		"INVALID");
	    break;
	}
      }
}
#endif
