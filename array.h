 /* array.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef ARRAY_H
#define ARRAY_H

#include <kernel.h>

typedef struct _array_entry_base { } array_entry_base;
typedef struct _array_t
{
  struct _array_entry_base *entries;
  int num_entries;
  int entry_size;
} array_t;

_kernel_oserror *array_ctor(array_t *this, const int entry_size);

_kernel_oserror *array_alloc(array_t *table);
void array_free(array_t *table);

static inline void array_dtor(array_t *this)
{
  array_free(this);
}

#endif
