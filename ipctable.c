/* ipctable.c
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "memory.h"
#include "ipctable.h"
#include "fdtable.h"
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

_kernel_oserror *openipctable_deref_entry(int opentable_idx)
{
  TRACE("%s: IPC opentable_idx=%d", __func__, opentable_idx);

  openipctable_entry *open_entry = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC],
									     opentable_idx);
  open_entry->table_base.ref_count--;
  LOG("%s:   Open table ref count now %d for IPC entry index %d",
      __func__, open_entry->table_base.ref_count, opentable_idx);
  if (open_entry->table_base.ref_count == 0)
    {
      systemipctable_deref_ipc(open_entry->systemtable_idx);
      open_entry->systemtable_idx = ARRAY_INVALID_INDEX;
      array_entry_mark_unused(&open_entry->table_base.array_base);
    }

#if ENABLE_LOGGING
  openipctable_log();
  systemipctable_log();
#endif

  return NULL;
}

_kernel_oserror *systemipctable_deref_ipc(int systemtable_idx)
{
  TRACE("%s: IPC systemtable index=%d", __func__, systemtable_idx);

  systemipctable_entry *entry = (systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
									    systemtable_idx);
  if (!array_entry_is_in_use(&entry->table_base.array_base))
    return armeabisupport_EINVAL;

  entry->table_base.ref_count--;
  LOG("%s:   System table ref count now %d for IPC (%X)", __func__,
      entry->table_base.ref_count,
      entry->block ? page_to_addr(&entry->allocator->base, entry->block->start_page) : NULL);
  if (entry->table_base.ref_count == 0)
    {
      LOG("%s:   Destroying IPC system table entry %X", __func__,
	  entry->block ? page_to_addr(&entry->allocator->base, entry->block->start_page) : NULL);
      if (entry->allocator && entry->block)
	{
	  allocator_munmap_block(entry->allocator, entry->block);
	  entry->allocator = NULL;
	  entry->block = NULL;
	}
      array_entry_mark_unused(&entry->table_base.array_base);
    }

#if ENABLE_LOGGING
  fdtable_log(NULL);
#endif

  return NULL;
}

void openipctable_print(void)
{
  array_t *table = &global.open_table[fd_type_IPC];
  openipctable_entry *entry = (openipctable_entry *)table->entries;

  printf("%-4s %-10s %-3s %10s %10s %10s %10s\n",
		 "IDX", "SYSIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].table_base.array_base))
	{
	  printf("%-4d %-10s %-3d %10X %10X %10d %10d\n",
		 i,
		 "IPC",
		 entry->table_base.ref_count,
		 (unsigned)entry->write_buffer,
		 (unsigned)entry->read_buffer,
		 entry->write_pos,
		 entry->read_pos);
	}
    }
}

void systemipctable_print(void)
{
  array_t *table = &global.system_table[fd_type_IPC];
  systemipctable_entry *entry = (systemipctable_entry *)table->entries;

  printf("%-4s %-3s %-10s %-10s\n", "IDX", "REF","ALLOCATOR", "ADDR");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  printf("%-4d %-3d %-10s %-10X\n",
		 i,
		 entry[i].table_base.ref_count,
		 entry[i].allocator->base.name,
		 (unsigned)page_to_addr(&entry[i].allocator->base,
					entry[i].block->start_page));
	}
    }
}

#if ENABLE_LOGGING
void openipctable_log(void)
{
  array_t *table = &global.open_table[fd_type_IPC];
  openipctable_entry *entry = (openipctable_entry *)table->entries;

  reporter_printf("%4s %6s %6s %4s %10s %10s %10s %10s\n",
		  "IDX", "SYSIDX", "OPPIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].table_base.array_base))
	{
	  reporter_printf("%4d %6d %6d %4d %10X %10X %10d %10d\n",
			  i,
			  entry[i].systemtable_idx,
			  entry[i].openipctable_oppend_idx,
			  entry[i].table_base.ref_count,
			  entry[i].write_buffer,
			  entry[i].read_buffer,
			  entry[i].write_pos,
			  entry[i].read_pos);
	}
    }
}

void systemipctable_log(void)
{
  array_t *table = &global.system_table[fd_type_IPC];
  systemipctable_entry *entry = (systemipctable_entry *)table->entries;

  reporter_printf("%4s %3s %10s %10s\n", "IDX", "REF","ALLOCATOR", "ADDR");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  reporter_printf("%4d %3d %10s %10X\n",
			  i,
			  entry[i].table_base.ref_count,
			  entry[i].allocator->base.name,
			  page_to_addr(&entry[i].allocator->base, entry[i].block->start_page));
	}
    }
}
#endif
