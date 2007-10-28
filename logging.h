/*  -*- tab-width: 4; -*-
 *
 *  logging.h
 */
#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* message level */
enum {
	LOG_INFO,
	LOG_NOTICE,
	LOG_WARN,
	LOG_ERR,
	LOG_SYSERR
};

void set_logging_prefix_func(void (*func)(FILE *, int));
void open_logging(FILE *fp, const char *name);
int open_logfile(const char *path, const char *name, int append);
void close_logging(void);
void set_logging_level(const char *str);
void logging(int type, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif /* !LOGGING_H */
