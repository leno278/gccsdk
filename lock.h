/* lock.h
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef LOCK_H
#define LOCK_H

#include "array.h"
#include "types.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>

#define INVALID_LINK (-1)

typedef struct _filelock
{
  array_entry_base base;

  int prev;
  int next;

  off_t start;
  off_t end;
  int type;

  app_object *owner;

  int openfiletable_idx;

} filelock;

typedef struct _filelock_list
{
  array_t lock_allocator;

  int first;
  int last;

} filelock_list;

_kernel_oserror *
filelocklist_ctor(filelock_list *this);
void filelocklist_dtor(filelock_list *this);
_kernel_oserror *
filelocklist_add_lock(app_object *this_app, int openfiletable_idx, off_t start, off_t end, int type);
_kernel_oserror *
filelocklist_remove_lock(app_object *this_app, int openfiletable_idx, off_t start, off_t end);
bool
filelocklist_get_lock(filelock_list *list, int openfiletable_idx, off_t *start, off_t *end, int *type);
void
filelocklist_lslocks(void);
void filelocklist_clear_locks(app_object *app, int openfiletable_idx);

static inline filelock *
filelocklist_get_first(filelock_list *list)
{
  return (filelock *)array_get_entry_ptr(&list->lock_allocator, list->first);
}

static inline filelock *
filelocklist_get_last(filelock_list *list)
{
  return (filelock *)array_get_entry_ptr(&list->lock_allocator, list->last);
}

static inline filelock *
filelocklist_get_next(filelock_list *list, filelock *lock)
{
  return (filelock *)array_get_entry_ptr(&list->lock_allocator, lock->next);
}

static inline filelock *
filelocklist_get_prev(filelock_list *list, filelock *lock)
{
  return (filelock *)array_get_entry_ptr(&list->lock_allocator, lock->prev);
}

#endif
