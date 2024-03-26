/* shm.h
 *
 * Copyright 2021-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef SHM_H
#define SHM_H

#include "types.h"

#define TRACING_SHM 0

#if TRACING_SHM
#define TRACE_SHM(format,...) reporter_printf(format, ##__VA_ARGS__)
#else
#define TRACE_SHM(format,...)
#endif
#define VALID_SHM 0x304D4853	/* SHM0 */

typedef struct _filedesc filedesc;

typedef struct _shm_object
{
  uint32_t valid;

  armeabisupport_allocator_mmap *allocator;
  mmap_block *block;

  int ref_count;
  bool linked;

  char *name;
} shm_object;

extern _kernel_oserror *shm_op(_kernel_swi_regs * r);

/* Return true if the object was deleted.  */
extern bool shm_deref_object(shm_object *);
extern _kernel_oserror *shm_close(shm_object *shm);
extern void shm_dump_object_list(void);

#endif
