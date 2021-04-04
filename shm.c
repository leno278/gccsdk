/* shm.c
 *
 * Copyright 2021 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shm.h"
#include <stddef.h>

#define SHMOP_OPEN	0
#define SHMOP_UNLINK	1

/* r0 = reason code:
*	0 - shm_open
*	  Entry:
*	    r1 = name (beginning with a '/' and upto 256 chars)
*	    r2 = flags
*	    r3 = mode
*	  Exit:
*	    r0 = handle (not guarenteed to be lowest fd in current process) if success (V flag
*			 clear) or error pointer (V flag set).
*	1 - shm_unlink
*	  Entry:
*	    r1 = name
*	  Exit:
*	    r0 = 0 for success (V flag clear) or error pointer (V flag set).
*/

_kernel_oserror *
shm_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  int reason = r->r[0];

  switch (reason)
  {
  case SHMOP_OPEN:
    break;
  case SHMOP_UNLINK:
    break;
  }

  return err;
}
