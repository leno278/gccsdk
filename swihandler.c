/* som_swihandler.c
 *
 * Copyright 2019-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include <stdlib.h>
#include "armeabisupport.h"
#include "memory.h"
#include "abort.h"
#include "stack.h"
#include "mmap.h"
#include "shm.h"
#include "fileop.h"
#include "fdop.h"
#include "main.h"
#include "types.h"

#define DEBUG_PRINT 1
#define DEBUG_REPORT 1
#include "debug.h"

_kernel_oserror *
module_swihandler (int number, _kernel_swi_regs * r, void *pw)
{
  _kernel_oserror *err = NULL;

  switch (number)
    {
    case ARMEABISupport_MemoryOp - ARMEABISupport_00:
      err = memory_op(r);
      break;
    case ARMEABISupport_AbortOp - ARMEABISupport_00:
      err = abort_op(r);
      break;
    case ARMEABISupport_StackOp - ARMEABISupport_00:
      err = stack_op(r);
      break;
    case ARMEABISupport_Cleanup - ARMEABISupport_00:
      err = armeabi_cleanup();
      break;
    case ARMEABISupport_MMapOp - ARMEABISupport_00:
      err = mmap_op(r);
      break;
    case ARMEABISupport_ShmOp - ARMEABISupport_00:
      err = shm_op(r);
      break;
    case ARMEABISupport_FdOp - ARMEABISupport_00:
      err = fd_op(r);
      break;
    case ARMEABISupport_FileOp - ARMEABISupport_00:
      err = file_op(r);
      break;
    case ARMEABISupport_SetAsParent - ARMEABISupport_00: {
      _kernel_oserror *err;
      app_object *app;
      if ((err = app_find(&app)) != NULL)
	return err;
      global.parent = app;
      break;
    }
    default:
      err = error_BAD_SWI;
      break;
    }

  return err;
}
