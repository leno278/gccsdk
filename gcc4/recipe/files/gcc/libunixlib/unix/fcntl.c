/* UnixLib fcntl() implementation.
   Copyright (c) 2000-2024 UnixLib Developers.  */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

#include <internal/unix.h>
#include <internal/fd.h>
#include <internal/dev.h>
#ifdef __UNIXLIB_SHARED_FDS
#include <internal/os.h>
#include <internal/local.h>
#include <internal/swiparams.h>

static int armeabisupport_fcntl(int fd, int cmd, va_list *);
#endif

int
fcntl (int fd, int cmd, ...)
{
  PTHREAD_UNSAFE_CANCELLATION

  if (BADF (fd))
#if __UNIXLIB_SHARED_FDS
    {
      /* It's not a standard Unix fd, check if it's an ARMEABISupport one.  */
      va_list ap;
      va_start (ap, cmd);
      int result = armeabisupport_fcntl (fd, cmd, &ap);
      va_end (ap);
      return result;
    }
#else
    return __set_errno (EBADF);
#endif

  struct __unixlib_fd *file_desc = getfd (fd);

  switch (cmd)
    {
    case F_DUPFD:
      {
	/* Duplicate the file descriptor.  */
	va_list ap;
	va_start (ap, cmd);
	int duplicate_fd = va_arg (ap, int);
	va_end (ap);

	/* Check the new file descriptor for validity.  */
	if ((unsigned int) duplicate_fd >= __ul_global.sulproc->maxfd)
	  return __set_errno (EINVAL);

	/* Allocate a new file descriptor.  */
	duplicate_fd = __alloc_file_descriptor (duplicate_fd);

	if (duplicate_fd != -1)
	  {
	    struct __unixlib_fd *dup_file_desc = getfd (duplicate_fd);

	    dup_file_desc->devicehandle = file_desc->devicehandle;
	    __atomic_modify (&(dup_file_desc->devicehandle->refcount), 1);
	    /* File descriptor flags (like FILE_ISDIR, FILE_HANDLE_FROM_OS)
	       are duplicated.  */
	    dup_file_desc->dflag = file_desc->dflag;
	    /* The close-on-exec flag isn't duplicated.  */
	    dup_file_desc->fflag = file_desc->fflag & ~O_EXECCL;
	  }

	return duplicate_fd;
      }

    case F_GETFD:
      return (file_desc->fflag & O_EXECCL) ? 1 : 0;

    case F_SETFD:
      {
	/* Set close-on-exec flag */
	va_list ap;
	va_start(ap, cmd);
	if (va_arg (ap, int))
	  file_desc->fflag |= O_EXECCL;
	else
	  file_desc->fflag &= ~O_EXECCL;
	va_end(ap);
	return 0;
      }

    case F_GETFL:
      return file_desc->fflag;

    case F_SETFL:
      {
	va_list ap;
	va_start (ap, cmd);
	int newfflag = va_arg (ap, int);
	va_end (ap);

	if (file_desc->devicehandle->type == DEV_SOCKET)
	  {
	     if ((file_desc->fflag ^ newfflag) & O_NONBLOCK)
	       {
		 int arg = (newfflag & O_NONBLOCK) ? 1 : 0;
		 _sioctl((int)file_desc->devicehandle->handle, FIONBIO, &arg);
	       }

	     if ((file_desc->fflag ^ newfflag) & O_ASYNC)
	       {
		 int arg = (newfflag & O_ASYNC) ? 1 : 0;
		 _sioctl((int)file_desc->devicehandle->handle, FIOASYNC, &arg);
	       }
	  }

	const int modify = O_APPEND | O_NONBLOCK | O_ASYNC;
	file_desc->fflag = (file_desc->fflag & ~modify) | (newfflag & modify);
      }
      return 0;

    case F_GETUNL:
      return (file_desc->fflag & O_UNLINKED) ? 1 : 0;

    case F_SETUNL:
      {
	va_list ap;
	va_start (ap, cmd);
	int arg = va_arg (ap, int);
	va_end (ap);

	if (arg)
	  file_desc->fflag |= O_UNLINKED;
	else
	  file_desc->fflag &= ~O_UNLINKED;
	return 0;
      }

    case F_SETOWN:
      return 0;

    case F_GETLK:
      {
	va_list ap;
	va_start (ap, cmd);
	struct flock *arg = va_arg (ap, struct flock *);
	va_end (ap);

	arg->l_type = F_UNLCK;
	return 0;
      }

    case F_SETLK:
    case F_SETLKW:
      return 0; /* Dummy functionality */

    default:
      return __set_errno (EINVAL);
    }
}

#if __UNIXLIB_SHARED_FDS
int armeabisupport_fcntl(int fd, int cmd, va_list *ap)
{
  switch (cmd)
    {
    case F_DUPFD:
      {
	_kernel_oserror *err;
	int duplicate_fd;

	/* Flags are ignored.  */
	err = _swix(ARMEABISupport_FdOp, _INR(0,1) | _OUT(0),
		    ARMEABISUPPORT_FDOP_DUP,		/* In r0 */
		    fd,					/* In r1 */
		    &duplicate_fd);			/* Out r0 */
	return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
		   : duplicate_fd;
      }
    case F_GETFD:
      {
	/* Get close-on-exec flag */
	int result;
	_kernel_oserror *err;
	err = _swix (ARMEABISupport_FdOp, _INR(0,1) | _OUT(0),
		     ARMEABISUPPORT_FDOP_GETFD,		/* In r0 */
		     fd,				/* In r1 */
		     &result);				/* Out r0 */
	return result;
      }
    case F_SETFD:
      {
	/* Set close-on-exec flag */
	_kernel_oserror *err;
	err = _swix (ARMEABISupport_FdOp, _INR(0,2),
		     ARMEABISUPPORT_FDOP_SETFD,		/* In r0 */
		     fd,				/* In r1 */
		     va_arg (*ap, int));		/* In r2 */
	return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum))
		   : 0;
      }
    case F_GETFL:
    case F_SETFL:
    case F_GETUNL:
    case F_SETUNL:
    case F_SETOWN:
      return 0;
    case F_GETLK:{
      struct flock *arg = va_arg (*ap, struct flock *);
      _kernel_oserror *err;
      err = _swix (ARMEABISupport_FdOp, _INR(0,5) | _OUTR(2,5),
		   ARMEABISUPPORT_FDOP_GETLOCK,		/* In r0 */
		   fd,					/* In r1 */
		   arg->l_type,				/* In r2 */
		   arg->l_whence,			/* In r3 */
		   arg->l_start,			/* In r4 */
		   arg->l_len,				/* In r5 */
		   &arg->l_type,			/* Out r2 */
		   &arg->l_whence,			/* Out r3 */
		   &arg->l_start,			/* Out r4 */
		   &arg->l_len);			/* Out r5 */
      return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum)) : 0;
    }
    case F_SETLK:
      {
	struct flock *arg = va_arg (*ap, struct flock *);

	_kernel_oserror *err;
	err = _swix (ARMEABISupport_FdOp, _INR(0,5),
		     ARMEABISUPPORT_FDOP_SETLOCK,	/* In r0 */
		     fd,				/* In r1 */
		     arg->l_type,			/* In r2 */
		     arg->l_whence,			/* In r3 */
		     arg->l_start,			/* In r4 */
		     arg->l_len);			/* In r5 */
	return err ? __ul_seterr (err, __errno_from_armeabisupport (err->errnum)) : 0;
      }
    case F_SETLKW:
      return 0;
    default:
      return __set_errno (EINVAL);
    }
}
#endif
