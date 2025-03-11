/* opentable.h
 *
 * Copyright 2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef OPENTABLE_H
#define OPENTABLE_H

#include "array.h"
#include "filetable.h"
#include "shmtable.h"
#include "ipctable.h"
#include "mmap.h"

typedef enum _opentable_type
{
  opentable_type_INVALID = -1,
  opentable_type_SHM = 0,
  opentable_type_FILE,
  opentable_type_IPC,
  opentable_type_MAX

} opentable_type;

typedef struct _opentable_methods
{
  _kernel_oserror *(*deref_system)(int);
  _kernel_oserror *(*unmap)(mmap_allocator *allocator,
			    mmap_mapping *block);
  void (*log)(struct _opentable_entry *ot_entry);

} opentable_methods;

typedef struct _opentable_entry
{
  array_entry_base array_base;

  int ref_count;

  int systemtable_idx;

  opentable_type type;

  opentable_methods *method;

  union
    {
      openfiletable file;
      openshmtable shm;
      openipctable ipc;
    };

} opentable_entry;

static inline opentable_entry *opentable_get_entry(int idx)
{
  return (opentable_entry *)array_get_entry_ptr(&global.open_table, idx);
}

static inline int opentable_get_index(opentable_entry *ot_entry)
{
  return array_get_entry_idx(&global.open_table, &ot_entry->array_base);
}

_kernel_oserror *opentable_add_entry(opentable_type type,
				     int systemtable_idx,
				     opentable_methods *methods,
				     int *idx_ret);
_kernel_oserror *opentable_deref_entry(int opentable_idx);
_kernel_oserror *opentable_deref_mmap(mmap_allocator *allocator,
				      mmap_mapping *mapping);
void opentable_log(void);

static inline void ipc_write_int(opentable_entry *ot_entry, int value)
{
  *(int *)(ot_entry->ipc.write_buffer + ot_entry->ipc.write_pos) = value;
  ot_entry->ipc.write_pos += sizeof(int);
}

static inline void ipc_write_uint(opentable_entry *ot_entry, unsigned value)
{
  *(unsigned *)(ot_entry->ipc.write_buffer + ot_entry->ipc.write_pos) = value;
  ot_entry->ipc.write_pos += sizeof(unsigned);
}

static inline int ipc_read_int(opentable_entry *ot_entry)
{
  int value = *(int *)(ot_entry->ipc.read_buffer + ot_entry->ipc.read_pos);
  ot_entry->ipc.read_pos += sizeof(int);

  return value;
}

static inline unsigned ipc_read_uint(opentable_entry *ot_entry)
{
  unsigned value = *(unsigned *)(ot_entry->ipc.read_buffer + ot_entry->ipc.read_pos);
  ot_entry->ipc.read_pos += sizeof(unsigned);

  return value;
}

static inline void ipc_ensure_write(opentable_entry *ot_entry, int length)
{
  if ((ot_entry->ipc.write_pos + length) > ot_entry->ipc.buffer_size)
    ot_entry->ipc.write_pos = 0;
}

static inline void ipc_ensure_read(opentable_entry *ot_entry, int length)
{
  if ((ot_entry->ipc.read_pos + length) > ot_entry->ipc.buffer_size)
    ot_entry->ipc.read_pos = 0;
}

static inline unsigned align64(unsigned x)
{
    return (x + 7) & ~7;
}

static inline void ipc_ensure_aligned_read64(opentable_entry *ot_entry)
{
  ot_entry->ipc.read_pos = align64(ot_entry->ipc.read_pos);
}

static inline void ipc_inc_and_align_read64(opentable_entry *ot_entry, int len)
{
  ot_entry->ipc.read_pos = align64(ot_entry->ipc.read_pos + len);
}

static inline void ipc_ensure_aligned_write64(opentable_entry *ot_entry)
{
  ot_entry->ipc.write_pos = align64(ot_entry->ipc.write_pos);
}

static inline void ipc_inc_and_align_write64(opentable_entry *ot_entry, int len)
{
  ot_entry->ipc.write_pos = align64(ot_entry->ipc.write_pos + len);
}

#endif
