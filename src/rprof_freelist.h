/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_FREELIST_H
#define RPROF_FREELIST_H

#include <stdint.h>

typedef struct rprofFreeList_t
{
	uint32_t	m_maxBlocks;
	uint32_t	m_blockSize;
	uint32_t	m_blocksFree;
	uint32_t	m_blocksAlllocated;
	uint8_t*	m_buffer;
	uint8_t*	m_next;

} rprofFreeList_t;

void	rprofFreeListCreate(size_t _blockSize, uint32_t _maxBlocks, struct rprofFreeList_t* _freeList);
void	rprofFreeListDestroy(struct rprofFreeList_t* _freeList);
void*	rprofFreeListAlloc(struct rprofFreeList_t* _freeList);
void	rprofFreeListFree(struct rprofFreeList_t* _freeList, void* _ptr);
int		rprofFreeListCheckPtr(struct rprofFreeList_t* _freeList, void* _ptr);

#endif /* RPROF_FREELIST_H */
