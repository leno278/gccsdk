/* UnixLib open() and open64() implementation.
   Copyright (c) 2000-2024 UnixLib Developers.  */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <swis.h>

#include <internal/os.h>
#include <unixlib/local.h>
#include <internal/unix.h>
#include <internal/dev.h>
#include <internal/fd.h>
#include <internal/local.h>
#include <internal/swiparams.h>
#include <internal/symlinks.h>

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

/* This function will open a file on the specified file descriptor
   with given RISC OS file handle. Caller assures fd and fh are valid.  */
int
__open_fh (int fd, int fh, int oflag, int mode)
{
  PTHREAD_UNSAFE

  struct __unixlib_fd *file_desc = getfd (fd);
  file_desc->fflag = oflag;
  file_desc->dflag = FILE_HANDLE_FROM_OS; /* Never close this handle. */

  struct __sul_process *sulproc = __ul_global.sulproc;
  file_desc->devicehandle = sulproc->sul_malloc (sulproc->pid,
						 sulproc->fdhandlesize);
  if (file_desc->devicehandle == NULL)
    return __set_errno (ENOMEM);

  char *rofs = __canonicalise_handle (fh);
  if (rofs == NULL)
    {
      sulproc->sul_free (sulproc->pid, file_desc->devicehandle);
      file_desc->devicehandle = NULL;
      return -1;
    }
  /* Perform a special check for devices.  */
  file_desc->devicehandle->type = __getdevtype (rofs, __RISCOSIFY_NO_PROCESS);
  file_desc->devicehandle->refcount = 1;
  free (rofs);

  file_desc->devicehandle->handle = (void *) fh;

  return fd;
}

/* This function will open a file on the specified file descriptor
   with given filename. Caller assures fd is valid.  */
int
__open_fn (int fd, const char *file, int oflag, int mode)
{
  PTHREAD_UNSAFE

  struct __unixlib_fd *file_desc = getfd (fd);
  file_desc->fflag = oflag;
  file_desc->dflag = 0;

  struct __sul_process *sulproc = __ul_global.sulproc;
  file_desc->devicehandle = sulproc->sul_malloc (sulproc->pid,
						 sulproc->fdhandlesize);
  if (file_desc->devicehandle == NULL)
    return __set_errno (ENOMEM);

  /* Perform a special check for devices.  */
  file_desc->devicehandle->type = __getdevtype (file,
						__get_riscosify_control());
  file_desc->devicehandle->refcount = 1;

  /* Perform the device specific open operation.  */
  file_desc->devicehandle->handle = dev_funcall (file_desc->devicehandle->type,
						 open, (file_desc, file, mode));
  if (file_desc->devicehandle->handle == (void *) -1)
    {
      sulproc->sul_free (sulproc->pid, file_desc->devicehandle);
      file_desc->devicehandle = NULL;
      return -1;
    }

  return fd;
}

#if __UNIXLIB_SHARED_FDS
static int
__open_shared_file (const char *ux_filename, int oflag, int mode)
{
  int sftype;
  char file[MAXPATHLEN];
  char *end_of_filename = __riscosify_std (ux_filename,
					   oflag & (O_CREAT | O_WRONLY | O_RDWR),
					   file, sizeof (file), &sftype);
  if (end_of_filename == NULL)
    return __set_errno (ENAMETOOLONG);

  const char *ro_filename;
#if __UNIXLIB_SYMLINKS
  char target[MAXPATHLEN];
  if (__resolve_symlinks (file, target, MAXPATHLEN, 0) != 0)
    return -1;

  ro_filename = target;
#else
  ro_filename = file;
#endif

  int fd;
  _kernel_oserror *err;
  err = _swix(ARMEABISupport_FileOp, _INR(0,3) | _OUT(0),
	      ARMEABISUPPORT_FILEOP_OPEN,
	      ro_filename,
	      oflag,
	      mode,
	      &fd);
  return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum)) : fd;
}
#endif

int
open (const char *file, int oflag, ...)
{
  PTHREAD_UNSAFE_CANCELLATION

#ifdef DEBUG
  debug_printf ("open(file=%s)\n", file);
#endif

  if (file == NULL)
    return __set_errno (EFAULT);
  if (*file == '\0')
    return __set_errno (ENOENT);

  int mode;
  if (oflag & O_CREAT)
    {
      va_list ap;
      va_start (ap, oflag);
      mode = va_arg (ap, int);
      va_end (ap);
    }
  else
    mode = 0777;

#if __UNIXLIB_SHARED_FDS
  if (oflag & O_SHAREDFILE)
    {
      return __open_shared_file(file, oflag, mode);
    }
#endif

  /* Allocate a new file descriptor. This function automatically
     sets errno if there are none available.  */
  int fd = __alloc_file_descriptor (0);
  if (fd == -1)
    return -1;

  return __open_fn (fd, file, oflag, mode);
}
#if __UNIXLIB_LFS64_SUPPORT
#  error "64-bit LFS support missing."
#else
strong_alias (open, open64)
#endif

