/* mmap.h
 *
 * Copyright 2021-2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef MMAP_H
#define MMAP_H

#include <kernel.h>
#include "memory.h"
#include "array.h"
#include "types.h"

typedef int mmap_allocator_t;
typedef int mmap_mapping_t;

typedef struct _app_mmap_mapping
{
  array_entry_base base;

  mmap_allocator_t allocator_idx;
  mmap_mapping_t mapping_idx;

  eabi_PTR start_addr;		/* Inclusive */
  eabi_PTR end_addr;		/* Exclusive */

} app_mmap_mapping;

typedef struct _mmap_mapping
{
  array_entry_base base;

  int prev;
  int next;

  uint32_t start_page;	/* Inclusive */
  uint32_t end_page;	/* Non inclusive */

  uint32_t access;	/* Memory protection flags as used by the OS  */

  int prot;		/* Memory protection flags as used by mmap  */
  int mmap_flags;	/* Flags as used by mmap  */
  int opentable_idx;
  int fd;
  uint32_t offset;

} mmap_mapping;

typedef struct _mmap_list
{
  array_t array;

  int first;
  int last;
  int count;

} mmap_list;

typedef struct _mmap_allocator
{
  array_entry_base base;
  dynamicarea da;

  mmap_list list;

  char name[ALLOCATOR_NAME_MAX_LEN];
} mmap_allocator;

static inline mmap_allocator *mmap_allocator_get_entry(int idx)
{
  return (mmap_allocator *)array_get_entry_ptr(&global.mmap_allocator_pool, idx);
}

static inline int mmap_allocator_get_index(mmap_allocator *entry)
{
  return array_get_entry_idx(&global.mmap_allocator_pool, &entry->base);
}

static inline mmap_mapping *mmap_mapping_get_entry(mmap_allocator *allocator,
						   mmap_mapping_t mapping_idx)
{
  return (mmap_mapping *)array_get_entry_ptr(&allocator->list.array, mapping_idx);
}

static inline mmap_mapping *mmap_mapping_get_by_idx(mmap_allocator_t allocator_idx,
						    mmap_mapping_t mapping_idx)
{
  mmap_allocator *allocator = mmap_allocator_get_entry(allocator_idx);
  return mmap_mapping_get_entry(allocator, mapping_idx);
}

static inline mmap_mapping *
mmap_list_get_first(mmap_list *list)
{
  return (mmap_mapping *)array_get_entry_ptr(&list->array, list->first);
}

static inline mmap_mapping *
mmap_list_get_last(mmap_list *list)
{
  return (mmap_mapping *)array_get_entry_ptr(&list->array, list->last);
}

static inline mmap_mapping *
mmap_list_get_next(mmap_list *list, mmap_mapping *map)
{
  return (mmap_mapping *)array_get_entry_ptr(&list->array, map->next);
}

static inline mmap_mapping *
mmap_list_get_prev(mmap_list *list, mmap_mapping *map)
{
  return (mmap_mapping *)array_get_entry_ptr(&list->array, map->prev);
}

static inline mmap_mapping *mmap_list_get_entry(mmap_list *list, int idx)
{
  return (mmap_mapping *)array_get_entry_ptr(&list->array, idx);
}

static inline bool mmap_list_is_empty(mmap_list *list)
{
  return (list->first == ARRAY_INVALID_INDEX && list->last == ARRAY_INVALID_INDEX);
}

extern _kernel_oserror *mmap_op(_kernel_swi_regs * r);
extern void *get_mmap_abort_handler(void);
extern void mmap_cleanup_app(app_object *app);
extern _kernel_oserror *
armeabi_mmap(eabi_PTR addr, size_t size, int prot, int flags, int fd, size_t offset,
	     eabi_PTR *);
extern _kernel_oserror *
armeabi_mmap_internal(size_t size, int prot, int flags, int opentable_idx, size_t offset,
		      app_mmap_mapping **mapping_ret);

extern _kernel_oserror *
mmap_list_ctor(mmap_list *this);
void mmap_list_dtor(mmap_list *this);

extern void mmap_list_dtor(mmap_list *this);

extern _kernel_oserror *
mmap_allocator_mmap(mmap_allocator_t allocator_idx,
		    const int required_page_count,
		    unsigned flags,
		    mmap_mapping_t *map_idx_ret);

_kernel_oserror *
mmap_allocator_change_protection(mmap_allocator *allocator,
				 eabi_PTR addr,
				 size_t num_pages,
				 uint32_t flags);

_kernel_oserror *
mmap_new_allocator(mmap_allocator_t *idx_ret);

/* Unmap and free memory where we have an address and the number of pages.  */
_kernel_oserror *
mmap_allocator_munmap(mmap_allocator *handle,
		      eabi_PTR block, size_t num_pages);
_kernel_oserror *
mmap_allocator_mremap(app_mmap_mapping *app_mmap,
		      mmap_allocator *allocator,
		      mmap_mapping *block,
		      int page_diff,
		      uint32_t flags,
		      app_mmap_mapping **app_mmap_ret);


/* Unmap and free memory where we already know the block.  */
_kernel_oserror *
mmap_allocator_munmap_block(mmap_allocator *allocator,
			    mmap_mapping *block);


app_mmap_mapping *
appmmap_add_mapping(app_object *app,
		    mmap_allocator_t,
		    mmap_mapping_t,
		    eabi_PTR,
		    eabi_PTR);

app_mmap_mapping *
appmmap_get_mapping(app_object *app,
		    eabi_PTR addr);
app_mmap_mapping *
appmmap_get_mapping_by_index(app_object *app,
			     mmap_allocator_t allocator_idx,
			     mmap_mapping_t mapping_idx);

void appmmap_list_mappings(app_object *app);

#endif
