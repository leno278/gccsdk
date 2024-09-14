/* array.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "array.h"
#include <stdlib.h>
#include <string.h>

#define ARRAY_INIT_NUM_ENTRIES 16

int array_ctor(array_t *this, const int entry_size)
{
  if (!this || !entry_size)
    return 0;

  this->entries = NULL;
  this->num_entries = 0;
  this->entry_size = entry_size;

  return 1;
}

int array_alloc(array_t *table)
{
  int new_size;
  array_entry_base *new_table;

  void *memset_clear_start;
  int memset_clear_length;

  if (!table)
    return 0;

  if (!table->entries)
    {
      new_size = ARRAY_INIT_NUM_ENTRIES;
      new_table = malloc(new_size * table->entry_size);
      if (!table->entries)
	return 0;
      memset_clear_start = new_table;
      memset_clear_length = new_size * table->entry_size;
    }
  else
    {
      new_size = table->num_entries * 2;
      new_table = realloc(table->entries, new_size * table->entry_size);
      if (!new_table)
	return 0;
      memset_clear_start = new_table + table->num_entries * table->entry_size;
      memset_clear_length = table->num_entries * table->entry_size;
    }

  memset(memset_clear_start, 0, memset_clear_length);

  table->entries = new_table;
  table->num_entries = new_size;

  return 1;
}

void array_free(array_t *table)
{
  if (table && table->entries)
    {
      free(table->entries);
      table->entries = NULL;
    }
}
