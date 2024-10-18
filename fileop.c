/* fileop.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "fileop.h"
#include "fdtable.h"

#define ENABLE_LOGGING 1
#include "log.h"

#define FILEOP_OPEN	0
#define FILEOP_CLOSE	1
#define FILEOP_SEEK	2
#define FILEOP_READ	3
#define FILEOP_WRITE	4
#define FILEOP_GETINFO	5
#define FILEOP_LOCK	6
#define FILEOP_UNLOCK	7
#define FILEOP_GETLOCK	8

/* r0 = reason code:
 *	0 - file open
 *	  Entry:
 *	    r1 = filename
 *	    r2 = flags
 *	    r3 = mode
 *	  Exit:
 *	    r0 = fd
 *	1 - file close
 *	  Entry:
 *	    r1 = fd
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
*/
_kernel_oserror *
file_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  int reason = r->r[0];

  switch (reason)
  {
  case FILEOP_OPEN:
    err = fdtable_open_file((const char *)r->r[1],
			    r->r[2],
			    r->r[3],
			    &r->r[0]);
    break;
  case FILEOP_CLOSE:
    err = fdtable_close_file(r->r[1]);
    break;
  case FILEOP_SEEK:
    err = fdtable_lseek(r->r[1], r->r[2], r->r[3], (off_t *)&r->r[0]);
    break;
  case FILEOP_READ:
    err = fdtable_read(r->r[1], (void *)r->r[2], r->r[3], &r->r[0]);
    break;
  case FILEOP_WRITE:
    err = fdtable_write(r->r[1], (void *)r->r[2], r->r[3], &r->r[0]);
    break;
  case FILEOP_GETINFO:
    err = fdtable_get_info(r->r[1],
			   &r->r[0],
			   (const char **)&r->r[1],
			   (void **)&r->r[2],
			   (void **)&r->r[3],
			   (off_t *)&r->r[4],
			   (unsigned *)&r->r[5]);
    break;
  }

  return err;
}
