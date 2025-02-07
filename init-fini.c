/* init-fini.c
 *
 * Copyright 2019-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include <stdlib.h>
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "swi.h"
#include "abort.h"
#include "memory.h"
#include "shmtable.h"
#include "filetable.h"
#include "ipctable.h"
#include "opentable.h"
#include "mmap.h"

#define ENABLE_TRACING 0
#include "trace.h"

static const char filter_name[] = "ARMEABISupport";

static _kernel_oserror *install_pre_filter(void)
{
  if (global.pre_filter_installed)
    return NULL;

  _kernel_oserror *err;

  if ((err = filter_register_pre_filter(filter_name,
				   abort_get_pre_filter(),
				   &global,
				   0)) == NULL)
    global.pre_filter_installed = true;

  return err;
}

static _kernel_oserror *remove_pre_filter (void)
{
  if (!global.pre_filter_installed)
    return NULL;

  _kernel_oserror *err;

  if ((err = filter_deregister_pre_filter (filter_name,
					   abort_get_pre_filter(),
					   &global,
					   0)) == NULL)
    global.pre_filter_installed = false;

  return err;
}

static _kernel_oserror *install_post_filter (void)
{
  if (global.post_filter_installed)
    return NULL;

  _kernel_oserror *err;

  if ((err = filter_register_post_filter (filter_name,
					  abort_get_post_filter(),
					  &global, 0)) == NULL)
    global.post_filter_installed = true;

  return err;
}

static _kernel_oserror *remove_post_filter (void)
{
  if (!global.post_filter_installed)
    return NULL;

  _kernel_oserror *err;

  if ((err = filter_deregister_post_filter (filter_name,
					    abort_get_post_filter(),
					    &global, 0)) == NULL)
    global.post_filter_installed = false;

  return err;
}

_kernel_oserror *module_initialise (const char *tail, int podule_base, void *pw)
{
  _kernel_oserror *err;

  memset (&global, 0, sizeof (armeabisupport_globals));

  static const int systemtable_entry_size[fd_type_MAX] = {
    sizeof(systemshmtable_entry),
    sizeof(systemfiletable_entry),
    sizeof(systemipctable_entry)
  };

  for (int i = 0; i < fd_type_MAX; i++)
    array_ctor(&global.system_table[i], systemtable_entry_size[i]);

  array_ctor(&global.open_table, sizeof(opentable_entry));

  // linklist_init_list (&global.app_list);
  // global.flags.no_client_check = false;

  if ((err = install_pre_filter()) != NULL
   || (err = install_post_filter()) != NULL)
    goto error;

  get_os_permissions(OS_PERMISSION_FLAG_USER_READ,
		     OS_PERMISSION_FLAG_USER_READ | OS_PERMISSION_FLAG_USER_WRITE | OS_PERMISSION_FLAG_USER_EXEC,
		     &global.os_memory_prot_read);
  get_os_permissions(OS_PERMISSION_FLAG_USER_READ | OS_PERMISSION_FLAG_USER_WRITE,
		     OS_PERMISSION_FLAG_USER_READ | OS_PERMISSION_FLAG_USER_WRITE | OS_PERMISSION_FLAG_USER_EXEC,
		     &global.os_memory_prot_read_write);
  get_os_permissions(OS_PERMISSION_FLAG_USER_EXEC,
		     OS_PERMISSION_FLAG_USER_READ | OS_PERMISSION_FLAG_USER_WRITE | OS_PERMISSION_FLAG_USER_EXEC,
		     &global.os_memory_prot_exec);
  get_os_permissions(OS_PERMISSION_FLAG_PRIV_READ,
		     OS_PERMISSION_FLAG_PRIV_READ | OS_PERMISSION_FLAG_PRIV_WRITE | OS_PERMISSION_FLAG_PRIV_EXEC |
		     OS_PERMISSION_FLAG_USER_READ | OS_PERMISSION_FLAG_USER_WRITE | OS_PERMISSION_FLAG_USER_EXEC,
		     &global.os_memory_prot_none);

  dynamicarea_block test_da;
  dynamic_area_create("abort-test", 4096, ALLOCATOR_TYPE_PAGE, &test_da);

  _kernel_swi_regs regs;
  regs.r[0] = 24;		// Check memory access
  regs.r[1] = (int)test_da.base;
  regs.r[2] = (int)test_da.base + test_da.size;
  _kernel_swi(XOS_Bit | OS_Memory, &regs, &regs);
  global.aborttrap_enabled = ((regs.r[1] & DYNAMICAREA_ACCESS_COMPLETELY_ABORTABLE) ==
				    DYNAMICAREA_ACCESS_COMPLETELY_ABORTABLE);
  dynamic_area_remove(test_da.number);

  TRACE("Dynamic areas support aborttrap = %d", global.aborttrap_enabled);

  return NULL;

error:
  remove_pre_filter();
  remove_post_filter();

  return err;
}

_kernel_oserror *module_finalisation (int fatal, int podule_base, void *pw)
{
#if !DEBUG_ALLOCATOR
  if (global.app_list.count > 0)
    return armeabisupport_in_use;
#endif

  // Remove the stack allocator.
  allocator_destroy_internal(global.stack_allocator);

  // Remove all mmap allocations and allocators.
  allocator_destroy_all(&global.mmap_allocator_pool);

  for (int i = 0; i < global.open_table.num_entries; i++)
    if (array_entry_is_in_use(&global.open_table.entries[i]))
      {
	opentable_entry *ot_entry = opentable_get_entry(i);
	if (ot_entry->type == opentable_type_FILE)
	  filelocklist_dtor(&ot_entry->file.locks);
      }

  for (int i = 0; i < fd_type_MAX; i++)
    array_dtor(&global.system_table[i]);

  array_dtor(&global.open_table);

  remove_pre_filter();
  remove_post_filter();

  return NULL;
}
