/* ipctable.c
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "memory.h"
#include "ipctable.h"
#include "opentable.h"
#include "fdtable.h"
#include "mmap.h"
#include "main.h"

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 1
#include "log.h"

static void opentable_log_ipc_entry(opentable_entry *ot_entry);

static opentable_methods ipc_methods =
{
  systemipctable_deref_ipc,
  opentable_unmap_ipc,
  opentable_log_ipc_entry
};

_kernel_oserror *opentable_add_ipc(int systemtable_idx,
				   eabi_PTR write_buffer,
				   eabi_PTR read_buffer,
				   size_t buffer_size,
				   int *idx_ret)
{
  _kernel_oserror *err;
  if ((err = opentable_add_entry(opentable_type_IPC,
				 systemtable_idx,
				 &ipc_methods,
				 idx_ret)) == NULL)
    {
      opentable_entry *entry = opentable_get_entry(*idx_ret);

      entry->ipc.read_pos = entry->ipc.write_pos = 0;
      entry->ipc.write_buffer = write_buffer;
      entry->ipc.read_buffer = read_buffer;
      entry->ipc.buffer_size = buffer_size;
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

  app_mmap_mapping *app_mmap;
  if ((err = armeabi_mmap_internal(size * 2, 0, 0, -1, 0, &app_mmap)) == NULL)
    {

      entry->table_base.ref_count = 1;
      entry->allocator_idx = app_mmap->allocator_idx;
      entry->mapping_idx = app_mmap->mapping_idx;

      LOG("%s: Return index %d for IPC object", __func__, *idx_ret);
    }

  return err;
}

_kernel_oserror *systemipctable_deref_ipc(int systemtable_idx)
{
  TRACE("%s: IPC systemtable index=%d", __func__, systemtable_idx);
  systemipctable_entry *entry = (systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
									    systemtable_idx);
  if (!array_entry_is_in_use(&entry->table_base.array_base))
    return armeabisupport_EINVAL;

  mmap_allocator *allocator = mmap_allocator_get_entry(entry->allocator_idx);
  mmap_mapping *block = mmap_mapping_get_entry(allocator, entry->mapping_idx);

  entry->table_base.ref_count--;
  LOG("%s:   System table ref count now %d for IPC (%X)", __func__,
      entry->table_base.ref_count,
      block ? page_to_addr(&allocator->da, block->start_page) : NULL);
  if (entry->table_base.ref_count == 0)
    {
      LOG("%s:   Destroying IPC system table entry %X", __func__,
	  block ? page_to_addr(&allocator->da, block->start_page) : NULL);
      if (allocator && block)
	{
	  mmap_allocator_munmap_block(allocator, block);
	  entry->allocator_idx = ARRAY_INVALID_INDEX;
	  entry->mapping_idx = ARRAY_INVALID_INDEX;
	}
      /* System table entry now free to re-use.  */
      array_entry_mark_unused(&entry->table_base.array_base);
    }

  return NULL;
}

_kernel_oserror *opentable_unmap_ipc(mmap_allocator *allocator,
				     mmap_mapping *block)
{
  /* NOOP  */
  LOG("%s: NOOP", __func__);
  return NULL;
}

void openipctable_print(void)
{
  array_t *table = &global.open_table;
  opentable_entry *entry = (opentable_entry *)table->entries;

  printf("IPC Open Table:\n");
  printf("  %-4s %-10s %-3s %10s %10s %10s %10s\n",
		 "IDX", "SYSIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].array_base) && entry[i].type == opentable_type_IPC)
	{
	  printf("  %-4d %-10s %-3d %10X %10X %10d %10d\n",
		 i,
		 "IPC",
		 entry[i].ref_count,
		 (unsigned)entry[i].ipc.write_buffer,
		 (unsigned)entry[i].ipc.read_buffer,
		 entry[i].ipc.write_pos,
		 entry[i].ipc.read_pos);
	}
    }
}

void systemipctable_print(void)
{
  array_t *table = &global.system_table[fd_type_IPC];
  systemipctable_entry *entry = (systemipctable_entry *)table->entries;

  printf("IPC System Table:\n");
  printf("  %-4s %-3s %-10s %-10s\n", "IDX", "REF","ALLOCATOR", "ADDR");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  mmap_allocator *allocator= mmap_allocator_get_entry(entry[i].allocator_idx);
	  mmap_mapping *mapping = mmap_mapping_get_entry(allocator, entry[i].mapping_idx);
	  printf("  %-4d %-3d %-10s %-10X\n",
		 i,
		 entry[i].table_base.ref_count,
		 allocator->name,
		 (unsigned)page_to_addr(&allocator->da,
					mapping->start_page));
	}
    }
}

static void opentable_log_ipc_entry(opentable_entry *ot_entry)
{
  int index = opentable_get_index(ot_entry);
  systemipctable_entry *system_entry =
	    (systemipctable_entry *)array_get_entry_ptr(&global.system_table[fd_type_IPC],
							ot_entry->systemtable_idx);
  reporter_printf("%4d %4s %4d %6d",
		  index,
		  "IPC",
		  ot_entry->ref_count,
		  ot_entry->systemtable_idx);
  reporter_printf("                   SYSTEM: REF:%3d OPP:%3d WB:%9X RB:%9X WP:%10d RP:%10d",
		  system_entry->table_base.ref_count,
		  ot_entry->ipc.opentable_oppend_idx,
		  (unsigned)ot_entry->ipc.write_buffer,
		  (unsigned)ot_entry->ipc.read_buffer,
		  ot_entry->ipc.write_pos,
		  ot_entry->ipc.read_pos);
}

#if ENABLE_LOGGING
void openipctable_log(void)
{
  array_t *table = &global.open_table;
  opentable_entry *entry = (opentable_entry *)table->entries;

  reporter_printf("%4s %6s %6s %4s %10s %10s %10s %10s",
		  "IDX", "SYSIDX", "OPPIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].array_base) && entry[i].type == opentable_type_IPC)
	{
	  reporter_printf("%4d %6d %6d %4d %10X %10X %10d %10d",
			  i,
			  entry[i].systemtable_idx,
			  entry[i].ipc.opentable_oppend_idx,
			  entry[i].ref_count,
			  entry[i].ipc.write_buffer,
			  entry[i].ipc.read_buffer,
			  entry[i].ipc.write_pos,
			  entry[i].ipc.read_pos);
	}
    }
}

void systemipctable_log(void)
{
  array_t *table = &global.system_table[fd_type_IPC];
  systemipctable_entry *entry = (systemipctable_entry *)table->entries;

  reporter_printf("%4s %3s %10s %10s", "IDX", "REF","ALLOCATOR", "ADDR");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  mmap_allocator *allocator= mmap_allocator_get_entry(entry[i].allocator_idx);
	  mmap_mapping *mapping = mmap_mapping_get_entry(allocator, entry[i].mapping_idx);
	  reporter_printf("%4d %3d %10s %10X",
			  i,
			  entry[i].table_base.ref_count,
			  allocator->name,
			  page_to_addr(&allocator->da, mapping->start_page));
	}
    }
}
#endif
