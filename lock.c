/* lock.c
 *
 * Copyright 2024 GCCSDK Developers
 * Written by Lee Noar
 */

#include "armeabisupport.h"
#include "filetable.h"
#include "lock.h"
#include <stdbool.h>
#include <kernel.h>
#include <fcntl.h>
#include <limits.h>

#define ENABLE_TRACING 0
#include "trace.h"

#define ENABLE_LOGGING 0
#include "log.h"

/*
 * Locks of the same type created by the same process and created adjacent are
 * merged together.
 * Unlocking removes lock(s) from the specified bytes regardless of lock type,
 * and how many locks it spans.
 */

static inline const char *__lock_type(int type)
{
  if (type == F_RDLCK)
    return "F_RDLCK";
  else if (type == F_WRLCK)
    return "F_WRLCK";
  else if (type == F_UNLCK)
    return "F_UNLCK";

  return "INVALID LOCK TYPE";
}

_kernel_oserror *
filelocklist_ctor(filelock_list *this)
{
  _kernel_oserror *err = array_ctor(&this->lock_allocator, sizeof(filelock));
  if (err)
    return err;

  this->first = INVALID_LINK;
  this->last = INVALID_LINK;

  return NULL;
}

void filelocklist_dtor(filelock_list *this)
{
  array_dtor(&this->lock_allocator);
}

/* Return true if a conflicting lock was found and update start, end and type.
 * 
 * end can be 0 which means end of file.
 *
 */
bool
filelocklist_get_lock(filelock_list *list, int openfiletable_idx, off_t *start, off_t *end, int *type)
{
  TRACE("%s: start=%ld, end=%ld, type=%s, file=%s",
	__func__, *start, *end, __lock_type(*type),
	openfiletable_get_filename(openfiletable_idx));

#if ENABLE_LOGGING
  filelocklist_lslocks();
#endif

  openfiletable_entry *file = openfiletable_get_entry(openfiletable_idx);

  /* A request for a shared lock shall fail if the file descriptor was not opened with read access.  */
  if (*type == F_RDLCK && (file->flags & O_ACCMODE) == O_WRONLY)
    {
      LOG("%s: FAIL - request for shared lock on write only fd", __func__);
      return false;
    }

  /* A request for an exclusive lock shall fail if the file descriptor was not opened with write access.  */
  if (*type == F_WRLCK && (file->flags & O_ACCMODE) == O_RDONLY)
    {
      LOG("%s: FAIL - request for exclusive lock on read only fd", __func__);
      return false;
    }

  off_t file_len;
  if ((systemfiletable_get_file_length(file->systemtable_idx, &file_len)) != NULL)
    return false;
  off_t real_end = (*end == 0) ? file_len : *end;

  for (filelock *lock = filelocklist_get_first(&file->locks);
		 lock;
		 lock = filelocklist_get_next(&file->locks, lock))
    {
      off_t lock_end = (lock->end == 0) ? file_len : lock->end;
      LOG("%s: trying lock %p start=%ld, end=%ld, type=%s",
	  __func__, lock, lock->start, lock_end, __lock_type(lock->type));

      /* Ignore if the start of the required lock is after the end of this lock.  */
      if (*start >= lock_end)
	continue;
      /* Ignore if the end of the required lock is before the start of this lock.  */
      if (real_end <= lock->start)
	continue;

      /* We have a lock that overlaps the requested one.  */

      /* Ignore if compatible type.  */
      if (*type == lock->type)
	continue;

      LOG("%s: Found conflicting lock, start=%ld, end=%ld, type=%s",
	  __func__, lock->start, lock->end, __lock_type(lock->type));
      /* One of the above conflicted, report the conflicted lock.  */
      *start = lock->start;
      *end = lock->end;
      *type = lock->type;
      return true;
    }

  LOG("%s: No conflict found", __func__);

  *type = F_UNLCK;
  return false;
}

static void lock_list_add_to_end(filelock_list *list, filelock *lock)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  filelock *old_last = filelocklist_get_last(list);
  lock->next = INVALID_LINK;
  lock->prev = list->last;

  int lock_idx = array_get_entry_idx(&list->lock_allocator, &lock->base);

  if (old_last == NULL)
    list->first = lock_idx;
  else
    old_last->next = lock_idx;

  list->last = lock_idx;
}

static void lock_list_add_to_front(filelock_list *list, filelock *lock)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  filelock *old_first = filelocklist_get_first(list);

  lock->next = list->first;
  lock->prev = INVALID_LINK;

  int lock_idx = array_get_entry_idx(&list->lock_allocator, &lock->base);

  if (old_first == NULL)
    list->last = lock_idx;
  else
    old_first->prev = lock_idx;

  list->first = lock_idx;
}

static void lock_list_add_before(filelock_list *list, filelock *at, filelock *lock)
{
//  TRACE("%s: list(%d,%d), at=%p", __func__, list->first, list->last, at);

  if (at == NULL)
    {
      lock_list_add_to_end(list, lock);
      return;
    }

  filelock *old_prev = (filelock *)array_get_entry_ptr(&list->lock_allocator, at->prev);
  if (old_prev == NULL)
    lock_list_add_to_front(list, lock);
  else
    {
      old_prev->next = array_get_entry_idx(&list->lock_allocator, &lock->base);
      lock->prev = array_get_entry_idx(&list->lock_allocator, &old_prev->base);
      at->prev = array_get_entry_idx(&list->lock_allocator, &lock->base);
      lock->next = array_get_entry_idx(&list->lock_allocator, &at->base);
    }
}

static void lock_list_remove(filelock_list *list, filelock *lock)
{
//  TRACE("%s: list(%d,%d)", __func__, list->first, list->last);

  int prev_idx = lock->prev;
  int next_idx = lock->next;
  filelock *prev = filelocklist_get_prev(list, lock);
  filelock *next = filelocklist_get_next(list, lock);

  if (next == NULL)
    list->last = prev_idx;
  else
    next->prev = prev_idx;

  if (prev == NULL)
    list->first = next_idx;
  else
    prev->next = next_idx;

  lock->prev = lock->next = INVALID_LINK;
}

static filelock *alloc_lock(app_object *app,
			    filelock_list *list,
			    int openfiletable_idx,
			    off_t start,
			    off_t end,
			    int type)
{
  int idx;
  _kernel_oserror *err;
  if ((err = array_alloc_entry(&list->lock_allocator, &idx)) == NULL)
    {
      filelock *lock = (filelock *)array_get_entry_ptr(&list->lock_allocator, idx);

      lock->start = start;
      lock->end = end;
      lock->type = type;
      lock->owner = app;
      lock->openfiletable_idx = openfiletable_idx;

      return lock;
    }

  return NULL;
}

/* There shall be at most one type of lock set for each byte in the file.
 * 
 * When a shared lock is set on a segment of a file, other processes shall
 * be able to set shared locks on that segment or a portion of it.
 * 
 * A shared lock (F_RDLCK) prevents any other process from setting an exclusive lock
 * on any portion of the protected area.
 *
 * An exclusive lock (F_WRLCK) shall prevent any other process from setting a shared
 * lock or an exclusive lock on any portion of the protected area.
*/
_kernel_oserror *
filelocklist_add_lock(app_object *this_app, int openfiletable_idx, off_t start, off_t end, int type)
{
  TRACE("%s: start=%ld, end=%ld, type=%s, file=%s",
	__func__, start, end, __lock_type(type),
	openfiletable_get_filename(openfiletable_idx));

  openfiletable_entry *file = openfiletable_get_entry(openfiletable_idx);

  /* A request for a shared lock shall fail if the file descriptor was not opened with read access.  */
  if (type == F_RDLCK && (file->flags & O_ACCMODE) == O_WRONLY)
    {
      LOG("%s: FAIL - request for shared lock on write only fd", __func__);
      return armeabisupport_EBADF;
    }

  /* A request for an exclusive lock shall fail if the file descriptor was not opened with write access.  */
  if (type == F_WRLCK && (file->flags & O_ACCMODE) == O_RDONLY)
    {
      LOG("%s: FAIL - request for exclusive lock on read only fd", __func__);
      return armeabisupport_EBADF;
    }

  filelock *new_lock;

  /* If this is the first lock, then fast track it.  */
  if (file->locks.first == INVALID_LINK)
    {
      if ((new_lock = alloc_lock(this_app, &file->locks, openfiletable_idx, start, end, type)) == NULL)
	return armeabisupport_ENOMEM;
      lock_list_add_to_end(&file->locks, new_lock);
      LOG("%s: New (first) lock added", __func__);
      return NULL;
    }

  /* At this point, there will always be at least one lock in the list because of the fast track above.  */
  off_t file_len;
  _kernel_oserror *err = systemfiletable_get_file_length(file->systemtable_idx, &file_len);
  if (err)
    return err;

  off_t real_end = (end == 0) ? file_len : end;

  /* Check if any other apps have a conflicting lock.  */
  for (filelock *conflict_lock = filelocklist_get_first(&file->locks);
       conflict_lock;
       conflict_lock = filelocklist_get_next(&file->locks, conflict_lock))
    {
      off_t list_end = (conflict_lock->end == 0) ? file_len : conflict_lock->end;
      if (start >= conflict_lock->start && start < list_end)
	{
	  /* Proposed lock starts within another locks range.  */
	  if (this_app != conflict_lock->owner &&
	      /* If another app has an exclusive lock, then we can't set an exclusive lock.  */
	      (conflict_lock->type == F_WRLCK ||
	      /* If another app has a shared lock, then we can't set an exclusive lock.  */
	      (conflict_lock->type == F_RDLCK && type == F_WRLCK)))
	    {
	      LOG("%s: FAIL - conflicting lock, start=%ld, end=%ld, type=%s",
		  __func__, conflict_lock->start, list_end, __lock_type(conflict_lock->type));
	      return armeabisupport_EACCES;
	    }
	}
    }

  /* If we get here, then there is no conflict and we can set the lock.  */
  filelock *list_lock = filelocklist_get_first(&file->locks);
  while (list_lock)
    {
      filelock *next_lock = filelocklist_get_next(&file->locks, list_lock);

      if (list_lock->owner != this_app)
	{
	  list_lock = next_lock;
	  continue;
	}

      LOG("%s: list lock %ld->%ld, type=%s",
	  __func__, list_lock->start, list_lock->end, __lock_type(list_lock->type));
      off_t list_end = (list_lock->end == 0) ? file_len : list_lock->end;

      /* Find a lock that may overlap the start of the proposed lock.  */
      if (start > list_lock->start && start < list_end)
	{
	  if (list_lock->type == type)
	    {
	      LOG("%s: start covered by same lock type %ld->%ld", __func__, list_lock->start, list_end);
	      /* Same type, adjust new lock to cover this one and remove the old one.  */
	      start = list_lock->start;
	      lock_list_remove(&file->locks, list_lock);
	      array_entry_mark_unused(&list_lock->base);
	    }
	  else
	    {
	      LOG("%s: start covered by different lock type %ld->%ld", __func__, list_lock->start, list_end);
	      /* Different type, adjust list lock to end where the new one starts.  */
	      list_lock->end = start;
	    }
	}
      /* Find any locks that are wholly covered by the new lock.  */
      else if (start <= list_lock->start && real_end >= list_end)
	{
	  LOG("%s: wholly covered by lock %ld->%ld", __func__, list_lock->start, list_end);
	  lock_list_remove(&file->locks, list_lock);
	  array_entry_mark_unused(&list_lock->base);
	}
      /* Find a lock that may overlap the end of the proposed lock.  */
      else if (real_end > list_lock->start && real_end < list_end)
	{
	  if (list_lock->type == type)
	    {
	      /* Same type, adjust new lock to cover this one and remove the old one.  */
	      LOG("%s: end covered by same lock type %ld->%ld", __func__, list_lock->start, list_end);
	      end = list_lock->end;
	      lock_list_remove(&file->locks, list_lock);
	      array_entry_mark_unused(&list_lock->base);
	    }
	  else
	    {
	      LOG("%s: end covered by different lock type %ld->%ld", __func__, list_lock->start, list_end);
	      /* Different type, adjust list lock to start where the new one ends.  */
	      list_lock->start = end;
	    }
	}
      list_lock = next_lock;
    }

  /* Find correct place in list based on the start offset of the lock.  */
  for (list_lock = filelocklist_get_first(&file->locks);
       list_lock && start >= list_lock->start;
       list_lock = filelocklist_get_next(&file->locks, list_lock))
    /* */;

  if ((new_lock = alloc_lock(this_app, &file->locks, openfiletable_idx, start, end, type)) == NULL)
    return armeabisupport_ENOMEM;

  /* list_lock can be NULL, in which case, it's added to the end of the list.  */
  lock_list_add_before(&file->locks, list_lock, new_lock);

#if ENABLE_LOGGING
  filelocklist_lslocks();
#endif

  return NULL;
}

_kernel_oserror *
filelocklist_remove_lock(app_object *this_app, int openfiletable_idx, off_t start, off_t end)
{
  TRACE("%s: start=%ld, end=%ld, file=%s",
	__func__, start, end,
	openfiletable_get_filename(openfiletable_idx));

  openfiletable_entry *file = openfiletable_get_entry(openfiletable_idx);

  off_t file_len;
  _kernel_oserror *err = systemfiletable_get_file_length(file->systemtable_idx, &file_len);
  if (err)
    return err;

#if ENABLE_LOGGING
  filelocklist_lslocks();
#endif

  off_t real_end = (end == 0) ? INT_MAX/*file_len*/ : end;

  LOG("%s: real_end=%ld", __func__, real_end);

  filelock *list_lock;
  list_lock = filelocklist_get_first(&file->locks);
  while (list_lock)
    {
      filelock *next_lock = filelocklist_get_next(&file->locks, list_lock);

      if (list_lock->owner != this_app)
	{
	  list_lock = next_lock;
	  continue;
	}

      off_t list_end = (list_lock->end == 0) ? INT_MAX/*file_len*/ : list_lock->end;

      /* Find a lock that may overlap the start of the unlock region.  */
      if (start > list_lock->start && start < list_end)
	{
	  LOG("%s: unlock region overlaps start of lock %ld->%ld", __func__, list_lock->start, list_end);
	  list_lock->end = start;
	}
      /* Find any locks that are wholely covered by the unlock region.  */
      else if (start <= list_lock->start && real_end >= list_end)
	{
	  LOG("%s: unlock region covers all of lock %ld->%ld", __func__, list_lock->start, list_end);
	  lock_list_remove(&file->locks, list_lock);
	  array_entry_mark_unused(&list_lock->base);
	}
      /* Find a lock that may overlap the end of the unlock region.  */
      else if (real_end > list_lock->start && real_end < list_end)
	{
	  LOG("%s: unlock region overlaps end of lock %ld->%ld", __func__, list_lock->start, list_end);
	  list_lock->start = end;
	}
      list_lock = next_lock;
    }

#if ENABLE_LOGGING
  filelocklist_lslocks();
#endif

  return NULL;
}

void filelocklist_clear_locks(app_object *this_app, int openfiletable_idx)
{
  openfiletable_entry *file = openfiletable_get_entry(openfiletable_idx);

  filelock *list_lock = filelocklist_get_first(&file->locks);
  while (list_lock)
    {
      filelock *next_lock = filelocklist_get_next(&file->locks, list_lock);

      if (list_lock->owner == this_app)
	{
	  lock_list_remove(&file->locks, list_lock);
	  array_entry_mark_unused(&list_lock->base);
	}

      list_lock = next_lock;
    }
}

void
filelocklist_lslocks(void)
{
  static const char *mode_str[] = { "READ", "WRITE" };

  printf("MODE      START        END PATH\n");
  LOG("MODE      START        END PATH\n");
  openfiletable_entry *file = (openfiletable_entry *)global.open_filetable.entries;
  for (int i = 0; i < global.open_filetable.num_entries; i++)
    {
      if (file[i].base.in_use)
	{
	  systemfiletable_entry *systemtable_entry = (systemfiletable_entry *)array_get_entry_ptr(&global.system_filetable, file->systemtable_idx);
	  filelock_list *list = &file[i].locks;
	  for (filelock *lock = filelocklist_get_first(list);
	       lock;
	       lock = filelocklist_get_next(list, lock))
	    {
	      printf("%s %-11ld %-11ld %s\n",
		     mode_str[lock->type - 1],
		     lock->start,
		     lock->end == 0 ? 0 : lock->end - 1,
		     systemtable_entry->filename);
	      LOG("%s %-11ld %-11ld %s\n",
		  mode_str[lock->type - 1],
		  lock->start,
		  lock->end == 0 ? 0 : lock->end - 1,
		  systemtable_entry->filename);
	    }
	}
    }
}
