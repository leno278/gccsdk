/* shmtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shmtable.h"
#include "opentable.h"
#include "fdtable.h"
#include "memory.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 1
#include "log.h"

static void opentable_log_shm_entry(opentable_entry *ot_entry);

static opentable_methods shm_methods =
{
  systemshmtable_deref_shm,
  opentable_unmap_shm,
  opentable_log_shm_entry
};

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

_kernel_oserror *systemshmtable_open_shm(const char *name, int oflag, int mode, int *idx_ret)
{
  TRACE("%s: name=%s", __func__, name);

  _kernel_oserror *err = NULL;
  int entry_idx = systemshmtable_shm_exists(name);
  if (entry_idx != ARRAY_INVALID_INDEX)
    {
      /* Fail if O_CREAT and O_EXCL are set and the named shared memory object already exists.  */
      if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
	{
	  LOG("shm_open: \"%s\" already exists, but O_CREAT and O_EXCL are set", name);
	  return armeabisupport_EEXIST;
	}

      systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
										entry_idx);
      entry->table_base.ref_count++;
      *idx_ret = entry_idx;
      LOG("%s: Found existing entry for shm object %s", __func__, name);
      LOG("%s:   at system table index %d, ref count now %d", __func__, entry_idx, entry->table_base.ref_count);
    }
  else
    {
      /* Fail if O_CREAT is not set and the named shared memory object does not exist.  */
      if (!(oflag & O_CREAT))
	{
	  LOG("shm_open: \"%s\" does not exist and O_CREATE not set", name);
	  return armeabisupport_ENOENT;
	}

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

  LOG("%s: Return system table index %d for shm object %s", __func__, *idx_ret, name);

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
	  entry->block ? page_to_addr(&entry->allocator->da, entry->block->start_page) : NULL);
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
  else
    {
      LOG("%s: FAIL: attempt to deref system shm entry %d that is no longer valid",
	  __func__, systemtable_idx);
    }

  return armeabisupport_EINVAL;
}

_kernel_oserror *opentable_add_shm(int systemtable_idx,
				   int flags,
				   int mode,
				   int *idx_ret)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  _kernel_oserror *err;
  if ((err = opentable_add_entry(opentable_type_SHM,
				 systemtable_idx,
				 &shm_methods,
				 idx_ret)) == NULL)
    {
      opentable_entry *entry = opentable_get_entry(*idx_ret);

      entry->shm.flags = flags;
      entry->shm.mode = mode;
    }
  else
    LOG("%s:     err=%s", __func__, err->errmess);

  return err;
}

_kernel_oserror *opentable_unmap_shm(armeabisupport_allocator *allocator,
				     mmap_block *block)
{
  TRACE("%s: opentable_idx=%d", __func__, block->opentable_idx);

#if ENABLE_LOGGING
  opentable_entry *ot_entry = opentable_get_entry(block->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    return armeabisupport_EBADF;

  systemshmtable_entry *st_entry =
		(systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
							    ot_entry->systemtable_idx);
  LOG("%s: shared memory object %p, \"%s\", open table index %d, addr=%p, len=%d, oref=%d, sref=%d",
       __func__,
       st_entry,
       st_entry->name,
       block->opentable_idx,
       page_to_addr(&st_entry->allocator->da, st_entry->block->start_page),
       pages_to_bytes(st_entry->block->end_page - st_entry->block->start_page),
       ot_entry->ref_count,
       st_entry->table_base.ref_count);
#endif

  /* This will only call allocator_munmap_block() when the final reference is released.  */
  opentable_deref_entry(block->opentable_idx);

  return NULL;
}

void openshmtable_print(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  printf("%-4s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_SHM && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  printf("%-4d %-12d %-12d %s\n",
		 i,
		 ot_entry[i].systemtable_idx,
		 ot_entry[i].ref_count,
		 systemshmtable_get_name(ot_entry[i].systemtable_idx));
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

static void opentable_log_shm_entry(opentable_entry *ot_entry)
{
  int index = opentable_get_index(ot_entry);
  systemshmtable_entry *system_entry =
	    (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
							ot_entry->systemtable_idx);
  reporter_printf("%4d %4s %4d %6d",
		  index,
		  "SHM",
		  ot_entry->ref_count,
		  ot_entry->systemtable_idx);
  reporter_printf("                   SYSTEM: REF:%3d ADDR:%10X NAME:%s",
		  system_entry->table_base.ref_count,
		  page_to_addr(&system_entry->allocator->da, system_entry->block->start_page),
		  system_entry->name);
}

#if ENABLE_LOGGING
void openshmtable_log(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  reporter_printf("%-4s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_SHM && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  reporter_printf("%-4d %-12d %-12d %s\n",
		 i,
		 ot_entry[i].systemtable_idx,
		 ot_entry[i].ref_count,
		 systemshmtable_get_name(ot_entry[i].systemtable_idx));
	}
    }
}

void systemshmtable_log(void)
{
  array_t *table = &global.system_table[fd_type_SHM];
  systemshmtable_entry *entry = (systemshmtable_entry *)table->entries;

  reporter_printf("%4s %4s %10s %10s %s\n", "IDX", "REF", "ADDR", "SIZE", "NAME");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  reporter_printf("%4d %4d %10X %10X %s\n",
			  i,
			  entry[i].table_base.ref_count,
			  page_to_addr(&entry[i].allocator->da, entry[i].block->start_page),
			  pages_to_bytes(entry[i].block->end_page - entry[i].block->start_page),
			  entry[i].name);
	}
    }
}
#endif
