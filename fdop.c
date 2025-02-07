/* fdop.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "fdop.h"
#include "fdtable.h"
#include "opentable.h"
#include "array.h"
#include "shm.h"
#include "memory.h"
#include "main.h"
#include "swi.h"
#include "debug.h"

#define FDOP_VALIDATE		0
#define FDOP_CLOSE		1
#define FDOP_SEEK		2
#define FDOP_READ		3
#define FDOP_WRITE		4
#define FDOP_GETINFO		5
#define FDOP_SETLOCK		6
#define FDOP_GETLOCK		7
#define FDOP_FSYNC		8
#define FDOP_FTRUNCATE		9
#define FDOP_DUP		10
#define FDOP_DUP2		11
#define FDOP_GETFD		12
#define FDOP_SETFD		13

#define ENABLE_LOGGING 0
#include "log.h"

#define ENABLE_TRACING 0
#include "trace.h"

/* r0 = reason code:
 *	0 - validate
 *	  Entry
 *	    r1 = fd
 *	  Exit
 *	    r0 = error pointer (V set) if unknown fd, or 0 (V clear) if known
 *	    r1 = type of fd, 0 if invalid
 *	1 - close
 *	  Entry
 *	    r1 = fd
 *	  Exit
 *	    r0 = error pointer (V set) if unknown fd, or 0 (V clear) if successful
 *	2 - seek
 *	  Entry:
 *	    r1 = fd
 *	    r2 = offset
 *	    r3 = whence
 *	  Exit:
 *	    r0 = new offset
 *	3 - read
 *	  Entry:
 *	    r1 = fd
 *	    r2 = buffer
 *	    r3 = count
 *	  Exit:
 *	    r0 = number of bytes read
 *	4 - write
 *	  Entry:
 *	    r1 = fd
 *	    r2 = buffer
 *	    r3 = count
 *	  Exit:
 *	    r0 = number of bytes written
 *	5 - GetInfo
 *	  Entry:
 *	    r1 = fd
 *	  Exit:
 *	    r0 = object type (Redundant as we only operate on regular files?)
 *	    r1 = filename
 *	    r2 = loadaddr
 *	    r3 = execaddr
 *	    r4 = length
 *	    r5 = attributes
 *	6 - SetLock
 *	  Entry:
 *	    r1 = fd
 *	    r2 = type
 *	    r3 = whence
 *	    r4 = start
 *	    r5 = len
 *	7 - GetLock
 *	  Entry:
 *	    r1 = fd
 *	    r2 = type
 *	    r3 = whence
 *	    r4 = start
 *	    r5 = len
 *	  Exit:
 *	    r2 = type updated
 *	    r3 = whence updated
 *	    r4 = start updated
 *	    r5 = len updated
 *	8 - fsync
 *	  Entry:
 *	    r1 = fd
 *	9 - ftruncate
 *	  Entry:
 *	    r1 = fd
 *	    r2 = new extent
 *     10 - dup
 *	  Entry:
 *	    r1 = fd
 *	  Exit:
 *	    r0 = new FD (will not be the lowest fd in current process) if success (V flag
 *			     clear) or error pointer (V flag set).
 *     11 - dup2
 *	  Entry:
 *	    r1 = source FD
 *	    r2 = destination FD
 *	  Exit:
 *	    r0 = new FD if successful (V flag clear) or error pointer (V flag set).
 *     12 - GETFD (fcntl)
 *	  Entry:
 *	    r1 = fd
 *	  Exit:
 *	    r0 = 1 if close-on-exec flag set, 0 if not (V flag clear), or error pointer (V flag set)
 *     13 - SETFD (fcntl)
 *	  Entry:
 *	    r1 = fd
 *	    r2 = 0 to clear close-on-exec flag, any other value to set flag
*/

_kernel_oserror *
fd_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  int reason = r->r[0];

  fdtable_entry *fd;
  if ((fd = get_fdtable_entry(app, r->r[1])) == NULL
	|| !array_entry_is_in_use(&fd->base))
    {
      if (reason == FDOP_SEEK)
	r->r[0] = -1;
      return armeabisupport_EBADF;
    }

  switch (reason)
  {
  case FDOP_VALIDATE: {
    opentable_entry *ot_entry = opentable_get_entry(fd->opentable_idx);
    r->r[1] = ot_entry->type;
    if (ot_entry->type == opentable_type_INVALID)
      err = armeabisupport_EBADF;
    break;
  }
  case FDOP_CLOSE:
    TRACE("%s:FDOP_CLOSE: fd=%d", __func__, r->r[1]);

    err = fdtable_close(app, fd);
    break;
  case FDOP_SEEK:
    TRACE("%s:FDOP_SEEK: fd=%d, offset=%d, whence=%d",
	  __func__,
	  r->r[1],
	  r->r[2],
	  r->r[3]);
    err = fdtable_lseek(fd, r->r[2], r->r[3], (off_t *)&r->r[0]);
    break;
  case FDOP_READ:
    TRACE("%s:FDOP_READ: fd=%d, buf=%p, count=%ld",
	  __func__,
	  r->r[1],
	  r->r[2],
	  r->r[3]);
    err = fdtable_read(fd, (void *)r->r[2], r->r[3], &r->r[0]);
    break;
  case FDOP_WRITE:
    TRACE("%s:FDOP_WRITE: fd=%d, buf=%p, count=%ld",
	  __func__,
	  r->r[1],
	  r->r[2],
	  r->r[3]);
    err = fdtable_write(fd, (void *)r->r[2], r->r[3], &r->r[0]);
    break;
  case FDOP_GETINFO:
    err = fdtable_get_info(fd,
			   &r->r[0],
			   (const char **)&r->r[1],
			   (void **)&r->r[2],
			   (void **)&r->r[3],
			   (off_t *)&r->r[4],
			   (unsigned *)&r->r[5]);
    break;
  case FDOP_SETLOCK:
    TRACE("%s:FDOP_SETLOCK: fd=%d, type=%d, whence=%d, offset=%d, len=%d",
	  __func__,
	  r->r[1],
	  r->r[2],
	  r->r[3],
	  r->r[4],
	  r->r[5]);
    err = fdtable_set_lock(app,
			   fd,
			   &r->r[2],		/* type */
			   &r->r[3],		/* whence */
			   (off_t *)&r->r[4],	/* start */
			   (off_t *)&r->r[5]);	/* len */
    break;
  case FDOP_GETLOCK:
    TRACE("%s:FDOP_GETLOCK: fd=%d, type=%d, whence=%d, offset=%d, len=%d",
	  __func__,
	  r->r[1],
	  r->r[2],
	  r->r[3],
	  r->r[4],
	  r->r[5]);
    err = fdtable_get_lock(fd,
			   &r->r[2],		/* type */
			   &r->r[3],		/* whence */
			   (off_t *)&r->r[4],	/* start */
			   (off_t *)&r->r[5]);	/* len */
    break;
  case FDOP_FSYNC:
    TRACE("%s:FDOP_FSYNC: fd=%d", __func__, r->r[1]);
    err = fdtable_fsync(fd);
    break;
  case FDOP_FTRUNCATE:
    TRACE("%s:FDOP_FTRUNCATE: fd=%d, new_extent=%ld",
	  __func__,
	  r->r[1],
	  r->r[2]);
    err = fdtable_ftruncate(fd, r->r[2]);
    break;
  case FDOP_DUP:
    TRACE("%s:FDOP_DUP: fd=%d",
	  __func__,
	  r->r[1]);
    err = fdtable_dup(fd, &r->r[0]);
    break;
  case FDOP_DUP2:
    TRACE("%s:FDOP_DUP2: old fd=%d, new fd=%d",
	  __func__,
	  r->r[1],
	  r->r[2]);
    if ((err = fdtable_dup2(fd, r->r[2])) == NULL)
      r->r[0] = r->r[2];
    break;
  case FDOP_SETFD:
    TRACE("%s:FDOP_SETFD: fd=%d, close_on_exec=%d",
	  __func__,
	  r->r[1],
	  r->r[2]);
    fd->close_on_exec = (r->r[2] != 0);
    break;
  case FDOP_GETFD:
    TRACE("%s:FDOP_SETFD: fd=%d",
	  __func__,
	  r->r[1]);
    r->r[0] = fd->close_on_exec;
    break;
  default:
    err = armeabisupport_bad_reason;
    break;
  }

  return err;
}
