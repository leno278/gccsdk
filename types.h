/* types.h
 *
 * Copyright 2019-2025 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef TYPES_H
#define TYPES_H

#include "link_list.h"
#include "array.h"
#include <stdint.h>
#include <stdbool.h>
#include <kernel.h>

#define APP_NAME_MAX_LEN 32
#define ALLOCATOR_NAME_MAX_LEN	32

#define VALIDATE_VT 0

#if VALIDATE_VT
#define VT_MAGIC 0x600D600D
#else
#define VT_MAGIC
#endif

typedef unsigned int stack_t;
typedef unsigned char *eabi_PTR;
typedef struct _armeabisupport_allocator *armeabi_allocator_t;

struct _opentable_entry;

typedef enum fd_type
{
  fd_type_INVALID = -1,
  fd_type_SHM = 0,
  fd_type_SHARED_FILE,
  fd_type_IPC,
  fd_type_MAX

} fd_type;

typedef struct pmp_phy_page_entry
{
  int pmp_page_index;
  int physical_page_number;
  unsigned flags;
} pmp_phy_page_entry;

typedef struct pmp_log_page_entry
{
  int da_page_number;
  int pmp_page_index;
  unsigned flags;
} pmp_log_page_entry;

typedef struct pmp_page_info_entry
{
  int pmp_page_index;
  int physical_page_number;
  int da_page_number;
  unsigned flags;
} pmp_page_info_entry;

typedef struct _stack_abort_data_object
{
  link_list *stack_list;
  armeabi_allocator_t *stack_allocator;
} stack_abort_data_object;

typedef struct _app_object
{
  link_hdr link;
  unsigned ID;
  link_list allocator_list;
  link_list abort_handler_list;
  link_list stack_list;
  stack_abort_data_object stack_abort_data;
  array_t fd_table;
  array_t mmap_table;		/* struct app_mmap_mapping */
  char name[APP_NAME_MAX_LEN];
} app_object;
LINKLIST_ACCESS_FUNCTIONS (app_object)

typedef struct armeabisupport_globals
{
  armeabi_allocator_t stack_allocator;
  array_t mmap_allocator_pool;	/* struct mmap_allocator */
  link_list app_list;
  link_list allocator_list;
  bool pre_filter_installed;
  bool post_filter_installed;
  bool aborttrap_enabled;

  unsigned os_memory_prot_none;
  unsigned os_memory_prot_read;
  unsigned os_memory_prot_read_write;
  unsigned os_memory_prot_exec;

  array_t open_table;
  array_t system_table[fd_type_MAX];

  app_object *parent;

} armeabisupport_globals;

enum block_type
{
  block_type_allocated,
  block_type_free
};

typedef struct memblock
{
  link_hdr link;

  /* Absolute address within dynamic area of this block.  */
  eabi_PTR start_addr;

  /* Absolute address of the end of this block.  */
  eabi_PTR end_addr;

  /* Number of pages in this block.  */
  int page_count;

  enum block_type type;
} memblock;
LINKLIST_ACCESS_FUNCTIONS (memblock)

typedef struct mmap_block
{
  link_hdr link;

  uint32_t start_page;	/* Inclusive */
  uint32_t end_page;	/* Non inclusive */

  uint32_t access;	/* Memory protection flags as used by the OS  */

  int prot;		/* Memory protection flags as used by mmap  */
  int mmap_flags;	/* Flags as used by mmap  */
  int opentable_idx;
  int fd;
  uint32_t offset;

  bool is_shm;

  app_object *owner;
} mmap_block;
LINKLIST_ACCESS_FUNCTIONS (mmap_block)

extern armeabisupport_globals global;

#endif
