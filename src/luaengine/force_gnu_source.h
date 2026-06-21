// ponytail: Force _GNU_SOURCE before ANY system header inclusion.
// DOSBox-X headers include <unistd.h> without _GNU_SOURCE,
// which with _XOPEN_SOURCE=700 hides POSIX functions (pipe/close/read/write).
// This file is -included via CXXFLAGS before any source file.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
