/* ipctable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "ipctable.h"
#include "mmap.h"

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

_kernel_oserror *openipctable_add_entry(int systemtable_idx,
					eabi_PTR write_buffer,
					eabi_PTR read_buffer,
					size_t buffer_size,
					int *idx_ret)
{
  array_t *ipctable = &global.open_table[fd_type_IPC];

  _kernel_oserror *err;
  if ((err = array_alloc_entry(ipctable, idx_ret)) == NULL)
    {
      openipctable_entry *entry = (openipctable_entry *)array_get_entry_ptr(ipctable, *idx_ret);

      entry->systemtable_idx = systemtable_idx;
      entry->table_base.ref_count = 1;
      entry->read_pos = entry->write_pos = 0;

      entry->write_buffer = write_buffer;
      entry->read_buffer = read_buffer;
      entry->buffer_size = buffer_size;
    }

  if (err)
    LOG("%s:     err=%s", __func__, err->errmess);

  return err;
}

_kernel_oserror *systemipctable_open_ipc(size_t size, int *idx_ret)
{
  _kernel_oserror *err;
  if ((err = array_alloc_entry(&global.system_table[fd_type_IPC], idx_ret)) != NULL)
    return err;

  systemipctable_entry *entry = (systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC], *idx_ret);

  if ((err = armeabi_mmap(NULL, size * 2, 0, 0, -1, 0, &entry->allocator, &entry->block)) == NULL)
    {
      entry->table_base.ref_count = 1;

      LOG("%s: Return index %d for IPC object", __func__, *idx_ret);
    }

  return err;
}

