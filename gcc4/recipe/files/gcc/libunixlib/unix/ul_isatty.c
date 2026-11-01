/* isatty ()
 * Copyright (c) 2000-2024 UnixLib Developers
 */

#include <errno.h>
#include <unistd.h>
#include <swis.h>

#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>
#include <pthread.h>
#include <internal/swiparams.h>
#include <internal/local.h>
#include <internal/os.h>

/* Return 1 if fd is a tty.  */
int
isatty (int fd)
{
  PTHREAD_UNSAFE

  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      _kernel_oserror *err;
      err = _swix (ARMEABISupport_FdOp, _INR(0,1), ARMEABISUPPORT_FDOP_VALIDATE, fd);
      return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum)) : 0;
    }
#else
    return __set_errno (EBADF);
#endif

  return getfd (fd)->devicehandle->type == DEV_TTY;
}
