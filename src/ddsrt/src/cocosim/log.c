#include "dds/cocosim/log.h"

/////////
// log //
/////////
void vcocosim_log_printf(int level, const char* format, va_list args){
  if (level & DEBUG_LEVEL) {
    vfprintf(DEBUG_STREAM, format, args);
  }
}

void cocosim_log_printf(int level, const char* format, ...){
  if (level & DEBUG_LEVEL) {
    va_list(args);
    va_start(args, format);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    va_end(args);
  }
}

void vcocosim_log(int level, const char* format, va_list args){
  if (level & DEBUG_LEVEL) {
    char levelstr[6];
    switch (level) {
      case LOG_FATAL : strcpy(levelstr,"FATAL"); break;
      case LOG_ERROR : strcpy(levelstr,"ERROR"); break;
      case LOG_WARN  : strcpy(levelstr,"WARN "); break;
      case LOG_INFO  : strcpy(levelstr,"INFO "); break;
      case LOG_DEBUG : strcpy(levelstr,"DEBUG"); break;
      default        : strcpy(levelstr,"?????"); break;
    }
    fprintf(DEBUG_STREAM,"CCS_%s: ",levelstr);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    fflush(DEBUG_STREAM);
  }
}

void cocosim_log(int level, const char* format, ...){
  if (level & DEBUG_LEVEL) {
    va_list(args);
    va_start(args, format);
    vcocosim_log(level, format, args);
    va_end(args);
    fflush(DEBUG_STREAM);
  }
}

bool cocosim_log_call_init(bool to_ns3, int* ret, const char* format, ...){
  if (LOG_DEBUG & DEBUG_LEVEL && (LOG_DEBUG2 & DEBUG_LEVEL || !ret)) {
    if(to_ns3){
      cocosim_log(LOG_DEBUG, "[ns-3] ");
    }else{
      cocosim_log(LOG_DEBUG, "[posix] ");
    }
    if(ret){
      fprintf(DEBUG_STREAM, "%d = ", *ret);
    }
    va_list(args);
    va_start(args, format);
    vfprintf(DEBUG_STREAM, format, args); // same as vcocosim_log_printf, avoid overhead
    va_end(args);
    return true;
  }else{
    return false;
  }
}
