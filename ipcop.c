/* ipcop.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "ipcop.h"
#include "ipctable.h"
#include "fdtable.h"
#include "table_entry_base.h"
#include "main.h"
#include <string.h>

#define ENABLE_LOGGING 0
#include "log.h"

#define ENABLE_TRACING 0
#include "trace.h"

#define IPCOP_OPEN		0
#define IPCOP_WRITE		1
#define IPCOP_WRITE_FDS		2
#define IPCOP_READ		3
#define IPCOP_READ_FDS		4
#define IPCOP_READ_RAW		5
#define IPCOP_STATUS		6

#define FD_MARKER		0x7e57ed

#if ENABLE_LOGGING
static const char *fd_type_str[] =
{
  "SHM",
  "FILE",
  "IPC"
};
#endif

static _kernel_oserror *ipcop_write(int fd, void *buffer, size_t len);
static _kernel_oserror *ipcop_read(int fd, void *buffer, size_t len);
static _kernel_oserror *ipcop_read_fds(int fd, int *fds);
static _kernel_oserror *ipcop_read_raw(int fd, size_t len, void **addr_ret);
static _kernel_oserror *ipcop_write_fds(int fd, int *fds, size_t count);
static _kernel_oserror *ipcop_status(int fd, unsigned *flags);

/* r0 = reason code:
 *	0 - IPCOP_OPEN
 *	  Entry
 *	    r1 = size
 *	    r2 = 2x32bit word buffer to receive resulting FDs
 *	1 - IPCOP_WRITE
 *	  Entry
 *	    r1 = ipc fd
 *	    r2 = buffer
 *	    r3 = length
 *	2 - IPCOP_WRITE_FDS
 *	  Entry
 *	    r1 = ipc fd
 *	    r2 = ptr to array of 32bit FDs
 *	    r3 = number of FDs
 *	3 - IPCOP_READ
 *	  Entry
 *	    r1 = ipc fd
 *	    r2 = buffer
 *	    r3 = length
 *	4 - IPCOP_READ_FDS
 *	  Entry
 *	    r1 = ipc fd
 *	    r2 = array of 32bit FDs
 *
 *	On reading the FD array, the FDs are duplicated in the reading process, but can
 *	have a different FD handle to the original whilst refering to the same object.
 *
 *	5 - IPCOP_READ_RAW
 *	  Entry
 *	    r1 = ipc fd
 *	    r2 = length
 *	  Exit
 *	    r0 = address of raw data in IPC buffer
 *	6 - IPCOP_STATUS
 *	  Entry
 *	    r1 = ipc fd
 *	  Exit
 *	    r0 = flags
 *	      bit 0 set = ready to read
*/

_kernel_oserror *
ipc_op(_kernel_swi_regs *r)
{
  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  int reason = r->r[0];

  switch (reason)
  {
  case IPCOP_OPEN:
    err = fdtable_open_ipc(r->r[1], (int *)r->r[2]);
    break;
  case IPCOP_WRITE:
    err = ipcop_write(r->r[1], (void *)r->r[2], (size_t)r->r[3]);
    break;
  case IPCOP_WRITE_FDS:
    err = ipcop_write_fds(r->r[1], (int *)r->r[2], (size_t)r->r[3]);
    break;
  case IPCOP_READ:
    err = ipcop_read(r->r[1], (void *)r->r[2], (size_t)r->r[3]);
    break;
  case IPCOP_READ_FDS:
    err = ipcop_read_fds(r->r[1], (int *)r->r[2]);
    break;
  case IPCOP_READ_RAW:
    err = ipcop_read_raw(r->r[1], (size_t)r->r[2], (void **)&r->r[0]);
    break;
  case IPCOP_STATUS:
    err = ipcop_status(r->r[1], (unsigned *)&r->r[0]);
    break;
  }

  return err;
}

static _kernel_oserror *ipcop_write(int fd, void *buffer, size_t len)
{
  TRACE("%s: fd=%d, buffer=%p, len=%ld", __func__, fd, buffer, len);

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL)
    {
      LOG("%s: fdtable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openipctable_entry *openipc = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC], fd_entry->opentable_idx);
  if (!openipc)
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  ipc_ensure_write(openipc, len);

  LOG("%s:   fd=%d, write_buffer=%X, write_pos=%d", __func__, fd, openipc->write_buffer, openipc->write_pos);

  memcpy(openipc->write_buffer + openipc->write_pos, buffer, len);

  ipc_inc_and_align_write64(openipc, len);

  return NULL;
}

static _kernel_oserror *ipcop_write_fds(int fd, int *fds, size_t count)
{
  TRACE("%s: fd=%d, fds=%p, count=%ld", __func__, fd, fds, count);

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL)
    {
      LOG("%s: fdtable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openipctable_entry *openipc = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC],
									  fd_entry->opentable_idx);
  if (!openipc)
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  ipc_ensure_write(openipc, 3 * 4);

  LOG("%s: FD %d - write pos before write_fds = %d", __func__, fd, openipc->write_pos);

  ipc_write_uint(openipc, FD_MARKER);
#if ENABLE_LOGGING
  ipc_write_uint(openipc, (unsigned)app);
#endif
  ipc_write_int(openipc, count);

  /* (Number of FDS * (size of 1 FD type (32bit int) and 1 open table index (32bit int)).  */
  ipc_ensure_write(openipc, count * 2 * sizeof(int));

  /* We can't send the FDs themselves as they may no longer exist by the time
   * the receiver gets them, so send the type and table index instead.  */
  for (int i = 0; i < count; i++)
    {
      fdtable_entry *list_fd = get_fdtable_entry(app, fds[i]);
      fd_type type;
      int opentable_index;

      if (!list_fd)
	{
	  type = fd_type_INVALID;
	  opentable_index = ARRAY_INVALID_INDEX;
	}
      else
	{
	  type = list_fd->type;
	  opentable_index = list_fd->opentable_idx;

	  table_entry_base *open_table;
	  open_table = (table_entry_base *)array_get_entry_ptr(&global.open_table[type],
							       opentable_index);
	  open_table->ref_count++;
	  LOG("%s: FD %d - ref count now %d for open table index %d",
	      __func__, fds[i], open_table->ref_count, opentable_index);
	}

      ipc_write_int(openipc, type);
      ipc_write_int(openipc, opentable_index);
    }

  ipc_ensure_aligned_write64(openipc);

  return NULL;
}

static _kernel_oserror *ipcop_read(int fd, void *buffer, size_t len)
{
  TRACE("%s: fd=%d, buffer=%p, len=%ld", __func__, fd, buffer, len);

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL)
    {
      LOG("%s: fd_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openipctable_entry *openipc = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC], fd_entry->opentable_idx);
  if (!openipc)
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(openipc, len);

  LOG("%s:   fd=%d, read_buffer=%X, read_pos=%d", __func__, fd, openipc->read_buffer, openipc->read_pos);

  memcpy(buffer, openipc->read_buffer + openipc->read_pos, len);

  ipc_inc_and_align_read64(openipc, len);

  return NULL;
}

static _kernel_oserror *ipcop_read_raw(int fd, size_t len, void **addr_ret)
{
  TRACE("%s: fd=%d, len=%ld", __func__, fd, len);

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL)
    {
      LOG("%s: fdtable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openipctable_entry *openipc = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC], fd_entry->opentable_idx);
  if (!openipc)
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(openipc, len);

  LOG("%s:   fd=%d, read_buffer=%X, read_pos=%d", __func__, fd, openipc->read_buffer, openipc->read_pos);

  *addr_ret = openipc->read_buffer + openipc->read_pos;

  ipc_inc_and_align_read64(openipc, len);

  return NULL;
}

static _kernel_oserror *ipcop_read_fds(int fd, int *fds)
{
  TRACE("%s: fd=%d, fds=%p", __func__, fd, fds);

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *receiver_fd_entry;
  if ((receiver_fd_entry = get_fdtable_entry(app, fd)) == NULL)
    {
      LOG("%s: fdtable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  openipctable_entry *openipc = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC], receiver_fd_entry->opentable_idx);
  if (!openipc)
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(openipc, 3 * 4);

  LOG("%s: FD %d - read pos before read_fds = %d", __func__, fd, openipc->read_pos);

  if (ipc_read_uint(openipc) != FD_MARKER)
    {
      LOG("%s: Bad read_pos marker: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }

#if ENABLE_LOGGING
  app_object *sender_app = (app_object *)ipc_read_uint(openipc);
  if (!sender_app)
    {
      LOG("%s: Bad sender_app: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }
#endif

  int count = ipc_read_int(openipc);
  LOG("%s: fd count=%d", __func__, count);
  if (count < 0)
    {
      LOG("%s: -ve FD count: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }

  /* (Number of FDS * size of 1 FD type (32bit int) and 1 open table index (32bit int))  */
  ipc_ensure_read(openipc, count * 2 * sizeof(int));

  for (int i = 0; i < count; i++)
    {
      fd_type type = ipc_read_int(openipc);
      int opentable_index = ipc_read_int(openipc);

      LOG("%s: fd_type=%d, opentable_index=%d", __func__, type, opentable_index);

      if (type <= fd_type_INVALID ||
	  type >= fd_type_MAX ||
	  opentable_index < 0 ||
	  opentable_index >= global.open_table[type].num_entries)
	goto bad_fd;

      table_entry_base *open_table = (table_entry_base *)array_get_entry_ptr(&global.open_table[type],
									     opentable_index);
      /* It should not be possible for open_table to be NULL given the checks above, but we'll check
       * just to be certain.  */
      if (!open_table || !array_entry_is_in_use(&open_table->array_base))
	goto bad_fd;
      open_table->ref_count++;

      if ((err = fdtable_alloc_entry(app,
				     type,
				     opentable_index,
				     /*close_on_exec=*/false,
				     fds)) != NULL)
	goto bad_fd;

      LOG("%s: open table idx %d of type '%s' duplicated in receiver as fd %d, ref count now %d",
	  __func__, opentable_index, fd_type_str[type], *fds, open_table->ref_count);

      fds++;

      continue;
bad_fd:
      LOG("%s: Failed to duplicate fd, open table idx %d, type %d",
	  __func__, opentable_index, type);
      *fds++ = -1;
    }

  ipc_ensure_aligned_read64(openipc);

#if ENABLE_LOGGING
  LOG("%s: SENDER '%s' FD TABLE:", __func__, sender_app->name);
  fdtable_print(sender_app);
  LOG("%s: RECEIVER '%s' FD TABLE:", __func__, app->name);
  fdtable_print(app);
#endif

  return NULL;
}

static _kernel_oserror *ipcop_status(int fd, unsigned *flags_ret)
{
//  TRACE("%s: fd=%d", __func__, fd);

  *flags_ret = 0;

  _kernel_oserror *err;
  app_object *app;
  if ((err = app_find(&app)) != NULL)
    return err;

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL)
    return armeabisupport_EBADF;

  openipctable_entry *openipc1 = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC],
									   fd_entry->opentable_idx);
  if (!openipc1)
    return armeabisupport_EBADF;

  openipctable_entry *openipc2 = (openipctable_entry *)array_get_entry_ptr(&global.open_table[fd_type_IPC],
									   openipc1->openipctable_oppend_idx);
  if (!openipc2)
    return armeabisupport_EBADF;

  *flags_ret = openipc1->read_pos < openipc2->write_pos;

  if (*flags_ret)
    {
      LOG("%s: fd=%d, flags=%d, read_pos1=%d, write_pos2=%d", __func__,
	  fd, *flags_ret, openipc1->read_pos, openipc2->write_pos);
    }

//  fdtable_print(app);

  return NULL;
}

