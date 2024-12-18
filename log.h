/* log.h
 *
 * Copyright 2021-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef LOG_H
#define LOG_H

#include "debug.h"

#if ENABLE_LOGGING
#define LOG(format,...) \
do { \
  log_printf(format, ##__VA_ARGS__); \
  reporter_printf(format, ##__VA_ARGS__); \
} while (0)
#define COMMAND_PRINTF(format,...) \
do { \
  reporter_printf(format, ##__VA_ARGS__); \
} while (0)
#else
#define LOG(format,...)
#define COMMAND_PRINTF(format,...) \
do { \
  printf(format, ##__VA_ARGS__); \
} while (0)
#endif

#endif
