/* filetable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FILETABLE_H
#define FILETABLE_H

#include "array.h"

typedef struct _filetable_entry
{
  array_entry_base base;

  const char *filename;
  int ref_count;

  int file_handle;

} filetable_entry;

int filetable_open_file(const char *file, int flags, int mode);

/* Return 0 for success, -1 for error.  */
int filetable_close_file(int filetable_idx);

#endif
