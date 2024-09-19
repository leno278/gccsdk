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
	      _INR(0,1) | _OUT(1),
	      0x5,		/*  in: r0=reason code - Read catalogue info for object */
	      canon_filename,	/*  in: r1=filename */
	      &object_type);	/* out: r0=object type, 0=not found, 1=file, 2=directory, 3=image */
  if (err)
    object_type = OBJECT_TYPE_ERROR;

  return object_type;
}

/* See if we already have the file open and return the index if so, otherwise -1.  */
static int filetable_scan_for_canon_filename(const char *canon_filename)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;
  int i;
  for (i = 0; i < filetable->num_entries; i++)
    if (strcmp(entry[i].filename, canon_filename) == 0)
      return i;

  return -1;
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

static _kernel_oserror *osargs_get_file_extent(int file_handle, unsigned *length)
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
	       0x0,		/*   in: r0=reason code - write bytes from given offset */
	       file_handle,	/*   in: r1=handle */
	       buf,		/*   in: r2=buffer */
	       count,		/*   in: r3=number of bytes to write */
	       file_pointer,	/*   in: r4=file pointer to start writing to */
	       bytes_not_written,/* out: r3=number of bytes not written */
	       new_file_pointer);/* out: r4=new file pointer */
}

static _kernel_oserror *filetable_add_entry(const char *canon_name, int flags, int *idx_ret)
{
  _kernel_oserror *err = NULL;
  int file_handle;
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  *idx_ret = -1;

  int type = file_object_type(canon_name);
  if (flags & O_CREAT)
    {
      if (type == OBJECT_TYPE_NOT_FOUND)
	{
	  err = osfile_create(canon_name, &file_handle);
	}
      else
	{
	  if (flags & O_EXCL)
	    return armeabisupport_EEXIST;
	  if (type == OBJECT_TYPE_DIR)
	    return armeabisupport_EINVAL;
	  err = osfile_open(canon_name, &file_handle);
	}
    }
  else
    {
      if (type == OBJECT_TYPE_NOT_FOUND)
	return armeabisupport_ENOENT;
      if (type == OBJECT_TYPE_DIR)
	return armeabisupport_EACCES;
      err = osfile_open(canon_name, &file_handle);
    }

  if (err)
    return err;

  int i;
  for (i = 0; i < filetable->num_entries; i++)
    if (entry[i].base.in_use == 0)
      break;

  if (i == filetable->num_entries)
    {
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(filetable);
      if (err)
	return err;
      i = filetable->num_entries / 2;
    }

  entry[i].base.in_use = 1;
  entry[i].filename = strdup(canon_name);
  entry[i].ref_count = 1;
  entry[i].file_handle = file_handle;

  *idx_ret = i;

  return NULL;
}

_kernel_oserror *filetable_open_file(const char *canon_name, int flags, int mode, int *idx_ret)
{
  _kernel_oserror *err = NULL;
  int entry_idx = filetable_scan_for_canon_filename(canon_name);
  if (entry_idx != -1)
    {
      array_t *filetable = &global.file_table;
      filetable_entry *entry = (filetable_entry *)filetable->entries;

      entry[entry_idx].ref_count++;
    }
  else
    {
      err = filetable_add_entry(canon_name, flags, &entry_idx);
    }

  *idx_ret = entry_idx;

  return err;
}

_kernel_oserror *filetable_close_file(int filetable_idx)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  if (entry[filetable_idx].filename && entry[filetable_idx].file_handle > 0)
    {
      entry[filetable_idx].ref_count--;
      if (entry[filetable_idx].ref_count == 0)
	{
	  osfile_close(entry[filetable_idx].file_handle);
	  free(entry[filetable_idx].filename);
	  entry[filetable_idx].filename = NULL;
	  entry[filetable_idx].file_handle = 0;
	  entry[filetable_idx].base.in_use = 0;
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}

_kernel_oserror *filetable_get_file_length(int filetable_idx, unsigned *length)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  return osargs_get_file_extent(entry[filetable_idx].file_handle, length);
}

_kernel_oserror *filetable_read_bytes(int filetable_idx,
				      void *buf,
				      size_t count,
				      off_t *file_ptr,
				      int *bytes_not_read)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  return osgbpb_read_bytes_at(entry[filetable_idx].file_handle,
			      buf,
			      count,
			      *file_ptr,
			      bytes_not_read,
			      file_ptr);
}

_kernel_oserror *filetable_write_bytes(int filetable_idx,
				       void *buf,
				       size_t count,
				       off_t *file_ptr,
				       int *bytes_not_written)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  return osgbpb_write_bytes_at(entry[filetable_idx].file_handle,
			       buf,
			       count,
			       *file_ptr,
			       bytes_not_written,
			       file_ptr);
}
