/* shmtable.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shmtable.h"
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
static int shmtable_scan_for_name(const char *name)
{
  array_t *shmtable = &global.system_shmtable;
  systemshmtable_entry *entry = (systemshmtable_entry *)shmtable->entries;
  for (int i = 0; i < shmtable->num_entries; i++)
    if (strcmp(entry[i].name, name) == 0)
      return i;

  return ARRAY_INVALID_INDEX;
}

_kernel_oserror *systemshmtable_open_shm(const char *name, int flags, int mode, int *idx_ret)
{
  TRACE("%s: name=%s", __func__, name);

  _kernel_oserror *err = NULL;
  int entry_idx = shmtable_scan_for_name(name);
  if (entry_idx != ARRAY_INVALID_INDEX)
    {
      systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_shmtable, entry_idx);
      entry->ref_count++;
      *idx_ret = entry_idx;
      LOG("%s: Found existing entry for shm object %s", __func__, name);
      LOG("%s:   at index %d, ref count now %d", __func__, entry_idx, entry->ref_count);
    }
  else
    {
      LOG("%s: No entry found for shm object %s", __func__, name);
      if ((err = array_alloc_entry(&global.system_shmtable, idx_ret)) == NULL)
	{
	  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_shmtable, *idx_ret);

	  entry->allocator = NULL;
	  entry->block = NULL;
	  entry->ref_count = 1;
	  entry->linked = true;
	  entry->name = strdup(name);
	}
    }

  LOG("%s: Return index %d for shm object %s", __func__, *idx_ret, name);

  return err;
}

_kernel_oserror *systemshmtable_close_shm(int systemtable_idx)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_shmtable,
									    systemtable_idx);
  if (array_entry_is_in_use(&entry->base))
    {
      entry->ref_count--;
      LOG("%s:   Ref count now %d for shm object %s", __func__,
	  entry->ref_count,
	  entry->name);
      if (entry->ref_count == 0)
	{
	  LOG("%s:   Destroying shm object %s", __func__, entry->name);
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
	  array_entry_mark_unused(&entry->base);
	  return NULL;
	}
    }

  return armeabisupport_EINVAL;
}

int systemshmtable_shm_exists(const char *name)
{
  int num_entries = global.system_shmtable.num_entries;
  systemshmtable_entry *entry = (systemshmtable_entry *)global.system_shmtable.entries;

  for (int i = 0; i < num_entries; i++)
    if (strcmp(entry[i].name, name) == 0)
      return i;

  return ARRAY_INVALID_INDEX;
}

_kernel_oserror *openshmtable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret)
{
  TRACE("%s: systemtable_idx=%d", __func__, systemtable_idx);

  array_t *shmtable = &global.open_shmtable;

  LOG("%s:     systemtable index=%d", __func__, systemtable_idx);

  _kernel_oserror *err;
  if ((err = array_alloc_entry(shmtable, idx_ret)) == NULL)
    {
      openshmtable_entry *entry = (openshmtable_entry *)array_get_entry_ptr(shmtable, *idx_ret);

      entry->systemtable_idx = systemtable_idx;
      entry->ref_count = 1;
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

  openshmtable_entry *openshm_entry = (openshmtable_entry *)array_get_entry_ptr(&global.open_shmtable,
										opentable_idx);
  openshm_entry->ref_count--;
  LOG("%s:   new ref_count = %d, close file", __func__, openshm_entry->ref_count);
  if (openshm_entry->ref_count == 0)
    {
      systemshmtable_close_shm(openshm_entry->systemtable_idx);
      array_entry_mark_unused(&openshm_entry->base);
    }

#if ENABLE_LOGGING
//  systemfiletable_print();
//  openfiletable_print();
#endif

  return NULL;
}
