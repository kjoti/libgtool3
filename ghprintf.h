/*
 * ghprintf.h
 */
#ifndef GHPRINTF_H
#define GHPRINTF_H

#include <sys/types.h>
#include "gtool3.h"

int gh_snprintf(char *str, size_t size, const char *format,
                const GT3_HEADER *head, const char *filename, int curr);

void ghprintf_shift(int onoff);
#endif
