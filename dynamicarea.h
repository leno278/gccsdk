/* dynamicarea.h
 *
 * Copyright 2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef DYNAMICAREA_H
#define DYNAMICAREA_H

#include "types.h"
#include "swi.h"

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define PAGE_SIZE_MASK 0xfff

typedef struct dynamicarea
{
  int number;
  eabi_PTR base;
  /* Size of DA in pages.  */
  size_t size;
  /* The maximum number of pages available for allocation.
   * base + (max_size - 1) gives the last addressable page. */
  size_t max_size;

} dynamicarea;

_kernel_oserror *dynamic_area_create (const char *name,
				      int max_size,
				      unsigned type,
				      dynamicarea *ret);

_kernel_oserror *
dynamicarea_claim_pages(dynamicarea *,
			eabi_PTR first_page_address,
			unsigned num_pages);

_kernel_oserror *
dynamicarea_release_pages(dynamicarea *,
			  eabi_PTR first_page_address,
			  unsigned num_pages);

_kernel_oserror *
dynamicarea_map_pages(dynamicarea *,
		      eabi_PTR address,
		      unsigned int num_pages,
		      unsigned flags);

_kernel_oserror *
dynamicarea_unmap_pages(dynamicarea *,
			eabi_PTR address,
			unsigned num_pages);

_kernel_oserror *
dynamicarea_unmap_release_all_pages(dynamicarea *da,
				    int page,
				    size_t num_pages);

_kernel_oserror *
dynamicarea_extend (dynamicarea *da, int by, void **block);

_kernel_oserror *
dynamicarea_heap_alloc(dynamicarea *da, int size, eabi_PTR *block_ret);

static inline size_t
page_align_size (size_t v)
{
  return (v + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

static inline eabi_PTR
page_align_addr (eabi_PTR v)
{
  return (eabi_PTR) (((unsigned int) v + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1));
}

static inline int
bytes_to_pages (size_t v)
{
  return (int)page_align_size (v) >> PAGE_SHIFT;
}

static inline size_t
pages_to_bytes (int v)
{
  return (size_t)(v << PAGE_SHIFT);
}

/* Return TRUE if the number of bytes is a multiple of the page size.  */
static inline int is_whole_pages(size_t bytes)
{
  return (bytes & (PAGE_SIZE - 1)) == 0;
}

/* Return TRUE if the address is page aligned.  */
static inline int is_page_aligned(eabi_PTR addr)
{
  return ((unsigned)addr & (PAGE_SIZE - 1)) == 0;
}

static inline eabi_PTR page_to_addr(dynamicarea *da, uint32_t page)
{
  return da->base + pages_to_bytes(page);
}

static inline int
get_page_number(dynamicarea *da, eabi_PTR address)
{
  return (address - (eabi_PTR)da->base) >> PAGE_SHIFT;
}

#endif
