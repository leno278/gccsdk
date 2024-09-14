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

static _kernel_oserror *filetable_create_osfile(const char *canon_filename, int *handle)
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

static _kernel_oserror *filetable_open_osfile(const char *canon_filename, int *handle)
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

static _kernel_oserror *filetable_close_osfile(int handle)
{
  _kernel_oserror *err;
  err = _swix(OS_Find,
	      _INR(0,1),
	      0x0,		/* in: r0=close file */
	      handle);		/* in: r1=file handle */
  return err;
}

static _kernel_oserror *filetable_add_entry(const char *canon_name, int flags, int *idx_ret)
{
  _kernel_oserror *err = NULL;
  int file_handle;
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  int type = file_object_type(canon_name);
  if (flags & O_CREAT)
    {
      if (type == OBJECT_TYPE_NOT_FOUND)
	{
	  err = filetable_create_osfile(canon_name, &file_handle);
	}
      else
	{
	  if (flags & O_EXCL)
	    return armeabisupport_EEXIST;
	  if (type == OBJECT_TYPE_DIR)
	    return armeabisupport_EINVAL;
	  err = filetable_open_osfile(canon_name, &file_handle);
	}
    }
  else
    {
      if (type == OBJECT_TYPE_NOT_FOUND)
	return armeabisupport_ENOENT;
      if (type == OBJECT_TYPE_DIR)
	return armeabisupport_EACCES;
      err = filetable_open_osfile(canon_name, &file_handle);
    }

  if (err)
    return err;

  int i;
  for (i = 0; i < filetable->num_entries; i++)
    if (entry[i].filename == NULL)
      break;

  if (i == filetable->num_entries)
    {
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(filetable);
      if (err)
	return err;
      i = filetable->num_entries / 2;
    }

  entry[i].filename = canon_name; /* Take ownership of the allocated memory.  */
  entry[i].ref_count = 1;
  entry[i].file_handle = file_handle;

  *idx_ret = i;

  return NULL;
}

_kernel_oserror *filetable_open_file(const char *file, int flags, int mode, int *idx_ret)
{
  _kernel_oserror *err = NULL;
  char *canon_name = canonicalise_filename(file);
  int entry_idx = filetable_scan_for_canon_filename(canon_name);
  if (entry_idx != -1)
    {
      array_t *filetable = &global.file_table;
      filetable_entry *entry = (filetable_entry *)filetable->entries;

      entry[entry_idx].ref_count++;

      /* This file is already open, so we don't need canon_name any more.  */
      free(canon_name);
    }
  else
    {
      /* Ownership of canon_name is passed to the table entry.  */
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
	  filetable_close_osfile(entry[filetable_idx].file_handle);
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}
