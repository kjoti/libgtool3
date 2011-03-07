/*
 * setmode.h
 */
#ifndef SETMODE__H
#define SETMODE__H

#if defined(MSDOS) || defined(CYGWIN) || defined(MINGW)
#  include <fcntl.h>
#  include <io.h>
#  include <stdio.h>
#  define SET_BINARY_MODE(fp) _setmode(fileno(fp), O_BINARY)
#  define SET_TEXT_MODE(fp) _setmode(fileno(fp), O_TEXT)
#else
#  define SET_BINARY_MODE(fp)
#  define SET_TEXT_MODE(fp)
#endif
#endif /* !SETMODE__H */
