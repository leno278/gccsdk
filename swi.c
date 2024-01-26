/* swi.c
 *
 * Copyright 2019-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include <stdlib.h>
#include <kernel.h>
#include "memory.h"
#include "swi.h"
#include "debug.h"

#define DYNAMICAREA_FLAG_REQUIRES_SPECIFIC_PAGES	(1 << 8)
#define DYNAMICAREA_FLAG_ABORTABLE			(1 << 16)
#define DYNAMICAREA_FLAG_IS_PMP				(1 << 20)

#define DYNAMICAREA_ACCESS_COMPLETELY_ABORTABLE		(1 << 9)

#define ENABLE_TRACE 1

#if ENABLE_TRACE
#define TRACE(format, ...) \
do {\
reporter_printf(format, ##__VA_ARGS__); \
} while (0)
#else
#define TRACE(format, ...)
#endif

extern _kernel_oserror *da_handler(void);

static _kernel_oserror *
dynamic_area_create_pmp(const char *name,
			int max_size,
			unsigned type,
			dynamicarea_block *ret)
{
  _kernel_oserror *err;
  _kernel_swi_regs regs;
  unsigned initial_size = 0;

  /* Create a dynamic area for general memory allocation.  */
  regs.r[0] = 0;		/* reason code - create */
  regs.r[1] = -1;
  /* PMP dynamic areas must have a logical initial size of 0. */
  regs.r[2] = 0;
  regs.r[3] = -1;
  regs.r[4] = 0x80 |		/* Not draggable */
	       (DYNAMICAREA_FLAG_IS_PMP |
		DYNAMICAREA_FLAG_REQUIRES_SPECIFIC_PAGES |
		DYNAMICAREA_FLAG_ABORTABLE);
  regs.r[5] = max_size;
  regs.r[6] = (int)da_handler;
  regs.r[7] = -1;
  regs.r[8] = (int) name;
  regs.r[9] = bytes_to_pages (max_size);

  err = _kernel_swi (XOS_Bit | OS_DynamicArea, &regs, &regs);
  /* If there's an error, assume that it's because aborttrap isn't supported and
   * try again without.  */
  if (!err)
    {
      ret->number = regs.r[1];
      ret->base = (eabi_PTR)regs.r[3];
      ret->size = bytes_to_pages (initial_size);
      ret->max_size = bytes_to_pages (max_size);

      regs.r[0] = 24;
      regs.r[1] = ret->base;
      regs.r[2] = ret->base + ret->size;
      _kernel_swi(XOS_Bit | OS_Memory, &regs, &regs);
      ret->aborttrap_enabled = ((regs.r[1] & DYNAMICAREA_ACCESS_COMPLETELY_ABORTABLE) ==
				    DYNAMICAREA_ACCESS_COMPLETELY_ABORTABLE);
      TRACE("Dynamic area \'%s\' supports aborttrap = %d",name, ret->aborttrap_enabled);
   }

  return err;
}

static _kernel_oserror *
dynamic_area_create_non_pmp(const char *name, int max_size,
			    unsigned type,
			    dynamicarea_block *ret)
{
  _kernel_oserror *err;
  _kernel_swi_regs regs;
  unsigned initial_size = 0;

  if (type == ALLOCATOR_TYPE_SINGLE_USE)
    initial_size = max_size;
  else if (type == ALLOCATOR_TYPE_HEAP)
    initial_size = ALLOCATOR_HEAP_INITIAL_SIZE; /* Need 1 page initial size to hold heap descriptor (which is only 8 bytes).  */

  /* Create a dynamic area for general memory allocation.  */
  regs.r[0] = 0;		/* reason code - create */
  regs.r[1] = -1;
  regs.r[2] = (int)initial_size;
  regs.r[3] = -1;
  regs.r[4] = 0x80;		/* Not draggable */
  regs.r[5] = max_size;
  regs.r[6] = 0;
  regs.r[7] = 0;
  regs.r[8] = (int) name;

  err = _kernel_swi (XOS_Bit | OS_DynamicArea, &regs, &regs);
  if (!err)
    {
      ret->number = regs.r[1];
      ret->base = (eabi_PTR)regs.r[3];
      ret->size = bytes_to_pages (initial_size);
      ret->max_size = bytes_to_pages (max_size);
      ret->aborttrap_enabled = false;
    }

  return err;
}

_kernel_oserror *
dynamic_area_create (const char *name, int max_size,
		     unsigned type,
		     dynamicarea_block *ret)
{
  return (type == ALLOCATOR_TYPE_PAGE || type == ALLOCATOR_TYPE_MMAP) ?
      dynamic_area_create_pmp(name, max_size, type, ret) :
      dynamic_area_create_non_pmp(name, max_size, type, ret);
}
