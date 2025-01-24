/* main.c
 *
 * Copyright 2019-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include <kernel.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "armeabisupport.h"
#include "types.h"
#include "swi.h"
#include "abort.h"
#include "stack.h"
#include "memory.h"
#include "mmap.h"
#include "shm.h"
#include "fdtable.h"
#include "array.h"
#include "debug.h"

#define ENABLE_TRACING 1
#include "trace.h"

#define ENABLE_LOGGING 1
#include "log.h"

armeabisupport_globals global;

_kernel_oserror *
app_new(app_object **app_ret, const char *name)
{
  TRACE("%s: name=%s", __func__, name);

  _kernel_oserror *err;
  unsigned app_base;

  err = get_app_base(&app_base);
  if (err)
    return err;

  app_object *app;

  err = rma_claim(sizeof(*app), (void **)&app);
  if (err)
    return err;

  memset(app, 0, sizeof(*app));
  app->ID = app_base;
  app->stack_abort_data.stack_list = &app->stack_list;
  app->stack_abort_data.stack_allocator = &global.stack_allocator;

  if (name && *app->name == '\0')
    {
      strncpy(app->name, name, APP_NAME_MAX_LEN);
      app->name[APP_NAME_MAX_LEN - 1] = '\0';
    }

  abort_register_internal(app, get_stack_abort_handler(), (unsigned)&app->stack_abort_data);
  abort_register_internal(app, get_mmap_abort_handler(), (unsigned)&global.mmap_allocator_pool);

  linklist_add_to_end(&global.app_list, &app->link);

  if (global.parent)
    {
      LOG("%s: Copying %d entries from fd_table of parent '%s'", __func__,
	  global.parent->fd_table.num_entries,
	  global.parent->name);

      fdtable_copy(app, global.parent);
      fdtable_close_on_exec(app);

#if ENABLE_LOGGING
      fdtable_log(app);
#endif

      global.parent = NULL;
    }
  else
    array_ctor(&app->fd_table, sizeof(fdtable_entry));

  *app_ret = app;

  return NULL;
}

_kernel_oserror *
app_find(app_object **app_ret)
{
  _kernel_oserror *err;
  unsigned app_base;

  err = get_app_base(&app_base);
  if (err)
    {
      *app_ret = NULL;
      return armeabisupport_bad_process;
    }

  app_object *app;
  for (app = linklist_first_app_object(&global.app_list);
       app != NULL && app->ID != app_base;
       app = linklist_next_app_object(app))
    /* Empty loop */;

  *app_ret = app;

  if (app == NULL)
    return armeabisupport_bad_process;

  return NULL;
}

static void
app_remove_by_handle(app_object *app)
{
  if (app)
    {
      abort_deregister(get_mmap_abort_handler(), (unsigned)&global.mmap_allocator_pool);
      abort_deregister(get_stack_abort_handler(), (unsigned)&app->stack_abort_data);
      fdtable_destroy(&app->fd_table);
      array_dtor(&app->fd_table);
      linklist_remove(&global.app_list, &app->link);
      rma_free(app);
    }
}

_kernel_oserror *
armeabi_cleanup(void)
{
  _kernel_oserror *err;
  app_object *app;

  abort_remove();

  if ((err = app_find(&app)) != NULL)
    return err;

  stack_object *stack = linklist_first_stack_object(&app->stack_list);
  while (stack)
    {
      stack_object *next = linklist_next_stack_object(stack);
      stack_free_internal(stack, app);
      stack = next;
    }

  allocator_destroy_all(&app->allocator_list);

  /* Remove all remaining mmap allocations for this app.  */
  mmap_cleanup_app(app);

  app_remove_by_handle(app);

  return NULL;
}

void report_swi_error(const char *func, int line, _kernel_oserror *err)
{
  char buffer[256];
  snprintf(buffer, sizeof(buffer) - 1, "%s:%d - %s", func, line, err->errmess);
  report_text(buffer);
}

static const char filename[] = "$.armeabisupport-debug-log";
static char buffer[512];

static void logwrite(const char *buffer)
{
  _kernel_oserror *err;
  unsigned handle;

  err = _swix(OS_Find, _INR(0,1)|_OUT(0),
		       0xC0 | (1 << 3), filename,
		       &handle);
  if (err || handle == 0)
  {
    err = _swix(OS_Find, _INR(0,1)|_OUT(0),
		       0x80 | (1 << 3), filename,
		       &handle);
  }
  if (!err && handle)
  {
    unsigned extent;
    _swix(OS_Args, _INR(0,1)|_OUT(2), 2, handle, &extent);
    _swix(OS_GBPB, _INR(0,4), 1, handle, buffer, strlen(buffer), extent);
    _swix(OS_Find, _INR(0,1), 0, handle);
  }
}

void log_printf(const char *format, ...)
{
  char *p = buffer;
  time_t t;
  struct tm *tmp;

  va_list ap;
  va_start (ap, format);

  t = time(NULL);
  tmp = localtime(&t);

  *p++ = '[';
  p += strftime(p, sizeof(buffer) - strlen(buffer) - 1, "%d/%m  %T", tmp);
  *p++ = ']';
  *p++ = ' ';
  *p++ = ':';
  *p++ = ' ';
  *p = '\0';

  p += vsnprintf(p, sizeof(buffer) - strlen(buffer) - 1, format, ap);
  *p++ = '\n';
  *p = '\0';

  logwrite(buffer);
}

void reporter_printf(const char *format, ...)
{
  char *p = buffer;
  time_t t;
  struct tm *tmp;

  app_object *app;
  if (app_find(&app) != NULL)
    return;

  va_list ap;
  va_start (ap, format);

  t = time(NULL);
  tmp = localtime(&t);

  *p++ = '[';
  p += snprintf(p, sizeof(buffer) - strlen(buffer) - 1, "%s", app->name);
  *p++ = ':';
  p += strftime(p, sizeof(buffer) - strlen(buffer) - 1, "%d/%m  %T", tmp);
  *p++ = ']';
  *p++ = ' ';
  *p++ = ':';
  *p++ = ' ';
  *p = '\0';

  p += vsnprintf(p, sizeof(buffer) - strlen(buffer) - 1, format, ap);
  *p++ = '\n';
  *p = '\0';

  report_text(buffer);
}
