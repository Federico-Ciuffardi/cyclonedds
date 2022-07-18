#ifndef COCOSIM_LOG_H
#define COCOSIM_LOG_H

#include "dds/export.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

#if defined (__cplusplus)
extern "C" {
#endif

// do not modify these (must be powers of 2)
#define LOG_FATAL  0b00000001
#define LOG_ERROR  0b00000010
#define LOG_WARN   0b00000100
#define LOG_INFO   0b00001000
#define LOG_DEBUG  0b00010000
#define LOG_DEBUG2 0b00100000 // display a second line after the ns-3/posix call finishes displaying the return value

// configuration 
#define DEBUG_STREAM stderr
#define DEBUG_LEVEL (LOG_FATAL | LOG_ERROR | LOG_WARN | LOG_INFO | LOG_DEBUG | LOG_DEBUG2)

// declaration
DDS_EXPORT void
  vcocosim_log_printf(int level,
      const char* format,
      va_list args);

DDS_EXPORT void 
cocosim_log_printf(int level, 
    const char* format, 
    ...);

DDS_EXPORT void
  vcocosim_log(int level,
      const char* format,
      va_list args);

DDS_EXPORT void 
cocosim_log(int level, 
    const char* format, 
    ...);

DDS_EXPORT bool 
cocosim_log_call_init(bool to_ns3,
    int* ret,
    const char* format,
    ...);

#if defined (__cplusplus)
}
#endif

#endif /* COCOSIM_LOG_H */
