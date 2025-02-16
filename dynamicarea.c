/* dynamicarea.c
 *
 * Copyright 2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "dynamicarea.h"

#include "debug.h"

#define ENABLE_LOGGING 0
#include "log.h"

#define ENABLE_TRACING 0
#include "trace.h"

_kernel_oserror *
dynamicarea_claim_pages(dynamicarea *da,
			eabi_PTR address,
			unsigned num_pages)
{
  pmp_phy_page_entry *list;
  _kernel_oserror *err;
  int i, pmp_index;

  if ((err = rma_claim(sizeof(*list) * num_pages, (void **)&list)) != NULL)
    return err;

  for (i = 0, pmp_index = get_page_number(da, address);
       i < num_pages;
       i++, pmp_index++)
    {
      list[i].pmp_page_index = pmp_index;
      list[i].physical_page_number = PMP_PAGE_KERNEL_CHOICE;
      list[i].flags = 0;
    }

#if DEBUG_ALLOCATOR
  DEBUG_OUTPUT("%s - address: %p, num_pages: %d, pages %d to %d",
		__func__,address,num_pages,
		get_page_number(da, address),
		get_page_number(da, address) + (num_pages - 1));
#endif

  err = dynamic_pmp_claim_release (da->number,
				   list,
				   num_pages);
  rma_free (list);

  if (err)
    {
      DEBUG_OUTPUT_SWI_ERROR(err);
    }

  return err;
}

_kernel_oserror *
dynamicarea_release_pages(dynamicarea *da,
			  eabi_PTR address,
			  unsigned num_pages)
{
  pmp_phy_page_entry *list;
  _kernel_oserror *err;
  int i, pmp_index;

  if ((err = rma_claim(sizeof(*list) * num_pages, (void **)&list)) != NULL)
    return err;

  for (i = 0, pmp_index = get_page_number(da, address);
       i < num_pages;
       i++, pmp_index++)
    {
      list[i].pmp_page_index = pmp_index;
      list[i].physical_page_number = PMP_PAGE_RELEASE;
      list[i].flags = 0;
    }

#if DEBUG_ALLOCATOR
  DEBUG_OUTPUT("%s - address: %p, num_pages: %d, pages %d to %d",
		__func__,address,num_pages,
		get_page_number(da, address),
		get_page_number(da, address) + (num_pages - 1));
#endif

  err = dynamic_pmp_claim_release (da->number,
				   list,
				   num_pages);
  rma_free(list);

  if (err)
    {
      DEBUG_OUTPUT_SWI_ERROR(err);
    }

  return err;
}

_kernel_oserror *
dynamicarea_map_pages(dynamicarea *da,
		      eabi_PTR address,
		      unsigned num_pages,
		      unsigned flags)
{
  pmp_log_page_entry *list;
  _kernel_oserror *err;
  int i, pmp_index, da_page_num;

  if ((err = rma_claim(sizeof(*list) * num_pages, (void **)&list)) != NULL)
    return err;

  for (i = 0,
        pmp_index = get_page_number(da, address),
        da_page_num = get_page_number(da, address);
       i < num_pages;
       i++, pmp_index++, da_page_num++)
    {
      list[i].da_page_number = da_page_num;
      list[i].pmp_page_index = pmp_index;
      list[i].flags = flags;
    }

#if DEBUG_ALLOCATOR
  DEBUG_OUTPUT("%s - address: %p, num_pages: %d, pages %d to %d",
		__func__, address, num_pages,
		get_page_number(da, address),
		get_page_number(da, address) + num_pages - 1);
#endif

  err = dynamic_pmp_map_unmap (da->number,
			       list,
			       num_pages);
  rma_free(list);

  if (err)
    {
/*      log_printf(LOG_REPORT,"name: %s, base: 0x%X, max_pages: %d",allocator.base->name,allocator.base->da.base,allocator.base->da.max_size);
      log_printf(LOG_REPORT,"address: 0x%X",address);
      log_printf(LOG_REPORT,"page_number: %d",get_page_number(&allocator.base, address));
      log_printf(LOG_REPORT,"num_pages: 0x%X",num_pages);*/
      DEBUG_OUTPUT_SWI_ERROR(err);
    }

  return err;
}

_kernel_oserror *
dynamicarea_unmap_pages(dynamicarea *da,
			eabi_PTR address,
			unsigned num_pages)
{
  pmp_log_page_entry *list;
  _kernel_oserror *err;
  int i, pmp_index, da_page_num;

  if ((err = rma_claim(sizeof(*list) * num_pages, (void **)&list)) != NULL)
    return err;

  for (i = 0,
        pmp_index = get_page_number(da, address),
        da_page_num = get_page_number(da, address);
       i < num_pages;
       i++, pmp_index++, da_page_num++)
    {
      list[i].da_page_number = da_page_num;
      list[i].pmp_page_index = PMP_PAGE_UNMAP;
      list[i].flags = 0;
    }

#if DEBUG_ALLOCATOR
  DEBUG_OUTPUT("%s - address: %p, num_pages: %d, pages %d to %d",
	       __func__,
	       address, num_pages,
	       get_page_number(da, address),
	       get_page_number(da, address) + num_pages - 1);
#endif

  err = dynamic_pmp_map_unmap (da->number,
			       list,
			       num_pages);
  rma_free(list);

  if (err)
    {
      DEBUG_OUTPUT_SWI_ERROR(err);
    }

  return err;
}

/* Step through the pages one by one checking if they're mapped,
 * and releasing and unmapping them if they are.  */
_kernel_oserror *
dynamicarea_unmap_release_all_pages(dynamicarea *da,
				    int page,
				    size_t num_pages)
{
  _kernel_oserror *err = NULL;
  pmp_page_info_entry entry;
  int page_entry[3];

#if DEBUG_ALLOCATOR
  DEBUG_OUTPUT("  unmap_release_pages(addr=%p, num_pages=%d)",
	       page_to_addr(da, page),
	       num_pages);
#endif

  if (num_pages < 0)
    return armeabisupport_page_map_error;

  while (num_pages--)
  {
#if DEBUG_ALLOCATOR
    DEBUG_OUTPUT("  unmap_release_pages(page=%d)", page);
#endif
    entry.pmp_page_index = page;//-1;
    entry.physical_page_number = -1;
    entry.da_page_number = -1;//page;
    entry.flags = 0;
    err = dynamic_pmp_page_info(da->number, &entry, 1);

    if (!err && entry.da_page_number != -1)
    {
      page_entry[0] = page;
      page_entry[1] = PMP_PAGE_UNMAP; /* Happens to be same as PMP_PAGE_RELEASE.  */
      page_entry[2] = 0;
      if ((err = dynamic_pmp_map_unmap(da->number, (pmp_log_page_entry *)page_entry, 1)) == NULL)
	err = dynamic_pmp_claim_release(da->number, (pmp_phy_page_entry *)page_entry, 1);
    }

    if (err)
      {
        DEBUG_OUTPUT_SWI_ERROR(err);
      }

    page++;
  }

  return err;
}

_kernel_oserror *
dynamicarea_heap_alloc(dynamicarea *da, int size, eabi_PTR *block_ret)
{
  bool heap_extended = false;
  _kernel_oserror *err = NULL;

  while (true)
    {
      /* If this fails, then assume heap is full. The failure could be due
         to a corrupted heap or invalid r1, but we can't distinguish
         here.  */
      err = heap_claim(da->base, size, (void **)block_ret);

      if (err || *block_ret == NULL)
	{
	  if (heap_extended)
	    {
	      /* If we've already extended the heap and it still fails,
	         then don't try again, return the error.  */
	      if (!err)
		err = armeabisupport_no_memory;

	      return err;
	    }

	  /* Attempt to increase heap size and try again. */
	  int heap_inc = page_align_size(size * 2);

	  /* Extend DA by (((allocation request * 2) + 0xfff) & ~0xfff)  */
	  if ((err = dynamic_area_extend(da->number, heap_inc)) != NULL)
	    {
	      DEBUG_OUTPUT_SWI_ERROR(err);
	      return err;
	    }
	  da->size += bytes_to_pages (heap_inc);

	  /* Extend heap by same amount.  */
	  if ((err = heap_extend(da->base, heap_inc)) != NULL)
	    return err;

	  heap_extended = true;
	}
      else
	break;
    }

  return NULL;
}

_kernel_oserror *
dynamicarea_extend(dynamicarea *da, int by, void **block)
{
  bool heap_extended = false;
  _kernel_oserror *err = NULL;

  while (true)
    {
      /* If this fails, then assume heap is full and attempt to extend it
         (only for +ve extends though). The failure could be due to a
         corrupted heap or invalid heap pointer, but we can't distinguish
         here.  */
      err = heap_extend_block (da->base, block, by);

      /* If an error occurs, then block is not changed, ie it will not be
         NULL.  */
      if (err)
	{
	  if (heap_extended)
	    {
	      /* If we've already extended the heap and it still fails,
	         then don't try again, return the error.  */
	      if (!err)
		err = armeabisupport_no_memory;

	      return err;
	    }

	  /* Attempt to increase heap size and try again.
	     Extend by the size of the block as well as the extra in case
	     the block needs to be moved.  */
	  size_t heap_inc;

	  if ((err = heap_block_size (da->base, *block,
				      &heap_inc)) != NULL)
	    return err;

	  /* Add on the extra and round to page size.  */
	  heap_inc = page_align_size(heap_inc + by);

	  if ((err = dynamic_area_extend (da->number, heap_inc)) != NULL)
	    {
	      DEBUG_OUTPUT("%s:%d - RISC OS Error: %s", __func__, __LINE__, err->errmess);
	      return err;
	    }
	  da->size += bytes_to_pages (heap_inc);

	  /* Extend heap by same amount.  */
	  if ((err = heap_extend (da->base, heap_inc)) != NULL)
	    return err;

	  heap_extended = true;
	}
      else
	break;
    }

  return NULL;
}
