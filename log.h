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
  printf(format "\n", ##__VA_ARGS__); \
  reporter_printf(format, ##__VA_ARGS__); \
} while (0)
#else
#define LOG(format,...)
#endif

#endif
