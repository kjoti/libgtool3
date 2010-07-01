/*
 * debug.h -- macros for debug print
 */

#ifndef DEBUG__H
#define DEBUG__H


#ifdef DEBUG_MSG
#  include <stdio.h>

#  define debug0(fmt)                                               \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__); }

#  define debug1(fmt, a1)                                       \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1)); }

#  define debug2(fmt, a1, a2)                                   \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2)); }

#  define debug3(fmt, a1, a2, a3)                               \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2), (a3)); }

#  define debug4(fmt, a1, a2, a3, a4)                           \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2), (a3), (a4)); }

#  define debug5(fmt, a1, a2, a3, a4, a5)                       \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2), (a3), (a4), (a5)); }

#  define debug6(fmt, a1, a2, a3, a4, a5, a6)                   \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2), (a3), (a4), (a5), (a6)); }

#  define debug7(fmt, a1, a2, a3, a4, a5, a6, a7)               \
    { fprintf(stderr, "%s (%d): " fmt "\n", __FILE__, __LINE__, \
              (a1), (a2), (a3), (a4), (a5), (a6), (a7)); }

#else /* !DEBUG_MSG */
#  define debug0(fmt)
#  define debug1(fmt, a1)
#  define debug2(fmt, a1, a2)
#  define debug3(fmt, a1, a2, a3)
#  define debug4(fmt, a1, a2, a3, a4)
#  define debug5(fmt, a1, a2, a3, a4, a5)
#  define debug6(fmt, a1, a2, a3, a4, a5, a6)
#  define debug7(fmt, a1, a2, a3, a4, a5, a6, a7)
#endif

#endif
