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

extern _kernel_oserror *shm_op(_kernel_swi_regs * r);

#endif
