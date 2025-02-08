/* filetable.c
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "filetable.h"
#include "opentable.h"
#include "fdtable.h"
#include "memory.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <swi.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define OBJECT_TYPE_ERROR -1
#define OBJECT_TYPE_NOT_FOUND 0
#define OBJECT_TYPE_FILE 1
#define OBJECT_TYPE_DIR 2
#define OBJECT_TYPE_IMAGE 2

static void opentable_log_file_entry(opentable_entry *ot_entry);

static opentable_methods file_methods =
{
  systemfiletable_deref_entry,
  opentable_unmap_file,
  opentable_log_file_entry
};

/* Return object type -1=error, 0=not found, 1=file, 2=directory, 3=image.  */
static int file_object_type(const char *canon_filename)
{
  _kernel_oserror *err;
  int object_type;
  err = _swix(OS_File,
	      _INR(0,1) | _OUT(0),
	      17,		/*  in: r0=reason code - Read catalogue info for object, no path */
	      canon_filename,	/*  in: r1=filename */
	      &object_type);	/* out: r0=object type, 0=not found, 1=file, 2=directory, 3=image */
  if (err)
    object_type = OBJECT_TYPE_ERROR;

  return object_type;
}

static _kernel_oserror *osfile_read_file_info(const char *canon_filename,
					      int *object_type,
					      void **load,
					      void **exec,
					      off_t *length,
					      unsigned *attributes)
{
  _kernel_oserror *err;
  err = _swix(OS_File,
	      _INR(0,1) | _OUT(0) | _OUTR(2,5),
	      17,		/*  in: r0=reason code - Read catalogue info for object, no path */
	      canon_filename,	/*  in: r1=filename */
	      object_type,	/* out: r0=object type, 0=not found, 1=file, 2=directory, 3=image */
	      load,
	      exec,
	      length,
	      attributes);
  return err;
}

/* See if we already have the file open and return the index if so, otherwise -1.  */
static int filetable_scan_for_canon_filename(const char *canon_filename)
{
  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;
  int i;
  for (i = 0; i < filetable->num_entries; i++)
    if (strcmp(entry[i].filename, canon_filename) == 0)
      return i;

  return ARRAY_INVALID_INDEX;
}

static _kernel_oserror *osfile_create(const char *canon_filename, int *handle)
{
  _kernel_oserror *err;
  err = _swix(OS_Find,
	      _INR(0,1) | _OUT(0),
	      0x80,		/*  in: r0=create empty file */
	      canon_filename,	/*  in: r1=filename */
	      handle);		/* out: r0=file handle */
  if (err)
      *handle = 0;

  return err;
}

static _kernel_oserror *osfile_open(const char *canon_filename, int *handle)
{
  _kernel_oserror *err;
  err = _swix(OS_Find,
	      _INR(0,1) | _OUT(0),
	      0xC0,		/*  in: r0=open existing file */
	      canon_filename,	/*  in: r1=filename */
	      handle);		/* out: r0=file handle */
  if (err)
      *handle = 0;

  return err;
}

static inline _kernel_oserror *osfile_close(int handle)
{
  if (handle == 0)
    return NULL;

  return _swix(OS_Find,
	       _INR(0,1),
	       0x0,		/* in: r0=close file */
	       handle);		/* in: r1=file handle */
}

static inline _kernel_oserror *osargs_set_pointer(int file_handle, unsigned file_pointer)
{
  return _swix(OS_Args,
	       _INR(0,2),
	       0x1,		/* in: r0=reason code - write pointer */
	       file_handle,	/* in: r1=handle */
	       file_pointer);	/* in: r2=pointer */
}

static inline _kernel_oserror *osargs_get_pointer(int file_handle, unsigned *file_pointer)
{
  return _swix(OS_Args,
	       _INR(0,1)|_OUT(2),
	       0x0,		/*  in: r0=reason code - read pointer */
	       file_handle,	/*  in: r1=handle */
	       file_pointer);	/* out: r2=pointer */
}

static _kernel_oserror *osargs_get_file_extent(int file_handle, off_t *length)
{
  return _swix(OS_Args,
	       _INR(0,1)|_OUT(2),
	       0x2,		/*  in: r0=reason code - read extent */
	       file_handle,	/*  in: r1=handle */
	       length);		/* out: r2=length */
}

static _kernel_oserror *osgbpb_read_bytes_at(int file_handle,
					     void *buf,
					     int count,
					     off_t file_pointer,
					     int *bytes_not_read,
					     off_t *new_file_pointer)
{
  return _swix(OS_GBPB,
	       _INR(0,4) | _OUTR(3,4),
	       0x3,		/*   in: r0=reason code - read bytes from given offset */
	       file_handle,	/*   in: r1=handle */
	       buf,		/*   in: r2=buffer */
	       count,		/*   in: r3=number of bytes to read */
	       file_pointer,	/*   in: r4=file pointer to start reading from */
	       bytes_not_read,	/*  out: r3=number of bytes not read */
	       new_file_pointer);/* out: r4=new file pointer */
}

static _kernel_oserror *osgbpb_write_bytes_at(int file_handle,
					      void *buf,
					      int count,
					      off_t file_pointer,
					      int *bytes_not_written,
					      off_t *new_file_pointer)
{
  return _swix(OS_GBPB,
	       _INR(0,4) | _OUTR(3,4),
	       0x1,		/*   in: r0=reason code - write bytes at given offset */
	       file_handle,	/*   in: r1=handle */
	       buf,		/*   in: r2=buffer */
	       count,		/*   in: r3=number of bytes to write */
	       file_pointer,	/*   in: r4=file pointer to start writing to */
	       bytes_not_written,/* out: r3=number of bytes not written */
	       new_file_pointer);/* out: r4=new file pointer */
}

static _kernel_oserror *systemfiletable_add_entry(const char *canon_name, int flags, int *idx_ret)
{
  TRACE("%s: name=%s", __func__, canon_name);

  _kernel_oserror *err = NULL;
  int file_handle = 0;
  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];

  int type = file_object_type(canon_name);
  if (flags & O_CREAT)
    {
      LOG("%s: O_CREAT specified, type=%d", __func__, type);
      if (type == OBJECT_TYPE_NOT_FOUND)
	{
	  err = osfile_create(canon_name, &file_handle);
	}
      else
	{
	  if (flags & O_EXCL)
	    err = armeabisupport_EEXIST;
	  else if (type == OBJECT_TYPE_DIR)
	    err = armeabisupport_EINVAL;
	  else
	    err = osfile_open(canon_name, &file_handle);
	}
    }
  else
    {
      if (type == OBJECT_TYPE_NOT_FOUND)
	err = armeabisupport_ENOENT;
      else if (type == OBJECT_TYPE_DIR)
	err = armeabisupport_EACCES;
      else
	err = osfile_open(canon_name, &file_handle);
    }

  if (err)
    goto error_return;
  if (!file_handle)
    {
      err = armeabisupport_EOPSYS;
      goto error_return;
    }

  if ((err = array_alloc_entry(filetable, idx_ret)) == NULL)
    {
      systemfiletable_entry *entry = (systemfiletable_entry *)array_get_entry_ptr(filetable, *idx_ret);

      entry->filename = strdup(canon_name);
      entry->table_base.ref_count = 1;
      entry->file_handle = file_handle;

      return NULL;
    }

error_return:
  LOG("%s:   Error=%s", __func__, err->errmess);

  if (file_handle)
    osfile_close(file_handle);

  *idx_ret = ARRAY_INVALID_INDEX;

  return err;
}

_kernel_oserror *systemfiletable_open_file(const char *canon_name, int flags, int mode, int *idx_ret)
{
  TRACE("%s: name=%s", __func__, canon_name);

  _kernel_oserror *err = NULL;
  int entry_idx = filetable_scan_for_canon_filename(canon_name);
  if (entry_idx != ARRAY_INVALID_INDEX)
    {
      array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
      systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

      entry[entry_idx].table_base.ref_count++;
      LOG("%s: Found existing entry for file %s", __func__, canon_name);
      LOG("%s:   at index %d, ref count now %d", __func__, entry_idx, entry[entry_idx].table_base.ref_count);
    }
  else
    {
      LOG("%s: No entry found for file %s", __func__, canon_name);
      err = systemfiletable_add_entry(canon_name, flags, &entry_idx);
    }

  *idx_ret = entry_idx;
  LOG("%s: Return index %d for file %s", __func__, entry_idx, canon_name);

  return err;
}

_kernel_oserror *systemfiletable_deref_entry(int filetable_idx)
{
  TRACE("%s: filetable_idx=%d", __func__, filetable_idx);

  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  if (array_entry_is_in_use(&entry[filetable_idx].table_base.array_base))
    {
      entry[filetable_idx].table_base.ref_count--;
      LOG("%s:   System table ref count now %d for file handle %d (%s)", __func__,
	  entry[filetable_idx].table_base.ref_count,
	  entry[filetable_idx].file_handle,
	  entry[filetable_idx].filename);
      if (entry[filetable_idx].table_base.ref_count == 0)
	{
	  LOG("%s:   Closing file handle %d (%s)", __func__,
	      entry[filetable_idx].file_handle,
	      entry[filetable_idx].filename);
	  if (entry[filetable_idx].file_handle > 0)
	    {
	      osfile_close(entry[filetable_idx].file_handle);
	      entry[filetable_idx].file_handle = 0;
	    }
	  if (entry[filetable_idx].filename)
	    {
	      free((void *)entry[filetable_idx].filename);
	      entry[filetable_idx].filename = NULL;
	    }
	  array_entry_mark_unused(&entry[filetable_idx].table_base.array_base);
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}

_kernel_oserror *systemfiletable_get_file_length(int filetable_idx, off_t *length)
{
//  TRACE("%s: filetable_idx=%d", __func__, filetable_idx);

  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  return osargs_get_file_extent(entry[filetable_idx].file_handle, length);
}

_kernel_oserror *systemfiletable_read_bytes(int systemtable_idx,
					    void *buf,
					    size_t count,
					    off_t *file_ptr,
					    int *bytes_not_read)
{

  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  TRACE("%s: systemtable_idx=%d, buf=%p, count=%d, file=%s",
	__func__, systemtable_idx, buf, count, entry[systemtable_idx].filename);

  return osgbpb_read_bytes_at(entry[systemtable_idx].file_handle,
			      buf,
			      count,
			      *file_ptr,
			      bytes_not_read,
			      file_ptr);
}

_kernel_oserror *systemfiletable_write_bytes(int systemtable_idx,
					     void *buf,
					     size_t count,
					     off_t *file_ptr,
					     int *bytes_not_written)
{

  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  TRACE("%s: systemtable_idx=%d, buf=%p, count=%d, file=%s",
	__func__, systemtable_idx, buf, count ,entry[systemtable_idx].filename);

  return osgbpb_write_bytes_at(entry[systemtable_idx].file_handle,
			       buf,
			       count,
			       *file_ptr,
			       bytes_not_written,
			       file_ptr);
}

_kernel_oserror *
systemfiletable_get_info(int filetable_idx,
			 int *type,
			 const char **name,
			 void **load,
			 void **exec,
			 off_t *length,
			 unsigned *attributes)
{
  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  *name = entry[filetable_idx].filename;

  return osfile_read_file_info(entry[filetable_idx].filename, type, load, exec, length, attributes);
}

_kernel_oserror *opentable_add_file(int systemtable_idx, int flags, int mode, int *idx_ret)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  _kernel_oserror *err;
  if ((err = opentable_add_entry(opentable_type_FILE,
				 systemtable_idx,
				 &file_methods,
				 idx_ret)) == NULL)
    {
      opentable_entry *entry = opentable_get_entry(*idx_ret);

      entry->systemtable_idx = systemtable_idx;
      entry->file.flags = flags;
      entry->file.mode = mode;
      entry->file.file_offset = 0;

      err = filelocklist_ctor(&entry->file.locks);
    }

  if (err)
    {
      LOG("%s:     err=%s", __func__, err->errmess);
    }

  return err;
}

_kernel_oserror *opentable_unmap_file(armeabisupport_allocator *allocator,
				      mmap_block *block)
{
  TRACE("%s: opentable_idx=%d", __func__, block->opentable_idx);

  opentable_entry *ot_entry = opentable_get_entry(block->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    return armeabisupport_EBADF;

#if ENABLE_LOGGING
  systemfiletable_entry *st_entry =
		(systemfiletable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE],
							     ot_entry->systemtable_idx);
  LOG("%s: shared memory object %p, \"%s\", open table index %d, addr=%p, len=%d, oref=%d, sref=%d",
       __func__,
       st_entry,
       st_entry->filename,
       block->opentable_idx,
       page_to_addr(&allocator->base, block->start_page),
       pages_to_bytes(block->end_page - block->start_page),
       ot_entry->ref_count,
       st_entry->table_base.ref_count);
#endif

  off_t offset = block->offset;
  int bytes_not_written;
  _kernel_oserror *err;
  err = systemfiletable_write_bytes(ot_entry->systemtable_idx,
				    page_to_addr(allocator, block->start_page),
				    pages_to_bytes(block->end_page - block->start_page),
				    &offset,
				    &bytes_not_written);
  if (err)
    {
      LOG("%s: OS Error: %s", __func__, err->errmess);
    }

  /* This will only call allocator_munmap_block() when the final reference is released.  */
  opentable_deref_entry(block->opentable_idx);

  /* Don't return the error, a file error shouldn't halt the process of unmapping
   * the memory.  */
  return NULL;
}

const char *openfiletable_get_filename(int opentable_idx)
{
  opentable_entry *entry = opentable_get_entry(opentable_idx);
  return systemfiletable_get_filename(entry->systemtable_idx);
}

void openfiletable_print(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  printf("%-4s %-12s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF", "OFFSET");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_FILE && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  printf("%-4d %-12d %-12d %-12ld %s\n",
		 i,
		 ot_entry[i].systemtable_idx,
		 ot_entry[i].ref_count,
		 ot_entry[i].file.file_offset,
		 systemfiletable_get_filename(ot_entry[i].systemtable_idx));
	}
    }
}

void systemfiletable_print(void)
{
  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  printf("%-4s %-12s %-12s %s\n", "Idx", "Ref", "File handle", "File");
  for (int i = 0; i < filetable->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  printf("%-4d %-12d %-12d %s\n", i, entry[i].table_base.ref_count, entry[i].file_handle, entry[i].filename);
	}
    }
}

static void opentable_log_file_entry(opentable_entry *ot_entry)
{
  int index = opentable_get_index(ot_entry);
  systemfiletable_entry *system_entry =
	      (systemfiletable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE],
							   ot_entry->systemtable_idx);
  reporter_printf("%4d %4s %4d %6d",
		  index,
		  "FILE",
		  ot_entry->ref_count,
		  ot_entry->systemtable_idx);
  reporter_printf("                   SYSTEM: REF:%3d HANDLE:%4d PATH:%s",
		  system_entry->table_base.ref_count,
		  system_entry->file_handle,
		  system_entry->filename);
}

#if ENABLE_LOGGING

void openfiletable_log(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  reporter_printf("%-4s %-12s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF", "OFFSET");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_FILE && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  reporter_printf("%-4d %-12d %-12d %-12ld %s\n",
			  i,
			  ot_entry[i].systemtable_idx,
			  ot_entry[i].ref_count,
			  ot_entry[i].file.file_offset,
			  systemfiletable_get_filename(ot_entry[i].systemtable_idx));
	}
    }
}

void systemfiletable_log(void)
{
  array_t *filetable = &global.system_table[fd_type_SHARED_FILE];
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  reporter_printf("%-4s %-12s %-12s %s\n", "Idx", "Ref", "File handle", "File");
  for (int i = 0; i < filetable->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  reporter_printf("%-4d %-12d %-12d %s\n", i, entry[i].table_base.ref_count, entry[i].file_handle, entry[i].filename);
	}
    }
}

#endif
