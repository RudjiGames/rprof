/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 *
 * Internal allocation wrappers that route through the host provided allocator
 * (see rprofSetAllocator). Returns 0 if no allocator has been installed.
 */

#ifndef RPROF_ALLOC_H
#define RPROF_ALLOC_H

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

void* rprofAlloc(size_t _size);
void  rprofFree(void* _ptr);

#ifdef __cplusplus
}
#endif

#endif /* RPROF_ALLOC_H */
