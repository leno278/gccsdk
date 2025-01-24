/* ipctable.h
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef IPCTABLE_H
#define IPCTABLE_H

#include "table_entry_base.h"
#include "types.h"

typedef struct _openipctable_entry
{
  table_entry_base table_base;

  int systemtable_idx;

  unsigned read_pos;
  unsigned write_pos;

  eabi_PTR write_buffer;
  eabi_PTR read_buffer;
  size_t buffer_size;

  /* The entry index of the opposite end of the channel.  */
  int openipctable_oppend_idx;

} openipctable_entry;

_kernel_oserror *openipctable_add_entry(int systemtable_idx,
					eabi_PTR write_buffer,
					eabi_PTR read_buffer,
					size_t buffer_size,
					int *idx_ret);

typedef struct _systemipctable_entry
{
  table_entry_base table_base;

  armeabisupport_allocator_mmap *allocator;
  mmap_block *block;

} systemipctable_entry;

_kernel_oserror *systemipctable_open_ipc(size_t size, int *idx_ret);
_kernel_oserror *openipctable_deref_entry(int opentable_idx);
_kernel_oserror *systemipctable_deref_ipc(int systemtable_idx);
void openipctable_print(void);
void systemipctable_print(void);

static inline void ipc_write_int(openipctable_entry *ipc, int value)
{
  *(int *)(ipc->write_buffer + ipc->write_pos) = value;
  ipc->write_pos += sizeof(int);
}

static inline void ipc_write_uint(openipctable_entry *ipc, unsigned value)
{
  *(unsigned *)(ipc->write_buffer + ipc->write_pos) = value;
  ipc->write_pos += sizeof(unsigned);
}

static inline int ipc_read_int(openipctable_entry *ipc)
{
  int value = *(int *)(ipc->read_buffer + ipc->read_pos);
  ipc->read_pos += sizeof(int);

  return value;
}

static inline unsigned ipc_read_uint(openipctable_entry *ipc)
{
  unsigned value = *(unsigned *)(ipc->read_buffer + ipc->read_pos);
  ipc->read_pos += sizeof(unsigned);

  return value;
}

static inline void ipc_ensure_write(openipctable_entry *ipc, int length)
{
  if ((ipc->write_pos + length) > ipc->buffer_size)
    ipc->write_pos = 0;
}

static inline void ipc_ensure_read(openipctable_entry *ipc, int length)
{
  if ((ipc->read_pos + length) > ipc->buffer_size)
    ipc->read_pos = 0;
}

static inline unsigned align64(unsigned x)
{
    return (x + 7) & ~7;
}

static inline void ipc_ensure_aligned_read64(openipctable_entry *ipc)
{
  ipc->read_pos = align64(ipc->read_pos);
}

static inline void ipc_inc_and_align_read64(openipctable_entry *ipc, int len)
{
  ipc->read_pos = align64(ipc->read_pos + len);
}

static inline void ipc_ensure_aligned_write64(openipctable_entry *ipc)
{
  ipc->write_pos = align64(ipc->write_pos);
}

static inline void ipc_inc_and_align_write64(openipctable_entry *ipc, int len)
{
  ipc->write_pos = align64(ipc->write_pos + len);
}

void openipctable_log(void);
void systemipctable_log(void);

#endif
