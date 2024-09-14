/* filedesc.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "filedesc.h"
#include "array.h"
#include "shm.h"
#include "memory.h"
#include "main.h"
#include "swi.h"
#include "debug.h"

#define FDOP_VALIDATE_FD	0
#define FDOP_CLOSE_FD		1

#define TABLE_INIT_NUM_ENTRIES 16

#define TRACING 0
#define LOGGING 0

#if LOGGING
#define LOG(format,...) reporter_printf(format, ##__VA_ARGS__)
#else
#define LOG(format,...)
#endif

#undef TRACE
#if TRACING
#define TRACE(format,...) reporter_printf(format, ##__VA_ARGS__)
#else
#define TRACE(format,...)
#endif

/* r0 = reason code:
*	0 - validatefd
*	  Entry
*	    r1 = fd
*	  Exit
*	    r0 = error pointer (V set) if unknown fd, or 0 (V clear) if known
*	    r1 = type of fd, 0 if invalid
*	1 - close
*	  Entry
*	    r1 = fd
*	  Exit
*	    r0 = error pointer (V set) if unknown fd, or 0 (V clear) if known	
*/

_kernel_oserror *
fd_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  int reason = r->r[0];

  switch (reason)
  {
  case FDOP_VALIDATE_FD: {
    filedesc *fd = filedesc_find_fd(r->r[1]);
    if (fd)
      r->r[1] = fd->type;
    else {
      r->r[1] = fdtype_INVALID;
      err = armeabisupport_EBADF;
    }
    break;
  }
  case FDOP_CLOSE_FD:
    err = filedesc_close(r->r[1]);
    break;
  }

  return err;
}

void filedesc_initialise(armeabisupport_globals *globals)
{
  globals->next_fd = BASE_FD;
}

_kernel_oserror *filedesc_new_fd(filedesc **result)
{
  /* Check if we can reuse a previously discarded fd. */
  filedesc *fd;
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd && fd->type != fdtype_INVALID;
       fd = linklist_next_filedesc(fd))
    /* Empty loop */ ;

  app_object *app;
  app_find(&app);

  if (fd)
    {
      TRACE("%s: Reusing filedesc %p, fd=%d", __func__, fd, fd->value);
      fd->owner = app;
      *result = fd;
      return NULL;
    }

  _kernel_oserror *err;
  if ((err = rma_claim(sizeof(filedesc), (void **)&fd)) != NULL)
    goto error;

  linklist_add_to_end(&global.fd_list, &fd->link);

  fd->owner = app;
  fd->value = global.next_fd++;
  *result = fd;

  TRACE("%s: Created new filedesc %p, fd=%d", __func__, fd, fd->value);

  return NULL;

error:
  LOG("%s: %s", __func__, err->errmess);

  return err;
}

_kernel_oserror *filedesc_close(int required_fd)
{
  filedesc *fd = filedesc_find_fd(required_fd);

  TRACE("%s: Close filedesc %p, fd=%d", __func__, fd, required_fd);

  if (!fd || fd->type == fdtype_INVALID)
    return armeabisupport_EBADF;

  _kernel_oserror *err = NULL;
  if (fd->type == fdtype_SHM)
    {
      err = shm_close(fd->shm);
      fd->shm = NULL;
    }

  fd->type = fdtype_INVALID;
  fd->owner = NULL;

  /* Don't free the filedesc memory or destroy the fd value, we'll reuse it */

  return err;
}

filedesc *filedesc_find_fd(int required_fd)
{
  if (required_fd == -1)
    return NULL;

  filedesc *fd;
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd && (fd->type == fdtype_INVALID || fd->value != required_fd);
       fd = linklist_next_filedesc(fd))
    /* Empty loop */ ;

  return fd;
}

/* Remove any shared memory objects for the given app
 * and while we're at it, garbage collect any unused fds.  */
void filedesc_cleanup_app(app_object *app)
{
  filedesc *fd = linklist_first_filedesc(&global.fd_list);
  while (fd)
    {
      filedesc *next = linklist_next_filedesc(fd);

      if (fd->owner == app && fd->type == fdtype_SHM && fd->shm && fd->shm->valid == VALID_SHM)
	{
	  TRACE("filedesc_cleanup_app: filedesc = 0x%X", fd);
	  TRACE("  fd->shm = %p",fd->shm);
	  TRACE("  fd->value = %d",fd->value);
	  TRACE("  fd->shm->ref_count = %d",fd->shm->ref_count);
	  TRACE("  fd->shm->block = %p",page_to_addr(&fd->shm->allocator->base,fd->shm->block->start_page));
	  shm_deref_object(fd->shm);

	  linklist_remove(&global.fd_list, &fd->link);
	  rma_free(fd);
	}
      else if (fd->owner == NULL && fd->type == fdtype_INVALID)
	{
	  /* Garbage collect any unused fds.  */
	  TRACE("filedesc_cleanup_app: removing unused filedesc = 0x%X", fd);
	  TRACE("  fd->value = %d",fd->value);
	  linklist_remove(&global.fd_list, &fd->link);
	  rma_free(fd);
	}

      fd = next;
    }
}

void filedesc_dump_fds(void)
{
  filedesc *fd;
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd;
       fd = linklist_next_filedesc(fd))
    {
      printf("fd=%d, owner=0x%X\n", fd->value, fd->owner);
    }
}
