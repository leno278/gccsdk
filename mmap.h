/* mmap.h
 *
 * Copyright 2021-2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef MMAP_H
#define MMAP_H

#include <kernel.h>
#include "memory.h"
#include "types.h"

extern _kernel_oserror *mmap_op(_kernel_swi_regs * r);
extern void *get_mmap_abort_handler(void);
extern void mmap_cleanup_app(app_object *app);
extern _kernel_oserror *
armeabi_mmap(eabi_PTR addr, size_t size, int prot, int flags, int fd, size_t offset,
	     armeabisupport_allocator **allocator_ret, mmap_block **block_ret);
extern _kernel_oserror *
armeabi_mmap_internal(eabi_PTR addr, size_t size, int prot, int flags, int opentable_idx, size_t offset,
		      armeabisupport_allocator **allocator_ret, mmap_block **block_ret);

#endif
