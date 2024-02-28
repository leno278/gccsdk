/* shm.c
 *
 * Copyright 2021-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "shm.h"
#include "filedesc.h"
#include "memory.h"
#include "swi.h"
#include "link_list.h"
#include "types.h"
#include "debug.h"
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>

#define SHMOP_OPEN	0
#define SHMOP_UNLINK	1
#define SHMOP_CLOSE	2	// No longer used, as we use close() (via filedesc) instead
#define SHMOP_DUP	3

#define LOGGING 0
#define TRACING 0
#define TRACE_SWIS 0

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

#if TRACE_SWIS
#define TRACE_SWI(format,...) reporter_printf(format, ##__VA_ARGS__)
#else
#define TRACE_SWI(format,...)
#endif

/* r0 = reason code:
*	0 - shm_open
*	  Entry:
*	    r1 = name (beginning with a '/' and upto 256 chars)
*	    r2 = flags
*	    r3 = mode
*	  Exit:
*	    r0 = handle (will not be lowest fd in current process) if success (V flag
*			 clear) or error pointer (V flag set).
*	1 - shm_unlink
*	  Entry:
*	    r1 = name
*	  Exit:
*	    r0 = 0 for success (V flag clear) or error pointer (V flag set).
*	2 - close
*	  Entry:
*	    r1 = fd
*	  Exit:
*	    r0 = 0 for success (V flag clear) or error pointer (V flag set).
*	3 - dup
*	  Entry:
*	    r1 = fd
*	  Exit:
*	    r0 = new handle (will not be the lowest fd in current process) if success (V flag
*			     clear) or error pointer (V flag set).
*/

static _kernel_oserror *shm_open(const char *name, int flags, int mode, filedesc **fd);
static _kernel_oserror *shm_unlink(const char *name);
static _kernel_oserror *shm_dup(int fd, filedesc **);

_kernel_oserror *
shm_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err = NULL;
  int reason = r->r[0];

  switch (reason)
  {
  case SHMOP_OPEN: {
    filedesc *fd;
    TRACE_SWI("SWI \"SHMOp\", SHMOP_OPEN, name=\"%s\", oflag=0x%X, mode=0x%X", r->r[1], r->r[2], r->r[3]);

    err = shm_open((const char *)r->r[1], r->r[2], r->r[3], &fd);
    if (!err && fd)
      {
	r->r[0] = fd->value;
	TRACE_SWI("  Result = %d", r->r[0]);
      }
    break;
  }
  case SHMOP_UNLINK:
    TRACE_SWI("SWI \"SHMOp\", SHMOP_UNLINK, name=\"%s\"", r->r[1]);
    err = shm_unlink((const char *)r->r[1]);
    break;
  case SHMOP_CLOSE:
    TRACE_SWI("SWI \"SHMOp\", SHMOP_CLOSE, fd=%d", r->r[1]);
/*    err = shm_close((shm_object *)r->r[1]);*/
    break;
  case SHMOP_DUP:
    TRACE_SWI("SWI \"SHMOp\", SHMOP_DUP, fd=%d", r->r[1]);
    filedesc *fd;
    err = shm_dup(r->r[1], &fd);
    if (!err)
      {
	r->r[0] = fd->value;
	TRACE_SWI("  Result = %d", r->r[0]);
      }
    break;
  }

  if (err)
    {
      TRACE_SWI("  Error = %s", err->errmess);
    }

  return err;
}

void
shm_dump_object_list(void)
{
  filedesc *fd;
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd;
       fd = linklist_next_filedesc(fd))
    {
      if (fd->type == fdtype_SHM)
	{
	  shm_object *shm = fd->shm;
	  printf("  %p \'%s\', linked=%d, fd=%d, ref=%d",
		 shm, shm->name, shm->linked, fd->value, shm->ref_count);
	  if (shm->block)
	    printf(", addr=%p, size=0x%lX\n",
		   page_to_addr(&shm->allocator->base, shm->block->start_page),
		   pages_to_bytes(shm->block->end_page - shm->block->start_page));
	  else
	    printf(" unallocated\n");
	}
    }
}

static void
report_shm_object_list(void)
{
  filedesc *fd;
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd;
       fd = linklist_next_filedesc(fd))
    {
      if (fd->type == fdtype_SHM)
	{
	  shm_object *shm = fd->shm;
	  LOG("  %p \'%s\', linked=%d, ref=%d", shm, shm->name, shm->linked, shm->ref_count);
	}
    }
}

static filedesc *
find_shm_object(const char *name)
{
  filedesc *fd;
  size_t name_len = strlen(name);
  for (fd = linklist_first_filedesc(&global.fd_list);
       fd;
       fd = linklist_next_filedesc(fd))
    {
      if (fd->type != fdtype_SHM)
	continue;
      if (fd->shm->linked && strncmp(name, fd->shm->name, name_len) == 0)
	break;
    }

  return fd;
}

static _kernel_oserror *
create_shm_object(const char *name, shm_object **result)
{
  _kernel_oserror *err;
  shm_object *shm = NULL;

  if ((err = rma_claim(sizeof(shm_object), (void **)&shm)) != NULL)
    goto error;
  memset(shm, 0, sizeof(*shm));

  if ((err = rma_claim(strlen(name) + 1, (void **)&shm->name)) != NULL)
    goto error;

  strcpy(shm->name, name);
  shm->valid = VALID_SHM;
  shm->ref_count = 1;
  shm->linked = true;

  *result = shm;

  return NULL;

error:
  if (shm)
    {
      if (shm->name)
	rma_free(shm->name);
      rma_free(shm);
    }

  LOG("create_shm_object: %s", err->errmess);

  return err;
}

static _kernel_oserror *
shm_open(const char *name, int oflag, int mode, filedesc **result)
{
  _kernel_oserror *err;
  filedesc *fd = NULL;

  if (strlen(name) > PATH_MAX)
    {
      LOG("shm_open: \"%s\" name too long", name);
      return armeabisupport_ENAMETOOLONG;
    }

  fd = find_shm_object(name);

  /* Fail if O_CREAT is not set and the named shared memory object does not exist.  */
  if (!fd && !(oflag & O_CREAT))
    {
      LOG("shm_open: \"%s\" does not exist and O_CREATE not set", name);
      return armeabisupport_ENOENT;
    }

  /* Fail if O_CREAT and O_EXCL are set and the named shared memory object already exists.  */
  if (fd && ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)))
    {
      LOG("shm_open: \"%s\" already exists, but O_CREAT and O_EXCL are set", name);
      return armeabisupport_EEXIST;
    }

  filedesc *new_fd;
  if ((err = filedesc_new_fd(&new_fd)) != NULL)
    return err;

  shm_object *shm;
  if (fd)
    {
      TRACE_SHM("Found existing shared memory object \"%s\", addr=0x%X, len=%d, existing fd=%d, new fd=%d",
		name,
		page_to_addr(&fd->shm->allocator->base, fd->shm->block->start_page),
		pages_to_bytes(fd->shm->block->end_page - fd->shm->block->start_page),
		fd->value,
		new_fd->value);
      shm = fd->shm;
      shm->ref_count++;
    }
  else
    {
      TRACE_SHM("Creating shared memory object \"%s\", new fd=%d, not yet bound to any address",
		name,
		new_fd->value);
      err = create_shm_object(name, &shm);
      if (err)
	{
	  LOG("shm_open: Error creating shared memory \"%s\"; %s", name, err->errmess);
	  goto error;
	}
    }

  new_fd->shm = shm;
  new_fd->type = fdtype_SHM;
  *result = new_fd;

  return NULL;

error:
  *result = NULL;  return err;
}

static _kernel_oserror *
shm_unlink(const char *name)
{
  filedesc *fd;

  if (strlen(name) > PATH_MAX)
    return armeabisupport_ENAMETOOLONG;

  fd = find_shm_object(name);
  if (!fd || fd->shm->valid != VALID_SHM)
    {
      LOG("shm_unlink: Shared memory object \"%s\" not found", name);
      return armeabisupport_ENOENT;
    }

  fd->shm->linked = false;
/*  if (fd->name)
    {
      rma_free(fd->name);
      fd->name = NULL;
    }*/

  return NULL;
}

_kernel_oserror *
shm_close(shm_object *shm)
{
  if (!shm || shm->valid != VALID_SHM)
    return armeabisupport_EINVAL;

  TRACE_SHM("shm_close(0x%X) name=\"%s\"", shm, shm->name);

  shm_deref_object(shm);

  return NULL;
}

/* Return true if the object was deleted.  */
bool shm_deref_object(shm_object *shm)
{
  if (!shm || shm->valid != VALID_SHM)
    return false;

  if (--shm->ref_count == 0)
    {
      TRACE("shm_deref_object: deleting shared memory object 0x%X, \"%s\", unmapping memory 0x%X",
	    shm, shm->name, page_to_addr(&shm->allocator->base, shm->block->start_page));

      allocator_munmap_block(shm->allocator, shm->block);

      if (shm->name)
	rma_free(shm->name);

      shm->valid = 0;
      rma_free(shm);

      return true;
    }

  TRACE("shm_deref_object: shared memory object 0x%X, new ref=%d, addr=0x%X, \"%s\"",
	shm, shm->ref_count, page_to_addr(&shm->allocator->base, shm->block->start_page), shm->name);

  return false;
}

/* Remove any shared memory objects for the given app.
 * There can be multiple file descriptors pointing to the same shared memory,
 * so we can't just delete it outright, we have to dereference it until there
 * is only one left at which point it will be deleted.
 */
void shm_cleanup_app(app_object *app, filedesc *fd)
{
  TRACE_SHM("shm_cleanup_app: fd = 0x%X",fd);
  TRACE_SHM("  fd->shm = 0x%X",fd->shm);
  TRACE_SHM("  fd->shm->fd = %d",fd->value);
  TRACE_SHM("  fd->shm->ref_count = %d",fd->shm->ref_count);
  TRACE_SHM("  fd->shm->block = 0x%X",page_to_addr(&fd->shm->allocator->base,fd->shm->block->start_page));
  if (fd && fd->shm && fd->shm->valid == VALID_SHM/* && fd->shm->block->owner == app*/)
    shm_deref_object(fd->shm);
}


_kernel_oserror *
shm_dup(int fd_to_dup, filedesc **result)
{
  _kernel_oserror *err;
  filedesc *fd = filedesc_find_fd(fd_to_dup);
  if (!fd || fd->type != fdtype_SHM || fd->shm->valid != VALID_SHM)
    return armeabisupport_EBADF;

  filedesc *new_fd;
  if ((err = filedesc_new_fd(&new_fd)) != NULL)
    {
      LOG("shm_dup: filedesc_new_fd returned error: %s", err->errmess);
      return err;
    }

  shm_object *shm = fd->shm;
  shm->ref_count++;

  new_fd->shm = shm;
  new_fd->type = fdtype_SHM;
  *result = new_fd;

  TRACE_SHM("shm_dup: fd %d duplicated as fd %d, shm = 0x%X, addr=0x%X, len=%d, ref = %d",
	    fd_to_dup,
	    new_fd->value,
	    shm,
	    page_to_addr(&fd->shm->allocator->base, fd->shm->block->start_page),
		pages_to_bytes(shm->block->end_page - shm->block->start_page),
	    shm->ref_count);

  return NULL;
}
