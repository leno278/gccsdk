/* ipctable.h
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef IPCTABLE_H
#define IPCTABLE_H

#include "table_entry_base.h"
#include "memory.h"
#include "mmap.h"
#include "types.h"

typedef struct _openipctable
{
  unsigned read_pos;
  unsigned write_pos;

  eabi_PTR write_buffer;
  eabi_PTR read_buffer;
  size_t buffer_size;

  /* The entry index of the opposite end of the channel.  */
  int opentable_oppend_idx;

} openipctable;

_kernel_oserror *openipctable_add_entry(int systemtable_idx,
					eabi_PTR write_buffer,
					eabi_PTR read_buffer,
					size_t buffer_size,
					int *idx_ret);

typedef struct _systemipctable_entry
{
  table_entry_base table_base;

  mmap_allocator_t allocator_idx;
  mmap_mapping_t mapping_idx;

} systemipctable_entry;

_kernel_oserror *systemipctable_open_ipc(size_t size, int *idx_ret);
_kernel_oserror *systemipctable_deref_ipc(int systemtable_idx);
_kernel_oserror *opentable_unmap_ipc(mmap_allocator *allocator,
				     mmap_mapping *block);
void openipctable_print(void);
void systemipctable_print(void);

void openipctable_log(void);
void systemipctable_log(void);

#endif
