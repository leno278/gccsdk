/* array.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "array.h"
#include "armeabisupport.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

_kernel_oserror *array_ctor(array_t *this, const int entry_size)
{
  TRACE("%s: entry_size=%d", __func__, entry_size);
  if (!this || !entry_size)
    return armeabisupport_EINVAL;

  this->entries = NULL;
  this->num_entries = 0;
  this->entry_size = entry_size;

  return NULL;
}

static inline unsigned int next_power_of_2(unsigned int v)
{
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  v += (v == 0);

  return v;
}

/* (Re)allocate the table to include include_idx_N or to twice the current size
 * if include_idx_N is -ve.  */
_kernel_oserror *array_alloc(array_t *table, int include_idx_N)
{
  TRACE("%s: table=%p", __func__, table);
  int new_size;
  array_entry_base *new_table;

  void *memset_clear_start;
  int memset_clear_length;

  if (!table)
    return armeabisupport_EINVAL;

  if (include_idx_N >= 0)
    {
      if (include_idx_N < table->num_entries)
	return NULL; /* Array already includes the required index.  */
      include_idx_N = next_power_of_2(include_idx_N);
    }

  if (!table->entries)
    {
      new_size = (include_idx_N < 0) ? ARRAY_INIT_NUM_ENTRIES : include_idx_N;
      new_table = malloc(new_size * table->entry_size);
      LOG("%s: creating array of %d elements (%d bytes)",
	  __func__, new_size, new_size * table->entry_size);
      LOG("%s: array ptr = %p", __func__, new_table);
      if (!new_table)
	return armeabisupport_ENOMEM;
      memset_clear_start = new_table;
      memset_clear_length = new_size * table->entry_size;
    }
  else
    {
      new_size = (include_idx_N < 0) ? table->num_entries * 2 : include_idx_N;
      LOG("%s: resizing array from %d to %d elements (%d bytes)", __func__,
	  table->num_entries, new_size, new_size * table->entry_size);
      new_table = realloc(table->entries, new_size * table->entry_size);
      LOG("%s: new array ptr = %p", __func__, new_table);
      if (!new_table)
	return armeabisupport_ENOMEM;
      memset_clear_start = (void *)((unsigned)new_table + table->num_entries * table->entry_size);
      memset_clear_length = (new_size - table->num_entries) * table->entry_size;
    }

  memset(memset_clear_start, 0, memset_clear_length);

  table->entries = new_table;
  table->num_entries = new_size;

  return NULL;
}

void array_free(array_t *table)
{
  TRACE("%s: table=%p", __func__, table);

  if (table && table->entries)
    {
      free(table->entries);
      table->entries = NULL;
    }
}

_kernel_oserror *array_alloc_entry(array_t *this, int *idx_ret)
{
  *idx_ret = ARRAY_INVALID_INDEX;

  int i;
  for (i = 0; i < this->num_entries; i++)
    if (array_get_entry_ptr(this, i)->in_use == 0)
      {
	LOG("%s:   Found free entry at index %d", __func__, i);
	break;
      }

  if (i == this->num_entries)
    {
      LOG("%s:   No free entries (i=%d), extending array", __func__, i);
      /* No free entries, increase array.  */
      _kernel_oserror *err = array_alloc(this, -1);
      if (err)
	return err;
      i = (this->num_entries == ARRAY_INIT_NUM_ENTRIES) ? 0 : this->num_entries / 2;
    }

  LOG("%s:   Inserting entry at index %d", __func__, i);

  array_entry_mark_used(array_get_entry_ptr(this, i));

  *idx_ret = i;

  return NULL;
}

_kernel_oserror *array_copy(array_t *dst, array_t *src)
{
  if (!dst || !src)
    return armeabisupport_EINVAL;

  dst->entries = malloc(src->num_entries * src->entry_size);
  if (!dst->entries)
    return armeabisupport_ENOMEM;

  memcpy(dst->entries, src->entries, src->num_entries * src->entry_size);
  dst->num_entries = src->num_entries;
  dst->entry_size = src->entry_size;

  return NULL;
}
