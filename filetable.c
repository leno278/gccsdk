/* filetable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "filetable.h"
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
  array_t *filetable = &global.system_filetable;
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
  array_t *filetable = &global.system_filetable;

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
      entry->ref_count = 1;
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
      array_t *filetable = &global.system_filetable;
      systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

      entry[entry_idx].ref_count++;
      LOG("%s: Found existing entry for file %s", __func__, canon_name);
      LOG("%s:   at index %d, ref count now %d", __func__, entry_idx, entry[entry_idx].ref_count);
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

_kernel_oserror *systemfiletable_close_file(int filetable_idx)
{
  TRACE("%s: filetable_idx=%d", __func__, filetable_idx);

  array_t *filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  if (entry[filetable_idx].base.in_use)
    {
      entry[filetable_idx].ref_count--;
      LOG("%s:   Ref count now %d for file %s", __func__,
	  entry[filetable_idx].ref_count,
	  entry[filetable_idx].filename);
      if (entry[filetable_idx].ref_count == 0)
	{
	  LOG("%s:   Closing file %s", __func__, entry[filetable_idx].filename);
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
	  entry[filetable_idx].base.in_use = 0;
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}

_kernel_oserror *systemfiletable_get_file_length(int filetable_idx, off_t *length)
{
//  TRACE("%s: filetable_idx=%d", __func__, filetable_idx);

  array_t *filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  return osargs_get_file_extent(entry[filetable_idx].file_handle, length);
}

_kernel_oserror *systemfiletable_read_bytes(int systemtable_idx,
					    void *buf,
					    size_t count,
					    off_t *file_ptr,
					    int *bytes_not_read)
{

  array_t *filetable = &global.system_filetable;
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

  array_t *filetable = &global.system_filetable;
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
  array_t *filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  *name = entry[filetable_idx].filename;

  return osfile_read_file_info(entry[filetable_idx].filename, type, load, exec, length, attributes);
}

void systemfiletable_print(void)
{
  array_t *filetable = &global.system_filetable;
  systemfiletable_entry *entry = (systemfiletable_entry *)filetable->entries;

  printf("%-4s %-12s %-12s %s", "Idx", "Ref", "File handle", "File");
  for (int i = 0; i < filetable->num_entries; i++)
    {
      if (entry[i].base.in_use)
	{
	  printf("%-4d %-12d %-12d %s", i, entry[i].ref_count, entry[i].file_handle, entry[i].filename);
	}
    }
}

_kernel_oserror *openfiletable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  array_t *filetable = &global.open_filetable;

  LOG("%s:     systemtable index=%d", __func__, systemtable_idx);

  _kernel_oserror *err;
  if ((err = array_alloc_entry(filetable, idx_ret)) == NULL)
    {
      openfiletable_entry *entry = (openfiletable_entry *)array_get_entry_ptr(filetable, *idx_ret);

      entry->systemtable_idx = systemtable_idx;
      entry->flags = flags;
      entry->mode = mode;
      entry->file_offset = 0;
      entry->ref_count = 1;

      if ((err = filelocklist_ctor(&entry->locks)) == NULL)
	return NULL;
    }

  LOG("%s:     err=%s", __func__, err->errmess);
  return err;
}

_kernel_oserror *openfiletable_deref_entry(int openfiletable_idx)
{
  TRACE("%s: openfiletable_idx=%d", __func__, openfiletable_idx);
  _kernel_oserror *err = NULL;

  openfiletable_entry *openfile_entry = openfiletable_get_entry(openfiletable_idx);
  openfile_entry->ref_count--;
  LOG("%s:   new ref_count = %d, close file", __func__, openfile_entry->ref_count);
  if (openfile_entry->ref_count == 0)
    {
      err = systemfiletable_close_file(openfile_entry->systemtable_idx);
      array_entry_mark_unused(&openfile_entry->base);
    }

#if ENABLE_LOGGING
  systemfiletable_print();
  openfiletable_print();
#endif

  return err;
}

void openfiletable_print(void)
{
  array_t *filetable = &global.open_filetable;
  openfiletable_entry *entry = (openfiletable_entry *)filetable->entries;

  printf("%-4s %-12s %-12s %-12s PATH", "IDX", "SYSIDX", "REF", "OFFSET");
  for (int i = 0; i < filetable->num_entries; i++)
    {
      if (entry[i].base.in_use)
	{
	  printf("%-4d %-12d %-12d %-12ld %s",
		 i,
		 entry[i].systemtable_idx,
		 entry[i].ref_count,
		 entry[i].file_offset,
		 systemfiletable_get_filename(entry[i].systemtable_idx));
	}
    }
}
