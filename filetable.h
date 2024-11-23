/* filetable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FILETABLE_H
#define FILETABLE_H

#include "array.h"
#include "types.h"
#include "lock.h"
#include <unistd.h>
#include <swi.h>

typedef struct _openfiletable_entry
{
  array_entry_base base;

  int systemtable_idx;
  int flags;
  int mode;
  off_t file_offset;

  int ref_count;

  filelock_list locks;

} openfiletable_entry;

static inline openfiletable_entry *openfiletable_get_entry(int idx)
{
  array_t *open_filetable = &global.open_filetable;
  openfiletable_entry *openfile_entry = (openfiletable_entry *)open_filetable->entries;
  return &openfile_entry[idx];
}

static _kernel_oserror *osargs_flush_file(int file_handle)
{
  return _swix(OS_Args,
	       _INR(0,1),
	       255,		/* in: r0=reason code - flush file */
	       file_handle);	/* in: r1=file handle */
}

_kernel_oserror *openfiletable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret);
_kernel_oserror *openfiletable_deref_entry(int openfiletable_idx);
void openfiletable_print(void);

typedef struct _systemfiletable_entry
{
  array_entry_base base;

  const char *filename;
  int ref_count;

  int file_handle;

} systemfiletable_entry;

_kernel_oserror *systemfiletable_open_file(const char *file, int flags, int mode, int *);
_kernel_oserror *systemfiletable_close_file(int filetable_idx);

_kernel_oserror *
systemfiletable_get_info(int filetable_idx,
			 int *type,
			 const char **name,
			 void **load,
			 void **exec,
			 off_t *length,
			 unsigned *attributes);

_kernel_oserror *systemfiletable_get_file_length(int filetable_idx, off_t *length);
_kernel_oserror *systemfiletable_read_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_read);
_kernel_oserror *systemfiletable_write_bytes(int fd, void *buf, size_t count, off_t *, int *bytes_not_written);

static inline _kernel_oserror *systemfiletable_fsync(int systemtable_idx)
{
  array_t *system_filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(system_filetable, systemtable_idx);

  return osargs_flush_file(entry->file_handle);
}

void systemfiletable_print(void);

static inline const char *
systemfiletable_get_filename(int systemtable_idx)
{
  array_t *system_filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(system_filetable, systemtable_idx);
  return entry->filename;
}

static inline const char *
openfiletable_get_filename(int opentable_idx)
{
  openfiletable_entry *entry = openfiletable_get_entry(opentable_idx);
  return systemfiletable_get_filename(entry->systemtable_idx);
}

#endif
