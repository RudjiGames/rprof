/*
 * Copyright 2025 Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 *
 * Minimal, self contained replacements for the C runtime functions used by
 * rprof, so the library does not depend on the CRT.
 */

#ifndef RPROF_CSTD_H
#define RPROF_CSTD_H

#include <stdint.h> /* uint*_t */
#include <stddef.h> /* size_t  */

static inline size_t rprofStrLen(const char* _str)
{
	const char* s = _str;
	while (*s)
		++s;
	return (size_t)(s - _str);
}

static inline int rprofStrCmp(const char* _a, const char* _b)
{
	while (*_a && (*_a == *_b))
	{
		++_a;
		++_b;
	}
	return (int)(unsigned char)*_a - (int)(unsigned char)*_b;
}

static inline char* rprofStrCpy(char* _dst, const char* _src)
{
	char* d = _dst;
	while ((*d++ = *_src++) != 0)
		;
	return _dst;
}

static inline void rprofMemCopy(void* _dst, const void* _src, size_t _len)
{
	uint8_t*		dst = (uint8_t*)_dst;
	const uint8_t*	src = (const uint8_t*)_src;
	for (size_t i=0; i<_len; ++i)
		dst[i] = src[i];
}

static inline void rprofMemSet(void* _dst, int _val, size_t _len)
{
	uint8_t* dst = (uint8_t*)_dst;
	uint8_t  val = (uint8_t)_val;
	for (size_t i=0; i<_len; ++i)
		dst[i] = val;
}

/* Formats "<name>  -  0x<hex>" into _buf, always null terminated. */
static inline void rprofFormatThreadLabel(char* _buf, size_t _bufSize, const char* _name, uint64_t _threadID)
{
	if (_bufSize == 0)
		return;

	size_t pos = 0;

	for (const char* n = _name; *n && (pos + 1 < _bufSize); ++n)
		_buf[pos++] = *n;

	for (const char* s = "  -  0x"; *s && (pos + 1 < _bufSize); ++s)
		_buf[pos++] = *s;

	/* build hex digits (reversed), then emit in order */
	char		hex[16];
	int			hexLen	= 0;
	uint64_t	v		= _threadID;
	if (v == 0)
		hex[hexLen++] = '0';
	else
		while (v && (hexLen < (int)sizeof(hex)))
		{
			uint32_t d = (uint32_t)(v & 0xf);
			hex[hexLen++] = (char)(d < 10 ? ('0' + d) : ('a' + (d - 10)));
			v >>= 4;
		}

	while ((hexLen > 0) && (pos + 1 < _bufSize))
		_buf[pos++] = hex[--hexLen];

	_buf[pos] = 0;
}

#endif /* RPROF_CSTD_H */
