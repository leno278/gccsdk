/* fileop.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "fileop.h"
#include "fdtable.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define FILEOP_OPEN		0

/* r0 = reason code:
 *	0 - file open
 *	  Entry:
 *	    r1 = filename
 *	    r2 = flags
 *	    r3 = mode
 *	  Exit:
 *	    r0 = fd
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
  }

  return err;
}
