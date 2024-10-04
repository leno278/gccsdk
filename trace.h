/* trace.h
 *
 * Copyright 2021-2024 GCCSDK Developers
 * Written by Lee Noar
 */

#ifndef TRACE_H
#define TRACE_H

#include "debug.h"

#if ENABLE_TRACING
#define TRACE(format, ...) \
do {\
reporter_printf(format, ##__VA_ARGS__); \
} while (0)
#else
#define TRACE(format,...)
#endif

#endif
