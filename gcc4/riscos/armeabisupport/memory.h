/* memory.h
 *
 * Copyright 2019-2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <kernel.h>
#include "link_list.h"
#include "dynamicarea.h"
#include "swis.h"
#include "types.h"

#define DEBUG_ALLOCATOR 0

#define ALLOCATOR_TYPE_MASK		0xF
#define ALLOCATOR_TYPE_MMAP		0
#define ALLOCATOR_TYPE_PAGE		1
#define ALLOCATOR_TYPE_HEAP		2
#define ALLOCATOR_TYPE_SINGLE_USE	3
#define ALLOCATOR_FLAG_GLOBAL		(1 << 5)
#define ALLOCATOR_FLAG_ABORTTRAP	(1 << 6)

#define ALLOCATOR_VALIDATE_VALUE	0x58494E55	/* "UNIX" in ASCII.  */

#define MEMORYOP_NEW_ALLOCATOR		0
#define MEMORYOP_DESTROY_ALLOCATOR 	1
#define MEMORYOP_ALLOC			2
#define MEMORYOP_CLAIM_PAGES		3
#define MEMORYOP_RELEASE_PAGES		4
#define MEMORYOP_MAP_PAGES		5
#define MEMORYOP_UNMAP_PAGES		6
#define MEMORYOP_FREE			7
#define MEMORYOP_INFO			8
#define MEMORYOP_PAGE_NUMBER		9
#define MEMORYOP_ALLOCATOR_FROM_ADDRESS	10
#define MEMORYOP_ALLOCATOR_FROM_DA	11
#define MEMORYOP_DUMP_PAGE_MAPPINGS	20
#define MEMORYOP_DUMP_ALLOCATOR		21

#define ALLOCATOR_ALLOC_FLAG_ALLOC_ONLY	(0 << ALLOCATOR_ALLOC_FLAGS_SHIFT)
#define ALLOCATOR_ALLOC_FLAG_CLAIM	(1 << ALLOCATOR_ALLOC_FLAGS_SHIFT)
#define ALLOCATOR_ALLOC_FLAG_MAP	(2 << ALLOCATOR_ALLOC_FLAGS_SHIFT)
#define ALLOCATOR_ALLOC_FLAGS_MASK	(3 << ALLOCATOR_ALLOC_FLAGS_SHIFT)
#define ALLOCATOR_ALLOC_FLAGS_SHIFT	16

#define ALLOCATOR_ALLOC_FLAGS_ACCESS_MASK	0xffff

#define ALLOCATOR_HEAP_INITIAL_SIZE	4096

typedef enum _allocator_type
{
  allocator_type_INVALID = -1,
  allocator_type_MMAP = 0,
  allocator_type_PAGE,
  allocator_type_HEAP,
  allocator_type_SINGLE,
  allocator_type_MAX

} allocator_type;

typedef struct _armeabisupport_allocator armeabisupport_allocator;

typedef struct _virtual_function_table
{
  _kernel_oserror * (*new)(armeabisupport_allocator *);
  void (*destroy)(armeabisupport_allocator *);
  _kernel_oserror * (*alloc)(armeabi_allocator_t, size_t size, unsigned flags, eabi_PTR *ret);
  _kernel_oserror * (*free)(armeabi_allocator_t, eabi_PTR block, size_t size);
  void (*dump)(armeabisupport_allocator *);
#if VALIDATE_VT
  uint32_t magic;
#endif
} virtual_function_table;

typedef struct _armeabisupport_allocator
{
  link_hdr link;
  unsigned validate;
  dynamicarea da;
  unsigned flags;
  allocator_type type;
  const virtual_function_table *vt;

  /* Used by paged and mmap allocators.  */
  link_list block_list;

  char name[ALLOCATOR_NAME_MAX_LEN];
} armeabisupport_allocator;
LINKLIST_ACCESS_FUNCTIONS (armeabisupport_allocator)

_kernel_oserror *
memory_op(_kernel_swi_regs * r);

_kernel_oserror *
allocator_new(const char *da_name,
	      int max_size,
	      unsigned flags,
	      armeabi_allocator_t *allocator_ret);

_kernel_oserror *
allocator_new_internal(const char *da_name,
		       int max_size,
		       unsigned flags,
		       armeabi_allocator_t *allocator_ret);

_kernel_oserror *
allocator_destroy(armeabisupport_allocator *handle);

_kernel_oserror *
allocator_destroy_internal(armeabisupport_allocator *allocator);

_kernel_oserror *
allocator_alloc(armeabi_allocator_t handle,
		const int required_page_count,
		unsigned flags,
		eabi_PTR *block_ret);

_kernel_oserror *
allocator_free(armeabi_allocator_t handle,
	       eabi_PTR block);

void allocator_destroy_all(link_list *list);

/* Remove any mmap allocations from the given allocator for the given app.  */
void
allocator_mmap_cleanup_app(armeabisupport_allocator *allocator,
			   app_object *app);

extern void allocator_dump_all(link_list *list);

#endif
