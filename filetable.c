/* filetable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "filetable.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>
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
	      _INR(0,5),
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

/* Return index of new entry or -1 for failure.  */
static int filetable_add_entry(const char *canon_name)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;
  int i;
  for (i = 0; i < filetable->num_entries; i++)
    if (entry[i].filename == NULL)
      break;

  if (i == filetable->num_entries)
    {
      /* No free entries, increase array.  */
      if (!array_alloc(filetable))
	return -1;
      i = filetable->num_entries / 2;
    }

  entry[i].filename = canon_name; /* Take ownership of the allocated memory.  */
  entry[i].ref_count = 1;
  entry[i].file_handle = -1; /* Temporary until we write the code to actually open the file.  */

  return i;
}

int filetable_open_file(const char *file, int flags, int mode)
{
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
      entry_idx = filetable_add_entry(canon_name);
    }

  return entry_idx;
}

int filetable_close_file(int filetable_idx)
{
  array_t *filetable = &global.file_table;
  filetable_entry *entry = (filetable_entry *)filetable->entries;

  if (entry[filetable_idx].filename && entry[filetable_idx].file_handle != -1)
    {
      entry[filetable_idx].ref_count--;
      if (entry[filetable_idx].ref_count == 0)
	{
	  /* Close the file.  */
	  return 0;
	}
    }

  return -1;
}
