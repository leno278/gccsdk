/* command.c
 *
 * Copyright 2018-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include "armeabisupport.h"
#include "stack.h"
#include "types.h"
#include "memory.h"
#include "fdtable.h"
#include "filetable.h"
#include "shmtable.h"
#include "ipctable.h"
#include "lock.h"
#include "debug.h"

static void
command_info(void)
{
  app_object *app;
  for (app = linklist_first_app_object (&global.app_list);
       app;
       app = linklist_next_app_object (app))
    {
      printf("App (%s) %X\n", app->name, app->ID);

      fdtable_print(app);

      allocator_dump_all(&app->allocator_list);

      stack_object *stack;
      for (stack = linklist_first_stack_object (&app->stack_list);
	   stack;
	   stack = linklist_next_stack_object (stack))
	{
	  printf("  Stack: %.32s %p -> %p\n", stack->name, stack->base, stack->top);
	}
    }

  printf("Global allocators:\n");
  allocator_dump_all(&global.allocator_list);

  printf("mmaps:\n");
  allocator_dump_all(&global.mmap_allocator_pool);

  printf("FILE TABLES:\n");
  openfiletable_print();
  systemfiletable_print();

  printf("SHM TABLES:\n");
  openshmtable_print();
  systemshmtable_print();

  printf("IPC TABLES:\n");
  openipctable_print();
  systemipctable_print();
}

_kernel_oserror *
module_command (const char *arg_string, int argc, int number, void *pw)
{
  _kernel_oserror *err = NULL;

  switch (number)
    {
    case CMD_ARMEABISupport_Info:
      command_info ();
      break;
    case CMD_ARMEABISupport_lslocks:
      filelocklist_lslocks ();
      break;
    }

  return err;
}
