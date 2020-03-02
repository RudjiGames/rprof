/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */


#include <stdlib.h>
#include "rprof_freelist.h"

void rprofFreeListCreate(size_t _blockSize, uint32_t _maxBlocks, struct rprofFreeList_t* _freeList)
{
	_freeList->m_maxBlocks			= _maxBlocks;
	_freeList->m_blockSize			= (uint32_t)_blockSize;
	_freeList->m_blocksFree			= _maxBlocks;
	_freeList->m_blocksAlllocated	= 0;
	_freeList->m_buffer				= (uint8_t*)malloc(_blockSize * _maxBlocks);
	_freeList->m_next				= _freeList->m_buffer;
}

void rprofFreeListDestroy(struct rprofFreeList_t* _freeList)
{
	free(_freeList->m_buffer);
}

void* rprofFreeListAlloc(struct rprofFreeList_t* _freeList)
{
	if (_freeList->m_blocksAlllocated < _freeList->m_maxBlocks)
	{
		uint32_t* p = (uint32_t*)(_freeList->m_buffer + (_freeList->m_blocksAlllocated * _freeList->m_blockSize));
		*p = _freeList->m_blocksAlllocated + 1;
		_freeList->m_blocksAlllocated++;
	}
	
	void* ret = 0;
	if (_freeList->m_blocksFree)
	{
		ret = _freeList->m_next;
		--_freeList->m_blocksFree;
		if (_freeList->m_blocksFree)
			_freeList->m_next = _freeList->m_buffer + (*(uint32_t*)_freeList->m_next * _freeList->m_blockSize);
		else
			_freeList->m_next = 0;
	}
	return ret;
}

void rprofFreeListFree(struct rprofFreeList_t* _freeList, void* _ptr)
{
	if (_freeList->m_next)
	{
		uint32_t index		= ((uint32_t)(_freeList->m_next - _freeList->m_buffer)) / _freeList->m_blockSize;
		*(uint32_t*)_ptr	= index;
		_freeList->m_next	= (uint8_t*)_ptr;
	}
	else
	{
		*(uint32_t*)_ptr	= _freeList->m_maxBlocks;
		_freeList->m_next	= (uint8_t*)_ptr;
	}
	++_freeList->m_blocksFree;
}int rprofFreeListCheckPtr(struct rprofFreeList_t* _freeList, void* _ptr){	return	(_freeList->m_maxBlocks * _freeList->m_blockSize) > 			(uintptr_t)(((uint8_t*)_ptr) - _freeList->m_buffer) ? 1 : 0;}