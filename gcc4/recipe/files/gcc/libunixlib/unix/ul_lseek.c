/* lseek (), lseek64 ()
 * Copyright (c) 2000-2024 UnixLib Developers
 */

#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#if __UNIXLIB_SHARED_FDS
#include <swis.h>

#include <internal/swiparams.h>
#include <internal/local.h>
#include <internal/os.h>
#endif
#include <internal/dev.h>
#include <internal/unix.h>
#include <internal/fd.h>

__off_t
lseek (int fd, __off_t offset, int whence)
{
  PTHREAD_UNSAFE

  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      __off_t new_offset;
      _kernel_oserror *err;
      return (err = _swix(ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
		     ARMEABISUPPORT_FDOP_SEEK,	/*  in: r0  */
		     fd,			/*  in: r1  */
		     offset,			/*  in: r2  */
		     whence,			/*  in: r3  */
		     &new_offset)) != NULL	/* out: r0  */
	? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
	: new_offset;
    }
#else
    return __set_errno (EBADF);
#endif

  struct __unixlib_fd *file_desc = getfd (fd);

  /* The validity of whence is check by the device specific operation.  */

  return dev_funcall (file_desc->devicehandle->type, lseek,
		      (file_desc, offset, whence));
}

__off64_t
lseek64 (int fd, __off64_t offset, int whence)
{
#if __UNIXLIB_LFS64_SUPPORT
#  error "64-bit LFS support missing."
#else
  /* Check if the __off64_t offset fits in a __off_t one.  Further
     checking will be done in lseek itself.  */
  if (offset != (__off64_t)(__off_t)offset)
    return __set_errno (EOVERFLOW);
#endif

  return lseek (fd, (__off_t) offset, whence);
}
