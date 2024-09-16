/* filedesc.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef FD_H
#define FD_H

#include "types.h"
#include "link_list.h"

#define BASE_FD 1000

#define FD_INVALID_FD (-1)
#define FD_TYPE_INVALID 0
#define FD_TYPE_SHM 1

typedef struct _shm_object shm_object;

typedef enum
{
  fdtype_INVALID,
  fdtype_SHM
} fdtype;

typedef struct _filedesc
{
  link_hdr link;

  app_object *owner;
  fdtype type;
  int32_t value;

  union
  {
    shm_object *shm;
    void *object;
  };
} filedesc;
LINKLIST_ACCESS_FUNCTIONS(filedesc)

extern void filedesc_initialise(armeabisupport_globals *globals);

extern _kernel_oserror *fd_op(_kernel_swi_regs * r);

extern _kernel_oserror *filedesc_new_fd(filedesc **);
extern _kernel_oserror *filedesc_close(int);
extern filedesc *filedesc_find_fd(int fd);
extern void filedesc_cleanup_app(app_object *app);
extern void filedesc_dump_fds(void);

#endif
