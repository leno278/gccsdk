/* fstat (), fstat64 ()
 * Copyright (c) 2000-2024 UnixLib Developers
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#include <internal/dev.h>
#include <internal/os.h>
#include <internal/unix.h>
#include <unixlib/local.h>
#include <internal/swiparams.h>
#include <internal/local.h>
#include <pthread.h>

int
fstat (int fd, struct stat *buf)
{
  PTHREAD_UNSAFE

  if (buf == NULL)
    return __set_errno (EINVAL);

  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      _kernel_oserror *err;
      unsigned objtype, loadaddr, execaddr, objlen, attr;
      const char *filename;
      err = _swix(ARMEABISupport_FdOp, _INR(0,1) | _OUTR(0,5),
		  ARMEABISUPPORT_FDOP_GETINFO,	/*  in: r0 */
		  fd,				/*  in: r1 */
		  &objtype,			/* out: r0 */
		  &filename,			/* out: r1 */
		  &loadaddr,			/* out: r2 */
		  &execaddr,			/* out: r3 */
		  &objlen,			/* out: r4 */
		  &attr);			/* out: r5 */
      if (err)
	return __set_errno (EBADF);

      buf->st_dev = DEV_CUSTOM;
      buf->st_ino = __get_file_ino (NULL, filename);

      return __stat (objtype, loadaddr, execaddr, objlen, attr, buf);
    }
#else
    return __set_errno (EBADF);
#endif
  const struct __unixlib_fd *file_desc = getfd (fd);

  buf->st_dev = file_desc->devicehandle->type;

  /* Perform the device specific open operation.  */
  return dev_funcall (file_desc->devicehandle->type, fstat, (fd, buf));
}

int
fstat64 (int fd, struct stat64 *buf)
{
  /* struct stat64 is currently the same as struct stat.  */
  return fstat (fd, (struct stat *)buf);
}
