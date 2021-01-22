/* command.c
 *
 * Copyright 2018 GCCSDK Developers
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
#include "debug.h"

static void
command_info(void)
{
  app_object *app;
  armeabisupport_allocator *allocator;

  for (app = linklist_first_app_object (&global.app_list);
       app;
       app = linklist_next_app_object (app))
    {
      printf("App %X\n", app->ID);

      for (allocator = linklist_first_armeabisupport_allocator(&app->allocator_list);
	   allocator;
	   allocator = linklist_next_armeabisupport_allocator(allocator))
	{
#if DEBUG_ALLOCATOR
	  dump_memblock_list(allocator);
#else
	  printf("  Allocator: '%s'\n", allocator->name);
#endif
	}
      stack_object *stack;
      for (stack = linklist_first_stack_object (&app->stack_list);
	   stack;
	   stack = linklist_next_stack_object (stack))
	{
	  printf("  Stack: %.32s %p -> %p\n", stack->name, stack->base, stack->top);
	}
    }

  printf("Global allocators:\n");
  for (allocator = linklist_first_armeabisupport_allocator(&global.allocator_list);
       allocator;
       allocator = linklist_next_armeabisupport_allocator(allocator))
    {
      printf("  Allocator: '%s'\n", allocator->name);
      printf("  Type: ");
      switch (allocator->flags & ALLOCATOR_TYPE_MASK) {
      case ALLOCATOR_TYPE_PAGE:
	printf("paged\n");
	break;
      case ALLOCATOR_TYPE_HEAP:
	printf("heap\n");
	break;
      case ALLOCATOR_TYPE_SINGLE_USE:
	printf("single use\n");
	break;
      case ALLOCATOR_TYPE_MMAP:
	printf("mmap\n\n");
	dump_freeblock_list(allocator);
	break;
      }
    }
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
    }

  return err;
}
