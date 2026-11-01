/* write () for UnixLib
 * Copyright (c) 2000-2024 UnixLib Developers
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
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

/* #define DEBUG */
#ifdef DEBUG
#  include <sys/debug.h>
#endif

ssize_t
write (int fd, const void *buf, size_t nbytes)
{
#ifdef DEBUG
  debug_printf ("-- write(fd=%d, nbytes=%d)\n", fd, nbytes);
#endif

  if (nbytes == 0)
    return 0;
  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      /* Not known to Unixlib, try ARMEABISupport.  */
      _kernel_oserror *err;
      ssize_t bytes_written;
      return (err = _swix(ARMEABISupport_FdOp, _INR(0,3) | _OUT(0),
			  ARMEABISUPPORT_FDOP_WRITE,	/*  in: r0  */
			  fd,				/*  in: r1  */
			  buf,				/*  in: r2  */
			  nbytes,			/*  in: r3  */
			  &bytes_written)) != NULL	/* out: r0  */
	? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
	: bytes_written;
    }
#else
    return __set_errno (EBADF);
#endif
  if (buf == NULL)
    return __set_errno (EINVAL);

  struct __unixlib_fd *file_desc = getfd (fd);

  /* Confirm that the file is open for writing.  */
  if ((file_desc->fflag & O_ACCMODE) == O_RDONLY)
    {
#ifdef DEBUG
      debug_printf ("write(): not open for writing\n");
#endif
      return __set_errno (EBADF);
    }

  pthread_testcancel();
  __pthread_disable_ints();

  /* If the file is open for appending then we perform all write
     operations at the end of the file.  */
  if (file_desc->fflag & O_APPEND)
    dev_funcall (file_desc->devicehandle->type, lseek, (file_desc, 0, SEEK_END));

  /* Increment the number of times we have written to device.  */
  __u->usage.ru_oublock++;

  ssize_t status = dev_funcall (file_desc->devicehandle->type, write,
			        (file_desc, buf, nbytes));

  __pthread_enable_ints();

  /* Raise the SIGPIPE signal if we tried to write to a pipe
     or FIFO that isn't open for reading by any process.  */
  if (status == -1 && errno == EPIPE)
    raise (SIGPIPE);

  return status;
}
