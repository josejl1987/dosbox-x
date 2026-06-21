// ponytail: Force _GNU_SOURCE before unistd.h to expose close/pipe/read/write.
// glib2/libslirp CPPFLAGS set _XOPEN_SOURCE which hides POSIX functions from asio.
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <unistd.h>
