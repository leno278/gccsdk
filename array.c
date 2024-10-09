/* array.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "array.h"
#include "armeabisupport.h"
#include <stdlib.h>
#include <string.h>

#define ENABLE_TRACING 1
#include "trace.h"

#define ENABLE_LOGGING 1
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

_kernel_oserror *array_alloc(array_t *table)
{
  TRACE("%s: table=%p", __func__, table);
  int new_size;
  array_entry_base *new_table;

  void *memset_clear_start;
  int memset_clear_length;

  if (!table)
    return armeabisupport_EINVAL;

  if (!table->entries)
    {
      new_size = ARRAY_INIT_NUM_ENTRIES;
      new_table = malloc(new_size * table->entry_size);
      LOG("%%s: creating array of %d elements (%d bytes)",
	  __func__, new_size, new_size * table->entry_size);
      LOG("%s: array ptr = %p", __func__, new_table);
      if (!new_table)
	return armeabisupport_ENOMEM;
      memset_clear_start = new_table;
      memset_clear_length = new_size * table->entry_size;
    }
  else
    {
      new_size = table->num_entries * 2;
      LOG("%s: resizing array from %d to %d elements (%d bytes)", __func__,
	  table->num_entries, new_size, new_size * table->entry_size);
      new_table = realloc(table->entries, new_size * table->entry_size);
      LOG("%s: new array ptr = %p", __func__, new_table);
      if (!new_table)
	return armeabisupport_ENOMEM;
      memset_clear_start = new_table + table->num_entries * table->entry_size;
      memset_clear_length = table->num_entries * table->entry_size;
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
