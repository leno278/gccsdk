/* read () for UnixLib
 * Copyright (c) 2000-2024 UnixLib Developers
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#if __UNIXLIB_SHARED_FDS
#include <swis.h>

#include <internal/os.h>
#include <internal/local.h>
#include <internal/swiparams.h>
#endif
#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>

ssize_t
read (int fd, void *buf, size_t nbytes)
{
  PTHREAD_UNSAFE_CANCELLATION

  if (nbytes == 0)
    return 0;
  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      /* Not known to Unixlib, try ARMEABISupport.  */
      _kernel_oserror *err;
      ssize_t bytes_read;
      return (err = _swix(ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
			  ARMEABISUPPORT_FDOP_READ,	/*  in: r0  */
			  fd,				/*  in: r1  */
			  buf,				/*  in: r2  */
			  nbytes,			/*  in: r3  */
			  &bytes_read)) != NULL		/* out: r0  */
	? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
	: bytes_read;
    }
#else
    return __set_errno (EBADF);
#endif

  struct __unixlib_fd *file_desc = getfd (fd);

  /* Confirm that the file is open for reading.  */
  if ((file_desc->fflag & O_ACCMODE) == O_WRONLY)
    return __set_errno (EBADF);

  /* Increment the number of times we have had to read from a device.  */
  __u->usage.ru_inblock++;

  /* Perform the device specific read operation.  */
  return dev_funcall (file_desc->devicehandle->type, read,
		    (file_desc, buf, nbytes));
}
