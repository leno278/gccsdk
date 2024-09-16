/* fdtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdtable.h"
#include "filetable.h"
#include "types.h"
#include "main.h"
#include <kernel.h>
#include <stdlib.h>
#include <swis.h>

#define FDTABLE_BASE_FD 10000

static inline fdtable_entry *get_fdtable_entry(int fd)
{
  app_object *app;
  if ((app_find(&app)) != NULL)
    return NULL;
  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  return &entry[fd - FDTABLE_BASE_FD];
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

_kernel_oserror *fdtable_open_file(const char *file, int flags, int mode, int *fd_ret)
{
  _kernel_oserror *err = NULL;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  array_t *fdtable = &app->fd_table;
  fdtable_entry *entry = (fdtable_entry *)fdtable->entries;

  char *canon_name = canonicalise_filename(file);
  if (!canon_name)
    return armeabisupport_ENOMEM;

  int filetable_idx;
  err = filetable_open_file(canon_name, flags, mode, &filetable_idx);
  if (err)
    goto error_return;

  int i;
  for (i = 0; i < fdtable->num_entries; i++)
    if (entry[i].base.in_use == 0)
      break;

  if (i == fdtable->num_entries)
    {
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(fdtable);
      if (err)
	goto error_return;
      i = fdtable->num_entries / 2;
    }

  free(canon_name);

  *fd_ret = i + FDTABLE_BASE_FD;
  entry[i].base.in_use = 1;
  entry[i].filetable_idx = filetable_idx;

  return NULL;

error_return:
  free(canon_name);
  *fd_ret = -1;
  return err;
}

_kernel_oserror *fdtable_close_file(int fd)
{
  fdtable_entry *entry = get_fdtable_entry(fd);
  if (!entry)
    return armeabisupport_EBADF;

  filetable_close_file(entry->filetable_idx);
  entry->base.in_use = 0;

  return NULL;
}
