 /* array.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef ARRAY_H
#define ARRAY_H

#include <kernel.h>
#include <stddef.h>
#include <stdbool.h>

#define ARRAY_INIT_NUM_ENTRIES 16

typedef struct _array_entry_base
{
  bool in_use;
} array_entry_base;

typedef struct _array_t
{
  array_entry_base *entries;
  int num_entries;
  int entry_size;
} array_t;

_kernel_oserror *array_ctor(array_t *this, const int entry_size);

_kernel_oserror *array_alloc(array_t *table);
void array_free(array_t *table);

_kernel_oserror *array_alloc_entry(array_t *this, int *idx_ret);

static inline void array_dtor(array_t *this)
{
  array_free(this);
}

static inline array_entry_base *array_get_entry_ptr(array_t *this, int index)
{
  return index >= 0
	 ? (array_entry_base *)((unsigned)this->entries + (index * this->entry_size))
	 : NULL;
}

static inline int array_get_entry_idx(array_t *this, array_entry_base *entry)
{
  return entry ? (int)(((unsigned)entry - (unsigned)this->entries) / this->entry_size)
	       : -1;
}

static inline void array_entry_mark_unused(array_entry_base *entry)
{
  entry->in_use = false;
}

static inline void array_entry_mark_used(array_entry_base *entry)
{
  entry->in_use = true;
}

#endif
