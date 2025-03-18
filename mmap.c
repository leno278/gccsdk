/* mmap.c
 *
 * Copyright 2021-2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "mmap.h"
#include "memory.h"
#include "abort.h"
#include "shm.h"
#include "fdtable.h"
#include "shmtable.h"
#include "opentable.h"
#include "main.h"
#include "swi.h"
#include "debug.h"
#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <string.h>

#define ENABLE_TRACING 0
#define ENABLE_SWI_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define MMAP_MAX_DA_SIZE (100 * 1024 * 1024)

#define MMAPOP_MAP	0
#define MMAPOP_UNMAP	1
#define MMAPOP_ADVISE	2
#define MMAPOP_PROTECT	3
#define MMAPOP_GET_INFO	4
#define MMAPOP_REMAP	5
#define MMAPOP_GET_PROT	6
#define MMAPOP_MSYNC	7
#define MMAPOP_DUMP	16

static _kernel_oserror *
armeabi_munmap(eabi_PTR addr,
	       size_t size);
static _kernel_oserror *
armeabi_madvise(eabi_PTR addr,
		size_t size,
		int advice);
static _kernel_oserror *
armeabi_mprotect(void *addr,
		 size_t len,
		 int prot);
static void mmap_dump(void);
static _kernel_oserror *
armeabi_get_info(eabi_PTR addr,
		 eabi_PTR *mmap_addr,
		 size_t *size,
		 int *prot,
		 int *flags,
		 int *handle,
		 size_t *offset);
static _kernel_oserror *
armeabi_mremap (eabi_PTR addr,
		size_t old_len,
		size_t new_len,
		int flags,
		eabi_PTR *);
static _kernel_oserror *
armeabi_get_page_protection (eabi_PTR addr,
			     int *prot_ret);
static _kernel_oserror *
armeabi_msync (eabi_PTR addr,
	       size_t len,
	       unsigned flags);
static void
mmap_allocator_destroy(mmap_allocator * allocator);

/*  r0 = reason code
*	0 - Map
*	  Entry:
*	    r1 = address (usually NULL, otherwise must point to existing block and even then can
*			  only be used to change protection)
*	    r2 = length of mapping in bytes
*	    r3 = protection flags as used by mmap
*	    r4 = mmap flags
*	    r5 = fd
*	    r6 = offset
*	Exit:
*	    r0 = page aligned address of allocated block if successful (V flag clear) or error pointer (V flag set)
*	1 - Unmap
*	  Entry:
*	    r1 = address - must exist in an existing block, but does not have to be its start address
*	    r2 = length of mapping in bytes - address + length must be contained within an existing block
*	  Exit:
*	    r0 = 0 if successful (V flag clear) or error pointer (V flag set)
*	2 - Advise
*	  Entry:
*	    r1 = address
*	    r2 = number of bytes
*	    r3 = advice - as per madvise
*	  Exit:
*	    r0 = 0 if successful (V flag clear) or error pointer (V flag set)
*	3 - Protect
*	  Entry:
*	    r1 = address
*	    r2 = number of bytes
*	    r3 = prot - as per mprotect
*	  Exit:
*	    r0 = 0 if successful (V flag clear) or error pointer (V flag set)
*	4 - GetInfo
*	  Entry:
*	    r1 = address within an existing block
*	  Exit:
*	    r0 = address of start of block if successful (V flag clear) or error pointer (V flag set)
*	    r1 = length of block in bytes
*	    r2 = mmap protection flags when block was allocated
*	    r3 = mmap flags used when block was allocated
*	    r4 = mmap fd given when block was allocated
*	    r5 = mmap offset given when block was allocated
*	5 - Remap
*	  Entry:
*	    r1 = address of block to remap
*	    r2 = old length of block in bytes
*	    r3 = new length of block in bytes
*	    r4 = flags as mremap
*	  Exit:
*	    r0 = address of block's new address if successful (V flag clear) or error pointer (V flag set)
*	6 - Get Protection
*	  Entry:
*	    r1 = address within page to get protection flags of
*	  Exit:
*	    r0 = mmap protection flags for page containing address given
*	7 - msync
*	  Entry:
*	    r1 = address
*	    r2 = number of bytes
*	    r3 = flags
*	  Exit:
*	    r0 = 0 if successful (V flag clear) or error pointer (V flag set)
*/

_kernel_oserror *
mmap_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  switch (r->r[0])
  {
    case MMAPOP_MAP: {
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_MAP, addr=0x%X, size=0x%X, prot=0x%X, flags=0x%X, fd=%d, offset:%d",
		r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6]);
      err = armeabi_mmap((eabi_PTR)r->r[1],
			 (size_t)r->r[2],
			 r->r[3],
			 r->r[4],
			 r->r[5],
			 (size_t)r->r[6],
			 (eabi_PTR *)&r->r[0]);
      if (!err)
	{
	  TRACE_SWI("  Result = 0x%X", r->r[0]);
	}
      break;
    }
    case MMAPOP_UNMAP:
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_UNMAP, addr=0x%X, size=0x%X",
		r->r[1], r->r[2]);
      err = armeabi_munmap((eabi_PTR)r->r[1],
			   (size_t)r->r[2]);
      break;
    case MMAPOP_ADVISE:
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_ADVISE, addr=0x%X, size=0x%X, advice=%d",
		r->r[1], r->r[2], r->r[3]);
      err = armeabi_madvise((eabi_PTR)r->r[1],
			    (size_t)r->r[2],
			    r->r[3]);
      break;
    case MMAPOP_PROTECT:
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_PROTECT, addr=0x%X, size=0x%X, prot=%d",
		r->r[1], r->r[2], r->r[3]);
      err = armeabi_mprotect((eabi_PTR)r->r[1],
			     (size_t)r->r[2],
			     r->r[3]);
      break;
    case MMAPOP_GET_INFO: {
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_GET_INFO, addr=0x%X", r->r[1]);

      eabi_PTR map_addr;
      size_t size, offset;
      int prot, flags, handle;
      err = armeabi_get_info((eabi_PTR)r->r[1],
			     &map_addr,
			     &size,
			     &prot,
			     &flags,
			     &handle,
			     &offset);
      if (!err)
        {
	  r->r[0] = (int)map_addr;
	  r->r[1] = (int)size;
	  r->r[2] = prot;
	  r->r[3] = flags;
	  r->r[4] = handle;
	  r->r[5] = (int)offset;
	}
      break;
    }
    case MMAPOP_REMAP: {
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_REMAP, addr=0x%X, old_size=0x%X, new_size=0x%X, flags=0x%X",
		r->r[1], r->r[2], r->r[3], r->r[4]);
      err = armeabi_mremap((eabi_PTR)r->r[1],
			   (size_t)r->r[2],
			   (size_t)r->r[3],
			   r->r[4],
			   (eabi_PTR *)&r->r[0]);
      if (!err)
	{
	  TRACE_SWI("  Result = 0x%X", r->r[0]);
	}
      break;
    }
    case MMAPOP_GET_PROT: {
      int prot;
      err = armeabi_get_page_protection((eabi_PTR)r->r[1],
					&prot);
      if (!err)
	r->r[0] = prot;
      break;
    }
    case MMAPOP_MSYNC:
      TRACE_SWI("SWI \"ARMEABISupport_MMapOp\", MMAPOP_MSYNC, addr=0x%X, len=0x%X, flags=0x%X",
		r->r[1], r->r[2], r->r[3]);
      err = armeabi_msync((eabi_PTR)r->r[1],
			  (size_t)r->r[2],
			  (unsigned)r->r[3]);
      break;
    case MMAPOP_DUMP:
      mmap_dump();
      break;
    default:
      err = armeabisupport_bad_reason;
      break;
  }

  if (err)
    {
      TRACE_SWI("  Error = %s", err->errmess);
    }

  return err;
}

static inline mmap_allocator *
get_allocator_ptr(const eabi_PTR addr)
{
  mmap_allocator *allocator = (mmap_allocator *)global.mmap_allocator_pool.entries;
  for (int i = 0; i < global.mmap_allocator_pool.num_entries; i++)
    if (array_entry_is_in_use(&allocator[i].base))
      {
	if (addr >= allocator[i].da.base && addr < allocator[i].da.base + pages_to_bytes(allocator[i].da.size))
	  return &allocator[i];
      }

  return NULL;
}

static inline mmap_allocator_t
get_allocator_idx(const eabi_PTR addr)
{
  mmap_allocator *allocator = (mmap_allocator *)global.mmap_allocator_pool.entries;
  for (int i = 0; i < global.mmap_allocator_pool.num_entries; i++)
    if (array_entry_is_in_use(&allocator[i].base))
      {
	if (addr >= allocator[i].da.base && addr < allocator[i].da.base + pages_to_bytes(allocator[i].da.size))
	  return i;
      }

  return ARRAY_INVALID_INDEX;
}

static inline mmap_mapping *
get_block(mmap_allocator *allocator, eabi_PTR addr)
{
  int page = get_page_number(&allocator->da, addr);
  mmap_mapping *block;
  for (block = mmap_list_get_first(&allocator->list);
       block && (page < block->start_page || page >= block->end_page);
       block = mmap_list_get_next(&allocator->list, block))
    /* Empty loop  */;

  return block;
}

_kernel_oserror *
mmap_list_ctor(mmap_list *this)
{
  _kernel_oserror *err = array_ctor(&this->array, sizeof(mmap_mapping));
  if (err)
    return err;

  this->first = ARRAY_INVALID_INDEX;
  this->last = ARRAY_INVALID_INDEX;
  this->count = 0;

  return NULL;
}

void mmap_list_dtor(mmap_list *this)
{
  if (this)
    {
      this->first = ARRAY_INVALID_INDEX;
      this->last = ARRAY_INVALID_INDEX;

      array_dtor(&this->array);
    }
}

static void mmap_list_add_to_end(mmap_list *list, mmap_mapping *map)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  mmap_mapping *old_last = mmap_list_get_last(list);
  map->next = ARRAY_INVALID_INDEX;
  map->prev = list->last;

  int mapping_idx = array_get_entry_idx(&list->array, &map->base);

  if (old_last == NULL)
    list->first = mapping_idx;
  else
    old_last->next = mapping_idx;

  list->last = mapping_idx;
  list->count++;
}

static void mmap_list_add_to_front(mmap_list *list, mmap_mapping *map)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  mmap_mapping *old_first = mmap_list_get_first(list);

  map->next = list->first;
  map->prev = ARRAY_INVALID_INDEX;

  int mapping_idx = array_get_entry_idx(&list->array, &map->base);

  if (old_first == NULL)
    list->last = mapping_idx;
  else
    old_first->prev = mapping_idx;

  list->first = mapping_idx;
  list->count++;
}

static void mmap_list_add_before(mmap_list *list, mmap_mapping *at, mmap_mapping *map)
{
//  TRACE("%s: list(%d,%d), at=%p", __func__, list->first, list->last, at);

  if (at == NULL)
    {
      mmap_list_add_to_end(list, map);
      return;
    }

  mmap_mapping *old_prev = (mmap_mapping *)array_get_entry_ptr(&list->array, at->prev);
  if (old_prev == NULL)
    mmap_list_add_to_front(list, map);
  else
    {
      old_prev->next = array_get_entry_idx(&list->array, &map->base);
      map->prev = array_get_entry_idx(&list->array, &old_prev->base);
      at->prev = array_get_entry_idx(&list->array, &map->base);
      map->next = array_get_entry_idx(&list->array, &at->base);
      list->count++;
    }
}

static void mmap_list_dump(mmap_list *list)
{
  reporter_printf("%s: list=0x%X, count=%d",__func__,list,list->count);
  mmap_mapping *entry = mmap_list_get_first(list);
  if (!entry)
    reporter_printf("%s: Empty", __func__);
  else
    while (entry)
    {
      reporter_printf("%s: current=%-4d, prev=%d, next=%d, start=%d, end=%d",
		      __func__,
		      array_get_entry_idx(&list->array, &entry->base),
		      entry->prev, entry->next,
		      entry->start_page, entry->end_page);
      entry = mmap_list_get_next(list, entry);
    }
}

static void mmap_list_remove(mmap_list *list, mmap_mapping *map)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  int prev_idx = map->prev;
  int next_idx = map->next;

  /* Guard against double removal.  */
  if (prev_idx == ARRAY_INVALID_INDEX &&
      next_idx == ARRAY_INVALID_INDEX &&
      list->count > 1)
    {
      LOG("%s: Possible double removal: list=0x%X, mapping=%d, prev=%d, next=%d, count=%d",
	  __func__,
	  list,
	  array_get_entry_idx(&list->array, &map->base),
	  prev_idx,
	  next_idx,
	  list->count);
      return;
    }

  mmap_mapping *prev = mmap_list_get_prev(list, map);
  mmap_mapping *next = mmap_list_get_next(list, map);

  if (next == NULL)
    list->last = prev_idx;
  else
    next->prev = prev_idx;

  if (prev == NULL)
    list->first = next_idx;
  else
    prev->next = next_idx;

  map->prev = map->next = ARRAY_INVALID_INDEX;
  list->count--;
}

static _kernel_oserror *convert_protection(int prot, uint32_t *access_flags_ret)
{
  uint32_t access_flags = 0;
  if (prot & PROT_READ)
    access_flags |= OS_PERMISSION_FLAG_USER_READ;
  if (prot & PROT_WRITE)
    access_flags |= OS_PERMISSION_FLAG_USER_WRITE;
  if (prot & PROT_EXEC)
    access_flags |= OS_PERMISSION_FLAG_USER_EXEC;

  return get_os_permissions(access_flags,
			    OS_PERMISSION_FLAG_USER_READ |
				OS_PERMISSION_FLAG_USER_WRITE |
				OS_PERMISSION_FLAG_USER_EXEC,
			    access_flags_ret);
}

/* We only support addr being non-null when it refers to an existing block and we are changing
 * the protection.  */
static _kernel_oserror *
armeabi_mmap_non_null_addr(eabi_PTR addr, size_t size, int prot, eabi_PTR *addr_ret)
{
  TRACE("armeabi_mmap_non_null_addr(0x%X, %d, %d)", addr, size, prot);

  _kernel_oserror *err;
  mmap_allocator *allocator = get_allocator_ptr(addr);
  if (!allocator)
    {
      err = armeabisupport_ENOMEM;
      goto error;
    }

  mmap_mapping *block = get_block(allocator, addr);
  if (!block)
    {
      err = armeabisupport_ENOMEM;
      goto error;
    }

  int page = get_page_number(&allocator->da, addr);
  uint32_t num_pages = bytes_to_pages(size);

  if (page + num_pages > block->end_page)
    {
      err = armeabisupport_ENOMEM;
      goto error;
    }

  uint32_t access_flags;
  if ((err = convert_protection(prot, &access_flags)) != NULL)
    goto error;

  /* Record the new protection only if it's for the whole block.  */
  if (page == block->start_page && num_pages == block->end_page - block->start_page)
    {
      block->prot = prot;
      block->access = access_flags;
    }

  *addr_ret = addr;
  return mmap_allocator_change_protection(allocator, addr, num_pages, access_flags);

error:
  *addr_ret = NULL;
  return err;
}

_kernel_oserror *
armeabi_mmap(eabi_PTR addr, size_t size, int prot, int flags, int fd, size_t offset,
	     eabi_PTR *addr_ret)
{
  if (size == 0)
    return armeabisupport_EINVAL;

  if (addr != NULL)
    return armeabi_mmap_non_null_addr(addr, size, prot, addr_ret);

  if (flags & MAP_FIXED)
    return armeabisupport_EINVAL;

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  int opentable_idx = ARRAY_INVALID_INDEX;
  if (fd != -1)
    {
      fdtable_entry *fd_entry = get_fdtable_entry(app, fd);
      if (fd_entry && array_entry_is_in_use(&fd_entry->base))
	opentable_idx = fd_entry->opentable_idx;
    }

  app_mmap_mapping *mapping;
  if ((err = armeabi_mmap_internal(app, size, prot, flags, opentable_idx, offset, &mapping)) == NULL)
    {
      *addr_ret = mapping->start_addr;

      mapping->fd = fd;
    }

  return err;
}

/* Try to allocate the memory from an existing allocator in the pool.
 * If that fails, create a new allocator.
 */
static _kernel_oserror *
mmap_pool_allocate_memory(int num_pages,
			  mmap_allocator_t *allocator_idx_ret,
			  mmap_mapping_t *mapping_idx_ret)
{
  _kernel_oserror *err = NULL;
  mmap_allocator *allocator = (mmap_allocator *)global.mmap_allocator_pool.entries;
  mmap_mapping_t mmap_idx = ARRAY_INVALID_INDEX;
  mmap_allocator_t allocator_idx;
  for (allocator_idx = 0; allocator_idx < global.mmap_allocator_pool.num_entries; allocator_idx++)
    if (array_entry_is_in_use(&allocator[allocator_idx].base))
      {
	err = mmap_allocator_mmap(allocator_idx,
				  num_pages,
				  ALLOCATOR_ALLOC_FLAG_MAP,
				  &mmap_idx);
	if (!err && mmap_idx != ARRAY_INVALID_INDEX)
	  break;
      }

  if (err || mmap_idx == ARRAY_INVALID_INDEX)
    {
      /* Failed to get an allocation from an existing allocator, create a new one.  */
      LOG("%s: Failed to get allocation from existing allocator, creating new allocator", __func__);
      if ((err = mmap_new_allocator(&allocator_idx)) == NULL)
	{
	  LOG("%s",__LINE__,__func__);
	  err = mmap_allocator_mmap(allocator_idx,
				    num_pages,
				    ALLOCATOR_ALLOC_FLAG_MAP,
				    &mmap_idx);
	}

    }

  if (err || mmap_idx == ARRAY_INVALID_INDEX)
    {
      LOG("%d:%s - err=%s",__LINE__,__func__,err->errmess);
      *allocator_idx_ret = ARRAY_INVALID_INDEX;
      *mapping_idx_ret = ARRAY_INVALID_INDEX;
      return armeabisupport_ENOMEM;
    }

  *allocator_idx_ret = allocator_idx;
  *mapping_idx_ret = mmap_idx;

  return NULL;
}

_kernel_oserror *
armeabi_mmap_internal(app_object *app,
		      size_t size,
		      int prot,
		      int flags,
		      int opentable_idx,
		      size_t offset,
		      app_mmap_mapping **mapping_ret)
{
  _kernel_oserror *err;

  /* fdtable_open_shm (shm_open) creates a shared memory object with no memory mapping
   * and returns an FD to it. If we can see here that there is no mapping for the given FD
   * then this is the first time that mmap has been called for this shared object and we
   * make the allocation and assign it to the shared object.
   * If there is already a mapping assigned to the shared object, then mmap has already been
   * called for this shared object and we use that rather than creating another mapping.  */
  opentable_entry *ot_entry = opentable_get_entry(opentable_idx);
  systemshmtable_entry *systemshm_entry = NULL;
  if (ot_entry)
    {
      if (ot_entry->type == opentable_type_SHM)
	{
	  LOG("%s: shm detected, open table index %d", __func__, opentable_idx);
	  systemshm_entry = (systemshmtable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHM],
								      ot_entry->systemtable_idx);
	  if (systemshm_entry->mapping_idx != ARRAY_INVALID_INDEX)
	    {
	      /* We've already got a memory allocation for this shared memory object.  */
	      mmap_allocator *allocator = mmap_allocator_get_entry(systemshm_entry->allocator_idx);
	      mmap_mapping *block = mmap_mapping_get_entry(allocator, systemshm_entry->mapping_idx);
	      ot_entry->ref_count++;
	      *mapping_ret = appmmap_add_mapping(app,
						 systemshm_entry->allocator_idx,
						 systemshm_entry->mapping_idx,
						 page_to_addr(&allocator->da, block->start_page),
						 page_to_addr(&allocator->da, block->end_page));
	      LOG("%s: Already connected to shm, ref count now %d for open table index %d, name=\"%s\", addr=0x%X (+++)",
		  __func__,
		  ot_entry->ref_count,
		  opentable_idx,
		  systemshm_entry->name,
		  (*mapping_ret) ? (*mapping_ret)->start_addr : NULL);
	      return (*mapping_ret) ? NULL : armeabisupport_ENOMEM;
	    }
	}
      else if (ot_entry->type == opentable_type_FILE)
	{
	  LOG("%s: Looking for file mapping with offset=%d, size=%d",
	      __func__,
	      offset,
	      size);
	  app_mmap_mapping *entry = (app_mmap_mapping *)app->mmap_table.entries;
	  for (int i = 0; i < app->mmap_table.num_entries; i++)
	    if (array_entry_is_in_use(&entry[i].base))
	      {
		mmap_allocator *allocator = mmap_allocator_get_entry(entry[i].allocator_idx);
		mmap_mapping *mapping = mmap_mapping_get_entry(allocator, entry[i].mapping_idx);
		if (mapping->opentable_idx == opentable_idx)
		  {
		    LOG("%s: Found mapping for file with open table index %d - offset=%d, size=%d",
			__func__,
			opentable_idx,
			mapping->offset,
			pages_to_bytes(mapping->end_page - mapping->start_page));
		    *mapping_ret = &entry[i];
		    return NULL;
		  }
	      }
	}
      }

  uint32_t num_pages = bytes_to_pages(size);

  mmap_allocator_t allocator_idx;
  mmap_mapping_t mmap_idx;

  /* Try to allocate the memory from an existing allocator in the pool or create a new allocator.  */
  if ((err = mmap_pool_allocate_memory(num_pages, &allocator_idx, &mmap_idx)) != NULL)
    {
      *mapping_ret = NULL;
      return err;
    }

  mmap_allocator *allocator = mmap_allocator_get_entry(allocator_idx);
  mmap_mapping *block = mmap_mapping_get_entry(allocator, mmap_idx);

  LOG("%s: Pages %d to %d allocated from %s", __func__, block->start_page, block->end_page, allocator->name);

  block->opentable_idx = opentable_idx;
  block->mmap_flags = flags;
  block->prot = prot;
  block->offset = offset;
  block->size = size;

  if (ot_entry && systemshm_entry)
    {
      systemshm_entry->allocator_idx = allocator_idx;
      systemshm_entry->mapping_idx = mmap_idx;
      ot_entry->ref_count++;
      LOG("%s: Connecting to shm, ref count now %d for open table index %d, name=\"%s\", addr=0x%X (+++)",
	  __func__,
	  ot_entry->ref_count,
	  opentable_idx,
	  systemshm_entry->name,
	  page_to_addr(&allocator->da, block->start_page));
    }
  else if (ot_entry && ot_entry->type == opentable_type_FILE)
    {
      ot_entry->ref_count++;
    }

  LOG("%s: Allocated %d pages @ 0x%X from allocator %s",
      __func__, num_pages, page_to_addr(&allocator->da, block->start_page), allocator->name);

  *mapping_ret = appmmap_add_mapping(app,
				     allocator_idx,
				     mmap_idx,
				     page_to_addr(&allocator->da, block->start_page),
				     page_to_addr(&allocator->da, block->end_page));
  return (*mapping_ret) ? NULL : armeabisupport_ENOMEM;
}

/* The address addr must be a multiple of the page size (but length need not be).  */
_kernel_oserror *
armeabi_munmap(eabi_PTR addr, size_t length)
{
  if (!is_page_aligned(addr))
    return armeabisupport_EINVAL;

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  app_mmap_mapping *app_mmap = appmmap_get_mapping(app, addr);
  if (!app_mmap)
    {
      LOG("%s: Failed to find mapping for address 0x%X", __func__, addr);
      return NULL;
    }

  LOG("%s: Found mapping %d for address 0x%X, fd = %d",
      __func__, array_get_entry_idx(&app->mmap_table, &app_mmap->base), addr, app_mmap->fd);

  mmap_allocator *allocator = mmap_allocator_get_entry(app_mmap->allocator_idx);
  uint32_t page = get_page_number(&allocator->da, addr);
  uint32_t num_pages = bytes_to_pages(length);

  mmap_mapping *mapping;
  mapping = mmap_mapping_get_by_idx(app_mmap->allocator_idx, app_mmap->mapping_idx);
  LOG("%s: page=%d, num_pages=%d, start_page=%d, end_page=%d",
      __func__, page, num_pages, mapping->start_page, mapping->end_page);
  if (page == mapping->start_page && num_pages == mapping->end_page - mapping->start_page)
    {
      /* Freeing whole block. Is it a shared object?  */
      if (mapping->opentable_idx != ARRAY_INVALID_INDEX)
	{
	  LOG("%s: Found whole block with open table index %d", __func__, mapping->opentable_idx);

	  opentable_entry *ot_entry;
	  if ((ot_entry = opentable_get_entry(mapping->opentable_idx)) == NULL
		  || !array_entry_is_in_use(&ot_entry->array_base))
	    {
	      LOG("%s: Bad entry for open table index %d: armeabisupport_EBADF", __func__, mapping->opentable_idx);
	      return armeabisupport_EBADF;
	    }

	  err = ot_entry->method->unmap(allocator, mapping);
	  if (err)
	    {
	      LOG("%s: OS Error: %s", __func__, err->errmess);
	      return err;
	    }
	}
      else
	{
	  LOG("%s: Free whole block 0x%X -> 0x%X", __func__, app_mmap->start_addr, app_mmap->end_addr);

	  mmap_allocator_munmap_block(allocator, mapping);
	}

      /* The mapping is always removed from this apps table even if the mapping still exists
       * for other apps.  */
      array_entry_mark_unused(&app_mmap->base);
    }
  else if (page == mapping->start_page)
    {
      /* Freeing start of a block.  */
      dynamicarea_unmap_release_all_pages(&allocator->da, page, num_pages);
      mapping->start_page += num_pages;
      /* Update the apps view of this mapping.  */
      app_mmap->start_addr = page_to_addr(&allocator->da, mapping->start_page);
      LOG("%s: Free start of block - start page = %d, start addr = 0x%X",
	  __func__, mapping->start_page, app_mmap->start_addr);
    }
  else if (page + num_pages == mapping->end_page)
    {
      /* Freeing the end of a block.  */
      mapping->end_page -= num_pages;
      /* Update the apps view of this mapping.  */
      app_mmap->end_addr = page_to_addr(&allocator->da, mapping->end_page);
      dynamicarea_unmap_release_all_pages(&allocator->da, mapping->end_page, num_pages);
      LOG("%s: Free end of block - end page = %d, end addr = 0x%X",
	  __func__, mapping->end_page, app_mmap->end_addr);
    }
  else
    {
      LOG("%s: Attempt to punch hole in mapping", __func__);
      /* Creating a central hole is not allowed.  */
      return armeabisupport_ENOMEM;
    }

  mapping = mmap_list_get_last(&allocator->list);
  if (!mapping)
    allocator->da.size = 0;
  else if (mapping->end_page < allocator->da.size)
    allocator->da.size = mapping->end_page;

  return NULL;
}

_kernel_oserror *
armeabi_madvise(eabi_PTR addr, size_t size, int advice)
{
  mmap_allocator *allocator = get_allocator_ptr(addr);
  if (!allocator)
    {
      LOG("armeabi_madvise: Failed to find address in allocator pool");
      return armeabisupport_ENOMEM;
    }

  _kernel_oserror *err;
  uint32_t num_pages = bytes_to_pages(size);
  if (advice == MADV_DONTNEED || advice == MADV_FREE)
    {
      if ((err = dynamicarea_unmap_pages(&allocator->da, addr, num_pages)) == NULL)
        err = dynamicarea_release_pages(&allocator->da, addr, num_pages);
    }
  else if (advice == MADV_WILLNEED || advice == MADV_NORMAL)
    {
      if ((err = dynamicarea_claim_pages(&allocator->da, addr, num_pages)) == NULL)
	err = dynamicarea_map_pages(&allocator->da, addr, num_pages, 0);
    }
  else
    err = armeabisupport_EINVAL;

  if (err)
    LOG("%s: OS Error %s", __func__, err->errmess);

  return err;
}

void mmap_dump(void)
{
//  allocator_dump_all(&global.mmap_allocator_pool);
}

_kernel_oserror *
armeabi_mprotect(void *addr, size_t len, int prot)
{
  /* The caller should already have checked this.  */
  if (!is_page_aligned(addr) || len == 0)
    return armeabisupport_EINVAL;

  mmap_allocator *allocator = get_allocator_ptr(addr);
  if (!allocator)
    {
      LOG("armeabi_mprotect: Failed to find address in allocator pool");
      return armeabisupport_ENOMEM;
    }

  uint32_t access_flags;
  _kernel_oserror *err = convert_protection(prot, &access_flags);
  if (err)
    return err;

  uint32_t num_pages = bytes_to_pages(len);
  return mmap_allocator_change_protection(allocator, addr, num_pages, access_flags);
}

static _kernel_oserror *
armeabi_get_info(eabi_PTR addr,
		 eabi_PTR *mmap_addr,
		 size_t *size,
		 int *prot,
		 int *flags,
		 int *fd,
		 size_t *offset)
{
  if (!is_page_aligned(addr))
    {
      LOG("armeabi_get_info: Address 0x%X not page aligned", addr);
      return armeabisupport_EINVAL;
    }

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  app_mmap_mapping *app_mapping = appmmap_get_mapping(app, addr);
  mmap_allocator *allocator = mmap_allocator_get_entry(app_mapping->allocator_idx);
  if (!allocator)
    {
      LOG("armeabi_get_info: Failed to find address 0x%X in allocator pool", addr);
      return armeabisupport_ENOMEM;
    }

  mmap_mapping *mapping = mmap_mapping_get_entry(allocator, app_mapping->mapping_idx);
  if (!mapping)
    {
      LOG("%s: Failed to find block containing 0x%X in allocator %s", __func__, addr, allocator->name);
      return armeabisupport_ENOMEM;
    }

  *mmap_addr = page_to_addr(&allocator->da, mapping->start_page);
  *size = pages_to_bytes(mapping->end_page - mapping->start_page);
  *prot = mapping->prot;
  *flags = mapping->mmap_flags;
  *fd = app_mapping->fd;
  *offset = mapping->offset;

  LOG("armeabi_get_info: addr=0x%X, mmap_addr=0x%X size=0x%X, prot=0x%X flags=0x%X, fd=%d, offset=%d",
      addr, *mmap_addr, *size, *prot, *flags, *fd, *offset);

  return NULL;
}

_kernel_oserror *
armeabi_mremap (eabi_PTR addr,
		size_t old_len,
		size_t new_len,
		int flags,
		eabi_PTR *addr_ret)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  if (new_len == 0)
    {
      LOG("armeabi_mremap: new_len = 0");
      err = armeabisupport_EINVAL;
      goto error;
    }

  app_mmap_mapping *app_mmap = appmmap_get_mapping(app, addr);
  if (!app_mmap)
    {
      LOG("%s: Failed to find mapping for address 0x%X", __func__, addr);
      return NULL;
    }

  mmap_allocator *allocator = mmap_allocator_get_entry(app_mmap->allocator_idx);
  mmap_mapping *mapping = mmap_mapping_get_entry(allocator,
						 app_mmap->mapping_idx);
  if (!mapping)
    {
      LOG("%s: Failed to find address (%p) in mmap allocator pool", __func__, addr);
      err = armeabisupport_ENOMEM;
      goto error;
    }

  /* For simplicity don't allow remapping of mapped files.  */
  if (mapping->opentable_idx != ARRAY_INVALID_INDEX)
    {
      LOG("%s: Attempt to remap file with open table index %d, addr:%p, old_len:%ld, new_len:%ld",
	  __func__, mapping->opentable_idx, addr, old_len, new_len);
      return armeabisupport_EOPSYS;
    }

  if (new_len == old_len)
    {
      *addr_ret = app_mmap->start_addr;
      return NULL;
    }

  /* We can only remap a block that was fully allocated previously.  */
  int page = get_page_number(&allocator->da, addr);
  if (page != mapping->start_page || bytes_to_pages(old_len) != mapping->end_page - mapping->start_page)
    {
      LOG("armeabi_mremap: Non matching block, looking for (%d : %d), got (%d : %d)",
	  page, bytes_to_pages(old_len), mapping->start_page, mapping->end_page - mapping->start_page);
      err = armeabisupport_EINVAL;
      goto error;
    }

  /* Ok, we have a block that exactly matches the one in the remap request.  */
  if ((err = mmap_allocator_mremap(app_mmap,
				   allocator,
				   mapping,
				   bytes_to_pages(new_len - old_len),
				   flags,
				   &app_mmap)) != NULL)
    return err;

  *addr_ret = app_mmap->start_addr;

  return NULL;

error:
  *addr_ret = NULL;

  return err;
}

_kernel_oserror *
armeabi_get_page_protection (eabi_PTR addr, int *prot_ret)
{
  mmap_allocator *allocator = get_allocator_ptr(addr);
  int page = get_page_number(&allocator->da, addr);

  pmp_page_info_entry entry;
  entry.pmp_page_index = page;
  entry.physical_page_number = -1;
  entry.da_page_number = -1;
  entry.flags = 0;
  _kernel_oserror *err = dynamic_pmp_page_info(allocator->da.number, &entry, 1);
  if (err || entry.da_page_number == -1)
    {
      LOG("armeabi_get_page_protection: dynamic_pmp_page_info error: %s", err->errmess);
      return armeabisupport_page_map_error;
    }

  int flags = get_access_permissions(entry.flags);
  int prot = PROT_NONE;
  if (flags != -1)
    {
      if (flags & OS_PERMISSION_FLAG_USER_EXEC)
        prot |= PROT_EXEC;
      if (flags & OS_PERMISSION_FLAG_USER_WRITE)
        prot |= PROT_WRITE;
      if (flags & OS_PERMISSION_FLAG_USER_READ)
        prot |= PROT_READ;
    }

  *prot_ret = prot;

  return NULL;
}

_kernel_oserror *
armeabi_msync (eabi_PTR addr,
	       size_t len,
	       unsigned flags)
{
  if ((unsigned)addr & PAGE_SIZE_MASK)
    return armeabisupport_EINVAL;

  mmap_allocator *allocator = get_allocator_ptr(addr);
  if (!allocator)
    {
      LOG("%s: Failed to find address (%p) in mmap allocator pool", __func__, addr);
      return armeabisupport_ENOMEM;
    }

  mmap_mapping *block = get_block(allocator, addr);
  if (!block)
    {
      LOG("%s: Failed to find block", __func__);
      return armeabisupport_ENOMEM;
    }

  if (block->opentable_idx == ARRAY_INVALID_INDEX)
    {
      LOG("%s: Invalid open table index %d", __func__, block->opentable_idx);
      return armeabisupport_ENOMEM;
    }

  /* Only valid if this is a file open for non-private writing.  */
  if (!(block->prot & PROT_WRITE) || (block->mmap_flags & MAP_PRIVATE))
    return armeabisupport_EINVAL;

  opentable_entry *ot_entry = opentable_get_entry(block->opentable_idx);
  if (!ot_entry ||
      !array_entry_is_in_use(&ot_entry->array_base) ||
      ot_entry->type != opentable_type_FILE)
    return armeabisupport_EBADF;

#if ENABLE_LOGGING
  systemfiletable_entry *st_entry =
		(systemfiletable_entry *)array_get_entry_ptr(&global.system_table[fd_type_SHARED_FILE],
							     ot_entry->systemtable_idx);
  LOG("%s: shared memory object %p, \"%s\", open table index %d, addr=%p, len=%d, oref=%d, sref=%d",
       __func__,
       st_entry,
       st_entry->filename,
       block->opentable_idx,
       page_to_addr(&allocator->da, block->start_page),
       pages_to_bytes(block->end_page - block->start_page),
       ot_entry->ref_count,
       st_entry->table_base.ref_count);
#endif

  off_t offset = block->offset;
  int bytes_not_written;
  _kernel_oserror *err;
  err = systemfiletable_write_bytes(ot_entry->systemtable_idx,
				    page_to_addr(&allocator->da, block->start_page),
				    pages_to_bytes(block->end_page - block->start_page),
				    &offset,
				    &bytes_not_written);
  if (err)
    {
      LOG("%s: OS Error: %s", __func__, err->errmess);
    }
  else if (bytes_not_written != 0)
    err = armeabisupport_EIO;

  return err;
}

_kernel_oserror *
mmap_new_allocator(mmap_allocator_t *idx_ret)
{
  *idx_ret = ARRAY_INVALID_INDEX;

  LOG("%s: Allocate mmap_allocator", __func__);
  _kernel_oserror *err;
  if ((err = array_alloc_entry(&global.mmap_allocator_pool, idx_ret)) != NULL)
    return err;
  LOG("%s - Allocated mmap_allocator index %d",__func__,*idx_ret);

  mmap_allocator *allocator = mmap_allocator_get_entry(*idx_ret);

  snprintf(allocator->name, ALLOCATOR_NAME_MAX_LEN, "mmap#%d", *idx_ret);

  if ((err = dynamic_area_create (allocator->name,
				  MMAP_MAX_DA_SIZE,
				  ALLOCATOR_TYPE_MMAP,
				  &allocator->da)) == NULL)
    {
      LOG("%s: New allocator %s created with base 0x%X", __func__, allocator->name, allocator->da.base);
      mmap_list_ctor(&allocator->list);
      return NULL;
    }

  if (allocator->da.number != 0)
    dynamic_area_remove(allocator->da.number);
  if (*idx_ret != ARRAY_INVALID_INDEX)
    array_entry_mark_unused(&allocator->base);

  return err;
}

static void
mmap_allocator_destroy(mmap_allocator * allocator)
{
  mmap_list_dtor(&allocator->list);
  if (allocator->da.number != 0)
    {
      dynamic_area_remove(allocator->da.number);
      allocator->da.number = 0;
    }
  array_entry_mark_unused(&allocator->base);
}

_kernel_oserror *
mmap_allocator_mmap(mmap_allocator_t allocator_idx,
		    const int required_page_count,
		    unsigned flags,
		    mmap_mapping_t *map_idx_ret)
{
  mmap_allocator *allocator = mmap_allocator_get_entry(allocator_idx);

  LOG("%s - Trying to allocate %d pages from %s",
      __func__,required_page_count,allocator->name);

  mmap_mapping *current, *next, *new_block;

  LOG("%s: Allocate mmap_mapping", __func__);
  _kernel_oserror *err;
  if ((err = array_alloc_entry(&allocator->list.array, map_idx_ret)) != NULL)
    return err;
  LOG("%s: Allocated mmap_mapping index %d", __func__, *map_idx_ret);

  new_block = mmap_list_get_entry(&allocator->list, *map_idx_ret);

  current = mmap_list_get_first(&allocator->list);
  if (!current || required_page_count <= current->start_page)
    {
      if (!current)
	{
	  LOG("%s: First allocation in %s", __func__, allocator->name);
	}
      else
	{
	  LOG("%s: Found space before first block in %s", __func__, allocator->name);
	}

      /* If there are no allocations yet, then this is the first block.
       * Otherwise, if there is space before the first block, then we use it by
       * adding the new block before the first one.  */
      new_block->start_page = 0;
      new_block->end_page = required_page_count;
      mmap_list_add_to_front(&allocator->list, new_block);
    }
  else
  {
    /* Go through the list of blocks looking for a gap that we can use
     * between the current block and the next one.  */
    while (current)
    {
      next = mmap_list_get_next(&allocator->list, current);
      if (!next)
        {
	  /* If we get to the end of the list, then just add the new allocation
	   * to the end of the list.  */
	  if (current->end_page + required_page_count > allocator->da.max_size)
	    {
	      /* This allocation would take us beyond the end of the DA, so
	       * clean up and return an error.  */
	      array_entry_mark_unused(&new_block->base);
	      *map_idx_ret = ARRAY_INVALID_INDEX;
	      return armeabisupport_ENOMEM;
	    }

	  new_block->start_page = current->end_page;
	  new_block->end_page = new_block->start_page + required_page_count;
	  mmap_list_add_to_end(&allocator->list, new_block);
	  break;
        }
      else
        {
	  size_t gap = next->start_page - current->end_page;
	  if (required_page_count <= gap)
	    {
	      /* If we find a gap we can use, then insert the new allocation into the
	       * list.  */
	      new_block->start_page = current->end_page;
	      new_block->end_page = new_block->start_page + required_page_count;
	      mmap_list_add_before(&allocator->list, next, new_block);
	      break;
	    }
        }
      current = next;
    }
  }

  /* Keep the allocator overall size up to date.  */
  if (new_block->end_page > allocator->da.size)
    allocator->da.size = new_block->end_page;

  if ((flags & ALLOCATOR_ALLOC_FLAGS_MASK) == ALLOCATOR_ALLOC_FLAG_ALLOC_ONLY)
    return NULL;

  eabi_PTR block_address = page_to_addr(&allocator->da, new_block->start_page);
  if ((err = dynamicarea_claim_pages(&allocator->da,
				block_address,
				required_page_count)) != NULL)
    {
      LOG("%d:%s - err=%s", __LINE__, __func__, err->errmess);
      return err;
    }

  if ((flags & ALLOCATOR_ALLOC_FLAGS_MASK) != ALLOCATOR_ALLOC_FLAG_MAP)
    return NULL;

  return dynamicarea_map_pages(&allocator->da,
			       block_address,
			       required_page_count,
			       flags & ALLOCATOR_ALLOC_FLAGS_ACCESS_MASK);
}

_kernel_oserror *mmap_allocator_change_protection(mmap_allocator *allocator,
						  eabi_PTR addr,
						  size_t num_pages,
						  uint32_t flags)
{
  _kernel_oserror *err = NULL;
  pmp_page_info_entry entry;

  if (num_pages < 0)
    return armeabisupport_page_map_error;

  int page = get_page_number(&allocator->da, addr);
  mmap_mapping *block;
  for (block = mmap_list_get_first (&allocator->list);
       block && (page < block->start_page || page >= block->end_page);
       block = mmap_list_get_next(&allocator->list, block))
    /* Empty loop  */;

  if (!block)
    {
      LOG("%s: Failed to find a block for %p", __func__, addr);
      return armeabisupport_page_map_error;
    }

  /* First check if all pages are mapped.  */
  int i = num_pages;
  int p = page;
  while (i--)
  {
    entry.pmp_page_index = p;
    entry.physical_page_number = -1;
    entry.da_page_number = -1;
    entry.flags = 0;
    err = dynamic_pmp_page_info(allocator->da.number, &entry, 1);
    if (err || entry.da_page_number == -1)
      {
	if (err)
	  {
	    LOG("allocator_change_protection: dynamic_pmp_page_info error: %s", err->errmess);
	  }
	return armeabisupport_page_map_error;
      }
    p++;
  }

  /* Now change the memory protection.  */
  p = page;
  i = num_pages;
  while (i--)
  {
    pmp_log_page_entry logical_op;
    logical_op.da_page_number = p; /* Same page means don't change the mapping.  */
    logical_op.pmp_page_index = p;
    logical_op.flags = flags;
    if ((err = dynamic_pmp_map_unmap(allocator->da.number, &logical_op, 1)) != NULL)
      {
	LOG("allocator_change_protection: dynamic_pmp_map_unmap error: %s", err->errmess);
        return armeabisupport_page_map_error;
      }
    p++;
  }

  block->mmap_flags = flags;

  return NULL;
}

static _kernel_oserror *
mmap_allocator_move_block(app_mmap_mapping *app_mmap,
			  mmap_allocator *allocator,
			  mmap_mapping *mapping,
			  int page_diff,
			  app_mmap_mapping **app_mmap_ret)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  err = armeabi_mmap_internal(app,
			      pages_to_bytes((mapping->end_page - mapping->start_page) + page_diff),
			      mapping->prot,
			      mapping->mmap_flags,
			      mapping->opentable_idx,
			      mapping->offset,
			      app_mmap_ret);
  if (err)
    {
      *app_mmap_ret = NULL;
      return err;
    }

  mmap_allocator *new_allocator = mmap_allocator_get_entry((*app_mmap_ret)->allocator_idx);
  mmap_mapping *new_mapping = mmap_mapping_get_entry(allocator, (*app_mmap_ret)->mapping_idx);

  /* Copy the contents of the old mapping to the new.  */
  memcpy(page_to_addr(&new_allocator->da, new_mapping->start_page),
	 page_to_addr(&allocator->da, mapping->start_page),
	 pages_to_bytes(mapping->end_page - mapping->start_page));
  /* Zero fill the new pages.  */
  memset(page_to_addr(&new_allocator->da, new_mapping->end_page - page_diff),
	 0,
	 pages_to_bytes(page_diff));
  /* Delete the old mapping.  */
  dynamicarea_unmap_release_all_pages(&allocator->da,
				      mapping->start_page,
				      mapping->end_page - mapping->start_page);
  mmap_list_remove(&allocator->list, mapping);
  array_entry_mark_unused(&app_mmap->base);
  array_entry_mark_unused(&mapping->base);

  return NULL;
}

_kernel_oserror *
mmap_allocator_mremap(app_mmap_mapping *app_mmap,
		      mmap_allocator *allocator,
		      mmap_mapping *mapping,
		      int page_diff,
		      uint32_t flags,
		      app_mmap_mapping **app_mmap_ret)
{
  if (page_diff < 0)
    {
      LOG("%s: Reduce mapping by 0x%X pages", __func__, -page_diff);

      /* Request to shrink the block. Unmap the pages we no longer need.  */
      mapping->end_page += page_diff;		/* Note -ve page_diff.  */
      app_mmap->end_addr = page_to_addr(&allocator->da, mapping->end_page);
      *app_mmap_ret = app_mmap;
      return dynamicarea_unmap_release_all_pages(&allocator->da,
						 mapping->end_page,
						 -page_diff);	/* Note -ve page_diff, this makes it +ve.  */
    }

  mmap_mapping *next_mapping = mmap_list_get_next(&allocator->list, mapping);
  if (next_mapping)
    {
      /* Check if there's space between this and the next block to expand into.  */
      if (next_mapping->start_page - mapping->end_page > page_diff)
        {
	  LOG("%s: Extend mapping into gap before next mapping", __func__);

	  _kernel_oserror *err;
	  /* Expand into gap. Claim and Map in the new pages.  */
	  if ((err = dynamicarea_claim_pages(&allocator->da,
					     page_to_addr(&allocator->da, mapping->end_page),
					     page_diff)) == NULL)
	    err = dynamicarea_map_pages(&allocator->da,
					page_to_addr(&allocator->da, mapping->end_page),
					page_diff,
					0);
	  if (err)
	    return err;

	  /* Zero fill the new pages.  */
	  memset(page_to_addr(&allocator->da, mapping->end_page),
		 0,
		 pages_to_bytes(page_diff));
	  mapping->end_page += page_diff;
	  app_mmap->end_addr = page_to_addr(&allocator->da, mapping->end_page);
	  *app_mmap_ret = app_mmap;
	  /* FIXME: Need to reapply memory protections after zeroing.  */
	}
      else
	{
	  LOG("%s: Gap before next mapping not big enough, move to new mapping", __func__);

	  if ((flags & MREMAP_MAYMOVE) == 0)
	    {
	      LOG("%s: MREMAP_MAYMOVE not set, not allowed to move mapping", __func__);
	      return armeabisupport_no_memory;
	    }

	  /* Can't expand into gap, have to move to a new mapping instead.  */
	  return mmap_allocator_move_block(app_mmap,
					   allocator,
					   mapping,
					   page_diff,
					   app_mmap_ret);
	}
    }
  else
    {
      /* This is the last mapping, check if there's enough space at the end of the DA
       * to expand into.  */
      if (mapping->end_page + page_diff > allocator->da.max_size)
	{
	  LOG("%s: End of DA, but not enough space, move to new mapping", __func__);

	  if ((flags & MREMAP_MAYMOVE) == 0)
	    {
	      LOG("%s: MREMAP_MAYMOVE not set, not allowed to move mapping", __func__);
	      return armeabisupport_no_memory;
	    }

	  /* Extending the mapping would take us past the end of the DA, have
	   * to move to a new mapping instead.  */
	  return mmap_allocator_move_block(app_mmap,
					   allocator,
					   mapping,
					   page_diff,
					   app_mmap_ret);
	}
      else
        {
	  LOG("%s: End of DA, extend into unused space", __func__);

	  _kernel_oserror *err;
	  /* We can extend into the end of the DA. Claim and Map in the new pages.  */
	  if ((err = dynamicarea_claim_pages(&allocator->da,
					     page_to_addr(&allocator->da, mapping->end_page),
					     page_diff)) == NULL)
	    err = dynamicarea_map_pages(&allocator->da,
					page_to_addr(&allocator->da, mapping->end_page),
					page_diff,
					0);
	  if (err)
	    return err;

	  /* Zero fill the new pages.  */
	  memset(page_to_addr(&allocator->da, mapping->end_page),
		 0,
		 pages_to_bytes(page_diff));
	  mapping->end_page += page_diff;
	  app_mmap->end_addr = page_to_addr(&allocator->da, mapping->end_page);
	  allocator->da.size += page_diff;
	  *app_mmap_ret = app_mmap;
	  /* FIXME: Need to reapply memory protections after zeroing.  */
	}
    }
  return NULL;
}

_kernel_oserror *
mmap_allocator_munmap_block(mmap_allocator *allocator,
			    mmap_mapping *mapping)
{
  if (!allocator)
    {
      LOG("%s: attempt to free from NULL allocator", __func__);
      return NULL;
    }

  if (!mapping)
    {
      LOG("%s: attempt to free NULL mapping for allocator %s", __func__, allocator->name);
      return NULL;
    }

  dynamicarea_unmap_release_all_pages(&allocator->da, mapping->start_page, mapping->end_page - mapping->start_page);
  mmap_list_remove(&allocator->list, mapping);
  array_entry_mark_unused(&mapping->base);

  if (mmap_list_is_empty(&allocator->list))
    {
      LOG("%s: allocator %s now empty (%d), destroying...", __func__, allocator->name, allocator->list.count);
      mmap_allocator_destroy(allocator);
    }

  return NULL;
}

/* Remove any mmap allocations from the mmap pool for the given app.  */
void mmap_cleanup_app(app_object *app)
{
  TRACE("%s", __func__);
  abort_deregister(get_mmap_abort_handler(), (unsigned)&app->mmap_table);
  app_mmap_mapping *app_mmap = (app_mmap_mapping *)app->mmap_table.entries;
  for (int i = 0; i < app->mmap_table.num_entries; i++)
    if (array_entry_is_in_use(&app_mmap[i].base))
      {
	armeabi_munmap(app_mmap[i].start_addr, app_mmap[i].end_addr - app_mmap[i].start_addr);

	/* This isn't really necessary as we're about to release the array memory.
	array_entry_mark_unused(&app_mmap[i]->base);  */
      }

  array_dtor(&app->mmap_table);
}

app_mmap_mapping *
appmmap_add_mapping(app_object *app,
		    mmap_allocator_t allocator_idx,
		    mmap_mapping_t mapping_idx,
		    eabi_PTR start,
		    eabi_PTR end)
{
  LOG("%s: Allocate app_mmap_mapping", __func__);

  int mmap_table_idx;
  _kernel_oserror *err;
  if ((err = array_alloc_entry(&app->mmap_table, &mmap_table_idx)) != NULL)
    return NULL;

  app_mmap_mapping *mmap_table_entry = (app_mmap_mapping *)array_get_entry_ptr(&app->mmap_table, mmap_table_idx);

  LOG("%s: Allocated app_mmap_mapping index %d - 0x%X - 0x%X -> 0x%X",
      __func__, mmap_table_idx, mmap_table_entry, start, end);

  mmap_table_entry->allocator_idx = allocator_idx;
  mmap_table_entry->mapping_idx = mapping_idx;
  mmap_table_entry->start_addr = start;
  mmap_table_entry->end_addr = end;
  mmap_table_entry->fd = ARRAY_INVALID_INDEX;

  return mmap_table_entry;
}

app_mmap_mapping *
appmmap_get_mapping(app_object *app,
		    eabi_PTR addr)
{
  app_mmap_mapping *entry = (app_mmap_mapping *)app->mmap_table.entries;
  for (int i = 0; i < app->mmap_table.num_entries; i++)
    if (array_entry_is_in_use(&entry[i].base))
      if (addr >= entry[i].start_addr && addr < entry[i].end_addr)
	return &entry[i];

  return NULL;
}

app_mmap_mapping *
appmmap_get_mapping_by_index(app_object *app,
			     mmap_allocator_t allocator_idx,
			     mmap_mapping_t mapping_idx)
{
  app_mmap_mapping *entry = (app_mmap_mapping *)app->mmap_table.entries;
  for (int i = 0; i < app->mmap_table.num_entries; i++)
    if (array_entry_is_in_use(&entry[i].base) &&
	entry[i].allocator_idx == allocator_idx &&
	entry[i].mapping_idx == mapping_idx)
      return &entry[i];

  return NULL;
}

void appmmap_list_mappings(app_object *app)
{
  app_mmap_mapping *entry = (app_mmap_mapping *)app->mmap_table.entries;
  for (int i = 0; i < app->mmap_table.num_entries; i++)
    if (array_entry_is_in_use(&entry[i].base))
      {
	mmap_allocator *allocator = mmap_allocator_get_entry(entry[i].allocator_idx);
	mmap_mapping *mapping = mmap_mapping_get_entry(allocator, entry[i].mapping_idx);

	reporter_printf("%2d) %4d -> %4d - start=0x%X end=0x%X - allocator=%s",
			i,
			mapping->start_page,
			mapping->end_page,
			entry[i].start_addr,
			entry[i].end_addr,
			allocator->name);
      }
}

void mmap_allocator_pool_destroy(void)
{
  mmap_allocator *allocator = (mmap_allocator *)global.mmap_allocator_pool.entries;
  int num_entries = global.mmap_allocator_pool.num_entries;
  for (int i = 0; i < num_entries; i++)
    if (array_entry_is_in_use(&allocator[i].base))
      {
	if (allocator[i].da.number != -1)
	  {
	    dynamic_area_remove(allocator[i].da.number);
	    allocator[i].da.number = -1;
	  }
	mmap_list_dtor(&allocator[i].list);
      }
  array_dtor(&global.mmap_allocator_pool);
}
