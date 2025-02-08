/* filetable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FILETABLE_H
#define FILETABLE_H

#include "table_entry_base.h"
#include "types.h"
#include "lock.h"
#include "memory.h"
#include <unistd.h>
#include <swi.h>

typedef struct _openfiletable
{
  int flags;
  int mode;
  off_t file_offset;

  filelock_list locks;

} openfiletable;

static inline _kernel_oserror *osargs_flush_file(int file_handle)
{
  return _swix(OS_Args,
	       _INR(0,1),
	       255,		/* in: r0=reason code - flush file */
	       file_handle);	/* in: r1=file handle */
}

static inline _kernel_oserror *osargs_write_file_extent(int file_handle, off_t new_extent)
{
  return _swix(OS_Args,
	       _INR(0,2),
	       0x3,		/* in: r0=reason code - write extent */
	       file_handle,	/* in: r1=handle */
	       new_extent);	/* in: r2=new_extent */
}

_kernel_oserror *openfiletable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret);
void openfiletable_print(void);

typedef struct _systemfiletable_entry
{
  table_entry_base table_base;

  const char *filename;

  int file_handle;

} systemfiletable_entry;

_kernel_oserror *systemfiletable_open_file(const char *file, int flags, int mode, int *);
_kernel_oserror *systemfiletable_deref_entry(int filetable_idx);

_kernel_oserror *
systemfiletable_get_info(int filetable_idx,
			 int *type,
			 const char **name,
			 void **load,
			 void **exec,
			 off_t *length,
			 unsigned *attributes);

_kernel_oserror *systemfiletable_get_file_length(int filetable_idx, off_t *length);
_kernel_oserror *systemfiletable_read_bytes(int systemtable_idx, void *buf, size_t count, off_t *, int *bytes_not_read);
_kernel_oserror *systemfiletable_write_bytes(int systemtable_idx, void *buf, size_t count, off_t *, int *bytes_not_written);
_kernel_oserror *opentable_unmap_file(armeabisupport_allocator *allocator,
				      mmap_block *block);
const char *openfiletable_get_filename(int opentable_idx);

static inline _kernel_oserror *systemfiletable_fsync(int systemtable_idx)
{
  array_t *system_filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(system_filetable,
									      systemtable_idx);

  return osargs_flush_file(entry->file_handle);
}

static inline _kernel_oserror *
systemfiletable_ftruncate(int systemtable_idx, off_t new_extent)
{
  array_t *system_filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(system_filetable,
									      systemtable_idx);

  return osargs_write_file_extent(entry->file_handle, new_extent);
}

void systemfiletable_print(void);

static inline const char *
systemfiletable_get_filename(int systemtable_idx)
{
  array_t *system_filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(system_filetable,
									      systemtable_idx);
  return entry->filename;
}

void openfiletable_log(void);
void systemfiletable_log(void);

#endif
