/* shmtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shmtable.h"
#include "fdtable.h"
#include "memory.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

/* See if we already have a shared memory object with this name open and return
 * the index if so, otherwise ARRAY_INVALID_INDEX.  */
int systemshmtable_shm_exists(const char *name)
{
  int num_entries = global.system_table[fd_type_SHM].num_entries;
  systemshmtable_entry *entry = (systemshmtable_entry *)global.system_table[fd_type_SHM].entries;

  for (int i = 0; i < num_entries; i++)
    if (strcmp(entry[i].name, name) == 0)
      return i;

  return ARRAY_INVALID_INDEX;
}

_kernel_oserror *systemshmtable_open_shm(const char *name, int flags, int mode, int *idx_ret)
{
  TRACE("%s: name=%s", __func__, name);

  _kernel_oserror *err = NULL;
  int entry_idx = systemshmtable_shm_exists(name);
  if (entry_idx != ARRAY_INVALID_INDEX)
    {
      systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
										entry_idx);
      entry->table_base.ref_count++;
      *idx_ret = entry_idx;
      LOG("%s: Found existing entry for shm object %s", __func__, name);
      LOG("%s:   at index %d, ref count now %d", __func__, entry_idx, entry->table_base.ref_count);
    }
  else
    {
      LOG("%s: No entry found for shm object %s", __func__, name);
      if ((err = array_alloc_entry(&global.system_table[fd_type_SHM], idx_ret)) == NULL)
	{
	  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
										    *idx_ret);

	  entry->allocator = NULL;
	  entry->block = NULL;
	  entry->table_base.ref_count = 1;
	  entry->linked = true;
	  entry->name = strdup(name);
	}
    }

  LOG("%s: Return index %d for shm object %s", __func__, *idx_ret, name);

  return err;
}

_kernel_oserror *systemshmtable_deref_shm(int systemtable_idx)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
									    systemtable_idx);
  if (entry && array_entry_is_in_use(&entry->table_base.array_base))
    {
      entry->table_base.ref_count--;
      LOG("%s:   Ref count now %d for system table entry %s (%X)", __func__,
	  entry->table_base.ref_count,
	  entry->name,
	  entry->block ? page_to_addr(&entry->allocator->base, entry->block->start_page) : NULL);
      if (entry->table_base.ref_count == 0)
	{
	  LOG("%s:   Destroying system table entry %s", __func__, entry->name);
	  if (entry->allocator && entry->block)
	    {
	      allocator_munmap_block(entry->allocator, entry->block);
	      entry->allocator = NULL;
	      entry->block = NULL;
	    }
	  if (entry->name)
	    {
	      free((void *)entry->name);
	      entry->name = NULL;
	    }
	  array_entry_mark_unused(&entry->table_base.array_base);
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}

_kernel_oserror *openshmtable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  array_t *shmtable = &global.open_table[fd_type_SHM];

  _kernel_oserror *err;
  if ((err = array_alloc_entry(shmtable, idx_ret)) == NULL)
    {
      openshmtable_entry *entry = (openshmtable_entry *)array_get_entry_ptr(shmtable, *idx_ret);

      entry->systemtable_idx = systemtable_idx;
      entry->table_base.ref_count = 1;
      entry->flags = flags;
      entry->mode = mode;
    }
  else
    LOG("%s:     err=%s", __func__, err->errmess);

  return err;
}

_kernel_oserror *openshmtable_deref_entry(int opentable_idx)
{
  TRACE("%s: opentable_idx=%d", __func__, opentable_idx);

  openshmtable_entry *openshm_entry = (openshmtable_entry *)array_get_entry_ptr(&global.open_table[fd_type_SHM],
										opentable_idx);
  if (openshm_entry && array_entry_is_in_use(&openshm_entry->table_base.array_base))
    {
      openshm_entry->table_base.ref_count--;
      LOG("%s:   Open table ref count now %d for open table entry", __func__, openshm_entry->table_base.ref_count);
      if (openshm_entry->table_base.ref_count == 0)
	{
	  systemshmtable_deref_shm(openshm_entry->systemtable_idx);
	  openshm_entry->systemtable_idx = ARRAY_INVALID_INDEX;
	  array_entry_mark_unused(&openshm_entry->table_base.array_base);
	}
    }

  return NULL;
}

void openshmtable_print(void)
{
  array_t *table = &global.open_table[fd_type_SHM];
  openshmtable_entry *entry = (openshmtable_entry *)table->entries;

  printf("%-4s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].table_base.array_base))
	{
	  printf("%-4d %-12d %-12d %s\n",
		 i,
		 entry[i].systemtable_idx,
		 entry[i].table_base.ref_count,
		 systemshmtable_get_name(entry[i].systemtable_idx));
	}
    }
}

void systemshmtable_print(void)
{
  array_t *table = &global.system_table[fd_type_SHM];
  systemshmtable_entry *entry = (systemshmtable_entry *)table->entries;

  printf("%-4s %-12s %s\n", "IDX", "REF","NAME");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  printf("%-4d %-12d %s\n", i, entry[i].table_base.ref_count, entry[i].name);
	}
    }
}

#if ENABLE_LOGGING
void openshmtable_log(void)
{
  array_t *table = &global.open_table[fd_type_SHM];
  openshmtable_entry *entry = (openshmtable_entry *)table->entries;

  reporter_printf("%-4s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&entry[i].table_base.array_base))
	{
	  reporter_printf("%-4d %-12d %-12d %s\n",
		 i,
		 entry[i].systemtable_idx,
		 entry[i].table_base.ref_count,
		 systemshmtable_get_name(entry[i].systemtable_idx));
	}
    }
}

void systemshmtable_log(void)
{
  array_t *table = &global.system_table[fd_type_SHM];
  systemshmtable_entry *entry = (systemshmtable_entry *)table->entries;

  reporter_printf("%-4s %-12s %s\n", "IDX", "REF","NAME");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  reporter_printf("%-4d %-12d %s\n", i, entry[i].table_base.ref_count, entry[i].name);
	}
    }
}
#endif
