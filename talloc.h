/*
 *  talloc.h
 */
#ifndef TALLOC_H
#define TALLOC_H

#include <stdlib.h>

void *tiny_alloc(void *tiny, size_t tiny_size, size_t size);
void tiny_free(void *ptr, const void *ref);

#endif /* !TALLOC_H */
