/* opentable.c
 *
 * Copyright 2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "opentable.h"
#include "filetable.h"

#define ENABLE_LOGGING 1
#include "log.h"

#define ENABLE_TRACING 0
#include "trace.h"

_kernel_oserror *opentable_add_entry(opentable_type type,
				     int systemtable_idx,
				     opentable_methods *methods,
				     int *idx_ret)
{
  array_t *opentable = &global.open_table;

  _kernel_oserror *err;
  if ((err = array_alloc_entry(opentable, idx_ret)) == NULL)
    {
      opentable_entry *entry = (opentable_entry *)array_get_entry_ptr(opentable, *idx_ret);
      entry->type = type;
      entry->systemtable_idx = systemtable_idx;
      entry->ref_count = 1;
      entry->method = methods;
    }
  else
    {
      LOG("%s:     err=%s", __func__, err->errmess);
    }

  return err;
}

_kernel_oserror *opentable_deref_entry(int opentable_idx)
{
  TRACE("%s: opentable_idx=%d", __func__, opentable_idx);

  _kernel_oserror *err = NULL;
  opentable_entry *ot_entry = opentable_get_entry(opentable_idx);
  if (ot_entry && array_entry_is_in_use(&ot_entry->array_base))
    {
      ot_entry->ref_count--;
      LOG("%s: ref count now %d for open table index %d  (---)",
	      __func__, ot_entry->ref_count, opentable_idx);
      if (ot_entry->ref_count == 0)
	{
	  err = ot_entry->method->deref_system(ot_entry->systemtable_idx);
	  ot_entry->systemtable_idx = ARRAY_INVALID_INDEX;
	  array_entry_mark_unused(&ot_entry->array_base);
	}
    }
  else
    {
      LOG("%s: FAIL: attempt to deref open table index %d that is no longer valid",
	  __func__, opentable_idx);
    }

  return err;
}

_kernel_oserror *opentable_deref_mmap(mmap_allocator *allocator,
				      mmap_mapping *mapping)
{
  TRACE("%s: opentable_idx=%d", __func__, mapping->opentable_idx);

  _kernel_oserror *err = NULL;
  opentable_entry *ot_entry = opentable_get_entry(mapping->opentable_idx);
  if (ot_entry && array_entry_is_in_use(&ot_entry->array_base))
    {
      ot_entry->ref_count--;
      LOG("%s: ref count now %d for open table index %d  (---)",
	      __func__, ot_entry->ref_count, mapping->opentable_idx);
      if (ot_entry->ref_count == 0)
	{
	  switch (ot_entry->type)
	    {
	      case opentable_type_SHM:
		systemshmtable_deref_shm(allocator, mapping, ot_entry->systemtable_idx);
		break;
	      case opentable_type_FILE:
		systemfiletable_deref_file(allocator, mapping, ot_entry->systemtable_idx);
		break;
	    }
	  ot_entry->systemtable_idx = ARRAY_INVALID_INDEX;
	  array_entry_mark_unused(&ot_entry->array_base);
	}
    }
  else
    {
      LOG("%s: FAIL: attempt to deref open table index %d that is no longer valid",
	  __func__, mapping->opentable_idx);
    }

  return err;
}

#if ENABLE_LOGGING

void opentable_log(void)
{
  array_t *table = &global.open_table;
  opentable_entry *ot_entry = (opentable_entry *)table->entries;

  reporter_printf("%4s %4s %4s %6s", "IDX", "TYPE", "REF", "SYSIDX");
  for (int i = 0; i < table->num_entries; i++)
    {
      if (array_entry_is_in_use(&ot_entry[i].array_base))
	ot_entry[i].method->log(&ot_entry[i]);
    }
}

#endif
