/* table_entry_base.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef TABLE_ENTRY_BASE_H
#define TABLE_ENTRY_BASE_H

#include "array.h"

typedef struct _table_entry_base
{
  array_entry_base array_base;

  int ref_count;

} table_entry_base;

#endif
