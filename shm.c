/* shm.c
 *
 * Copyright 2021-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shm.h"
#include "memory.h"
#include "fdtable.h"
#include "swi.h"
#include "link_list.h"
#include "types.h"
#include "debug.h"
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>

#define SHMOP_OPEN	0
#define SHMOP_UNLINK	1
#define SHMOP_DUP	2

#define TRACE_SWIS 0

#define ENABLE_LOGGING 0
#include "log.h"

#define ENABLE_TRACING 0
#include "trace.h"

#if TRACE_SWIS
#define TRACE_SWI(format,...) reporter_printf(format, ##__VA_ARGS__)
#else
#define TRACE_SWI(format,...)
#endif

/* r0 = reason code:
*	0 - shm_open
*	  Entry:
*	    r1 = name (beginning with a '/' and upto 256 chars)
*	    r2 = flags
*	    r3 = mode
*	  Exit:
*	    r0 = handle (will not be lowest fd in current process) if success (V flag
*			 clear) or error pointer (V flag set).
*	1 - shm_unlink
*	  Entry:
*	    r1 = name
*	  Exit:
*	    r0 = 0 for success (V flag clear) or error pointer (V flag set).
*	2 - dup
*	  Entry:
*	    r1 = fd
*	  Exit:
*	    r0 = new handle (will not be the lowest fd in current process) if success (V flag
*			     clear) or error pointer (V flag set).
*/

static _kernel_oserror *shm_unlink(const char *name);

_kernel_oserror *
shm_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  int reason = r->r[0];

  switch (reason)
  {
  case SHMOP_OPEN: {
    TRACE_SWI("SWI \"SHMOp\", SHMOP_OPEN, name=\"%s\", oflag=0x%X, mode=0x%X", r->r[1], r->r[2], r->r[3]);

    int fd;
    err = fdtable_open_shm((const char *)r->r[1], r->r[2], r->r[3], &fd);
    if (!err)
      {
	r->r[0] = fd;
	TRACE_SWI("  Result = %d", r->r[0]);
      }
    break;
  }
  case SHMOP_UNLINK:
    TRACE_SWI("SWI \"SHMOp\", SHMOP_UNLINK, name=\"%s\"", r->r[1]);
    err = shm_unlink((const char *)r->r[1]);
    break;
  }

  if (err)
    {
      TRACE_SWI("  Error = %s", err->errmess);
    }

  return err;
}

static _kernel_oserror *
shm_unlink(const char *name)
{
#if 0
  filedesc *fd;

  if (strlen(name) > PATH_MAX)
    return armeabisupport_ENAMETOOLONG;

  fd = find_shm_object(name);
  if (!fd || fd->shm->valid != VALID_SHM)
    {
      LOG("shm_unlink: Shared memory object \"%s\" not found", name);
      return armeabisupport_ENOENT;
    }

  fd->shm->linked = false;
/*  if (fd->name)
    {
      rma_free(fd->name);
      fd->name = NULL;
    }*/
#endif
  return NULL;
}
