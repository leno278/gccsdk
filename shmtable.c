/* shmtable.c
 *
 * Copyright 2024,2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shmtable.h"
#include "opentable.h"
#include "fdtable.h"
#include "memory.h"
#include "main.h"
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

	  entry->allocator_idx = ARRAY_INVALID_INDEX;
	  entry->mapping_idx = ARRAY_INVALID_INDEX;
	  entry->table_base.ref_count = 1;
	  entry->linked = true;
	  entry->name = strdup(name);
	}
    }

  LOG("%s: Return system table index %d for shm object %s", __func__, *idx_ret, name);

  return err;
}

_kernel_oserror *systemshmtable_deref_entry(int systemtable_idx)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
									    systemtable_idx);
  if (entry && array_entry_is_in_use(&entry->table_base.array_base))
    {
      mmap_allocator *allocator = mmap_allocator_get_entry(entry->allocator_idx);
      mmap_mapping *block = mmap_mapping_get_entry(allocator, entry->mapping_idx);

      entry->table_base.ref_count--;
      LOG("%s:   Ref count now %d for system table entry %s (%X)", __func__,
	  entry->table_base.ref_count,
	  entry->name,
	  block ? page_to_addr(&allocator->da, block->start_page) : NULL);
      if (entry->table_base.ref_count == 0)
	{
	  LOG("%s:   Destroying system table entry %s", __func__, entry->name);
	  if (allocator && block)
	    {
	      LOG("%s:   Releasing memory %X from allocator %s", __func__,
		  page_to_addr(&allocator->da, block->start_page),
		  allocator->name);
	      mmap_allocator_munmap_block(allocator, block);
	      entry->allocator_idx = ARRAY_INVALID_INDEX;
	      entry->mapping_idx = ARRAY_INVALID_INDEX;
	    }
	  else
	    {
	      LOG("%s:   Invalid allocator or mapping, failed to release memory", __func__);
	    }
	  if (entry->name)
	    {
	      free((void *)entry->name);
	      entry->name = NULL;
	    }
	  /* Sytem table entry now free to re-use.  */
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

_kernel_oserror *opentable_add_shm(const char *name,
				   int oflag,
				   int mode,
				   int *idx_ret)
{
  TRACE("%s: name=%s", __func__, name);

  int sysshmtbl_idx = ARRAY_INVALID_INDEX;

  _kernel_oserror *err;
  if ((err = systemshmtable_open_shm(name, oflag, mode, &sysshmtbl_idx)) != NULL)
    return err;

  if ((err = opentable_add_entry(opentable_type_SHM,
				 sysshmtbl_idx,
				 idx_ret)) != NULL)
    {
      LOG("%s:     err=%s", __func__, err->errmess);

      systemshmtable_deref_entry(sysshmtbl_idx);

      return err;
    }

  opentable_entry *entry = opentable_get_entry(*idx_ret);

  entry->shm.flags = oflag;
  entry->shm.mode = mode;

  return NULL;
}

_kernel_oserror *opentable_unmap_shm(mmap_allocator *allocator,
				     mmap_mapping *block)
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
       page_to_addr(&allocator->da, block->start_page),
       pages_to_bytes(block->end_page - block->start_page),
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

  printf("SHM Open Table:\n");
  printf("  %-4s %-12s %-12s PATH\n", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_SHM && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  printf("  %-4d %-12d %-12d %s\n",
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

  printf("SHM System Table:\n");
  printf("  %-4s %-4s %-6s %-7s %21s %s\n", "IDX", "REF", "ALLOC", "MAPPING", "ADDRESS RANGE", "NAME");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  mmap_allocator *allocator = mmap_allocator_get_entry(entry[i].allocator_idx);
	  mmap_mapping *mapping = mmap_mapping_get_entry(allocator,
							 entry[i].mapping_idx);
	  printf("  %-4d %-4d %-6d %-7d %X->%X %s\n",
		 i,
		 entry[i].table_base.ref_count,
		 entry[i].allocator_idx,
		 entry[i].mapping_idx,
		 (unsigned)page_to_addr(&allocator->da, mapping->start_page),
		 (unsigned)page_to_addr(&allocator->da, mapping->end_page),
		 entry[i].name);
	}
    }
}

#if ENABLE_LOGGING

void opentable_log_shm_entry(opentable_entry *ot_entry)
{
  int index = opentable_get_index(ot_entry);
  systemshmtable_entry *system_entry =
	    (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
							ot_entry->systemtable_idx);
  mmap_allocator *allocator = mmap_allocator_get_entry(system_entry->allocator_idx);
  mmap_mapping *block = mmap_mapping_get_entry(allocator, system_entry->mapping_idx);
  reporter_printf("%4d %4s %4d %6d",
		  index,
		  "SHM",
		  ot_entry->ref_count,
		  ot_entry->systemtable_idx);
  reporter_printf("                   SYSTEM: REF:%3d ADDR:%10X NAME:%s",
		  system_entry->table_base.ref_count,
		  page_to_addr(&allocator->da, block->start_page),
		  system_entry->name);
}

void openshmtable_log(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  reporter_printf("%-4s %-12s %-12s PATH", "IDX", "SYSIDX", "REF");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (ot_entry[i].type == opentable_type_SHM && array_entry_is_in_use(&ot_entry[i].array_base))
	{
	  reporter_printf("%-4d %-12d %-12d %s",
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

  reporter_printf("%4s %4s %10s %10s %s", "IDX", "REF", "ADDR", "SIZE", "NAME");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (entry[i].table_base.array_base.in_use)
	{
	  mmap_allocator *allocator = mmap_allocator_get_entry(entry[i].allocator_idx);
	  mmap_mapping *block = mmap_mapping_get_entry(allocator, entry[i].mapping_idx);
	  reporter_printf("%4d %4d %10X %10X %s",
			  i,
			  entry[i].table_base.ref_count,
			  page_to_addr(&allocator->da, block->start_page),
			  pages_to_bytes(block->end_page - block->start_page),
			  entry[i].name);
	}
    }
}
#endif
