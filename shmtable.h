/* shmtable.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef SHMTABLE_H
#define SHMTABLE_H

#include "table_entry_base.h"
#include "types.h"
#include <swi.h>

typedef struct _openshmtable_entry
{
  table_entry_base table_base;

  int systemtable_idx;
  int flags;
  int mode;

} openshmtable_entry;

_kernel_oserror *openshmtable_add_entry(int systemtable_idx, int flags, int mode, int *idx_ret);
_kernel_oserror *openshmtable_deref_entry(int opentable_idx);

typedef struct _systemshmtable_entry
{
  table_entry_base table_base;

  armeabisupport_allocator_mmap *allocator;
  mmap_block *block;

  bool linked;

  char *name;

} systemshmtable_entry;

_kernel_oserror *systemshmtable_open_shm(const char *name, int flags, int mode, int *);
_kernel_oserror *systemshmtable_deref_shm(int systemtable_idx);
void openshmtable_print(void);
void systemshmtable_print(void);

/* Return the index of the entry if it exists, otherwise ARRAY_INVALID_INDEX.  */
int systemshmtable_shm_exists(const char *name);

static inline const char *
systemshmtable_get_name(int systemtable_idx)
{
  array_t *system_shmtable = &global.system_table[fd_type_SHM];
  systemshmtable_entry *entry = (systemshmtable_entry *)array_get_entry_ptr(system_shmtable,
									    systemtable_idx);
  return entry->name;
}

void openshmtable_log(void);
void systemshmtable_log(void);

#endif
