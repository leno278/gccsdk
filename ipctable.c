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

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
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

  if ((err = armeabi_mmap(NULL, size * 2, 0, 0, -1, 0, &entry->allocator, &entry->block)) == NULL)
    {
      entry->table_base.ref_count = 1;

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

  return NULL;
}

_kernel_oserror *opentable_unmap_ipc(armeabisupport_allocator *allocator,
				     mmap_block *block)
{
  /* NOOP  */
  LOG("%s: NOOP", __func__);
  return NULL;
}

void openipctable_print(void)
{
  array_t *table = &global.open_table;
  opentable_entry *entry = (opentable_entry *)table->entries;

  printf("%-4s %-10s %-3s %10s %10s %10s %10s\n",
		 "IDX", "SYSIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].array_base) && entry[i].type == opentable_type_IPC)
	{
	  printf("%-4d %-10s %-3d %10X %10X %10d %10d\n",
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

  printf("%-4s %-3s %-10s %-10s\n", "IDX", "REF","ALLOCATOR", "ADDR");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  printf("%-4d %-3d %-10s %-10X\n",
		 i,
		 entry[i].table_base.ref_count,
		 entry[i].allocator->name,
		 (unsigned)page_to_addr(entry[i].allocator,
					entry[i].block->start_page));
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

  reporter_printf("%4s %6s %6s %4s %10s %10s %10s %10s\n",
		  "IDX", "SYSIDX", "OPPIDX", "REF", "WB", "RB", "WP", "RP");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].array_base) && entry[i].type == opentable_type_IPC)
	{
	  reporter_printf("%4d %6d %6d %4d %10X %10X %10d %10d\n",
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
