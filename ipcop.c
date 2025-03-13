/* ipcop.c
 *
 * Copyright 2024, 2025 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "ipcop.h"
#include "ipctable.h"
#include "fdtable.h"
#include "opentable.h"
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

typedef enum ipc_status
{
  ipc_status_READ_READY = 1

} ipc_status;

#if ENABLE_LOGGING
static const char *opentable_type_str[] =
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
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&fd_entry->base))
    {
      LOG("%s: fdtable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry = opentable_get_entry(fd_entry->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    {
      LOG("%s: opentable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  if (ot_entry->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  ipc_ensure_write(ot_entry, len);

  LOG("%s:   fd=%d, write_buffer=%X, write_pos=%d", __func__, fd, ot_entry->ipc.write_buffer, ot_entry->ipc.write_pos);

  memcpy(ot_entry->ipc.write_buffer + ot_entry->ipc.write_pos, buffer, len);

  ipc_inc_and_align_write64(ot_entry, len);

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
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&fd_entry->base))
    {
      LOG("%s: fdtable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry = opentable_get_entry(fd_entry->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    {
      LOG("%s: opentable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  if (ot_entry->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  ipc_ensure_write(ot_entry, 3 * 4);

  LOG("%s: FD %d - write pos before write_fds = %d", __func__, fd, ot_entry->ipc.write_pos);

  ipc_write_uint(ot_entry, FD_MARKER);
#if ENABLE_LOGGING
  ipc_write_uint(ot_entry, (unsigned)app);
#endif
  ipc_write_int(ot_entry, count);

  /* (Number of FDS * (size of 1 open table index (32bit int)).  */
  ipc_ensure_write(ot_entry, count * 1 * sizeof(int));

  /* We can't send the FDs themselves as they may no longer exist by the time
   * the receiver gets them, so send the open table index instead.  */
  for (int i = 0; i < count; i++)
    {
      fdtable_entry *list_fd = get_fdtable_entry(app, fds[i]);
      int opentable_index;

      if (!list_fd)
	{
	  opentable_index = ARRAY_INVALID_INDEX;
	}
      else
	{
	  opentable_index = list_fd->opentable_idx;

	  opentable_entry *open_table;
	  open_table = opentable_get_entry(opentable_index);
	  open_table->ref_count++;
	  LOG("%s: FD[%d]=%d - ref count now %d for open table index %d (+++)",
	      __func__, i, fds[i], open_table->ref_count, opentable_index);
	}

      ipc_write_int(ot_entry, opentable_index);
    }

#if ENABLE_LOGGING
  opentable_log();
#endif

  ipc_ensure_aligned_write64(ot_entry);

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
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&fd_entry->base))
    {
      LOG("%s: fd_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry = opentable_get_entry(fd_entry->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    {
      LOG("%s: openipctable_entry not found: armeabisupport_EBADF", __func__);
      return armeabisupport_EBADF;
    }

  if (ot_entry->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(ot_entry, len);

  LOG("%s: fd=%d, read_buffer=%X, read_pos=%d", __func__, fd, ot_entry->ipc.read_buffer, ot_entry->ipc.read_pos);

  memcpy(buffer, ot_entry->ipc.read_buffer + ot_entry->ipc.read_pos, len);

  ipc_inc_and_align_read64(ot_entry, len);

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
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&fd_entry->base))
    {
      LOG("%s: fdtable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry = opentable_get_entry(fd_entry->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    {
      LOG("%s: openipctable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  if (ot_entry->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(ot_entry, len);

  LOG("%s: fd=%d, read_buffer=%X, read_pos=%d", __func__, fd, ot_entry->ipc.read_buffer, ot_entry->ipc.read_pos);

  *addr_ret = ot_entry->ipc.read_buffer + ot_entry->ipc.read_pos;

  ipc_inc_and_align_read64(ot_entry, len);

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
  if ((receiver_fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&receiver_fd_entry->base))
    {
      LOG("%s: fdtable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry = opentable_get_entry(receiver_fd_entry->opentable_idx);
  if (!ot_entry || !array_entry_is_in_use(&ot_entry->array_base))
    {
      LOG("%s: openipctable_entry not found for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  if (ot_entry->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  ipc_ensure_read(ot_entry, 3 * 4);

  LOG("%s: FD %d - read pos before read_fds = %d", __func__, fd, ot_entry->ipc.read_pos);

  if (ipc_read_uint(ot_entry) != FD_MARKER)
    {
      LOG("%s: Bad read_pos marker: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }

#if ENABLE_LOGGING
  app_object *sender_app = (app_object *)ipc_read_uint(ot_entry);
  if (!sender_app)
    {
      LOG("%s: Bad sender_app: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }
#endif

  int count = ipc_read_int(ot_entry);
  LOG("%s: fd count=%d", __func__, count);
  if (count < 0)
    {
      LOG("%s: -ve FD count: armeabisupport_EINVAL", __func__);
      return armeabisupport_EINVAL;
    }

  /* (Number of FDS * size of 1 open table index (32bit int))  */
  ipc_ensure_read(ot_entry, count * 1 * sizeof(int));

  for (int i = 0; i < count; i++)
    {
      int opentable_index = ipc_read_int(ot_entry);
      if (opentable_index < 0 ||
	  opentable_index >= global.open_table.num_entries)
	goto bad_fd;

      opentable_entry *ot_received_entry = opentable_get_entry(opentable_index);
      if (!ot_received_entry || !array_entry_is_in_use(&ot_received_entry->array_base))
	goto bad_fd;

      if (ot_received_entry->type <= opentable_type_INVALID ||
	  ot_received_entry->type >= opentable_type_MAX)
	goto bad_fd;

      LOG("%s: opentable type=%d, open table index %d", __func__, ot_received_entry->type, opentable_index);

      if ((err = fdtable_alloc_entry(app,
				     opentable_index,
				     /*close_on_exec=*/false,
				     fds)) != NULL)
	goto bad_fd;

      LOG("%s: open table index %d of type '%s' duplicated in receiver as fd %d, ref count now %d (+++)",
	  __func__, opentable_index, opentable_type_str[ot_received_entry->type], *fds, ot_received_entry->ref_count);

      fds++;

      continue;
bad_fd:
      LOG("%s: Failed to duplicate fd, open table index %d",
	  __func__, opentable_index);
      *fds++ = -1;
    }

#if ENABLE_LOGGING
  opentable_log();
#endif

  ipc_ensure_aligned_read64(ot_entry);

#if 0
  ENABLE_LOGGING
  if (count)
    {
      LOG("%s: SENDER '%s' FD TABLE:", __func__, sender_app->name);
      fdtable_log(sender_app);
      LOG("%s: RECEIVER '%s' FD TABLE:", __func__, app->name);
      fdtable_log(app);
    }
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
    {
      LOG("%s: fd=%d, app_find failed, err = %s", __func__, fd, err->errmess);
      return err;
    }

  fdtable_entry *fd_entry;
  if ((fd_entry = get_fdtable_entry(app, fd)) == NULL ||
      !array_entry_is_in_use(&fd_entry->base))
    {
      LOG("%s: fd=%d, fd_entry=%p", __func__, fd, fd_entry);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry1 = opentable_get_entry(fd_entry->opentable_idx);
  if (!ot_entry1 || !array_entry_is_in_use(&ot_entry1->array_base))
    {
      LOG("%s: fd=%d, ot_entry1 == NULL", __func__, fd);
      return armeabisupport_EBADF;
    }

  opentable_entry *ot_entry2 = opentable_get_entry(ot_entry1->ipc.opentable_oppend_idx);
  if (!ot_entry2 || !array_entry_is_in_use(&ot_entry2->array_base))
    {
      LOG("%s: fd=%d, ot_entry2 == NULL", __func__, fd);
      return armeabisupport_EBADF;
    }

  if (ot_entry1->type != opentable_type_IPC || ot_entry2->type != opentable_type_IPC)
    {
      LOG("%s: opentable_entry not of type IPC for FD %d: armeabisupport_EBADF", __func__, fd);
      return armeabisupport_EBADF;
    }

  *flags_ret = ot_entry1->ipc.read_pos != ot_entry2->ipc.write_pos;

  if (*flags_ret)
    {
      LOG("%s: fd=%d, flags=%d, read_pos1=%d, write_pos2=%d", __func__,
	  fd, *flags_ret, ot_entry1->ipc.read_pos, ot_entry2->ipc.write_pos);
    }

//  fdtable_log(app);

  return NULL;
}

