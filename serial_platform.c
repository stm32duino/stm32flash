#ifdef __WIN32__
#   include "serial_w32.c"
#else
#   include "serial_posix.c"
#endif
